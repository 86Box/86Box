/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the IBM PCjr.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/cassette.h>
#include <86box/io.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/serial.h>
#include <86box/keyboard.h>
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/snd_sn76489.h>
#include <86box/video.h>
#include <86box/vid_cga_comp.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>

#define PCJR_RGB       0
#define PCJR_COMPOSITE 1

#define STAT_PARITY    0x80
#define STAT_RTIMEOUT  0x40
#define STAT_TTIMEOUT  0x20
#define STAT_LOCK      0x10
#define STAT_CD        0x08
#define STAT_SYSFLAG   0x04
#define STAT_IFULL     0x02
#define STAT_OFULL     0x01

typedef struct pcjr_t {
    /* Video Controller stuff. */
    mem_mapping_t mapping;
    uint8_t       crtc[32];
    int           crtcreg;
    int           array_index;
    uint8_t       array[32];
    int           array_ff;
    int           memctrl;
    uint8_t       stat;
    int           addr_mode;
    uint8_t      *vram;
    uint8_t      *b8000;
    int           linepos;
    int           displine;
    int           sc;
    int           vc;
    int           dispon;
    int           con;
    int           coff;
    int           cursoron;
    int           blink;
    int           vsynctime;
    int           fullchange;
    int           vadj;
    uint16_t      ma;
    uint16_t      maback;
    uint64_t      dispontime;
    uint64_t      dispofftime;
    pc_timer_t    timer;
    int           firstline;
    int           lastline;
    int           composite;
    int           apply_hd;

    /* Keyboard Controller stuff. */
    int        latched;
    int        data;
    int        serial_data[44];
    int        serial_pos;
    uint8_t    pa;
    uint8_t    pb;
    pc_timer_t send_delay_timer;
} pcjr_t;

/*PCjr keyboard has no escape scancodes, and no scancodes beyond 54
  Map right alt to 54h (FN) */
const scancode scancode_pcjr[512] = {
  // clang-format off
    { .mk = {            0 }, .brk = {                   0 } }, /* 000 */
    { .mk = {      0x01, 0 }, .brk = {             0x81, 0 } }, /* 001 */
    { .mk = {      0x02, 0 }, .brk = {             0x82, 0 } }, /* 002 */
    { .mk = {      0x03, 0 }, .brk = {             0x83, 0 } }, /* 003 */
    { .mk = {      0x04, 0 }, .brk = {             0x84, 0 } }, /* 004 */
    { .mk = {      0x05, 0 }, .brk = {             0x85, 0 } }, /* 005 */
    { .mk = {      0x06, 0 }, .brk = {             0x86, 0 } }, /* 006 */
    { .mk = {      0x07, 0 }, .brk = {             0x87, 0 } }, /* 007 */
    { .mk = {      0x08, 0 }, .brk = {             0x88, 0 } }, /* 008 */
    { .mk = {      0x09, 0 }, .brk = {             0x89, 0 } }, /* 009 */
    { .mk = {      0x0a, 0 }, .brk = {             0x8a, 0 } }, /* 00a */
    { .mk = {      0x0b, 0 }, .brk = {             0x8b, 0 } }, /* 00b */
    { .mk = {      0x0c, 0 }, .brk = {             0x8c, 0 } }, /* 00c */
    { .mk = {      0x0d, 0 }, .brk = {             0x8d, 0 } }, /* 00d */
    { .mk = {      0x0e, 0 }, .brk = {             0x8e, 0 } }, /* 00e */
    { .mk = {      0x0f, 0 }, .brk = {             0x8f, 0 } }, /* 00f */
    { .mk = {      0x10, 0 }, .brk = {             0x90, 0 } }, /* 010 */
    { .mk = {      0x11, 0 }, .brk = {             0x91, 0 } }, /* 011 */
    { .mk = {      0x12, 0 }, .brk = {             0x92, 0 } }, /* 012 */
    { .mk = {      0x13, 0 }, .brk = {             0x93, 0 } }, /* 013 */
    { .mk = {      0x14, 0 }, .brk = {             0x94, 0 } }, /* 014 */
    { .mk = {      0x15, 0 }, .brk = {             0x95, 0 } }, /* 015 */
    { .mk = {      0x16, 0 }, .brk = {             0x96, 0 } }, /* 016 */
    { .mk = {      0x17, 0 }, .brk = {             0x97, 0 } }, /* 017 */
    { .mk = {      0x18, 0 }, .brk = {             0x98, 0 } }, /* 018 */
    { .mk = {      0x19, 0 }, .brk = {             0x99, 0 } }, /* 019 */
    { .mk = {      0x1a, 0 }, .brk = {             0x9a, 0 } }, /* 01a */
    { .mk = {      0x1b, 0 }, .brk = {             0x9b, 0 } }, /* 01b */
    { .mk = {      0x1c, 0 }, .brk = {             0x9c, 0 } }, /* 01c */
    { .mk = {      0x1d, 0 }, .brk = {             0x9d, 0 } }, /* 01d */
    { .mk = {      0x1e, 0 }, .brk = {             0x9e, 0 } }, /* 01e */
    { .mk = {      0x1f, 0 }, .brk = {             0x9f, 0 } }, /* 01f */
    { .mk = {      0x20, 0 }, .brk = {             0xa0, 0 } }, /* 020 */
    { .mk = {      0x21, 0 }, .brk = {             0xa1, 0 } }, /* 021 */
    { .mk = {      0x22, 0 }, .brk = {             0xa2, 0 } }, /* 022 */
    { .mk = {      0x23, 0 }, .brk = {             0xa3, 0 } }, /* 023 */
    { .mk = {      0x24, 0 }, .brk = {             0xa4, 0 } }, /* 024 */
    { .mk = {      0x25, 0 }, .brk = {             0xa5, 0 } }, /* 025 */
    { .mk = {      0x26, 0 }, .brk = {             0xa6, 0 } }, /* 026 */
    { .mk = {      0x27, 0 }, .brk = {             0xa7, 0 } }, /* 027 */
    { .mk = {      0x28, 0 }, .brk = {             0xa8, 0 } }, /* 028 */
    { .mk = {      0x29, 0 }, .brk = {             0xa9, 0 } }, /* 029 */
    { .mk = {      0x2a, 0 }, .brk = {             0xaa, 0 } }, /* 02a */
    { .mk = {      0x2b, 0 }, .brk = {             0xab, 0 } }, /* 02b */
    { .mk = {      0x2c, 0 }, .brk = {             0xac, 0 } }, /* 02c */
    { .mk = {      0x2d, 0 }, .brk = {             0xad, 0 } }, /* 02d */
    { .mk = {      0x2e, 0 }, .brk = {             0xae, 0 } }, /* 02e */
    { .mk = {      0x2f, 0 }, .brk = {             0xaf, 0 } }, /* 02f */
    { .mk = {      0x30, 0 }, .brk = {             0xb0, 0 } }, /* 030 */
    { .mk = {      0x31, 0 }, .brk = {             0xb1, 0 } }, /* 031 */
    { .mk = {      0x32, 0 }, .brk = {             0xb2, 0 } }, /* 032 */
    { .mk = {      0x33, 0 }, .brk = {             0xb3, 0 } }, /* 033 */
    { .mk = {      0x34, 0 }, .brk = {             0xb4, 0 } }, /* 034 */
    { .mk = {      0x35, 0 }, .brk = {             0xb5, 0 } }, /* 035 */
    { .mk = {      0x36, 0 }, .brk = {             0xb6, 0 } }, /* 036 */
    { .mk = {      0x37, 0 }, .brk = {             0xb7, 0 } }, /* 037 */
    { .mk = {      0x38, 0 }, .brk = {             0xb8, 0 } }, /* 038 */
    { .mk = {      0x39, 0 }, .brk = {             0xb9, 0 } }, /* 039 */
    { .mk = {      0x3a, 0 }, .brk = {             0xba, 0 } }, /* 03a */
    { .mk = {      0x3b, 0 }, .brk = {             0xbb, 0 } }, /* 03b */
    { .mk = {      0x3c, 0 }, .brk = {             0xbc, 0 } }, /* 03c */
    { .mk = {      0x3d, 0 }, .brk = {             0xbd, 0 } }, /* 03d */
    { .mk = {      0x3e, 0 }, .brk = {             0xbe, 0 } }, /* 03e */
    { .mk = {      0x3f, 0 }, .brk = {             0xbf, 0 } }, /* 03f */
    { .mk = {      0x40, 0 }, .brk = {             0xc0, 0 } }, /* 040 */
    { .mk = {      0x41, 0 }, .brk = {             0xc1, 0 } }, /* 041 */
    { .mk = {      0x42, 0 }, .brk = {             0xc2, 0 } }, /* 042 */
    { .mk = {      0x43, 0 }, .brk = {             0xc3, 0 } }, /* 043 */
    { .mk = {      0x44, 0 }, .brk = {             0xc4, 0 } }, /* 044 */
    { .mk = {      0x45, 0 }, .brk = {             0xc5, 0 } }, /* 045 */
    { .mk = {      0x46, 0 }, .brk = {             0xc6, 0 } }, /* 046 */
    { .mk = {      0x47, 0 }, .brk = {             0xc7, 0 } }, /* 047 */
    { .mk = {      0x48, 0 }, .brk = {             0xc8, 0 } }, /* 048 */
    { .mk = {      0x49, 0 }, .brk = {             0xc9, 0 } }, /* 049 */
    { .mk = {      0x4a, 0 }, .brk = {             0xca, 0 } }, /* 04a */
    { .mk = {      0x4b, 0 }, .brk = {             0xcb, 0 } }, /* 04b */
    { .mk = {      0x4c, 0 }, .brk = {             0xcc, 0 } }, /* 04c */
    { .mk = {      0x4d, 0 }, .brk = {             0xcd, 0 } }, /* 04d */
    { .mk = {      0x4e, 0 }, .brk = {             0xce, 0 } }, /* 04e */
    { .mk = {      0x4f, 0 }, .brk = {             0xcf, 0 } }, /* 04f */
    { .mk = {      0x50, 0 }, .brk = {             0xd0, 0 } }, /* 050 */
    { .mk = {      0x51, 0 }, .brk = {             0xd1, 0 } }, /* 051 */
    { .mk = {      0x52, 0 }, .brk = {             0xd2, 0 } }, /* 052 */
    { .mk = {      0x53, 0 }, .brk = {             0xd3, 0 } }, /* 053 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 054 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 055 */
    { .mk = {      0x55, 0 }, .brk = {             0xd5, 0 } }, /* 056 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 057 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 058 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 059 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 05f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 060 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 061 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 062 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 063 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 064 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 065 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 066 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 067 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 068 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 069 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 06f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 070 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 071 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 072 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 073 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 074 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 075 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 076 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 077 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 078 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 079 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 07f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 080 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 081 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 082 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 083 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 084 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 085 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 086 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 087 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 088 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 089 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 08f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 090 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 091 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 092 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 093 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 094 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 095 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 096 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 097 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 098 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 099 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 09f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0a9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0aa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ab */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ac */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ad */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ae */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0af */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0b9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ba */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0bb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0bc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0bd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0be */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0bf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0c9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ca */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0cb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0cc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0cd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ce */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0cf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0d9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0da */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0db */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0dc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0dd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0de */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0df */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0e9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ea */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0eb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ec */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ed */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ee */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ef */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0f9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0fe */
    { .mk = {            0 }, .brk = {                   0 } }, /* 0ff */
    { .mk = {            0 }, .brk = {                   0 } }, /* 100 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 101 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 102 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 103 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 104 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 105 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 106 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 107 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 108 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 109 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 10f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 110 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 111 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 112 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 113 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 114 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 115 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 116 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 117 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 118 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 119 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11b */
    { .mk = {      0x1c, 0 }, .brk = {             0x9c, 0 } }, /* 11c */
    { .mk = {      0x1d, 0 }, .brk = {             0x9d, 0 } }, /* 11d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 120 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 121 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 122 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 123 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 124 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 125 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 126 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 127 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 128 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 129 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 12f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 130 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 131 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 132 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 133 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 134 */
    { .mk = {      0x35, 0 }, .brk = {             0xb5, 0 } }, /* 135 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 136 */
    { .mk = {      0x37, 0 }, .brk = {             0xb7, 0 } }, /* 137 */
    { .mk = {      0x54, 0 }, .brk = {             0xd4, 0 } }, /* 138 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 139 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 13f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 140 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 141 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 142 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 143 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 144 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 145 */
    { .mk = {      0x46, 0 }, .brk = {             0xc6, 0 } }, /* 146 */
    { .mk = {      0x47, 0 }, .brk = {             0xc7, 0 } }, /* 147 */
    { .mk = {      0x48, 0 }, .brk = {             0xc8, 0 } }, /* 148 */
    { .mk = {      0x49, 0 }, .brk = {             0xc9, 0 } }, /* 149 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 14a */
    { .mk = {      0x4b, 0 }, .brk = {             0xcb, 0 } }, /* 14b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 14c */
    { .mk = {      0x4d, 0 }, .brk = {             0xcd, 0 } }, /* 14d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 14e */
    { .mk = {      0x4f, 0 }, .brk = {             0xcf, 0 } }, /* 14f */
    { .mk = {      0x50, 0 }, .brk = {             0xd0, 0 } }, /* 150 */
    { .mk = {      0x51, 0 }, .brk = {             0xd1, 0 } }, /* 151 */
    { .mk = {      0x52, 0 }, .brk = {             0xd2, 0 } }, /* 152 */
    { .mk = {      0x53, 0 }, .brk = {             0xd3, 0 } }, /* 153 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 154 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 155 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 156 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 157 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 158 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 159 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 15a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 15b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 15c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 15d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 15e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 15f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 160 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 161 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 162 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 163 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 164 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 165 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 166 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 167 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 168 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 169 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 16f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 170 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 171 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 172 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 173 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 174 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 175 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 176 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 177 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 178 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 179 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 17f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 180 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 181 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 182 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 183 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 184 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 185 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 186 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 187 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 188 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 189 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 18f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 190 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 191 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 192 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 193 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 194 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 195 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 196 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 197 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 198 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 199 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19a */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19e */
    { .mk = {            0 }, .brk = {                   0 } }, /* 19f */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1a9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1aa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ab */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ac */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ad */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ae */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1af */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1b9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ba */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1be */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1bf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1c9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ca */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ce */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1cf */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1d9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1da */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1db */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1dc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1dd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1de */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1df */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1e9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ea */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1eb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ec */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ed */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ee */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1ef */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f0 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f1 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f2 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f3 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f4 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f5 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f6 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f7 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f8 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1f9 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fa */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fb */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fc */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fd */
    { .mk = {            0 }, .brk = {                   0 } }, /* 1fe */
    { .mk = {            0 }, .brk = {                   0 } }  /* 1ff */
  // clang-format on
};

static video_timings_t timing_dram = { VIDEO_BUS, 0, 0, 0, 0, 0, 0 }; /*No additional waitstates*/

static uint8_t crtcmask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static uint8_t key_queue[16];
static int     key_queue_start = 0;
static int     key_queue_end   = 0;

static void
recalc_address(pcjr_t *pcjr)
{
    uint8_t masked_memctrl = pcjr->memctrl;

    /* According to the Technical Reference, bits 2 and 5 are
       ignored if there is only 64k of RAM and there are only
       4 pages. */
    if (mem_size < 128)
        masked_memctrl &= ~0x24;

    if ((pcjr->memctrl & 0xc0) == 0xc0) {
        pcjr->vram  = &ram[(masked_memctrl & 0x06) << 14];
        pcjr->b8000 = &ram[(masked_memctrl & 0x30) << 11];
    } else {
        pcjr->vram  = &ram[(masked_memctrl & 0x07) << 14];
        pcjr->b8000 = &ram[(masked_memctrl & 0x38) << 11];
    }
}

static void
recalc_timings(pcjr_t *pcjr)
{
    double _dispontime;
    double _dispofftime;
    double disptime;

    if (pcjr->array[0] & 1) {
        disptime    = pcjr->crtc[0] + 1;
        _dispontime = pcjr->crtc[1];
    } else {
        disptime    = (pcjr->crtc[0] + 1) << 1;
        _dispontime = pcjr->crtc[1] << 1;
    }

    _dispofftime = disptime - _dispontime;
    _dispontime *= CGACONST;
    _dispofftime *= CGACONST;
    pcjr->dispontime  = (uint64_t) (_dispontime);
    pcjr->dispofftime = (uint64_t) (_dispofftime);
}

static int
vid_get_h_overscan_size(pcjr_t *pcjr)
{
    int ret;

    if (pcjr->array[0] & 1)
        ret = 128;
    else
        ret = 256;

    return ret;
}

static void
vid_out(uint16_t addr, uint8_t val, void *priv)
{
    pcjr_t *pcjr = (pcjr_t *) priv;
    uint8_t old;

    switch (addr) {
        case 0x3d0:
        case 0x3d2:
        case 0x3d4:
        case 0x3d6:
            pcjr->crtcreg = val & 0x1f;
            return;

        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
            old                       = pcjr->crtc[pcjr->crtcreg];
            pcjr->crtc[pcjr->crtcreg] = val & crtcmask[pcjr->crtcreg];
            if (pcjr->crtcreg == 2)
                overscan_x = vid_get_h_overscan_size(pcjr);
            if (old != val) {
                if (pcjr->crtcreg < 0xe || pcjr->crtcreg > 0x10) {
                    pcjr->fullchange = changeframecount;
                    recalc_timings(pcjr);
                }
            }
            return;

        case 0x3da:
            if (!pcjr->array_ff)
                pcjr->array_index = val & 0x1f;
            else {
                if (pcjr->array_index & 0x10)
                    val &= 0x0f;
                pcjr->array[pcjr->array_index & 0x1f] = val;
                if (!(pcjr->array_index & 0x1f))
                    update_cga16_color(val);
            }
            pcjr->array_ff = !pcjr->array_ff;
            break;

        case 0x3df:
            pcjr->memctrl   = val;
            pcjr->pa        = val; /* The PCjr BIOS expects the value written to 3DF to
                                      then be readable from port 60, others it errors out
                                      with only 64k RAM set (but somehow, still works with
                                      128k or more RAM). */
            pcjr->addr_mode = val >> 6;
            recalc_address(pcjr);
            break;

        default:
            break;
    }
}

static uint8_t
vid_in(uint16_t addr, void *priv)
{
    pcjr_t *pcjr = (pcjr_t *) priv;
    uint8_t ret  = 0xff;

    switch (addr) {
        case 0x3d0:
        case 0x3d2:
        case 0x3d4:
        case 0x3d6:
            ret = pcjr->crtcreg;
            break;

        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
            ret = pcjr->crtc[pcjr->crtcreg];
            break;

        case 0x3da:
            pcjr->array_ff = 0;
            pcjr->stat ^= 0x10;
            ret = pcjr->stat;
            break;

        default:
            break;
    }

    return ret;
}

static void
vid_write(uint32_t addr, uint8_t val, void *priv)
{
    pcjr_t *pcjr = (pcjr_t *) priv;

    if (pcjr->memctrl == -1)
        return;

    pcjr->b8000[addr & 0x3fff] = val;
}

static uint8_t
vid_read(uint32_t addr, void *priv)
{
    const pcjr_t *pcjr = (pcjr_t *) priv;

    if (pcjr->memctrl == -1)
        return 0xff;

    return (pcjr->b8000[addr & 0x3fff]);
}

static int
vid_get_h_overscan_delta(pcjr_t *pcjr)
{
    int def;
    int coef;
    int ret;

    switch ((pcjr->array[0] & 0x13) | ((pcjr->array[3] & 0x08) << 5)) {
        case 0x13: /*320x200x16*/
            def = 0x56;
            coef = 8;
            break;
        case 0x12: /*160x200x16*/
            def = 0x2c; /* I'm going to assume a datasheet erratum here. */
            coef = 16;
            break;
        case 0x03: /*640x200x4*/
            def = 0x56;
            coef = 8;
            break;
        case 0x01: /*80 column text*/
            def = 0x5a;
            coef = 8;
            break;
        case 0x00: /*40 column text*/
        default:
            def = 0x2c;
            coef = 16;
            break;
        case 0x02: /*320x200x4*/
            def = 0x2b;
            coef = 16;
            break;
        case 0x102: /*640x200x2*/
            def = 0x2b;
            coef = 16;
            break;
    }

    ret = pcjr->crtc[0x02] - def;

    if (ret < -8)
        ret = -8;

    if (ret > 8)
        ret = 8;

    return ret * coef;
}

static void
vid_blit_h_overscan(pcjr_t *pcjr)
{
    int      cols = (pcjr->array[2] & 0xf) + 16;;
    int      y0   = pcjr->firstline << 1;
    int      y    = (pcjr->lastline << 1) + 16;
    int      ho_s = vid_get_h_overscan_size(pcjr);
    int      i;
    int      x;

    if (pcjr->array[0] & 1)
        x = (pcjr->crtc[1] << 3) + ho_s;
    else
        x = (pcjr->crtc[1] << 4) + ho_s;

    for (i = 0; i < 16; i++) {
        hline(buffer32, 0, y0 + i, x, cols);
        hline(buffer32, 0, y + i, x, cols);

        if (pcjr->composite) {
            Composite_Process(pcjr->array[0], 0, x >> 2, buffer32->line[y0 + i]);
            Composite_Process(pcjr->array[0], 0, x >> 2, buffer32->line[y + i]);
        } else {
            video_process_8(x, y0 + i);
            video_process_8(x, y + i);
        }
    }
}

static void
vid_poll(void *priv)
{
    pcjr_t  *pcjr = (pcjr_t *) priv;
    uint16_t ca   = (pcjr->crtc[15] | (pcjr->crtc[14] << 8)) & 0x3fff;
    int      drawcursor;
    int      x;
    int      xs_temp;
    int      ys_temp;
    int      oldvc;
    uint8_t  chr;
    uint8_t  attr;
    uint16_t dat;
    int      cols[4];
    int      oldsc;
    int      l = (pcjr->displine << 1) + 16;
    int      ho_s = vid_get_h_overscan_size(pcjr);
    int      ho_d = vid_get_h_overscan_delta(pcjr) + (ho_s / 2);

    if (!pcjr->linepos) {
        timer_advance_u64(&pcjr->timer, pcjr->dispofftime);
        pcjr->stat &= ~1;
        pcjr->linepos = 1;
        oldsc         = pcjr->sc;
        if ((pcjr->crtc[8] & 3) == 3)
            pcjr->sc = (pcjr->sc << 1) & 7;
        if (pcjr->dispon) {
            uint16_t offset = 0;
            uint16_t mask   = 0x1fff;

            if (pcjr->displine < pcjr->firstline) {
                pcjr->firstline = pcjr->displine;
                video_wait_for_buffer();
            }
            pcjr->lastline = pcjr->displine;
            cols[0]        = (pcjr->array[2] & 0xf) + 16;

            if (pcjr->array[0] & 1) {
                hline(buffer32, 0, l, (pcjr->crtc[1] << 3) + ho_s, cols[0]);
                hline(buffer32, 0, l + 1, (pcjr->crtc[1] << 3) + ho_s, cols[0]);
            } else {
                hline(buffer32, 0, l, (pcjr->crtc[1] << 4) + ho_s, cols[0]);
                hline(buffer32, 0, l + 1, (pcjr->crtc[1] << 4) + ho_s, cols[0]);
            }

            switch (pcjr->addr_mode) {
                case 0: /*Alpha*/
                    offset = 0;
                    mask   = 0x3fff;
                    break;
                case 1: /*Low resolution graphics*/
                    offset = (pcjr->sc & 1) * 0x2000;
                    break;
                case 3: /*High resolution graphics*/
                    offset = (pcjr->sc & 3) * 0x2000;
                    break;
                default:
                    break;
            }
            switch ((pcjr->array[0] & 0x13) | ((pcjr->array[3] & 0x08) << 5)) {
                case 0x13: /*320x200x16*/
                    for (x = 0; x < pcjr->crtc[1]; x++) {
                        int ef_x = (x << 3) + ho_d;
                        dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) |
                              pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
                        pcjr->ma++;
                        buffer32->line[l][ef_x] = buffer32->line[l][ef_x + 1] =
                        buffer32->line[l + 1][ef_x] = buffer32->line[l + 1][ef_x + 1] =
                            pcjr->array[((dat >> 12) & pcjr->array[1] & 0x0f) + 16] + 16;
                        buffer32->line[l][ef_x + 2] = buffer32->line[l][ef_x + 3] =
                        buffer32->line[l + 1][ef_x + 2] = buffer32->line[l + 1][ef_x + 3] =
                            pcjr->array[((dat >> 8) & pcjr->array[1] & 0x0f) + 16] + 16;
                        buffer32->line[l][ef_x + 4] = buffer32->line[l][ef_x + 5] =
                        buffer32->line[l + 1][ef_x + 4] = buffer32->line[l + 1][ef_x + 5] =
                            pcjr->array[((dat >> 4) & pcjr->array[1] & 0x0f) + 16] + 16;
                        buffer32->line[l][ef_x + 6] = buffer32->line[l][ef_x + 7] =
                        buffer32->line[l + 1][ef_x + 6] = buffer32->line[l + 1][ef_x + 7] =
                            pcjr->array[(dat & pcjr->array[1] & 0x0f) + 16] + 16;
                    }
                    break;
                case 0x12: /*160x200x16*/
                    for (x = 0; x < pcjr->crtc[1]; x++) {
                        int ef_x = (x << 4) + ho_d;
                        dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) |
                              pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
                        pcjr->ma++;
                        buffer32->line[l][ef_x] = buffer32->line[l][ef_x + 1] =
                        buffer32->line[l][ef_x + 2] = buffer32->line[l][ef_x + 3] =
                        buffer32->line[l + 1][ef_x] = buffer32->line[l + 1][ef_x + 1] =
                        buffer32->line[l + 1][ef_x + 2] = buffer32->line[l + 1][ef_x + 3] =
                            pcjr->array[((dat >> 12) & pcjr->array[1] & 0x0f) + 16] + 16;
                        buffer32->line[l][ef_x + 4] = buffer32->line[l][ef_x + 5] =
                        buffer32->line[l][ef_x + 6] = buffer32->line[l][ef_x + 7] =
                        buffer32->line[l + 1][ef_x + 4] = buffer32->line[l + 1][ef_x + 5] =
                        buffer32->line[l + 1][ef_x + 6] = buffer32->line[l + 1][ef_x + 7] =
                            pcjr->array[((dat >> 8) & pcjr->array[1] & 0x0f) + 16] + 16;
                        buffer32->line[l][ef_x + 8] = buffer32->line[l][ef_x + 9] =
                        buffer32->line[l][ef_x + 10] = buffer32->line[l][ef_x + 11] =
                        buffer32->line[l + 1][ef_x + 8] = buffer32->line[l + 1][ef_x + 9] =
                        buffer32->line[l + 1][ef_x + 10] = buffer32->line[l + 1][ef_x + 11] =
                            pcjr->array[((dat >> 4) & pcjr->array[1] & 0x0f) + 16] + 16;
                        buffer32->line[l][ef_x + 12] = buffer32->line[l][ef_x + 13] =
                        buffer32->line[l][ef_x + 14] = buffer32->line[l][ef_x + 15] =
                        buffer32->line[l + 1][ef_x + 12] = buffer32->line[l + 1][ef_x + 13] =
                        buffer32->line[l + 1][ef_x + 14] = buffer32->line[l + 1][ef_x + 15] =
                            pcjr->array[(dat & pcjr->array[1] & 0x0f) + 16] + 16;
                    }
                    break;
                case 0x03: /*640x200x4*/
                    for (x = 0; x < pcjr->crtc[1]; x++) {
                        int ef_x = (x << 3) + ho_d;
                        dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1] << 8) |
                              pcjr->vram[((pcjr->ma << 1) & mask) + offset];
                        pcjr->ma++;
                        for (uint8_t c = 0; c < 8; c++) {
                            chr = (dat >> 7) & 1;
                            chr |= ((dat >> 14) & 2);
                            buffer32->line[l][ef_x + c] = buffer32->line[l + 1][ef_x + c] =
                                pcjr->array[(chr & pcjr->array[1] & 0x0f) + 16] + 16;
                            dat <<= 1;
                        }
                    }
                    break;
                case 0x01: /*80 column text*/
                    for (x = 0; x < pcjr->crtc[1]; x++) {
                        int ef_x = (x << 3) + ho_d;
                        chr        = pcjr->vram[((pcjr->ma << 1) & mask) + offset];
                        attr       = pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
                        drawcursor = ((pcjr->ma == ca) && pcjr->con && pcjr->cursoron);
                        if (pcjr->array[3] & 4) {
                            cols[1] = pcjr->array[((attr & 15) & pcjr->array[1] & 0x0f) + 16] + 16;
                            cols[0] = pcjr->array[(((attr >> 4) & 7) & pcjr->array[1] & 0x0f) + 16] + 16;
                            if ((pcjr->blink & 16) && (attr & 0x80) && !drawcursor)
                                cols[1] = cols[0];
                        } else {
                            cols[1] = pcjr->array[((attr & 15) & pcjr->array[1] & 0x0f) + 16] + 16;
                            cols[0] = pcjr->array[((attr >> 4) & pcjr->array[1] & 0x0f) + 16] + 16;
                        }
                        if (pcjr->sc & 8)
                            for (uint8_t c = 0; c < 8; c++)
                                buffer32->line[l][ef_x + c] =
                                buffer32->line[l + 1][ef_x + c] = cols[0];
                        else
                            for (uint8_t c = 0; c < 8; c++)
                                buffer32->line[l][ef_x + c] =
                                buffer32->line[l + 1][ef_x + c] =
                                    cols[(fontdat[chr][pcjr->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                        if (drawcursor)
                            for (uint8_t c = 0; c < 8; c++) {
                                buffer32->line[l][ef_x + c] ^= 15;
                                buffer32->line[l + 1][ef_x + c] ^= 15;
                            }
                        pcjr->ma++;
                    }
                    break;
                case 0x00: /*40 column text*/
                    for (x = 0; x < pcjr->crtc[1]; x++) {
                        int ef_x = (x << 4) + ho_d;
                        chr        = pcjr->vram[((pcjr->ma << 1) & mask) + offset];
                        attr       = pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
                        drawcursor = ((pcjr->ma == ca) && pcjr->con && pcjr->cursoron);
                        if (pcjr->array[3] & 4) {
                            cols[1] = pcjr->array[((attr & 15) & pcjr->array[1] & 0x0f) + 16] + 16;
                            cols[0] = pcjr->array[(((attr >> 4) & 7) & pcjr->array[1] & 0x0f) + 16] + 16;
                            if ((pcjr->blink & 16) && (attr & 0x80) && !drawcursor)
                                cols[1] = cols[0];
                        } else {
                            cols[1] = pcjr->array[((attr & 15) & pcjr->array[1] & 0x0f) + 16] + 16;
                            cols[0] = pcjr->array[((attr >> 4) & pcjr->array[1] & 0x0f) + 16] + 16;
                        }
                        pcjr->ma++;
                        if (pcjr->sc & 8)
                            for (uint8_t c = 0; c < 8; c++)
                                buffer32->line[l][ef_x + (c << 1)] =
                                buffer32->line[l][ef_x + (c << 1) + 1] =
                                buffer32->line[l + 1][ef_x + (c << 1)] =
                                buffer32->line[l + 1][ef_x + (c << 1) + 1] = cols[0];
                        else
                            for (uint8_t c = 0; c < 8; c++)
                                buffer32->line[l][ef_x + (c << 1)] =
                                buffer32->line[l][ef_x + (c << 1) + 1] =
                                buffer32->line[l + 1][ef_x + (c << 1)] =
                                buffer32->line[l + 1][ef_x + (c << 1) + 1] =
                                    cols[(fontdat[chr][pcjr->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                        if (drawcursor)
                            for (uint8_t c = 0; c < 16; c++) {
                                buffer32->line[l][ef_x + c] ^= 15;
                                buffer32->line[l + 1][ef_x + c] ^= 15;
                            }
                    }
                    break;
                case 0x02: /*320x200x4*/
                    cols[0] = pcjr->array[0 + 16] + 16;
                    cols[1] = pcjr->array[1 + 16] + 16;
                    cols[2] = pcjr->array[2 + 16] + 16;
                    cols[3] = pcjr->array[3 + 16] + 16;
                    for (x = 0; x < pcjr->crtc[1]; x++) {
                        int ef_x = (x << 4) + ho_d;
                        dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) |
                              pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
                        pcjr->ma++;
                        for (uint8_t c = 0; c < 8; c++) {
                            buffer32->line[l][ef_x + (c << 1)] =
                            buffer32->line[l][ef_x + (c << 1) + 1] =
                            buffer32->line[l + 1][ef_x + (c << 1)] =
                            buffer32->line[l + 1][ef_x + (c << 1) + 1] = cols[dat >> 14];
                            dat <<= 2;
                        }
                    }
                    break;
                case 0x102: /*640x200x2*/
                    cols[0] = pcjr->array[0 + 16] + 16;
                    cols[1] = pcjr->array[1 + 16] + 16;
                    for (x = 0; x < pcjr->crtc[1]; x++) {
                        int ef_x = (x << 4) + ho_d;
                        dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) |
                              pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
                        pcjr->ma++;
                        for (uint8_t c = 0; c < 16; c++) {
                            buffer32->line[l][ef_x + c] = buffer32->line[l + 1][ef_x + c] =
                                cols[dat >> 15];
                            dat <<= 1;
                        }
                    }
                    break;

                default:
                    break;
            }
        } else {
            if (pcjr->array[3] & 4) {
                if (pcjr->array[0] & 1) {
                    hline(buffer32, 0, l, (pcjr->crtc[1] << 3) + ho_s, (pcjr->array[2] & 0xf) + 16);
                    hline(buffer32, 0, l + 1, (pcjr->crtc[1] << 3) + ho_s, (pcjr->array[2] & 0xf) + 16);
                } else {
                    hline(buffer32, 0, l, (pcjr->crtc[1] << 4) + ho_s, (pcjr->array[2] & 0xf) + 16);
                    hline(buffer32, 0, l + 1, (pcjr->crtc[1] << 4) + ho_s, (pcjr->array[2] & 0xf) + 16);
                }
            } else {
                cols[0] = pcjr->array[0 + 16] + 16;
                if (pcjr->array[0] & 1) {
                    hline(buffer32, 0, l, (pcjr->crtc[1] << 3) + ho_s, cols[0]);
                    hline(buffer32, 0, l + 1, (pcjr->crtc[1] << 3) + ho_s, cols[0]);
                } else {
                    hline(buffer32, 0, l, (pcjr->crtc[1] << 4) + ho_s, cols[0]);
                    hline(buffer32, 0, l + 1, (pcjr->crtc[1] << 4) + ho_s, cols[0]);
                }
            }
        }
        if (pcjr->array[0] & 1)
            x = (pcjr->crtc[1] << 3) + ho_s;
        else
            x = (pcjr->crtc[1] << 4) + ho_s;
        if (pcjr->composite) {
            Composite_Process(pcjr->array[0], 0, x >> 2, buffer32->line[l]);
            Composite_Process(pcjr->array[0], 0, x >> 2, buffer32->line[l + 1]);
        } else {
            video_process_8(x, l);
            video_process_8(x, l + 1);
        }
        pcjr->sc = oldsc;
        if (pcjr->vc == pcjr->crtc[7] && !pcjr->sc) {
            pcjr->stat |= 8;
        }
        pcjr->displine++;
        if (pcjr->displine >= 360)
            pcjr->displine = 0;
    } else {
        timer_advance_u64(&pcjr->timer, pcjr->dispontime);
        if (pcjr->dispon)
            pcjr->stat |= 1;
        pcjr->linepos = 0;
        if (pcjr->vsynctime) {
            pcjr->vsynctime--;
            if (!pcjr->vsynctime) {
                pcjr->stat &= ~8;
            }
        }
        if (pcjr->sc == (pcjr->crtc[11] & 31) || ((pcjr->crtc[8] & 3) == 3 && pcjr->sc == ((pcjr->crtc[11] & 31) >> 1))) {
            pcjr->con  = 0;
            pcjr->coff = 1;
        }
        if (pcjr->vadj) {
            pcjr->sc++;
            pcjr->sc &= 31;
            pcjr->ma = pcjr->maback;
            pcjr->vadj--;
            if (!pcjr->vadj) {
                pcjr->dispon = 1;
                pcjr->ma = pcjr->maback = (pcjr->crtc[13] | (pcjr->crtc[12] << 8)) & 0x3fff;
                pcjr->sc                = 0;
            }
        } else if (pcjr->sc == pcjr->crtc[9] || ((pcjr->crtc[8] & 3) == 3 && pcjr->sc == (pcjr->crtc[9] >> 1))) {
            pcjr->maback = pcjr->ma;
            pcjr->sc     = 0;
            oldvc        = pcjr->vc;
            pcjr->vc++;
            pcjr->vc &= 127;
            if (pcjr->vc == pcjr->crtc[6])
                pcjr->dispon = 0;
            if (oldvc == pcjr->crtc[4]) {
                pcjr->vc   = 0;
                pcjr->vadj = pcjr->crtc[5];
                if (!pcjr->vadj)
                    pcjr->dispon = 1;
                if (!pcjr->vadj)
                    pcjr->ma = pcjr->maback = (pcjr->crtc[13] | (pcjr->crtc[12] << 8)) & 0x3fff;
                if ((pcjr->crtc[10] & 0x60) == 0x20)
                    pcjr->cursoron = 0;
                else
                    pcjr->cursoron = pcjr->blink & 16;
            }
            if (pcjr->vc == pcjr->crtc[7]) {
                pcjr->dispon    = 0;
                pcjr->displine  = 0;
                pcjr->vsynctime = 16;
                picint(1 << 5);
                if (pcjr->crtc[7]) {
                    if (pcjr->array[0] & 1)
                        x = (pcjr->crtc[1] << 3) + ho_s;
                    else
                        x = (pcjr->crtc[1] << 4) + ho_s;
                    pcjr->lastline++;

                    xs_temp = x;
                    ys_temp = (pcjr->lastline - pcjr->firstline) << 1;

                    if ((xs_temp > 0) && (ys_temp > 0)) {
                        int actual_ys = ys_temp;

                        if (xs_temp < 64)
                            xs_temp = 656;
                        if (ys_temp < 32)
                            ys_temp = 400;
                        if (!enable_overscan)
                            xs_temp -= ho_s;

                        if ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get()) {
                            xsize = xs_temp;
                            ysize = ys_temp;

                            set_screen_size(xsize, ysize + (enable_overscan ? 32 : 0));

                            if (video_force_resize_get())
                                video_force_resize_set(0);
                        }

                        vid_blit_h_overscan(pcjr);

                        if (enable_overscan) {
                            video_blit_memtoscreen(0, pcjr->firstline << 1,
                                                   xsize, actual_ys + 32);
                        } else if (pcjr->apply_hd) {
                            video_blit_memtoscreen(ho_s / 2, (pcjr->firstline << 1) + 16,
                                                   xsize, actual_ys);
                        } else {
                            video_blit_memtoscreen(ho_d, (pcjr->firstline << 1) + 16,
                                                   xsize, actual_ys);
                        }
                    }

                    frames++;
                    video_res_x = xsize;
                    video_res_y = ysize;
                }
                pcjr->firstline = 1000;
                pcjr->lastline  = 0;
                pcjr->blink++;
            }
        } else {
            pcjr->sc++;
            pcjr->sc &= 31;
            pcjr->ma = pcjr->maback;
        }
        if (pcjr->sc == (pcjr->crtc[10] & 31) || ((pcjr->crtc[8] & 3) == 3 && pcjr->sc == ((pcjr->crtc[10] & 31) >> 1)))
            pcjr->con = 1;
    }
}

static void
kbd_write(uint16_t port, uint8_t val, void *priv)
{
    pcjr_t *pcjr = (pcjr_t *) priv;

    if ((port >= 0xa0) && (port <= 0xa7))
        port = 0xa0;

    switch (port) {
        case 0x60:
            pcjr->pa = val;
            break;

        case 0x61:
            pcjr->pb = val;

            if (cassette != NULL)
                pc_cas_set_motor(cassette, (pcjr->pb & 0x08) == 0);

            speaker_update();
            speaker_gated  = val & 1;
            speaker_enable = val & 2;
            if (speaker_enable)
                was_speaker_enable = 1;
            pit_devs[0].set_gate(pit_devs[0].data, 2, val & 1);
            sn76489_mute = speaker_mute = 1;
            switch (val & 0x60) {
                case 0x00:
                    speaker_mute = 0;
                    break;

                case 0x60:
                    sn76489_mute = 0;
                    break;

                default:
                    break;
            }
            break;

        case 0xa0:
            nmi_mask = val & 0x80;
            pit_devs[0].set_using_timer(pit_devs[0].data, 1, !(val & 0x20));
            break;

        default:
            break;
    }
}

static uint8_t
kbd_read(uint16_t port, void *priv)
{
    pcjr_t *pcjr = (pcjr_t *) priv;
    uint8_t ret  = 0xff;

    if ((port >= 0xa0) && (port <= 0xa7))
        port = 0xa0;

    switch (port) {
        case 0x60:
            ret = pcjr->pa;
            break;

        case 0x61:
            ret = pcjr->pb;
            break;

        case 0x62:
            ret = (pcjr->latched ? 1 : 0);
            ret |= 0x02; /* Modem card not installed */
            if (mem_size < 128)
                ret |= 0x08; /* 64k expansion card not installed */
            if ((pcjr->pb & 0x08) || (cassette == NULL))
                ret |= (ppispeakon ? 0x10 : 0);
            else
                ret |= (pc_cas_get_inp(cassette) ? 0x10 : 0);
            ret |= (ppispeakon ? 0x10 : 0);
            ret |= (ppispeakon ? 0x20 : 0);
            ret |= (pcjr->data ? 0x40 : 0);
            if (pcjr->data)
                ret |= 0x40;
            break;

        case 0xa0:
            pcjr->latched = 0;
            ret           = 0;
            break;

        default:
            break;
    }

    return ret;
}

static void
kbd_poll(void *priv)
{
    pcjr_t *pcjr = (pcjr_t *) priv;
    int     c;
    int     p = 0;
    int     key;

    timer_advance_u64(&pcjr->send_delay_timer, 220 * TIMER_USEC);

    if (key_queue_start != key_queue_end && !pcjr->serial_pos && !pcjr->latched) {
        key = key_queue[key_queue_start];

        key_queue_start = (key_queue_start + 1) & 0xf;

        pcjr->latched        = 1;
        pcjr->serial_data[0] = 1; /*Start bit*/
        pcjr->serial_data[1] = 0;

        for (c = 0; c < 8; c++) {
            if (key & (1 << c)) {
                pcjr->serial_data[(c + 1) * 2]     = 1;
                pcjr->serial_data[(c + 1) * 2 + 1] = 0;
                p++;
            } else {
                pcjr->serial_data[(c + 1) * 2]     = 0;
                pcjr->serial_data[(c + 1) * 2 + 1] = 1;
            }
        }

        if (p & 1) { /*Parity*/
            pcjr->serial_data[9 * 2]     = 1;
            pcjr->serial_data[9 * 2 + 1] = 0;
        } else {
            pcjr->serial_data[9 * 2]     = 0;
            pcjr->serial_data[9 * 2 + 1] = 1;
        }

        for (c = 0; c < 11; c++) { /*11 stop bits*/
            pcjr->serial_data[(c + 10) * 2]     = 0;
            pcjr->serial_data[(c + 10) * 2 + 1] = 0;
        }

        pcjr->serial_pos++;
    }

    if (pcjr->serial_pos) {
        pcjr->data = pcjr->serial_data[pcjr->serial_pos - 1];
        nmi        = pcjr->data;
        pcjr->serial_pos++;
        if (pcjr->serial_pos == 42 + 1)
            pcjr->serial_pos = 0;
    }
}

static void
kbd_adddata(uint16_t val)
{
    key_queue[key_queue_end] = val;
    key_queue_end            = (key_queue_end + 1) & 0xf;
}

static void
kbd_adddata_ex(uint16_t val)
{
    kbd_adddata_process(val, kbd_adddata);
}

static void
speed_changed(void *priv)
{
    pcjr_t *pcjr = (pcjr_t *) priv;

    recalc_timings(pcjr);
}

static void
pcjr_vid_init(pcjr_t *pcjr)
{
    int     display_type;

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_dram);

    pcjr->memctrl   = -1;
    if (mem_size < 128)
        pcjr->memctrl &= ~0x24;

    display_type    = device_get_config_int("display_type");
    pcjr->composite = (display_type != PCJR_RGB);
    pcjr->apply_hd  = device_get_config_int("apply_hd");
    overscan_x = 256;
    overscan_y = 32;

    mem_mapping_add(&pcjr->mapping, 0xb8000, 0x08000,
                    vid_read, NULL, NULL,
                    vid_write, NULL, NULL, NULL, 0, pcjr);
    io_sethandler(0x03d0, 16,
                  vid_in, NULL, NULL, vid_out, NULL, NULL, pcjr);
    timer_add(&pcjr->timer, vid_poll, pcjr, 1);

    cga_palette = 0;
    cgapal_rebuild();
}

void
pit_irq0_timer_pcjr(int new_out, int old_out, UNUSED(void *priv))
{
    if (new_out && !old_out) {
        picint(1);
        pit_devs[0].ctr_clock(pit_devs[0].data, 1);
    }

    if (!new_out)
        picintc(1);
}

static const device_config_t pcjr_config[] = {
  // clang-format off
    {
        .name = "display_type",
        .description = "Display type",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = PCJR_RGB,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "RGB",       .value = PCJR_RGB       },
            { .description = "Composite", .value = PCJR_COMPOSITE },
            { .description = ""                                   }
        }
    },
    {
        .name = "apply_hd",
        .description = "Apply overscan deltas",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t pcjr_device = {
    .name          = "IBM PCjr",
    .internal_name = "pcjr",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = speed_changed,
    .force_redraw  = NULL,
    .config        = pcjr_config
};

int
machine_pcjr_init(UNUSED(const machine_t *model))
{
    pcjr_t *pcjr;

    int ret;

    ret = bios_load_linear("roms/machines/ibmpcjr/bios.rom",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    pcjr = calloc(1, sizeof(pcjr_t));

    pic_init_pcjr();
    pit_common_init(0, pit_irq0_timer_pcjr, NULL);

    cpu_set();

    /* Initialize the video controller. */
    video_reset(gfxcard[0]);
    loadfont("roms/video/mda/mda.rom", 0);
    device_context(&pcjr_device);
    pcjr_vid_init(pcjr);
    device_context_restore();
    device_add_ex(&pcjr_device, pcjr);

    /* Initialize the keyboard. */
    keyboard_scan   = 1;
    key_queue_start = key_queue_end = 0;
    io_sethandler(0x0060, 4,
                  kbd_read, NULL, NULL, kbd_write, NULL, NULL, pcjr);
    io_sethandler(0x00a0, 8,
                  kbd_read, NULL, NULL, kbd_write, NULL, NULL, pcjr);
    timer_add(&pcjr->send_delay_timer, kbd_poll, pcjr, 1);
    keyboard_set_table(scancode_pcjr);
    keyboard_send = kbd_adddata_ex;

    /* Technically it's the SN76496N, but the SN76489 is identical to the SN76496N. */
    device_add(&sn76489_device);

    nmi_mask = 0x80;

    device_add(&fdc_pcjr_device);

    device_add(&ns8250_pcjr_device);
    serial_set_next_inst(SERIAL_MAX); /* So that serial_standalone_init() won't do anything. */

    /* "All the inputs are 'read' with one 'IN' from address hex 201." - PCjr Technical Reference (Nov. 83), p.2-119

    Note by Miran Grca: Meanwhile, the same Technical Reference clearly says that
                        the gameport is on ports 201-207. */
    standalone_gameport_type = &gameport_201_device;

    return ret;
}
