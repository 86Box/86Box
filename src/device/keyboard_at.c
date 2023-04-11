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
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          EngiNerd, <webmaster.crrc@yahoo.it>
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
#include <86box/m_at_t3100e.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>
#include <86box/keyboard.h>

#define STAT_PARITY        0x80
#define STAT_RTIMEOUT      0x40
#define STAT_TTIMEOUT      0x20
#define STAT_MFULL         0x20
#define STAT_UNLOCKED      0x10
#define STAT_CD            0x08
#define STAT_SYSFLAG       0x04
#define STAT_IFULL         0x02
#define STAT_OFULL         0x01

#define RESET_DELAY_TIME   (100 * 10) /* 600ms */

#define CCB_UNUSED         0x80
#define CCB_TRANSLATE      0x40
#define CCB_PCMODE         0x20
#define CCB_ENABLEKBD      0x10
#define CCB_IGNORELOCK     0x08
#define CCB_SYSTEM         0x04
#define CCB_ENABLEMINT     0x02
#define CCB_ENABLEKINT     0x01

#define CCB_MASK           0x68
#define MODE_MASK          0x6c

#define KBC_TYPE_ISA       0x00 /* AT ISA-based chips */
#define KBC_TYPE_PS2_NOREF 0x01 /* PS2 type, no refresh */
#define KBC_TYPE_PS2_1     0x02 /* PS2 on PS/2, type 1 */
#define KBC_TYPE_PS2_2     0x03 /* PS2 on PS/2, type 2 */
#define KBC_TYPE_MASK      0x03

#define KBC_VEN_GENERIC    0x00
#define KBC_VEN_AMI        0x04
#define KBC_VEN_IBM_MCA    0x08
#define KBC_VEN_QUADTEL    0x0c
#define KBC_VEN_TOSHIBA    0x10
#define KBC_VEN_IBM_PS1    0x14
#define KBC_VEN_ACER       0x18
#define KBC_VEN_INTEL_AMI  0x1c
#define KBC_VEN_OLIVETTI   0x20
#define KBC_VEN_NCR        0x24
#define KBC_VEN_PHOENIX    0x28
#define KBC_VEN_ALI        0x2c
#define KBC_VEN_TG         0x30
#define KBC_VEN_TG_GREEN   0x34
#define KBC_VEN_MASK       0x3c

enum {
    KBC_STATE_RESET = 0,
    KBC_STATE_MAIN_IBF,
    KBC_STATE_MAIN_KBD,
    KBC_STATE_MAIN_MOUSE,
    KBC_STATE_MAIN_BOTH,
    KBC_STATE_KBC_OUT,
    KBC_STATE_KBC_PARAM,
    KBC_STATE_SEND_KBD,
    KBC_STATE_KBD,
    KBC_STATE_SEND_MOUSE,
    KBC_STATE_MOUSE
};
#define KBC_STATE_SCAN_KBD KBC_STATE_KBD
#define KBC_STATE_SCAN_MOUSE KBC_STATE_MOUSE

enum {
    DEV_STATE_RESET = 0,
    DEV_STATE_MAIN_1,
    DEV_STATE_MAIN_2,
    DEV_STATE_MAIN_CMD,
    DEV_STATE_MAIN_OUT,
    DEV_STATE_MAIN_WANT_IN,
    DEV_STATE_MAIN_IN,
    DEV_STATE_MAIN_WANT_RESET,
    DEV_STATE_RESET_OUT
};

typedef struct {
    /* Controller. */
    uint8_t pci, kbc_state, command, want60,
            status, ib, out, old_out,
            secr_phase, mem_addr, input_port, output_port,
            old_output_port, output_locked, ami_stat, ami_flags,
            key_ctrl_queue_start, key_ctrl_queue_end;

    /* Keyboard. */
    uint8_t kbd_state, key_command, key_wantdata, key_wantcmd,
            key_dat, kbd_last_scan_code, sc_or, key_cmd_queue_start,
            key_cmd_queue_end, key_queue_start, key_queue_end;

    /* Mouse. */
    uint8_t mouse_state, mouse_wantcmd, mouse_dat, mouse_cmd_queue_start,
            mouse_cmd_queue_end, mouse_queue_start, mouse_queue_end;

    /* Controller. */
    uint8_t mem[0x100];

    /* Controller - internal FIFO for the purpose of commands with multi-byte output. */
    uint8_t key_ctrl_queue[64];

    /* Keyboard - command response FIFO. */
    uint8_t key_cmd_queue[16];

    /* Keyboard - scan FIFO. */
    uint8_t key_queue[16];

    /* Mouse - command response FIFO. */
    uint8_t mouse_cmd_queue[16];

    /* Mouse - scan FIFO. */
    uint8_t mouse_queue[16];

    /* Keyboard. */
    int out_new, reset_delay;

    /* Mouse. */
    int out_new_mouse, mouse_reset_delay;

    /* Controller. */
    uint32_t flags;

    /* Controller (main timer). */
    pc_timer_t send_delay_timer;

    /* Controller (P2 pulse callback timer). */
    pc_timer_t pulse_cb;

    uint8_t (*write60_ven)(void *p, uint8_t val);
    uint8_t (*write64_ven)(void *p, uint8_t val);
} atkbd_t;

/* Global keyboard flags for scan code set 3:
   bit 0 = repeat, bit 1 = makes break code? */
uint8_t keyboard_set3_flags[512];
uint8_t keyboard_set3_all_repeat;
uint8_t keyboard_set3_all_break;

/* Global keyboard mode:
   Bits 0 - 1 = scan code set, bit 6 = translate or not. */
uint8_t        keyboard_mode = 0x42;

static void (*mouse_write)(uint8_t val, void *priv) = NULL;
static void    *mouse_p                             = NULL;
static atkbd_t *SavedKbd                            = NULL; // FIXME: remove!!! --FvK

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

// #define ENABLE_KEYBOARD_AT_LOG 1
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
kbc_queue_reset(atkbd_t *dev, uint8_t channel)
{
    switch (channel) {
        case 1:
            dev->key_queue_start = dev->key_queue_end = 0;
            memset(dev->key_queue, 0x00, sizeof(dev->key_queue));
            /* FALLTHROUGH */
        case 4:
            dev->key_cmd_queue_start = dev->key_cmd_queue_end = 0;
            memset(dev->key_cmd_queue, 0x00, sizeof(dev->key_cmd_queue));
            break;

        case 2:
            dev->mouse_queue_start = dev->mouse_queue_end = 0;
            memset(dev->mouse_queue, 0x00, sizeof(dev->mouse_queue));
            /* FALLTHROUGH */
        case 3:
            dev->mouse_cmd_queue_start = dev->mouse_cmd_queue_end = 0;
            memset(dev->mouse_cmd_queue, 0x00, sizeof(dev->mouse_cmd_queue));
            break;

        case 0:
        default:
            dev->key_ctrl_queue_start = dev->key_ctrl_queue_end = 0;
            memset(dev->key_ctrl_queue, 0x00, sizeof(dev->key_ctrl_queue));
    }
}

static void
kbc_queue_add(atkbd_t *dev, uint8_t val, uint8_t channel)
{
    switch (channel) {
        case 4:
            kbd_log("ATkbc: dev->key_cmd_queue[%02X] = %02X;\n", dev->key_cmd_queue_end, val);
            dev->key_cmd_queue[dev->key_cmd_queue_end] = val;
            dev->key_cmd_queue_end       = (dev->key_cmd_queue_end + 1) & 0xf;
            break;
        case 3:
            kbd_log("ATkbc: dev->mouse_cmd_queue[%02X] = %02X;\n", dev->mouse_cmd_queue_end, val);
            dev->mouse_cmd_queue[dev->mouse_cmd_queue_end] = val;
            dev->mouse_cmd_queue_end     = (dev->mouse_cmd_queue_end + 1) & 0xf;
            break;
        case 2:
            kbd_log("ATkbc: dev->mouse_queue[%02X] = %02X;\n", dev->mouse_queue_end, val);
            dev->mouse_queue[dev->mouse_queue_end] = val;
            dev->mouse_queue_end         = (dev->mouse_queue_end + 1) & 0xf;
            break;
        case 1:
            kbd_log("ATkbc: dev->key_queue[%02X] = %02X;\n", dev->key_queue_end, val);
            dev->key_queue[dev->key_queue_end] = val;
            dev->key_queue_end           = (dev->key_queue_end + 1) & 0xf;
            break;
        case 0:
        default:
            kbd_log("ATkbc: dev->key_ctrl_queue[%02X] = %02X;\n", dev->key_ctrl_queue_end, val);
            dev->key_ctrl_queue[dev->key_ctrl_queue_end] = val;
            dev->key_ctrl_queue_end                 = (dev->key_ctrl_queue_end + 1) & 0x3f;
            break;
    }
}

static int
kbc_translate(atkbd_t *dev, uint8_t val)
{
    int      xt_mode   = (keyboard_mode & 0x20) && ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_PS2_NOREF);
    int      translate = (keyboard_mode & 0x40);
    uint8_t  kbc_ven   = dev->flags & KBC_VEN_MASK;
    int      ret       = - 1;

    translate = translate || (keyboard_mode & 0x40) || xt_mode;
    translate = translate || ((dev->flags & KBC_TYPE_MASK) == KBC_TYPE_PS2_2);

    /* Allow for scan code translation. */
    if (translate && (val == 0xf0)) {
        kbd_log("ATkbd: translate is on, F0 prefix detected\n");
        dev->sc_or = 0x80;
        return ret;
    }

    /* Skip break code if translated make code has bit 7 set. */
    if (translate && (dev->sc_or == 0x80) && (nont_to_t[val] & 0x80)) {
        kbd_log("ATkbd: translate is on, skipping scan code: %02X (original: F0 %02X)\n", nont_to_t[val], val);
        dev->sc_or = 0;
        return ret;
    }

    /* Test for T3100E 'Fn' key (Right Alt / Right Ctrl) */
    if ((dev != NULL) && (kbc_ven == KBC_VEN_TOSHIBA) &&
        (keyboard_recv(0x138) || keyboard_recv(0x11d)))  switch (val) {
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
        case 0x4a:
            t3100e_notify_set(0x0c);
            break; /* Keypad - */
        case 0x4b:
            t3100e_notify_set(0x0d);
            break; /* Left */
        case 0x4c:
            t3100e_notify_set(0x0e);
            break; /* KP 5 */
        case 0x4d:
            t3100e_notify_set(0x0f);
            break; /* Right */
    }

    kbd_log("ATkbd: translate is %s, ", translate ? "on" : "off");
#ifdef ENABLE_KEYBOARD_AT_LOG
    kbd_log("scan code: ");
    if (translate) {
        kbd_log("%02X (original: ", (nont_to_t[val] | dev->sc_or));
        if (dev->sc_or == 0x80)
            kbd_log("F0 ");
        kbd_log("%02X)\n", val);
    } else
        kbd_log("%02X\n", val);
#endif

    ret = translate ? (nont_to_t[val] | dev->sc_or) : val;

    if (dev->sc_or == 0x80)
        dev->sc_or = 0;

    return ret;
}

static void
add_to_kbc_queue_front(atkbd_t *dev, uint8_t val, uint8_t channel, uint8_t stat_hi)
{
    uint8_t kbc_ven = dev->flags & KBC_VEN_MASK;
    int temp = (channel == 1) ? kbc_translate(dev, val) : val;

    if (temp == -1)
        return;

    if ((kbc_ven == KBC_VEN_AMI) || (kbc_ven == KBC_VEN_TG) ||
        (kbc_ven == KBC_VEN_TG_GREEN) || ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF))
        stat_hi |= ((dev->input_port & 0x80) ? 0x10 : 0x00);
    else
        stat_hi |= 0x10;

    kbd_log("ATkbc: Adding %02X to front on channel %i...\n", temp, channel);
    dev->status = (dev->status & ~0xf0) | STAT_OFULL | stat_hi;

    /* WARNING: On PS/2, all IRQ's are level-triggered, but the IBM PS/2 KBC firmware is explicitly
                written to pulse its P2 IRQ bits, so they should be kept as as edge-triggered here. */
    if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) {
        if (channel >= 2) {
            dev->status |= STAT_MFULL;

            if (dev->mem[0x20] & 0x02)
                picint_common(1 << 12, 0, 1);
            picint_common(1 << 1, 0, 0);
        } else {
            if (dev->mem[0x20] & 0x01)
                picint_common(1 << 1, 0, 1);
            picint_common(1 << 12, 0, 0);
        }
    } else if (dev->mem[0x20] & 0x01)
        picintlevel(1 << 1); /* AT KBC: IRQ 1 is level-triggered because it is tied to OBF. */

    dev->out = temp;
}

static void
add_data_kbd_cmd_queue(atkbd_t *dev, uint8_t val)
{
    if ((dev->reset_delay > 0) || (dev->key_cmd_queue_end >= 16)) {
        kbd_log("ATkbc: Unable to add to queue, conditions: %i, %i\n", (dev->reset_delay > 0), (dev->key_cmd_queue_end >= 16));
        return;
    }
    kbd_log("ATkbc: dev->key_cmd_queue[%02X] = %02X;\n", dev->key_cmd_queue_end, val);
    kbc_queue_add(dev, val, 4);
    dev->kbd_last_scan_code = val;
}

static void
add_data_kbd_queue(atkbd_t *dev, uint8_t val)
{
    if (!keyboard_scan || (dev->reset_delay > 0) || (dev->key_queue_end >= 16)) {
        kbd_log("ATkbc: Unable to add to queue, conditions: %i, %i, %i\n", !keyboard_scan, (dev->reset_delay > 0), (dev->key_queue_end >= 16));
        return;
    }
    kbd_log("ATkbc: key_queue[%02X] = %02X;\n", dev->key_queue_end, val);
    kbc_queue_add(dev, val, 1);
    dev->kbd_last_scan_code = val;
}

static void
add_data_kbd_front(atkbd_t *dev, uint8_t val)
{
    if (dev->reset_delay)
        return;

    add_data_kbd_cmd_queue(dev, val);
}

static void kbd_process_cmd(void *priv);
static void kbc_process_cmd(void *priv);

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
kbc_ibf_process(atkbd_t *dev)
{
    /* IBF set, process both commands and data. */
    dev->status &= ~STAT_IFULL;
    dev->kbc_state      = KBC_STATE_MAIN_IBF;
    if (dev->status & STAT_CD)
        kbc_process_cmd(dev);
    else {
        set_enable_kbd(dev, 1);
        dev->key_wantcmd = 1;
        dev->key_dat = dev->ib;
        dev->kbc_state = KBC_STATE_SEND_KBD;
    }
}

static void
kbc_scan_kbd_at(atkbd_t *dev)
{
    if (!(dev->mem[0x20] & 0x10)) {
        /* Both OBF and IBF clear and keyboard is enabled. */
        /* XT mode. */
        if (dev->mem[0x20] & 0x20) {
            if (dev->out_new != -1) {
                add_to_kbc_queue_front(dev, dev->out_new, 1, 0x00);
                dev->out_new        = -1;
                dev->kbc_state      = KBC_STATE_MAIN_IBF;
            } else if (dev->status & STAT_IFULL)
                kbc_ibf_process(dev);
        /* AT mode. */
        } else {
            // dev->t = dev->mem[0x28];
            if (dev->mem[0x2e] != 0x00) {
                // if (!(dev->t & 0x02))
                    // return;
                dev->mem[0x2e] = 0x00;
            }
            dev->output_port &= 0xbf;
            if (dev->out_new != -1) {
                /* In our case, we never have noise on the line, so we can simplify this. */
                /* Read data from the keyboard. */
                if (dev->mem[0x20] & 0x40) {
                    if ((dev->mem[0x20] & 0x08) || (dev->input_port & 0x80))
                        add_to_kbc_queue_front(dev, dev->out_new, 1, 0x00);
                    dev->mem[0x2d] = (dev->out_new == 0xf0) ? 0x80 : 0x00;
                } else
                    add_to_kbc_queue_front(dev, dev->out_new, 1, 0x00);
                dev->out_new        = -1;
                dev->kbc_state      = KBC_STATE_MAIN_IBF;
            }
        }
    }
}

static void    write_output(atkbd_t *dev, uint8_t val);

static void
kbc_poll_at(atkbd_t *dev)
{
    switch (dev->kbc_state) {
        case KBC_STATE_RESET:
            if (dev->status & STAT_IFULL) {
                dev->status = ((dev->status & 0x0f) | 0x10) & ~STAT_IFULL;
                if ((dev->status & STAT_CD) && (dev->ib == 0xaa))
                    kbc_process_cmd(dev);
            }
            break;
        case KBC_STATE_MAIN_IBF:
        default:
           if (dev->status & STAT_OFULL) {
                /* OBF set, wait until it is cleared but still process commands. */
                if ((dev->status & STAT_IFULL) && (dev->status & STAT_CD)) {
                    dev->status &= ~STAT_IFULL;
                    kbc_process_cmd(dev);
                }
            } else if (dev->status & STAT_IFULL)
                kbc_ibf_process(dev);
            else if (!(dev->mem[0x20] & 0x10))
                dev->kbc_state = KBC_STATE_MAIN_KBD;
            break;
        case KBC_STATE_MAIN_KBD:
        case KBC_STATE_MAIN_BOTH:
            if (dev->status & STAT_IFULL)
                kbc_ibf_process(dev);
            else {
                (void) kbc_scan_kbd_at(dev);
                dev->kbc_state = KBC_STATE_MAIN_IBF;
            }
            break;
        case KBC_STATE_KBC_OUT:
            /* Keyboard controller command want to output multiple bytes. */
            if (dev->status & STAT_IFULL) {
                /* Data from host aborts dumping. */
                dev->kbc_state = KBC_STATE_MAIN_IBF;
                kbc_ibf_process(dev);
            }
            /* Do not continue dumping until OBF is clear. */
            if (!(dev->status & STAT_OFULL)) {
                kbd_log("ATkbc: %02X coming from channel 0\n", dev->key_ctrl_queue[dev->key_ctrl_queue_start]);
                add_to_kbc_queue_front(dev, dev->key_ctrl_queue[dev->key_ctrl_queue_start], 0, 0x00);
                dev->key_ctrl_queue_start = (dev->key_ctrl_queue_start + 1) & 0x3f;
                if (dev->key_ctrl_queue_start == dev->key_ctrl_queue_end)
                    dev->kbc_state = KBC_STATE_MAIN_IBF;
            }
            break;
        case KBC_STATE_KBC_PARAM:
            /* Keyboard controller command wants data, wait for said data. */
            if (dev->status & STAT_IFULL) {
                /* Command written, abort current command. */
                if (dev->status & STAT_CD)
                    dev->kbc_state = KBC_STATE_MAIN_IBF;

                dev->status &= ~STAT_IFULL;
                kbc_process_cmd(dev);
            }
            break;
        case KBC_STATE_SEND_KBD:
            if (!dev->key_wantcmd)
                dev->kbc_state = KBC_STATE_SCAN_KBD;
            break;
        case KBC_STATE_SCAN_KBD:
            kbc_scan_kbd_at(dev);
            break;
    }
}

/*
    Correct Procedure:
        1. Controller asks the device (keyboard or mouse) for a byte.
        2. The device, unless it's in the reset or command states, sees if there's anything to give it,
           and if yes, begins the transfer.
        3. The controller checks if there is a transfer, if yes, transfers the byte and sends it to the host,
           otherwise, checks the next device, or if there is no device left to check, checks if IBF is full
           and if yes, processes it.
 */
static int
kbc_scan_kbd_ps2(atkbd_t *dev)
{
    if (dev->out_new != -1) {
        kbd_log("ATkbc: %02X coming from channel 1\n", dev->out_new & 0xff);
        add_to_kbc_queue_front(dev, dev->out_new, 1, 0x00);
        dev->out_new        = -1;
        dev->kbc_state      = KBC_STATE_MAIN_IBF;
        return 1;
    }

    return 0;
}

static int
kbc_scan_aux_ps2(atkbd_t *dev)
{
    if (dev->out_new_mouse != -1) {
        kbd_log("ATkbc: %02X coming from channel 2\n", dev->out_new_mouse & 0xff);
        add_to_kbc_queue_front(dev, dev->out_new_mouse, 2, 0x00);
        dev->out_new_mouse  = -1;
        dev->kbc_state      = KBC_STATE_MAIN_IBF;
        return 1;
    }

    return 0;
}

static void
kbc_poll_ps2(atkbd_t *dev)
{
    switch (dev->kbc_state) {
        case KBC_STATE_RESET:
            // pclog("KBC_STATE_RESET\n");
            if (dev->status & STAT_IFULL) {
                dev->status = ((dev->status & 0x0f) | 0x10) & ~STAT_IFULL;
                if ((dev->status & STAT_CD) && (dev->ib == 0xaa))
                    kbc_process_cmd(dev);
            }
            break;
        case KBC_STATE_MAIN_IBF:
        default:
            // pclog("KBC_STATE_MAIN_IBF\n");
            if (dev->status & STAT_IFULL)
                kbc_ibf_process(dev);
            else if (!(dev->status & STAT_OFULL)) {
                if (dev->mem[0x20] & 0x20) {
                    if (!(dev->mem[0x20] & 0x10)) {
                        dev->output_port &= 0xbf;
                        dev->kbc_state = KBC_STATE_MAIN_KBD;
                    }
                } else {
                    dev->output_port &= 0xf7;
                    if (dev->mem[0x20] & 0x10)
                        dev->kbc_state = KBC_STATE_MAIN_MOUSE;
                    else {
                        dev->output_port &= 0xbf;
                        dev->kbc_state = KBC_STATE_MAIN_BOTH;
                    }
                }
            }
            break;
        case KBC_STATE_MAIN_KBD:
            // pclog("KBC_STATE_MAIN_KBD\n");
            if (dev->status & STAT_IFULL)
                kbc_ibf_process(dev);
            else {
                (void) kbc_scan_kbd_ps2(dev);
                dev->kbc_state = KBC_STATE_MAIN_IBF;
            }
            break;
        case KBC_STATE_MAIN_MOUSE:
            // pclog("KBC_STATE_MAIN_MOUSE\n");
            if (dev->status & STAT_IFULL)
                kbc_ibf_process(dev);
            else {
                (void) kbc_scan_aux_ps2(dev);
                dev->kbc_state = KBC_STATE_MAIN_IBF;
            }
            break;
        case KBC_STATE_MAIN_BOTH:
            // pclog("KBC_STATE_MAIN_BOTH\n");
            if (kbc_scan_kbd_ps2(dev))
                dev->kbc_state = KBC_STATE_MAIN_IBF;
            else
                dev->kbc_state = KBC_STATE_MAIN_MOUSE;
            break;
        case KBC_STATE_KBC_OUT:
            // pclog("KBC_STATE_KBC_OUT\n");
            /* Keyboard controller command want to output multiple bytes. */
            if (dev->status & STAT_IFULL) {
                /* Data from host aborts dumping. */
                dev->kbc_state = KBC_STATE_MAIN_IBF;
                kbc_ibf_process(dev);
            }
            /* Do not continue dumping until OBF is clear. */
            if (!(dev->status & STAT_OFULL)) {
                kbd_log("ATkbc: %02X coming from channel 0\n", dev->out_new & 0xff);
                add_to_kbc_queue_front(dev, dev->key_ctrl_queue[dev->key_ctrl_queue_start], 0, 0x00);
                dev->key_ctrl_queue_start = (dev->key_ctrl_queue_start + 1) & 0x3f;
                if (dev->key_ctrl_queue_start == dev->key_ctrl_queue_end)
                    dev->kbc_state = KBC_STATE_MAIN_IBF;
            }
            break;
        case KBC_STATE_KBC_PARAM:
            // pclog("KBC_STATE_KBC_PARAM\n");
            /* Keyboard controller command wants data, wait for said data. */
            if (dev->status & STAT_IFULL) {
                /* Command written, abort current command. */
                if (dev->status & STAT_CD)
                    dev->kbc_state = KBC_STATE_MAIN_IBF;

                dev->status &= ~STAT_IFULL;
                kbc_process_cmd(dev);
            }
            break;
        case KBC_STATE_SEND_KBD:
            if (!dev->key_wantcmd)
                dev->kbc_state = KBC_STATE_SCAN_KBD;
            break;
        case KBC_STATE_SCAN_KBD:
            // pclog("KBC_STATE_SCAN_KBD\n");
            (void) kbc_scan_kbd_ps2(dev);
            break;
        case KBC_STATE_SEND_MOUSE:
            if (!dev->mouse_wantcmd)
                dev->kbc_state = KBC_STATE_SCAN_MOUSE;
            break;
        case KBC_STATE_SCAN_MOUSE:
            // pclog("KBC_STATE_SCAN_MOUSE\n");
            (void) kbc_scan_aux_ps2(dev);
            break;
    }
}

static void
kbc_poll_kbd(atkbd_t *dev)
{
    switch (dev->kbd_state) {
        case DEV_STATE_RESET:
            /* Reset state. */
            if (dev->reset_delay) {
                dev->reset_delay--;
                if (!dev->reset_delay) {
                    kbd_log("ATkbc: Sending AA on keyboard reset...\n");
                    add_data_kbd_front(dev, 0xaa);
                    dev->kbd_state = DEV_STATE_RESET_OUT;
                }
            }
            break;
        case DEV_STATE_MAIN_1:
            /* Process the command if needed and then return to main loop #2. */
            if (dev->key_wantcmd) {
                kbd_log("ATkbc: Processing keyboard command...\n");
                kbc_queue_reset(dev, 4);
                // dev->out_new = -1;
                kbd_process_cmd(dev);
                dev->key_wantcmd    = 0;
            } else
                dev->kbd_state = DEV_STATE_MAIN_2;
            break;
        case DEV_STATE_MAIN_2:
            /* Output from scan queue if needed and then return to main loop #1. */
            if (keyboard_scan && (dev->out_new == -1) && (dev->key_queue_start != dev->key_queue_end)) {
                kbd_log("ATkbc: %02X (DATA) on channel 1\n", dev->key_queue[dev->key_queue_start]);
                dev->out_new         = dev->key_queue[dev->key_queue_start];
                dev->key_queue_start = (dev->key_queue_start + 1) & 0xf;
            }
            if (!keyboard_scan || dev->key_wantcmd)
                dev->kbd_state = DEV_STATE_MAIN_1;
            break;
        case DEV_STATE_MAIN_OUT:
        case DEV_STATE_RESET_OUT:
            /* Output command response and then return to main loop #2. */
            if ((dev->out_new == -1) && (dev->key_cmd_queue_start != dev->key_cmd_queue_end)) {
                kbd_log("ATkbc: %02X (CMD ) on channel 1\n", dev->key_cmd_queue[dev->key_cmd_queue_start]);
                dev->out_new             = dev->key_cmd_queue[dev->key_cmd_queue_start];
                dev->key_cmd_queue_start = (dev->key_cmd_queue_start + 1) & 0xf;
            }
            if (dev->key_cmd_queue_start == dev->key_cmd_queue_end)
                dev->kbd_state = (dev->kbd_state == DEV_STATE_RESET_OUT) ? DEV_STATE_MAIN_1 : DEV_STATE_MAIN_2;
            break;
        case DEV_STATE_MAIN_WANT_IN:
            /* Output command response and then wait for host data. */
            if ((dev->out_new == -1) && (dev->key_cmd_queue_start != dev->key_cmd_queue_end)) {
                kbd_log("ATkbc: %02X (CMD ) on channel 1\n", dev->key_cmd_queue[dev->key_cmd_queue_start]);
                dev->out_new             = dev->key_cmd_queue[dev->key_cmd_queue_start];
                dev->key_cmd_queue_start = (dev->key_cmd_queue_start + 1) & 0xf;
            }
            if (dev->key_cmd_queue_start == dev->key_cmd_queue_end)
                dev->kbd_state = DEV_STATE_MAIN_IN;
            break;
        case DEV_STATE_MAIN_IN:
            /* Wait for host data. */
            if (dev->key_wantcmd) {
                kbd_log("ATkbc: Processing keyboard command...\n");
                kbc_queue_reset(dev, 4);
                // dev->out_new = -1;
                kbd_process_cmd(dev);
                dev->key_wantcmd    = 0;
            }
            break;
        case DEV_STATE_MAIN_WANT_RESET:
            /* Output command response and then go to the reset state. */
            if ((dev->out_new == -1) && (dev->key_cmd_queue_start != dev->key_cmd_queue_end)) {
                kbd_log("ATkbc: %02X (CMD ) on channel 1\n", dev->key_cmd_queue[dev->key_cmd_queue_start]);
                dev->out_new             = dev->key_cmd_queue[dev->key_cmd_queue_start];
                dev->key_cmd_queue_start = (dev->key_cmd_queue_start + 1) & 0xf;
            }
            if (dev->key_cmd_queue_start == dev->key_cmd_queue_end)
                dev->kbd_state = DEV_STATE_RESET;
            break;
    }
}

static void
kbc_poll_aux(atkbd_t *dev)
{
    switch (dev->mouse_state) {
#if 0
        case DEV_STATE_RESET:
            /* Reset state. */
            if (dev->mouse_reset_delay) {
                dev->mouse_reset_delay--;
                if (!dev->mouse_reset_delay) {
                    kbd_log("ATkbc: Sending AA 00 on mouse reset...\n");
                    keyboard_at_adddata_mouse_cmd(0xaa);
                    keyboard_at_adddata_mouse_cmd(0x00);
                    dev->mouse_state = DEV_STATE_RESET_OUT;
                }
            }
            break;
#endif
        case DEV_STATE_MAIN_1:
            /* Process the command if needed and then return to main loop #2. */
            if (dev->mouse_wantcmd) {
                kbd_log("ATkbc: Processing mouse command...\n");
                kbc_queue_reset(dev, 3);
                // dev->out_new_mouse = -1;
                dev->mouse_state = DEV_STATE_MAIN_OUT;
                mouse_write(dev->mouse_dat, mouse_p);
                if ((dev->mouse_dat == 0xe8) || (dev->mouse_dat == 0xf3))
                    dev->mouse_state = DEV_STATE_MAIN_WANT_IN;
                dev->mouse_wantcmd  = 0;
            } else
                dev->mouse_state = DEV_STATE_MAIN_2;
            break;
        case DEV_STATE_MAIN_2:
            /* Output from scan queue if needed and then return to main loop #1. */
            if (mouse_scan && (dev->out_new_mouse == -1) && (dev->mouse_queue_start != dev->mouse_queue_end)) {
                kbd_log("ATkbc: %02X (DATA) on channel 2\n", dev->mouse_queue[dev->mouse_queue_start]);
                dev->out_new_mouse       = dev->mouse_queue[dev->mouse_queue_start];
                dev->mouse_queue_start   = (dev->mouse_queue_start + 1) & 0xf;
            }
            if (!mouse_scan || dev->mouse_wantcmd)
                dev->mouse_state = DEV_STATE_MAIN_1;
            break;
        case DEV_STATE_MAIN_OUT:
        case DEV_STATE_RESET_OUT:
            /* Output command response and then return to main loop #2. */
            if ((dev->out_new_mouse == -1) && (dev->mouse_cmd_queue_start != dev->mouse_cmd_queue_end)) {
                kbd_log("ATkbc: %02X (CMD ) on channel 2\n", dev->mouse_cmd_queue[dev->mouse_cmd_queue_start]);
                dev->out_new_mouse         = dev->mouse_cmd_queue[dev->mouse_cmd_queue_start];
                dev->mouse_cmd_queue_start = (dev->mouse_cmd_queue_start + 1) & 0xf;
            }
            if (dev->mouse_cmd_queue_start == dev->mouse_cmd_queue_end)
                dev->mouse_state = (dev->mouse_state == DEV_STATE_RESET_OUT) ? DEV_STATE_MAIN_1 : DEV_STATE_MAIN_2;
            break;
        case DEV_STATE_MAIN_WANT_IN:
            /* Output command response and then wait for host data. */
            if ((dev->out_new_mouse == -1) && (dev->mouse_cmd_queue_start != dev->mouse_cmd_queue_end)) {
                kbd_log("ATkbc: %02X (CMD ) on channel 2\n", dev->mouse_cmd_queue[dev->mouse_cmd_queue_start]);
                dev->out_new_mouse         = dev->mouse_cmd_queue[dev->mouse_cmd_queue_start];
                dev->mouse_cmd_queue_start = (dev->mouse_cmd_queue_start + 1) & 0xf;
            }
            if (dev->mouse_cmd_queue_start == dev->mouse_cmd_queue_end)
                dev->mouse_state = DEV_STATE_MAIN_IN;
            break;
        case DEV_STATE_MAIN_IN:
            /* Wait for host data. */
            if (dev->mouse_wantcmd) {
                kbd_log("ATkbc: Processing mouse command...\n");
                kbc_queue_reset(dev, 3);
                // dev->out_new_mouse = -1;
                dev->mouse_state = DEV_STATE_MAIN_OUT;
                mouse_write(dev->mouse_dat, mouse_p);
                dev->mouse_wantcmd  = 0;
            }
            break;
        case DEV_STATE_MAIN_WANT_RESET:
            /* Output command response and then go to the reset state. */
            if ((dev->out_new_mouse == -1) && (dev->mouse_cmd_queue_start != dev->mouse_cmd_queue_end)) {
                kbd_log("ATkbc: %02X (CMD ) on channel 2\n", dev->mouse_cmd_queue[dev->mouse_cmd_queue_start]);
                dev->out_new_mouse         = dev->mouse_cmd_queue[dev->mouse_cmd_queue_start];
                dev->mouse_cmd_queue_start = (dev->mouse_cmd_queue_start + 1) & 0xf;
            }
            if (dev->mouse_cmd_queue_start == dev->mouse_cmd_queue_end)
                dev->mouse_state = DEV_STATE_RESET;
            break;
    }
}

/* TODO: State machines for controller, keyboard, and mouse. */
static void
kbd_poll(void *priv)
{
    atkbd_t *dev = (atkbd_t *) priv;

    timer_advance_u64(&dev->send_delay_timer, (100ULL * TIMER_USEC));

    /* TODO: Use a fuction pointer for this (also needed to the AMI KBC mode switching)
             and implement the password security state. */
    if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF)
        kbc_poll_ps2(dev);
    else
        kbc_poll_at(dev);

    kbc_poll_kbd(dev);

    if (((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) && mouse_write)
        kbc_poll_aux(dev);
}

static void
add_data_vals(atkbd_t *dev, uint8_t *val, uint8_t len)
{
    int i;

    if (dev->reset_delay)
        return;

    for (i = 0; i < len; i++)
        add_data_kbd_queue(dev, val[i]);
}

static void
add_data_kbd(uint16_t val)
{
    atkbd_t *dev       = SavedKbd;
    uint8_t  fake_shift[4];
    uint8_t  num_lock = 0, shift_states = 0;

    if (dev->reset_delay)
        return;

    keyboard_get_states(NULL, &num_lock, NULL);
    shift_states = keyboard_get_shift() & STATE_SHIFT_MASK;

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
            add_data_kbd_queue(dev, val);
            break;
    }
}

static void
write_output(atkbd_t *dev, uint8_t val)
{
    uint8_t old = dev->output_port;
    kbd_log("ATkbc: write output port: %02X (old: %02X)\n", val, dev->output_port);

    uint8_t kbc_ven = dev->flags & KBC_VEN_MASK;

    /* PS/2: Handle IRQ's. */
    if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) {
        /* IRQ 12 */
        picint_common(1 << 12, 0, val & 0x20);

        /* IRQ 1 */
        picint_common(1 << 1, 0, val & 0x10);
    }

    /* AT, PS/2: Handle A20. */
    if ((old ^ val) & 0x02) { /* A20 enable change */
        mem_a20_key = val & 0x02;
        mem_a20_recalc();
        flushmmucache();
    }

    /* AT, PS/2: Handle reset. */
    /* 0 holds the CPU in the RESET state, 1 releases it. To simplify this,
       we just do everything on release. */
    if ((old ^ val) & 0x01) { /*Reset*/
        if (!(val & 0x01)) {  /* Pin 0 selected. */
            /* Pin 0 selected. */
            kbd_log("write_output(): Pulse reset!\n");
            if (machines[machine].flags & MACHINE_COREBOOT) {
                /* The SeaBIOS hard reset code attempts a KBC reset if ACPI RESET_REG
                   is not available. However, the KBC reset is normally a soft reset, so
                   SeaBIOS gets caught in a soft reset loop as it tries to hard reset the
                   machine. Hack around this by making the KBC reset a hard reset only on
                   coreboot machines. */
                pc_reset_hard();
            } else {
                softresetx86(); /*Pulse reset!*/
                cpu_set_edx();
                flushmmucache();
                if (kbc_ven == KBC_VEN_ALI)
                    smbase = 0x00030000;
            }
        }
    }

    /* Do this here to avoid an infinite reset loop. */
    dev->output_port = val;
}

static void
write_output_fast_a20(atkbd_t *dev, uint8_t val)
{
    uint8_t old = dev->output_port;
    kbd_log("ATkbc: write output port in fast A20 mode: %02X (old: %02X)\n", val, dev->output_port);

    /* AT, PS/2: Handle A20. */
    if ((old ^ val) & 0x02) { /* A20 enable change */
        mem_a20_key = val & 0x02;
        mem_a20_recalc();
        flushmmucache();
    }

    /* Do this here to avoid an infinite reset loop. */
    dev->output_port = val;
}

static void
write_cmd(atkbd_t *dev, uint8_t val)
{
    uint8_t kbc_ven = dev->flags & KBC_VEN_MASK;
    kbd_log("ATkbc: write command byte: %02X (old: %02X)\n", val, dev->mem[0x20]);

    /* PS/2 type 2 keyboard controllers always force the XLAT bit to 0. */
    if ((dev->flags & KBC_TYPE_MASK) == KBC_TYPE_PS2_2) {
        val &= ~CCB_TRANSLATE;
        dev->mem[0x20] &= ~CCB_TRANSLATE;
    } else if ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_PS2_NOREF) {
        if (val & 0x10)
            dev->mem[0x2e] = 0x01;
    }

    /* Scan code translate ON/OFF. */
    keyboard_mode &= 0x93;
    keyboard_mode |= (val & MODE_MASK);

    kbd_log("ATkbc: keyboard interrupt is now %s\n", (val & 0x01) ? "enabled" : "disabled");

    /* ISA AT keyboard controllers use bit 5 for keyboard mode (1 = PC/XT, 2 = AT);
       PS/2 (and EISA/PCI) keyboard controllers use it as the PS/2 mouse enable switch.
       The AMIKEY firmware apparently uses this bit for something else. */
    if ((kbc_ven == KBC_VEN_AMI) || (kbc_ven == KBC_VEN_TG) ||
        (kbc_ven == KBC_VEN_TG_GREEN) || ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF)) {
        keyboard_mode &= ~CCB_PCMODE;

        kbd_log("ATkbc: mouse interrupt is now %s\n", (val & 0x02) ? "enabled" : "disabled");
    }

    if ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_PS2_NOREF) {
        /* Update the output port to mirror the IBF and OBF bits, if active. */
        write_output(dev, (dev->output_port & 0x0f) | ((val & 0x03) << 4) | ((val & 0x20) ? 0xc0 : 0x00));
    }

    kbd_log("Command byte now: %02X (%02X)\n", dev->mem[0x20], val);

    dev->status = (dev->status & ~STAT_SYSFLAG) | (val & STAT_SYSFLAG);
}

static void
pulse_output(atkbd_t *dev, uint8_t mask)
{
    if (mask != 0x0f) {
        dev->old_output_port = dev->output_port & ~(0xf0 | mask);
        kbd_log("pulse_output(): Output port now: %02X\n", dev->output_port & (0xf0 | mask));
        write_output(dev, dev->output_port & (0xf0 | mask));
        timer_set_delay_u64(&dev->pulse_cb, 6ULL * TIMER_USEC);
    }
}

static void
pulse_poll(void *priv)
{
    atkbd_t *dev = (atkbd_t *) priv;

    kbd_log("pulse_poll(): Output port now: %02X\n", dev->output_port | dev->old_output_port);
    write_output(dev, dev->output_port | dev->old_output_port);
}

static uint8_t
write64_generic(void *priv, uint8_t val)
{
    atkbd_t *dev = (atkbd_t *) priv;
    uint8_t  current_drive, fixed_bits;
    uint8_t  kbc_ven = 0x0;
    kbc_ven          = dev->flags & KBC_VEN_MASK;

    switch (val) {
        case 0xa4: /* check if password installed */
            if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) {
                kbd_log("ATkbc: check if password installed\n");
                add_to_kbc_queue_front(dev, 0xf1, 0, 0x00);
                return 0;
            }
            break;

        case 0xa7: /* disable mouse port */
            if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) {
                kbd_log("ATkbc: disable mouse port\n");
                set_enable_mouse(dev, 0);
                return 0;
            }
            break;

        case 0xa8: /*Enable mouse port*/
            if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) {
                kbd_log("ATkbc: enable mouse port\n");
                set_enable_mouse(dev, 1);
                return 0;
            }
            break;

        case 0xa9: /*Test mouse port*/
            kbd_log("ATkbc: test mouse port\n");
            if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) {
                add_to_kbc_queue_front(dev, 0x00, 0, 0x00); /* no error, this is testing the channel 2 interface */
                return 0;
            }
            break;

        case 0xaf: /* read keyboard version */
            kbd_log("ATkbc: read keyboard version\n");
            add_to_kbc_queue_front(dev, 0x42, 0, 0x00);
            return 0;

        case 0xc0: /* read input port */
            kbd_log("ATkbc: read input port\n");
            fixed_bits = 4;
            /* The SMM handlers of Intel AMI Pentium BIOS'es expect bit 6 to be set. */
            if (kbc_ven == KBC_VEN_INTEL_AMI)
                fixed_bits |= 0x40;
            if (kbc_ven == KBC_VEN_IBM_PS1) {
                current_drive = fdc_get_current_drive();
                add_to_kbc_queue_front(dev, dev->input_port | fixed_bits | (fdd_is_525(current_drive) ? 0x40 : 0x00),
                                       0, 0x00);
                dev->input_port = ((dev->input_port + 1) & 3) | (dev->input_port & 0xfc) | (fdd_is_525(current_drive) ? 0x40 : 0x00);
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
                add_to_kbc_queue_front(dev, (dev->input_port | fixed_bits | (video_is_mda() ? 0x40 : 0x00) | (hasfpu ? 0x08 : 0x00)) & 0xdf,
                                       0, 0x00);
                dev->input_port = ((dev->input_port + 1) & 3) | (dev->input_port & 0xfc);
            } else {
                if ((kbc_ven == KBC_VEN_TG) || (kbc_ven == KBC_VEN_TG_GREEN)) {
                    /* Bit 3, 2:
                           1, 1: TriGem logo;
                           1, 0: Garbled logo;
                           0, 1: Epson logo;
                           0, 0: Generic AMI logo. */
                    if (dev->pci)
                        fixed_bits |= 8;
                    add_to_kbc_queue_front(dev, dev->input_port | fixed_bits, 0, 0x00);
                } else if (((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) && ((dev->flags & KBC_VEN_MASK) != KBC_VEN_INTEL_AMI))
#if 0
                    add_to_kbc_queue_front(dev, (dev->input_port | fixed_bits) &
                                          (((dev->flags & KBC_VEN_MASK) == KBC_VEN_ACER) ? 0xeb : 0xef), 0, 0x00);
#else
                    add_to_kbc_queue_front(dev, ((dev->input_port | fixed_bits) & 0xf0) | (((dev->flags & KBC_VEN_MASK) == KBC_VEN_ACER) ? 0x08 : 0x0c), 0, 0x00);
#endif
                else
                    add_to_kbc_queue_front(dev, dev->input_port | fixed_bits, 0, 0x00);
                dev->input_port = ((dev->input_port + 1) & 3) | (dev->input_port & 0xfc);
            }
            return 0;

        case 0xd3: /* write mouse output buffer */
            if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) {
                kbd_log("ATkbc: write mouse output buffer\n");
                dev->want60 = 1;
                dev->kbc_state = KBC_STATE_KBC_PARAM;
                return 0;
            }
            break;

        case 0xd4: /* write to mouse */
            kbd_log("ATkbc: write to mouse\n");
            dev->want60 = 1;
            dev->kbc_state = KBC_STATE_KBC_PARAM;
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
    atkbd_t *dev = (atkbd_t *) priv;

    switch (dev->command) {
        /* 0x40 - 0x5F are aliases for 0x60-0x7F */
        case 0x40 ... 0x5f:
            kbd_log("ATkbc: AMI - alias write to %08X\n", dev->command);
            dev->mem[(dev->command & 0x1f) + 0x20] = val;
            if (dev->command == 0x60)
                write_cmd(dev, val);
            return 0;

        case 0xa5: /* get extended controller RAM */
            kbd_log("ATkbc: AMI - get extended controller RAM\n");
            add_to_kbc_queue_front(dev, dev->mem[val], 0, 0x00);
            return 0;

        case 0xaf: /* set extended controller RAM */
            kbd_log("ATkbc: AMI - set extended controller RAM\n");
            if (dev->secr_phase == 1) {
                dev->mem_addr   = val;
                dev->want60     = 1;
                dev->kbc_state = KBC_STATE_KBC_PARAM;
                dev->secr_phase = 2;
            } else if (dev->secr_phase == 2) {
                dev->mem[dev->mem_addr] = val;
                dev->secr_phase         = 0;
            }
            return 0;

        case 0xc1:
            kbd_log("ATkbc: AMI MegaKey - write %02X to input port\n", val);
            dev->input_port = val;
            return 0;

        case 0xcb: /* set keyboard mode */
            kbd_log("ATkbc: AMI - set keyboard mode\n");
            dev->ami_flags = val;
            return 0;
    }

    return 1;
}

static uint8_t
write64_ami(void *priv, uint8_t val)
{
    atkbd_t *dev     = (atkbd_t *) priv;
    uint8_t  kbc_ven = dev->flags & KBC_VEN_MASK;

    switch (val) {
        case 0x00 ... 0x1f:
            kbd_log("ATkbc: AMI - alias read from %08X\n", val);
            add_to_kbc_queue_front(dev, dev->mem[val + 0x20], 0, 0x00);
            return 0;

        case 0x40 ... 0x5f:
            kbd_log("ATkbc: AMI - alias write to %08X\n", dev->command);
            dev->want60 = 1;
            dev->kbc_state = KBC_STATE_KBC_PARAM;
            return 0;

        case 0xa0: /* copyright message */
            kbc_queue_add(dev, 0x28, 0);
            kbc_queue_add(dev, 0x00, 0);
            dev->kbc_state = KBC_STATE_KBC_OUT;
            break;

        case 0xa1: /* get controller version */
            kbd_log("ATkbc: AMI - get controller version\n");
            if ((kbc_ven == KBC_VEN_TG) || (kbc_ven == KBC_VEN_TG_GREEN))
                    add_to_kbc_queue_front(dev, 'Z', 0, 0x00);
            else if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) {
                if (kbc_ven == KBC_VEN_ALI)
                    add_to_kbc_queue_front(dev, 'F', 0, 0x00);
                else if ((dev->flags & KBC_VEN_MASK) == KBC_VEN_INTEL_AMI)
                    add_to_kbc_queue_front(dev, '5', 0, 0x00);
                else if (cpu_64bitbus)
                    add_to_kbc_queue_front(dev, 'R', 0, 0x00);
                else if (is486)
                    add_to_kbc_queue_front(dev, 'P', 0, 0x00);
                else
                    add_to_kbc_queue_front(dev, 'H', 0, 0x00);
            } else if (is386 && !is486) {
                if (cpu_16bitbus)
                    add_to_kbc_queue_front(dev, 'D', 0, 0x00);
                else
                    add_to_kbc_queue_front(dev, 'B', 0, 0x00);
            } else if (!is386)
                add_to_kbc_queue_front(dev, '8', 0, 0x00);
            else
                add_to_kbc_queue_front(dev, 'F', 0, 0x00);
            return 0;

        case 0xa2: /* clear keyboard controller lines P22/P23 */
            if ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_PS2_NOREF) {
                kbd_log("ATkbc: AMI - clear KBC lines P22 and P23\n");
                write_output(dev, dev->output_port & 0xf3);
                add_to_kbc_queue_front(dev, 0x00, 0, 0x00);
                return 0;
            }
            break;

        case 0xa3: /* set keyboard controller lines P22/P23 */
            if ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_PS2_NOREF) {
                kbd_log("ATkbc: AMI - set KBC lines P22 and P23\n");
                write_output(dev, dev->output_port | 0x0c);
                add_to_kbc_queue_front(dev, 0x00, 0, 0x00);
                return 0;
            }
            break;

        case 0xa4: /* write clock = low */
            if ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_PS2_NOREF) {
                kbd_log("ATkbc: AMI - write clock = low\n");
                dev->ami_stat &= 0xfe;
                return 0;
            }
            break;

        case 0xa5: /* write clock = high */
            if ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_PS2_NOREF) {
                kbd_log("ATkbc: AMI - write clock = high\n");
                dev->ami_stat |= 0x01;
            } else {
                kbd_log("ATkbc: get extended controller RAM\n");
                dev->want60     = 1;
                dev->kbc_state = KBC_STATE_KBC_PARAM;
            }
            return 0;

        case 0xa6: /* read clock */
            if ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_PS2_NOREF) {
                kbd_log("ATkbc: AMI - read clock\n");
                add_to_kbc_queue_front(dev, (dev->ami_stat & 1) ? 0xff : 0x00, 0, 0x00);
                return 0;
            }
            break;

        case 0xa7: /* write cache bad */
            if ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_PS2_NOREF) {
                kbd_log("ATkbc: AMI - write cache bad\n");
                dev->ami_stat &= 0xfd;
                return 0;
            }
            break;

        case 0xa8: /* write cache good */
            if ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_PS2_NOREF) {
                kbd_log("ATkbc: AMI - write cache good\n");
                dev->ami_stat |= 0x02;
                return 0;
            }
            break;

        case 0xa9: /* read cache */
            if ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_PS2_NOREF) {
                kbd_log("ATkbc: AMI - read cache\n");
                add_to_kbc_queue_front(dev, (dev->ami_stat & 2) ? 0xff : 0x00, 0, 0x00);
                return 0;
            }
            break;

        case 0xaf: /* set extended controller RAM */
            if (kbc_ven == KBC_VEN_ALI) {
                kbd_log("ATkbc: Award/ALi/VIA keyboard controller revision\n");
                add_to_kbc_queue_front(dev, 0x43, 0, 0x00);
            } else {
                kbd_log("ATkbc: set extended controller RAM\n");
                dev->want60     = 1;
                dev->kbc_state = KBC_STATE_KBC_PARAM;
                dev->secr_phase = 1;
            }
            return 0;

        case 0xb0 ... 0xb3:
            /* set KBC lines P10-P13 (input port bits 0-3) low */
            kbd_log("ATkbc: set KBC lines P10-P13 (input port bits 0-3) low\n");
            if (!(dev->flags & DEVICE_PCI) || (val > 0xb1))
                dev->input_port &= ~(1 << (val & 0x03));
            add_to_kbc_queue_front(dev, 0x00, 0, 0x00);
            return 0;

        case 0xb4: case 0xb5:
            /* set KBC lines P22-P23 (output port bits 2-3) low */
            kbd_log("ATkbc: set KBC lines P22-P23 (output port bits 2-3) low\n");
            if (!(dev->flags & DEVICE_PCI))
                write_output(dev, dev->output_port & ~(4 << (val & 0x01)));
            add_to_kbc_queue_front(dev, 0x00, 0, 0x00);
            return 0;

        case 0xb8 ... 0xbb:
            /* set KBC lines P10-P13 (input port bits 0-3) high */
            kbd_log("ATkbc: set KBC lines P10-P13 (input port bits 0-3) high\n");
            if (!(dev->flags & DEVICE_PCI) || (val > 0xb9)) {
                dev->input_port |= (1 << (val & 0x03));
                add_to_kbc_queue_front(dev, 0x00, 0, 0x00);
            }
            return 0;

        case 0xbc: case 0xbd:
            /* set KBC lines P22-P23 (output port bits 2-3) high */
            kbd_log("ATkbc: set KBC lines P22-P23 (output port bits 2-3) high\n");
            if (!(dev->flags & DEVICE_PCI))
                write_output(dev, dev->output_port | (4 << (val & 0x01)));
            add_to_kbc_queue_front(dev, 0x00, 0, 0x00);
            return 0;

        case 0xc1: /* write input port */
            kbd_log("ATkbc: AMI MegaKey - write input port\n");
            dev->want60 = 1;
            dev->kbc_state = KBC_STATE_KBC_PARAM;
            return 0;

        case 0xc4:
            /* set KBC line P14 low */
            kbd_log("ATkbc: set KBC line P14 (input port bit 4) low\n");
            dev->input_port &= 0xef;
            add_to_kbc_queue_front(dev, 0x00, 0, 0x00);
            return 0;
        case 0xc5:
            /* set KBC line P15 low */
            kbd_log("ATkbc: set KBC line P15 (input port bit 5) low\n");
            dev->input_port &= 0xdf;
            add_to_kbc_queue_front(dev, 0x00, 0, 0x00);
            return 0;

        case 0xc8:
            /*
             * unblock KBC lines P22/P23
             * (allow command D1 to change bits 2/3 of the output port)
             */
            kbd_log("ATkbc: AMI - unblock KBC lines P22 and P23\n");
            dev->ami_flags &= 0xfb;
            return 0;

        case 0xc9:
            /*
             * block KBC lines P22/P23
             * (disallow command D1 from changing bits 2/3 of the port)
             */
            kbd_log("ATkbc: AMI - block KBC lines P22 and P23\n");
            dev->ami_flags |= 0x04;
            return 0;

        case 0xcc:
            /* set KBC line P14 high */
            kbd_log("ATkbc: set KBC line P14 (input port bit 4) high\n");
            dev->input_port |= 0x10;
            add_to_kbc_queue_front(dev, 0x00, 0, 0x00);
            return 0;
        case 0xcd:
            /* set KBC line P15 high */
            kbd_log("ATkbc: set KBC line P15 (input port bit 5) high\n");
            dev->input_port |= 0x20;
            add_to_kbc_queue_front(dev, 0x00, 0, 0x00);
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
            dev->status |= ((((dev->input_port & 0xfc) | 0x84) & 0x0f) << 4);
            return 0;

        case 0xc2: /*Copy bits 4 to 7 of input port to status bits 4 to 7*/
            kbd_log("ATkbc: copy bits 4 to 7 of input port to status bits 4 to 7\n");
            dev->status &= 0x0f;
            dev->status |= (((dev->input_port & 0xfc) | 0x84) & 0xf0);
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

    switch (dev->command) {
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
        case 0x80: /* Olivetti-specific command */
            /*
             * bit 7: bus expansion board present (M300) / keyboard unlocked (M290)
             * bits 4-6: ???
             * bit 3: fast ram check (if inactive keyboard works erratically)
             * bit 2: keyboard fuse present
             * bits 0-1: ???
             */
            add_to_kbc_queue_front(dev, (0x0c | ((is386) ? 0x00 : 0x80)) & 0xdf, 0, 0x00);
            dev->input_port = ((dev->input_port + 1) & 3) | (dev->input_port & 0xfc);
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
            dev->want60 = 1;
            dev->kbc_state = KBC_STATE_KBC_PARAM;
            return 0;
    }

    return write64_generic(dev, val);
}

static uint8_t
write60_toshiba(void *priv, uint8_t val)
{
    atkbd_t *dev = (atkbd_t *) priv;

    switch (dev->command) {
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
            add_to_kbc_queue_front(dev, t3100e_config_get(), 0, 0x00);
            return 0;

        case 0xb5: /* T3100e: Get colour / mono byte */
            kbd_log("ATkbc: T3100e: Get colour / mono byte\n");
            add_to_kbc_queue_front(dev, t3100e_mono_get(), 0, 0x00);
            return 0;

        case 0xb6: /* T3100e: Set colour / mono byte */
            kbd_log("ATkbc: T3100e: Set colour / mono byte\n");
            dev->want60 = 1;
            dev->kbc_state = KBC_STATE_KBC_PARAM;
            return 0;

        case 0xb7: /* T3100e: Emulate PS/2 keyboard */
        case 0xb8: /* T3100e: Emulate AT keyboard */
            dev->flags &= ~KBC_TYPE_MASK;
            if (val == 0xb7) {
                kbd_log("ATkbc: T3100e: Emulate PS/2 keyboard\n");
                dev->flags |= KBC_TYPE_PS2_NOREF;
            } else {
                kbd_log("ATkbc: T3100e: Emulate AT keyboard\n");
                dev->flags |= KBC_TYPE_ISA;
            }
            return 0;

        case 0xbb: /* T3100e: Read 'Fn' key.
                      Return it for right Ctrl and right Alt; on the real
                      T3100e, these keystrokes could only be generated
                      using 'Fn'. */
            kbd_log("ATkbc: T3100e: Read 'Fn' key\n");
            if (keyboard_recv(0xb8) || /* Right Alt */
                keyboard_recv(0x9d))   /* Right Ctrl */
                add_to_kbc_queue_front(dev, 0x04, 0, 0x00);
            else
                add_to_kbc_queue_front(dev, 0x00, 0, 0x00);
            return 0;

        case 0xbc: /* T3100e: Reset Fn+Key notification */
            kbd_log("ATkbc: T3100e: Reset Fn+Key notification\n");
            t3100e_notify_set(0x00);
            return 0;

        case 0xc0: /*Read input port*/
            kbd_log("ATkbc: read input port\n");

            /* The T3100e returns all bits set except bit 6 which
             * is set by t3100e_mono_set() */
            dev->input_port = (t3100e_mono_get() & 1) ? 0xff : 0xbf;
            add_to_kbc_queue_front(dev, dev->input_port, 0, 0x00);
            return 0;
    }

    return write64_generic(dev, val);
}

static void
kbd_key_reset(atkbd_t *dev, int do_fa)
{
    dev->out_new = -1;
    kbc_queue_reset(dev, 1);

    dev->kbd_last_scan_code = 0x00;

    /* Set scan code set to 2. */
    keyboard_mode = (keyboard_mode & 0xfc) | 0x02;
    set_scancode_map(dev);

    keyboard_scan = 1;

    dev->sc_or = 0;

    if (do_fa)
        add_data_kbd_front(dev, 0xfa);

    dev->reset_delay = RESET_DELAY_TIME;

    if (do_fa)
        dev->kbd_state = DEV_STATE_MAIN_WANT_RESET;
    else
        dev->kbd_state = DEV_STATE_RESET;
}

static void
kbd_aux_reset(atkbd_t *dev, int do_fa)
{
    dev->out_new_mouse = -1;
    kbc_queue_reset(dev, 2);

    mouse_scan = 1;

    if (!do_fa)
        dev->mouse_state = DEV_STATE_MAIN_1;
}

void
keyboard_at_mouse_reset(void)
{
    atkbd_t *dev = SavedKbd;

    kbd_aux_reset(dev, 1);
}

static void
kbd_process_cmd(void *priv)
{
    atkbd_t *dev = (atkbd_t *) priv;

    dev->kbd_state = DEV_STATE_MAIN_OUT;

    if (dev->key_wantdata) {
        dev->key_wantdata = 0;

        /*
         * Several system BIOSes and OS device drivers
         * mess up with this, and repeat the command
         * code many times.  Fun!
         */
        if (dev->key_dat == dev->key_command) {
            /* Respond NAK and ignore it. */
            add_data_kbd_front(dev, 0xfe);
            dev->key_command = 0x00;
            return;
        }

        switch (dev->key_command) {
            case 0xed: /* set/reset LEDs */
                add_data_kbd_front(dev, 0xfa);
                kbd_log("ATkbd: set LEDs [%02x]\n", dev->key_dat);
                break;

            case 0xf0: /* get/set scancode set */
                add_data_kbd_front(dev, 0xfa);
                if (dev->key_dat == 0) {
                    kbd_log("Get scan code set: %02X\n", keyboard_mode & 3);
                    add_data_kbd_front(dev, keyboard_mode & 3);
                } else {
                    if ((dev->key_dat <= 3) && (dev->key_dat != 1)) {
                        keyboard_mode &= 0xfc;
                        keyboard_mode |= (dev->key_dat & 3);
                        kbd_log("Scan code set now: %02X\n", dev->key_dat);
                    }
                    set_scancode_map(dev);
                }
                break;

            case 0xf3: /* set typematic rate/delay */
                add_data_kbd_front(dev, 0xfa);
                break;

            default:
                kbd_log("ATkbd: bad keyboard 0060 write %02X command %02X\n", dev->key_dat, dev->key_command);
                add_data_kbd_front(dev, 0xfe);
                break;
        }

        /* Keyboard command is now done. */
        dev->key_command = 0x00;
    /* Do not process command if the existing command is outputting bytes. */
    } else {
        /* No keyboard command in progress. */
        dev->key_command = 0x00;

        switch (dev->key_dat) {
            case 0x00 ... 0x7f:
                kbd_log("ATkbd: invalid command %02X\n", dev->key_dat);
                add_data_kbd_front(dev, 0xfe);
                break;

            case 0xed: /* set/reset LEDs */
                kbd_log("ATkbd: set/reset leds\n");
                add_data_kbd_front(dev, 0xfa);

                dev->key_wantdata = 1;
                dev->kbd_state = DEV_STATE_MAIN_WANT_IN;
                break;

            case 0xee: /* diagnostic echo */
                kbd_log("ATkbd: ECHO\n");
                add_data_kbd_front(dev, 0xee);
                break;

            case 0xef: /* NOP (reserved for future use) */
                kbd_log("ATkbd: NOP\n");
                break;

            case 0xf0: /* get/set scan code set */
                kbd_log("ATkbd: scan code set\n");
                add_data_kbd_front(dev, 0xfa);
                dev->key_wantdata = 1;
                dev->kbd_state = DEV_STATE_MAIN_WANT_IN;
                break;

            case 0xf2: /* read ID */
                kbd_log("ATkbd: read keyboard id\n");
                /* TODO: After keyboard type selection is implemented, make this
                         return the correct keyboard ID for the selected type. */
                add_data_kbd_front(dev, 0xfa);
                add_data_kbd_front(dev, 0xab);
                add_data_kbd_front(dev, 0x83);
                break;

            case 0xf3: /* set typematic rate/delay */
                kbd_log("ATkbd: set typematic rate/delay\n");
                add_data_kbd_front(dev, 0xfa);
                dev->key_wantdata = 1;
                dev->kbd_state = DEV_STATE_MAIN_WANT_IN;
                break;

            case 0xf4: /* enable keyboard */
                kbd_log("ATkbd: enable keyboard\n");
                add_data_kbd_front(dev, 0xfa);
                keyboard_scan = 1;
                break;

            case 0xf5: /* set defaults and disable keyboard */
            case 0xf6: /* set defaults */
                kbd_log("ATkbd: set defaults%s\n", (dev->key_dat == 0xf6) ? "" : " and disable keyboard");
                keyboard_scan = (dev->key_dat == 0xf6);
                kbd_log("dev->key_dat = %02X, keyboard_scan = %i, dev->mem[0x20] = %02X\n",
                        dev->key_dat, keyboard_scan, dev->mem[0]);
                add_data_kbd_front(dev, 0xfa);

                keyboard_set3_all_break  = 0;
                keyboard_set3_all_repeat = 0;
                memset(keyboard_set3_flags, 0, 512);
                keyboard_mode = (keyboard_mode & 0xfc) | 0x02;
                set_scancode_map(dev);
                break;

            case 0xf7: /* set all keys to repeat */
                kbd_log("ATkbd: set all keys to repeat\n");
                add_data_kbd_front(dev, 0xfa);
                keyboard_set3_all_break = 1;
                break;

            case 0xf8: /* set all keys to give make/break codes */
                kbd_log("ATkbd: set all keys to give make/break codes\n");
                add_data_kbd_front(dev, 0xfa);
                keyboard_set3_all_break = 1;
                break;

            case 0xf9: /* set all keys to give make codes only */
                kbd_log("ATkbd: set all keys to give make codes only\n");
                add_data_kbd_front(dev, 0xfa);
                keyboard_set3_all_break = 0;
                break;

            case 0xfa: /* set all keys to repeat and give make/break codes */
                kbd_log("ATkbd: set all keys to repeat and give make/break codes\n");
                add_data_kbd_front(dev, 0xfa);
                keyboard_set3_all_repeat = 1;
                keyboard_set3_all_break  = 1;
                break;

            case 0xfe: /* resend last scan code */
                kbd_log("ATkbd: resend last scan code\n");
                add_data_kbd_front(dev, dev->kbd_last_scan_code);
                break;

            case 0xff: /* reset */
                kbd_log("ATkbd: kbd reset\n");
                kbd_key_reset(dev, 1);
                break;

           default:
                kbd_log("ATkbd: bad keyboard command %02X\n", dev->key_dat);
                add_data_kbd_front(dev, 0xfe);
        }

        /* If command needs data, remember command. */
        if (dev->key_wantdata == 1)
            dev->key_command = dev->key_dat;
    }
}

static void
kbc_process_cmd(void *priv)
{
    atkbd_t *dev = (atkbd_t *) priv;
    int      i = 0, bad    = 1;
    uint8_t  mask, kbc_ven = dev->flags & KBC_VEN_MASK;
    uint8_t  cmd_ac_conv[16] = { 0x0b, 2, 3, 4, 5, 6, 7, 8, 9, 0x0a, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21 };

    if (dev->status & STAT_CD) {
        /* Controller command. */
        dev->want60 = 0;
        dev->kbc_state = KBC_STATE_MAIN_IBF;

        /* Clear the keyboard controller queue. */
        kbc_queue_reset(dev, 0);

        switch (dev->ib) {
            /* Read data from KBC memory. */
            case 0x20 ... 0x3f:
                add_to_kbc_queue_front(dev, dev->mem[dev->ib], 0, 0x00);
                break;

            /* Write data to KBC memory. */
            case 0x60 ... 0x7f:
                dev->want60 = 1;
                dev->kbc_state = KBC_STATE_KBC_PARAM;
                break;

            case 0xaa: /* self-test */
                kbd_log("ATkbc: self-test\n");

                if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) {
                    if (dev->kbc_state != KBC_STATE_RESET) {
                        kbd_log("ATkbc: self-test reinitialization\n");
                        /* Yes, the firmware has an OR, but we need to make sure to keep any forcibly lowered bytes lowered. */
                        /* TODO: Proper P1 implementation, with OR and AND flags in the machine table. */
                        dev->input_port = dev->input_port & 0xff;
                        write_output(dev, 0x4b);
                    }

                    dev->status = (dev->status & 0x0f) | 0x60;

                    dev->mem[0x20] = 0x30;
                    dev->mem[0x21] = 0x01;
                    dev->mem[0x22] = 0x0b;
                    dev->mem[0x25] = 0x02;
                    dev->mem[0x27] = 0xf8;
                    dev->mem[0x28] = 0xce;
                    dev->mem[0x29] = 0x0b;
                    dev->mem[0x2a] = 0x10;
                    dev->mem[0x2b] = 0x20;
                    dev->mem[0x2c] = 0x15;
                    dev->mem[0x30] = 0x0b;
                } else {
                    if (dev->kbc_state != KBC_STATE_RESET) {
                        kbd_log("ATkbc: self-test reinitialization\n");
                        /* Yes, the firmware has an OR, but we need to make sure to keep any forcibly lowered bytes lowered. */
                        /* TODO: Proper P1 implementation, with OR and AND flags in the machine table. */
                        dev->input_port = dev->input_port & 0xff;
                        write_output(dev, 0xcf);
                    }

                    dev->status = (dev->status & 0x0f) | 0x60;

                    dev->mem[0x20] = 0x10;
                    dev->mem[0x21] = 0x01;
                    dev->mem[0x22] = 0x06;
                    dev->mem[0x25] = 0x01;
                    dev->mem[0x27] = 0xfb;
                    dev->mem[0x28] = 0xe0;
                    dev->mem[0x29] = 0x06;
                    dev->mem[0x2a] = 0x10;
                    dev->mem[0x2b] = 0x20;
                    dev->mem[0x2c] = 0x15;
                }

                dev->out_new = dev->out_new_mouse = -1;
                kbc_queue_reset(dev, 0);

                // dev->kbc_state = KBC_STATE_MAIN_IBF;
                dev->kbc_state = KBC_STATE_KBC_OUT;

                // add_to_kbc_queue_front(dev, 0x55, 0, 0x00);
                kbc_queue_add(dev, 0x55, 0);
                break;

            case 0xab: /* interface test */
                kbd_log("ATkbc: interface test\n");
                add_to_kbc_queue_front(dev, 0x00, 0, 0x00); /*no error*/
                break;

            case 0xac: /* diagnostic dump */
                if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) {
                    kbd_log("ATkbc: diagnostic dump\n");
                   dev->mem[0x30] = (dev->input_port & 0xf0) | 0x80;
                   dev->mem[0x31] = dev->output_port;
                   dev->mem[0x32] = 0x00;    /* T0 and T1. */
                   dev->mem[0x33] = 0x00;    /* PSW - Program Status Word - always return 0x00 because we do not emulate this byte. */
                   /* 20 bytes in high nibble in set 1, low nibble in set 1, set 1 space format = 60 bytes. */
                   for (i = 0; i < 20; i++) {
                       kbc_queue_add(dev, cmd_ac_conv[dev->mem[i + 0x20] >> 4], 0);
                       kbc_queue_add(dev, cmd_ac_conv[dev->mem[i + 0x20] & 0x0f], 0);
                       kbc_queue_add(dev, 0x39, 0);
                   }
                   dev->kbc_state = KBC_STATE_KBC_OUT;
                }
                break;

            case 0xad: /* disable keyboard */
                kbd_log("ATkbc: disable keyboard\n");
                set_enable_kbd(dev, 0);
                break;

            case 0xae: /* enable keyboard */
                kbd_log("ATkbc: enable keyboard\n");
                set_enable_kbd(dev, 1);
                break;

            case 0xc7: /* set port1 bits */
                kbd_log("ATkbc: Phoenix - set port1 bits\n");
                dev->want60 = 1;
                dev->kbc_state = KBC_STATE_KBC_PARAM;
                break;

            case 0xca: /* read keyboard mode */
                kbd_log("ATkbc: AMI - read keyboard mode\n");
                add_to_kbc_queue_front(dev, dev->ami_flags, 0, 0x00);
                break;

            case 0xcb: /* set keyboard mode */
                kbd_log("ATkbc: AMI - set keyboard mode\n");
                dev->want60 = 1;
                dev->kbc_state = KBC_STATE_KBC_PARAM;
                break;

            case 0xd0: /* read output port */
                kbd_log("ATkbc: read output port\n");
                 mask = 0xff;
                if ((kbc_ven != KBC_VEN_OLIVETTI) && ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_PS2_NOREF) && (dev->mem[0x20] & 0x10))
                    mask &= 0xbf;
                add_to_kbc_queue_front(dev, dev->output_port & mask, 0, 0x00);
                break;

            case 0xd1: /* write output port */
                kbd_log("ATkbc: write output port\n");
                dev->want60 = 1;
                dev->kbc_state = KBC_STATE_KBC_PARAM;
                break;

            case 0xd2: /* write keyboard output buffer */
                kbd_log("ATkbc: write keyboard output buffer\n");
                dev->want60 = 1;
                dev->kbc_state = KBC_STATE_KBC_PARAM;
                break;

            case 0xdd: /* disable A20 address line */
            case 0xdf: /* enable A20 address line */
                kbd_log("ATkbc: %sable A20\n", (dev->ib == 0xdd) ? "dis" : "en");
                write_output(dev, (dev->output_port & 0xfd) | (dev->ib & 0x02));
                break;

            case 0xe0: /* read test inputs */
                kbd_log("ATkbc: read test inputs\n");
                add_to_kbc_queue_front(dev, 0x00, 0, 0x00);
                break;

            default:
                /*
                 * Unrecognized controller command.
                 *
                 * If we have a vendor-specific handler, run
                 * that. Otherwise, or if that handler fails,
                 * log a bad command.
                 */
                if (dev->write64_ven)
                    bad = dev->write64_ven(dev, dev->ib);

                kbd_log(bad ? "ATkbc: bad controller command %02X\n" : "", dev->ib);
        }

        /* If the command needs data, remember the command. */
        if (dev->want60)
            dev->command = dev->ib;
    } else if (dev->want60) {
        /* Write data to controller. */
        dev->want60 = 0;
        dev->kbc_state = KBC_STATE_MAIN_IBF;

        switch (dev->command) {
            case 0x60 ... 0x7f:
                dev->mem[(dev->command & 0x1f) + 0x20] = dev->ib;
                if (dev->command == 0x60)
                    write_cmd(dev, dev->ib);
                break;

            case 0xc7: /* set port1 bits */
                kbd_log("ATkbc: Phoenix - set port1 bits\n");
                dev->input_port |= dev->ib;
                break;

            case 0xd1: /* write output port */
                kbd_log("ATkbc: write output port\n");
                /* Bit 2 of AMI flags is P22-P23 blocked (1 = yes, 0 = no),
                   discovered by reverse-engineering the AOpen Vi15G BIOS. */
                if (dev->ami_flags & 0x04) {
                    /* If keyboard controller lines P22-P23 are blocked,
                       we force them to remain unchanged. */
                    dev->ib &= ~0x0c;
                    dev->ib |= (dev->output_port & 0x0c);
                }
                write_output(dev, dev->ib | 0x01);
                break;

            case 0xd2: /* write to keyboard output buffer */
                kbd_log("ATkbc: write to keyboard output buffer\n");
                add_to_kbc_queue_front(dev, dev->ib, 0, 0x00);
                break;

            case 0xd3: /* write to mouse output buffer */
                kbd_log("ATkbc: write to mouse output buffer\n");
                if (mouse_write && ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF))
                    keyboard_at_adddata_mouse(dev->ib);
                break;

            case 0xd4: /* write to mouse */
                kbd_log("ATkbc: write to mouse (%02X)\n", dev->ib);

                if (dev->ib == 0xbb)
                    break;

                if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) {
                    set_enable_mouse(dev, 1);
                    if (mouse_write) {
                        dev->mouse_wantcmd = 1;
                        dev->mouse_dat = dev->ib;
                        dev->kbc_state = KBC_STATE_SEND_MOUSE;
                    } else
                        add_to_kbc_queue_front(dev, 0xfe, 2, 0x40);
                }
                break;

            default:
                /*
                 * Run the vendor-specific handler
                 * if we have one. Otherwise, or if
                 * it returns an error, log a bad
                 * controller command.
                 */
                if (dev->write60_ven)
                    bad = dev->write60_ven(dev, dev->ib);

                if (bad) {
                    kbd_log("ATkbc: bad controller command %02x data %02x\n", dev->command, dev->ib);
                }
        }
    }
}

static void
kbd_write(uint16_t port, uint8_t val, void *priv)
{
    atkbd_t *dev = (atkbd_t *) priv;

    kbd_log((port == 0x61) ? "" : "[%04X:%08X] ATkbc: write(%04X) = %02X\n", CS, cpu_state.pc, port, val);

    switch (port) {
        case 0x60:
            dev->status &= ~STAT_CD;
            if (dev->want60 && (dev->command == 0xd1)) {
                kbd_log("ATkbc: write output port\n");

                /* Fast A20 - ignore all other bits. */
                val = (val & 0x02) | (dev->output_port & 0xfd);

                /* Bit 2 of AMI flags is P22-P23 blocked (1 = yes, 0 = no),
                   discovered by reverse-engineering the AOpeN Vi15G BIOS. */
                if (dev->ami_flags & 0x04) {
                    /* If keyboard controller lines P22-P23 are blocked,
                       we force them to remain unchanged. */
                    val &= ~0x0c;
                    val |= (dev->output_port & 0x0c);
                }

                write_output_fast_a20(dev, val | 0x01);

                dev->want60 = 0;                
                dev->kbc_state = KBC_STATE_MAIN_IBF;
                return;
            }
            break;

        case 0x64:
            dev->status |= STAT_CD;
            if (val == 0xd1) {
                kbd_log("ATkbc: write output port\n");
                dev->want60 = 1;
                dev->kbc_state = KBC_STATE_KBC_PARAM;
                dev->command = 0xd1;
                return;
            }
            break;
    }

    dev->ib = val;
    dev->status |= STAT_IFULL;
}

static uint8_t
kbd_read(uint16_t port, void *priv)
{
    atkbd_t *dev     = (atkbd_t *) priv;
    uint8_t  ret     = 0xff;

    if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF)
        cycles -= ISA_CYCLES(8);

    switch (port) {
        case 0x60:
            ret = dev->out;
            dev->status &= ~STAT_OFULL;
            /* TODO: IRQ is only tied to OBF on the AT KBC, on the PS/2 KBC, it is controlled by a bit the
                     output port (P2).
                     This also means that in AT mode, the IRQ is level-triggered. */
            if ((dev->flags & KBC_TYPE_MASK) < KBC_TYPE_PS2_NOREF)
                picintc(1 << 1);
            break;

        case 0x64:
            ret = dev->status;
            break;

        default:
            kbd_log("ATkbc: read(%04x) invalid!\n",port);
            break;
    }

    kbd_log((port == 0x61) ? "" : "[%04X:%08X] ATkbc: read (%04X) = %02X\n",  CS, cpu_state.pc, port, ret);

    return (ret);
}

static void
kbd_reset(void *priv)
{
    atkbd_t *dev = (atkbd_t *) priv;
    int      i;
    uint8_t  kbc_ven = dev->flags & KBC_VEN_MASK;

    dev->status = STAT_UNLOCKED;
    dev->mem[0x20] = 0x01;
    dev->mem[0x20] |= CCB_TRANSLATE;
    dev->secr_phase                   = 0;
    dev->key_wantdata                 = 0;

    /* Set up the correct Video Type bits. */
    if (!is286 || (kbc_ven == KBC_VEN_ACER))
        dev->input_port = video_is_mda() ? 0xb0 : 0xf0;
    else
        dev->input_port = video_is_mda() ? 0xf0 : 0xb0;
    kbd_log("ATkbc: input port = %02x\n", dev->input_port);

    keyboard_mode = 0x02 | (dev->mem[0x20] & CCB_TRANSLATE);

    /* Enable keyboard, disable mouse. */
    set_enable_kbd(dev, 1);
    keyboard_scan = 1;
    set_enable_mouse(dev, 0);
    mouse_scan = 0;

    dev->out_new = dev->out_new_mouse = -1;
    for (i = 0; i < 3; i++)
        kbc_queue_reset(dev, i);
    dev->kbd_last_scan_code = 0;

    dev->sc_or = 0;

    memset(keyboard_set3_flags, 0, 512);

    set_scancode_map(dev);

    dev->ami_flags = ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) ? 0x01 : 0x00;
    dev->ami_stat |= 0x02;

    dev->output_port = 0xcd;
    if ((dev->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) {
        write_output(dev, 0x4b);
    } else {
        /* The real thing writes CF and then AND's it with BF. */
        write_output(dev, 0x8f);
    }

    /* Stage 1. */
    dev->status = (dev->status & 0x0f) | (dev->input_port & 0xf0);
    /* Wait for command AA. */
    dev->kbc_state = KBC_STATE_RESET;

    /* Reset the keyboard. */
    kbd_key_reset(dev, 0);

    /* Reset the mouse. */
    kbd_aux_reset(dev, 0);
}

/* Reset the AT keyboard - this is needed for the PCI TRC and is done
   until a better solution is found. */
void
keyboard_at_reset(void)
{
    kbd_reset(SavedKbd);
}

void
kbc_at_a20_reset(void)
{
    if (SavedKbd) {
        SavedKbd->output_port = 0xcd;
        if ((SavedKbd->flags & KBC_TYPE_MASK) >= KBC_TYPE_PS2_NOREF) {
            write_output(SavedKbd, 0x4b);
        } else {
            /* The real thing writes CF and then AND's it with BF. */
            write_output(SavedKbd, 0x8f);
        }
    }
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
    dev->pci = !!(info->flags & DEVICE_PCI);

    /* We need this, sadly. */
    SavedKbd = dev;

    video_reset(gfxcard[0]);
    kbd_reset(dev);

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
            dev->write64_ven = write64_generic;
            break;

        case KBC_VEN_OLIVETTI:
            dev->write64_ven = write64_olivetti;
            break;

        case KBC_VEN_AMI:
        case KBC_VEN_INTEL_AMI:
        case KBC_VEN_ALI:
        case KBC_VEN_TG:
        case KBC_VEN_TG_GREEN:
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

        case KBC_VEN_TOSHIBA:
            dev->write60_ven = write60_toshiba;
            dev->write64_ven = write64_toshiba;
            break;
    }

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

const device_t keyboard_at_tg_ami_device = {
    .name          = "PC/AT Keyboard (TriGem AMI)",
    .internal_name = "keyboard_at_tg_ami",
    .flags         = 0,
    .local         = KBC_TYPE_ISA | KBC_VEN_TG,
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
    .internal_name = "keyboard_ps2",
    .flags         = 0,
    .local         = KBC_TYPE_PS2_NOREF | KBC_VEN_GENERIC,
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
    .local         = KBC_TYPE_PS2_NOREF | KBC_VEN_IBM_PS1,
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
    .local         = KBC_TYPE_PS2_NOREF | KBC_VEN_IBM_PS1,
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
    .local         = KBC_TYPE_PS2_1 | KBC_VEN_GENERIC,
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
    .local         = KBC_TYPE_PS2_NOREF | KBC_VEN_AMI,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_tg_ami_device = {
    .name          = "PS/2 Keyboard (TriGem AMI)",
    .internal_name = "keyboard_ps2_tg_ami",
    .flags         = 0,
    .local         = KBC_TYPE_PS2_NOREF | KBC_VEN_TG,
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
    .local         = KBC_TYPE_PS2_NOREF | KBC_VEN_QUADTEL,
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
    .local         = KBC_TYPE_PS2_NOREF | KBC_VEN_GENERIC,
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
    .local         = KBC_TYPE_PS2_NOREF | KBC_VEN_AMI,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_ali_pci_device = {
    .name          = "PS/2 Keyboard (ALi M5123/M1543C)",
    .internal_name = "keyboard_ps2_ali_pci",
    .flags         = DEVICE_PCI,
    .local         = KBC_TYPE_PS2_NOREF | KBC_VEN_ALI,
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
    .local         = KBC_TYPE_PS2_NOREF | KBC_VEN_INTEL_AMI,
    .init          = kbd_init,
    .close         = kbd_close,
    .reset         = kbd_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t keyboard_ps2_tg_ami_pci_device = {
    .name          = "PS/2 Keyboard (TriGem AMI)",
    .internal_name = "keyboard_ps2_tg_ami_pci",
    .flags         = DEVICE_PCI,
    .local         = KBC_TYPE_PS2_NOREF | KBC_VEN_TG,
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
    .local         = KBC_TYPE_PS2_NOREF | KBC_VEN_ACER,
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
    mouse_write = func;
    mouse_p     = priv;
}

void
keyboard_at_adddata_mouse(uint8_t val)
{
    atkbd_t *dev = SavedKbd;

    if (!mouse_scan || (dev->mouse_reset_delay > 0) || (dev->mouse_queue_end >= 16)) {
        kbd_log("ATkbc: Unable to add to queue, conditions: %i, %i, %i\n", !mouse_scan, (dev->mouse_reset_delay > 0), (dev->mouse_queue_end >= 16));
        return;
    }
    kbc_queue_add(dev, val, 2);
}

void
keyboard_at_adddata_mouse_cmd(uint8_t val)
{
    atkbd_t *dev = SavedKbd;

    if ((dev->mouse_reset_delay > 0) || (dev->mouse_cmd_queue_end >= 16)) {
        kbd_log("ATkbc: Unable to add to queue, conditions: %i, %i\n", (dev->mouse_reset_delay > 0), (dev->mouse_cmd_queue_end >= 16));
        return;
    }
    kbc_queue_add(dev, val, 3);
}

uint8_t
keyboard_at_mouse_pos(void)
{
    atkbd_t *dev = SavedKbd;

    return ((dev->mouse_queue_end - dev->mouse_queue_start) & 0xf);
}

void
keyboard_at_set_a20_key(int state)
{
    atkbd_t *dev = SavedKbd;

    write_output(dev, (dev->output_port & 0xfd) | ((!!state) << 1));
}
