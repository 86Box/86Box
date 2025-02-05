/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of Tandy models 1000, 1000HX and 1000SL2.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/keyboard.h>
#include <86box/sound.h>
#include <86box/snd_sn76489.h>
#include <86box/video.h>
#include <86box/vid_cga_comp.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>

enum {
    TANDY_RGB = 0,
    TANDY_COMPOSITE
};

enum {
    TYPE_TANDY = 0,
    TYPE_TANDY1000SX,
    TYPE_TANDY1000HX,
    TYPE_TANDY1000SL2
};

enum {
    EEPROM_IDLE = 0,
    EEPROM_GET_OPERATION,
    EEPROM_READ,
    EEPROM_WRITE
};

typedef struct t1kvid_t {
    mem_mapping_t mapping;
    mem_mapping_t vram_mapping;

    uint8_t crtc[32];
    int     crtcreg;

    int     array_index;
    uint8_t array[256];
    int     memctrl;
    uint8_t mode;
    uint8_t col;
    uint8_t stat;

    uint8_t *vram;
    uint8_t *b8000;
    uint32_t b8000_mask;
    uint32_t b8000_limit;
    uint8_t  planar_ctrl;
    uint8_t  lp_strobe;

    int      linepos;
    int      displine;
    int      sc;
    int      vc;
    int      dispon;
    int      con;
    int      coff;
    int      cursoron;
    int      blink;
    int      fullchange;
    int      vsynctime;
    int      vadj;
    uint16_t ma;
    uint16_t maback;

    uint64_t   dispontime;
    uint64_t   dispofftime;
    pc_timer_t timer;
    int        firstline;
    int        lastline;

    int composite;
} t1kvid_t;

typedef struct t1keep_t {
    char *path;

    int      state;
    int      count;
    int      addr;
    int      clk;
    uint16_t data;
    uint16_t store[64];
} t1keep_t;

typedef struct tandy_t {
    mem_mapping_t ram_mapping;
    mem_mapping_t rom_mapping; /* SL2 */

    uint8_t *rom; /* SL2 */
    uint8_t  ram_bank;
    uint8_t  rom_bank;   /* SL2 */
    int      rom_offset; /* SL2 */

    uint32_t base;
    uint32_t mask;
    int      is_hx;
    int      is_sl2;

    t1kvid_t *vid;
} tandy_t;

static video_timings_t timing_dram = { VIDEO_BUS, 0, 0, 0, 0, 0, 0 }; /*No additional waitstates*/

static const scancode scancode_tandy[512] = {
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
    { .mk = {      0x12, 0 }, .brk = {             0x92, 0 } }, /* 013 */
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
    { .mk = {      0x56, 0 }, .brk = {             0xd6, 0 } }, /* 053 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 054 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 055 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 056 */
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
    { .mk = {      0x57, 0 }, .brk = {             0xd7, 0 } }, /* 11c */
    { .mk = {            0 }, .brk = {                   0 } }, /* 11d */
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
    { .mk = {      0x38, 0 }, .brk = {             0xb8, 0 } }, /* 138 */
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
    { .mk = {      0x29, 0 }, .brk = {             0xa9, 0 } }, /* 148 */
    { .mk = {      0x49, 0 }, .brk = {             0xc9, 0 } }, /* 149 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 14a */
    { .mk = {      0x2b, 0 }, .brk = {             0xab, 0 } }, /* 14b */
    { .mk = {            0 }, .brk = {                   0 } }, /* 14c */
    { .mk = {      0x4e, 0 }, .brk = {             0xce, 0 } }, /* 14d */
    { .mk = {            0 }, .brk = {                   0 } }, /* 14e */
    { .mk = {      0x4f, 0 }, .brk = {             0xcf, 0 } }, /* 14f */
    { .mk = {      0x4a, 0 }, .brk = {             0xca, 0 } }, /* 150 */
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
static uint8_t crtcmask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static uint8_t crtcmask_sl[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xff, 0xff,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static int eep_data_out;

static uint8_t vid_in(uint16_t addr, void *priv);
static void    vid_out(uint16_t addr, uint8_t val, void *priv);

#ifdef ENABLE_TANDY_LOG
int tandy_do_log = ENABLE_TANDY_LOG;

static void
tandy_log(const char *fmt, ...)
{
    va_list ap;

    if (tandy_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define tandy_log(fmt, ...)
#endif

static void
recalc_mapping(tandy_t *dev)
{
    t1kvid_t *vid = dev->vid;

    mem_mapping_disable(&vid->mapping);
    io_removehandler(0x03d0, 16,
                     vid_in, NULL, NULL, vid_out, NULL, NULL, dev);

    if (vid->planar_ctrl & 4) {
        mem_mapping_enable(&vid->mapping);
        if (vid->array[5] & 1)
            mem_mapping_set_addr(&vid->mapping, 0xa0000, 0x10000);
        else
            mem_mapping_set_addr(&vid->mapping, 0xb8000, 0x8000);
        io_sethandler(0x03d0, 16, vid_in, NULL, NULL, vid_out, NULL, NULL, dev);
    }
}

static void
recalc_timings(tandy_t *dev)
{
    t1kvid_t *vid = dev->vid;

    double _dispontime;
    double _dispofftime;
    double disptime;

    if (vid->mode & 1) {
        disptime    = vid->crtc[0] + 1;
        _dispontime = vid->crtc[1];
    } else {
        disptime    = (vid->crtc[0] + 1) << 1;
        _dispontime = vid->crtc[1] << 1;
    }

    _dispofftime = disptime - _dispontime;
    _dispontime *= CGACONST;
    _dispofftime *= CGACONST;
    vid->dispontime  = (uint64_t) (_dispontime);
    vid->dispofftime = (uint64_t) (_dispofftime);
}

static void
recalc_address(tandy_t *dev)
{
    t1kvid_t *vid = dev->vid;

    if ((vid->memctrl & 0xc0) == 0xc0) {
        vid->vram       = &ram[((vid->memctrl & 0x06) << 14) + dev->base];
        vid->b8000      = &ram[((vid->memctrl & 0x30) << 11) + dev->base];
        vid->b8000_mask = 0x7fff;
    } else {
        vid->vram       = &ram[((vid->memctrl & 0x07) << 14) + dev->base];
        vid->b8000      = &ram[((vid->memctrl & 0x38) << 11) + dev->base];
        vid->b8000_mask = 0x3fff;
    }
}

static void
recalc_address_sl(tandy_t *dev)
{
    t1kvid_t *vid = dev->vid;

    vid->b8000_limit = 0x8000;

    if (vid->array[5] & 1) {
        vid->vram  = &ram[((vid->memctrl & 0x04) << 14) + dev->base];
        vid->b8000 = &ram[((vid->memctrl & 0x20) << 11) + dev->base];
    } else if ((vid->memctrl & 0xc0) == 0xc0) {
        vid->vram  = &ram[((vid->memctrl & 0x06) << 14) + dev->base];
        vid->b8000 = &ram[((vid->memctrl & 0x30) << 11) + dev->base];
    } else {
        vid->vram  = &ram[((vid->memctrl & 0x07) << 14) + dev->base];
        vid->b8000 = &ram[((vid->memctrl & 0x38) << 11) + dev->base];
        if ((vid->memctrl & 0x38) == 0x38)
            vid->b8000_limit = 0x4000;
    }
}

static void
vid_update_latch(t1kvid_t *vid)
{
    uint32_t lp_latch = vid->displine * vid->crtc[1];

    vid->crtc[0x10] = (lp_latch >> 8) & 0x3f;
    vid->crtc[0x11] = lp_latch & 0xff;
}

static void
vid_out(uint16_t addr, uint8_t val, void *priv)
{
    tandy_t  *dev = (tandy_t *) priv;
    t1kvid_t *vid = dev->vid;
    uint8_t   old;

    if ((addr >= 0x3d0) && (addr <= 0x3d7))
        addr = (addr & 0xff9) | 0x004;

    switch (addr) {
        case 0x03d4:
            vid->crtcreg = val & 0x1f;
            break;

        case 0x03d5:
            old = vid->crtc[vid->crtcreg];
            if (dev->is_sl2)
                vid->crtc[vid->crtcreg] = val & crtcmask_sl[vid->crtcreg];
            else
                vid->crtc[vid->crtcreg] = val & crtcmask[vid->crtcreg];
            if (old != val) {
                if (vid->crtcreg < 0xe || vid->crtcreg > 0x10) {
                    vid->fullchange = changeframecount;
                    recalc_timings(dev);
                }
            }
            break;

        case 0x03d8:
            old = vid->mode;
            vid->mode = val;
            if ((old ^ val) & 0x01)
                recalc_timings(dev);
            if (!dev->is_sl2)
                update_cga16_color(vid->mode);
            break;

        case 0x03d9:
            vid->col = val;
            break;

        case 0x03da:
            vid->array_index = val & 0x1f;
            break;

        case 0x3db:
            if (!dev->is_sl2 && (vid->lp_strobe == 1))
                vid->lp_strobe = 0;
            break;

        case 0x3dc:
            if (!dev->is_sl2 && (vid->lp_strobe == 0)) {
                vid->lp_strobe = 1;
                vid_update_latch(vid);
            }
            break;

        case 0x03de:
            if (vid->array_index & 16)
                val &= 0xf;
            vid->array[vid->array_index & 0x1f] = val;
            if (dev->is_sl2) {
                if ((vid->array_index & 0x1f) == 5) {
                    recalc_mapping(dev);
                    recalc_address_sl(dev);
                }
            }
            break;

        case 0x03df:
            vid->memctrl = val;
            if (dev->is_sl2)
                recalc_address_sl(dev);
            else
                recalc_address(dev);
            break;

        case 0x0065:
            if (val == 8)
                return; /*Hack*/
            vid->planar_ctrl = val;
            recalc_mapping(dev);
            break;

        default:
            break;
    }
}

static uint8_t
vid_in(uint16_t addr, void *priv)
{
    const tandy_t  *dev = (tandy_t *) priv;
    t1kvid_t       *vid = dev->vid;
    uint8_t         ret = 0xff;

    if ((addr >= 0x3d0) && (addr <= 0x3d7))
        addr = (addr & 0xff9) | 0x004;

    switch (addr) {
        case 0x03d4:
            ret = vid->crtcreg;
            break;

        case 0x03d5:
            ret = vid->crtc[vid->crtcreg];
            break;

        case 0x03da:
            ret = vid->stat;
            break;

        case 0x3db:
            if (!dev->is_sl2 && (vid->lp_strobe == 1))
                vid->lp_strobe = 0;
            break;

        case 0x3dc:
            if (!dev->is_sl2 && (vid->lp_strobe == 0)) {
                vid->lp_strobe = 1;
                vid_update_latch(vid);
            }
            break;

        default:
            break;
    }

    return ret;
}

static void
vid_write(uint32_t addr, uint8_t val, void *priv)
{
    tandy_t  *dev = (tandy_t *) priv;
    t1kvid_t *vid = dev->vid;

    if (vid->memctrl == -1)
        return;

    if (dev->is_sl2) {
        if (vid->array[5] & 1)
            vid->b8000[addr & 0xffff] = val;
        else {
            if ((addr & 0x7fff) < vid->b8000_limit)
                vid->b8000[addr & 0x7fff] = val;
        }
    } else {
        vid->b8000[addr & vid->b8000_mask] = val;
    }
}

static uint8_t
vid_read(uint32_t addr, void *priv)
{
    const tandy_t  *dev = (tandy_t *) priv;
    const t1kvid_t *vid = dev->vid;

    if (vid->memctrl == -1)
        return 0xff;

    if (dev->is_sl2) {
        if (vid->array[5] & 1)
            return (vid->b8000[addr & 0xffff]);
        if ((addr & 0x7fff) < vid->b8000_limit)
            return (vid->b8000[addr & 0x7fff]);
        else
            return 0xff;
    } else {
        return (vid->b8000[addr & vid->b8000_mask]);
    }
}

static void
vid_poll(void *priv)
{
    tandy_t  *dev = (tandy_t *) priv;
    t1kvid_t *vid = dev->vid;
    uint16_t  ca  = (vid->crtc[15] | (vid->crtc[14] << 8)) & 0x3fff;
    int       drawcursor;
    int       x;
    int       c;
    int       xs_temp;
    int       ys_temp;
    int       oldvc;
    uint8_t   chr;
    uint8_t   attr;
    uint16_t  dat;
    int       cols[4];
    int       col;
    int       oldsc;

    if (!vid->linepos) {
        timer_advance_u64(&vid->timer, vid->dispofftime);
        vid->stat |= 1;
        vid->linepos = 1;
        oldsc        = vid->sc;
        if ((vid->crtc[8] & 3) == 3)
            vid->sc = (vid->sc << 1) & 7;
        if (vid->dispon) {
            if (vid->displine < vid->firstline) {
                vid->firstline = vid->displine;
                video_wait_for_buffer();
            }
            vid->lastline = vid->displine;
            cols[0]       = (vid->array[2] & 0xf) + 16;
            for (c = 0; c < 8; c++) {
                if (vid->array[3] & 4) {
                    buffer32->line[vid->displine << 1][c] = buffer32->line[(vid->displine << 1) + 1][c] = cols[0];
                    if (vid->mode & 1) {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 3) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 3) + 8] = cols[0];
                    } else {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 4) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 4) + 8] = cols[0];
                    }
                } else if ((vid->mode & 0x12) == 0x12) {
                    buffer32->line[vid->displine << 1][c] = buffer32->line[(vid->displine << 1) + 1][c] = 0;
                    if (vid->mode & 1) {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 3) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 3) + 8] = 0;
                    } else {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 4) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 4) + 8] = 0;
                    }
                } else {
                    buffer32->line[vid->displine << 1][c] = buffer32->line[(vid->displine << 1) + 1][c] = (vid->col & 15) + 16;
                    if (vid->mode & 1) {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 3) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 3) + 8] = (vid->col & 15) + 16;
                    } else {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 4) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 4) + 8] = (vid->col & 15) + 16;
                    }
                }
            }
            if (dev->is_sl2 && (vid->array[5] & 1)) { /*640x200x16*/
                for (x = 0; x < vid->crtc[1] * 2; x++) {
                    dat = (vid->vram[(vid->ma << 1) & 0xffff] << 8) | vid->vram[((vid->ma << 1) + 1) & 0xffff];
                    vid->ma++;
                    buffer32->line[vid->displine << 1][(x << 2) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 2) + 8] = vid->array[((dat >> 12) & 0xf) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 2) + 9] = buffer32->line[(vid->displine << 1) + 1][(x << 2) + 9] = vid->array[((dat >> 8) & 0xf) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 2) + 10] = buffer32->line[(vid->displine << 1) + 1][(x << 2) + 10] = vid->array[((dat >> 4) & 0xf) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 2) + 11] = buffer32->line[(vid->displine << 1) + 1][(x << 2) + 11] = vid->array[(dat & 0xf) + 16] + 16;
                }
            } else if ((vid->array[3] & 0x10) && (vid->mode & 1)) { /*320x200x16*/
                for (x = 0; x < vid->crtc[1]; x++) {
                    dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000)] << 8) | vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000) + 1];
                    vid->ma++;
                    buffer32->line[vid->displine << 1][(x << 3) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 8] = buffer32->line[vid->displine << 1][(x << 3) + 9] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 9] = vid->array[((dat >> 12) & vid->array[1] & 0x0f) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 3) + 10] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 10] = buffer32->line[vid->displine << 1][(x << 3) + 11] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 11] = vid->array[((dat >> 8) & vid->array[1] & 0x0f) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 3) + 12] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 12] = buffer32->line[vid->displine << 1][(x << 3) + 13] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 13] = vid->array[((dat >> 4) & vid->array[1] & 0x0f) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 3) + 14] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 14] = buffer32->line[vid->displine << 1][(x << 3) + 15] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 15] = vid->array[(dat & vid->array[1] & 0x0f) + 16] + 16;
                }
            } else if (vid->array[3] & 0x10) { /*160x200x16*/
                for (x = 0; x < vid->crtc[1]; x++) {
                    if (dev->is_sl2) {
                        dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000)] << 8) | vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000) + 1];
                    } else {
                        dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000)] << 8) | vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000) + 1];
                    }
                    vid->ma++;
                    buffer32->line[vid->displine << 1][(x << 4) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 8] = buffer32->line[vid->displine << 1][(x << 4) + 9] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 9] = buffer32->line[vid->displine << 1][(x << 4) + 10] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 10] = buffer32->line[vid->displine << 1][(x << 4) + 11] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 11] = vid->array[((dat >> 12) & vid->array[1] & 0x0f) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 4) + 12] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 12] = buffer32->line[vid->displine << 1][(x << 4) + 13] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 13] = buffer32->line[vid->displine << 1][(x << 4) + 14] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 14] = buffer32->line[vid->displine << 1][(x << 4) + 15] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 15] = vid->array[((dat >> 8) & vid->array[1] & 0x0f) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 4) + 16] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 16] = buffer32->line[vid->displine << 1][(x << 4) + 17] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 17] = buffer32->line[vid->displine << 1][(x << 4) + 18] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 18] = buffer32->line[vid->displine << 1][(x << 4) + 19] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 19] = vid->array[((dat >> 4) & vid->array[1] & 0x0f) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 4) + 20] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 20] = buffer32->line[vid->displine << 1][(x << 4) + 21] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 21] = buffer32->line[vid->displine << 1][(x << 4) + 22] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 22] = buffer32->line[vid->displine << 1][(x << 4) + 23] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 23] = vid->array[(dat & vid->array[1] & 0x0f) + 16] + 16;
                }
            } else if (vid->array[3] & 0x08) { /*640x200x4 - this implementation is a complete guess!*/
                for (x = 0; x < vid->crtc[1]; x++) {
                    dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000)] << 8) | vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000) + 1];
                    vid->ma++;
                    for (c = 0; c < 8; c++) {
                        chr = (dat >> 6) & 2;
                        chr |= ((dat >> 15) & 1);
                        buffer32->line[vid->displine << 1][(x << 3) + 8 + c] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 8 + c] = vid->array[(chr & vid->array[1]) + 16] + 16;
                        dat <<= 1;
                    }
                }
            } else if (vid->mode & 1) {
                for (x = 0; x < vid->crtc[1]; x++) {
                    chr        = vid->vram[(vid->ma << 1) & 0x3fff];
                    attr       = vid->vram[((vid->ma << 1) + 1) & 0x3fff];
                    drawcursor = ((vid->ma == ca) && vid->con && vid->cursoron);
                    if (vid->mode & 0x20) {
                        cols[1] = vid->array[((attr & 15) & vid->array[1]) + 16] + 16;
                        cols[0] = vid->array[(((attr >> 4) & 7) & vid->array[1]) + 16] + 16;
                        if ((vid->blink & 16) && (attr & 0x80) && !drawcursor)
                            cols[1] = cols[0];
                    } else {
                        cols[1] = vid->array[((attr & 15) & vid->array[1]) + 16] + 16;
                        cols[0] = vid->array[((attr >> 4) & vid->array[1]) + 16] + 16;
                    }
                    if (vid->sc & 8) {
                        for (c = 0; c < 8; c++) {
                            buffer32->line[vid->displine << 1][(x << 3) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] = cols[0];
                        }
                    } else {
                        for (c = 0; c < 8; c++) {
                            if (vid->sc == 8) {
                                buffer32->line[vid->displine << 1][(x << 3) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] = cols[(fontdat[chr][7] & (1 << (c ^ 7))) ? 1 : 0];
                            } else {
                                buffer32->line[vid->displine << 1][(x << 3) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] = cols[(fontdat[chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                            }
                        }
                    }
                    if (drawcursor) {
                        for (c = 0; c < 8; c++) {
                            buffer32->line[vid->displine << 1][(x << 3) + c + 8] ^= 15;
                            buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] ^= 15;
                        }
                    }
                    vid->ma++;
                }
            } else if (!(vid->mode & 2)) {
                for (x = 0; x < vid->crtc[1]; x++) {
                    chr        = vid->vram[(vid->ma << 1) & 0x3fff];
                    attr       = vid->vram[((vid->ma << 1) + 1) & 0x3fff];
                    drawcursor = ((vid->ma == ca) && vid->con && vid->cursoron);
                    if (vid->mode & 0x20) {
                        cols[1] = vid->array[((attr & 15) & vid->array[1]) + 16] + 16;
                        cols[0] = vid->array[(((attr >> 4) & 7) & vid->array[1]) + 16] + 16;
                        if ((vid->blink & 16) && (attr & 0x80) && !drawcursor)
                            cols[1] = cols[0];
                    } else {
                        cols[1] = vid->array[((attr & 15) & vid->array[1]) + 16] + 16;
                        cols[0] = vid->array[((attr >> 4) & vid->array[1]) + 16] + 16;
                    }
                    vid->ma++;
                    if (vid->sc & 8) {
                        for (c = 0; c < 8; c++)
                            buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 8] = buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 1 + 8] = cols[0];
                    } else {
                        for (c = 0; c < 8; c++) {
                            if (vid->sc == 8) {
                                buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] = buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 1 + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][7] & (1 << (c ^ 7))) ? 1 : 0];
                            } else {
                                buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] = buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 1 + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                            }
                        }
                    }
                    if (drawcursor) {
                        for (c = 0; c < 16; c++) {
                            buffer32->line[vid->displine << 1][(x << 4) + c + 8] ^= 15;
                            buffer32->line[(vid->displine << 1) + 1][(x << 4) + c + 8] ^= 15;
                        }
                    }
                }
            } else if (!(vid->mode & 16)) {
                cols[0] = (vid->col & 15);
                col     = (vid->col & 16) ? 8 : 0;
                if (vid->mode & 4) {
                    cols[1] = col | 3;
                    cols[2] = col | 4;
                    cols[3] = col | 7;
                } else if (vid->col & 32) {
                    cols[1] = col | 3;
                    cols[2] = col | 5;
                    cols[3] = col | 7;
                } else {
                    cols[1] = col | 2;
                    cols[2] = col | 4;
                    cols[3] = col | 6;
                }
                cols[0] = vid->array[(cols[0] & vid->array[1]) + 16] + 16;
                cols[1] = vid->array[(cols[1] & vid->array[1]) + 16] + 16;
                cols[2] = vid->array[(cols[2] & vid->array[1]) + 16] + 16;
                cols[3] = vid->array[(cols[3] & vid->array[1]) + 16] + 16;
                for (x = 0; x < vid->crtc[1]; x++) {
                    dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000)] << 8) | vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000) + 1];
                    vid->ma++;
                    for (c = 0; c < 8; c++) {
                        buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] = buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 1 + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
                        dat <<= 2;
                    }
                }
            } else {
                cols[0] = 0;
                cols[1] = vid->array[(vid->col & vid->array[1]) + 16] + 16;
                for (x = 0; x < vid->crtc[1]; x++) {
                    dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000)] << 8) | vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000) + 1];
                    vid->ma++;
                    for (c = 0; c < 16; c++) {
                        buffer32->line[vid->displine << 1][(x << 4) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + c + 8] = cols[dat >> 15];
                        dat <<= 1;
                    }
                }
            }
        } else {
            if (vid->array[3] & 4) {
                if (vid->mode & 1) {
                    hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 3) + 16, (vid->array[2] & 0xf) + 16);
                    hline(buffer32, 0, (vid->displine << 1) + 1, (vid->crtc[1] << 3) + 16, (vid->array[2] & 0xf) + 16);
                } else {
                    hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 4) + 16, (vid->array[2] & 0xf) + 16);
                    hline(buffer32, 0, (vid->displine << 1) + 1, (vid->crtc[1] << 4) + 16, (vid->array[2] & 0xf) + 16);
                }
            } else {
                cols[0] = ((vid->mode & 0x12) == 0x12) ? 0 : (vid->col & 0xf) + 16;
                if (vid->mode & 1) {
                    hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 3) + 16, cols[0]);
                    hline(buffer32, 0, (vid->displine << 1) + 1, (vid->crtc[1] << 3) + 16, cols[0]);
                } else {
                    hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 4) + 16, cols[0]);
                    hline(buffer32, 0, (vid->displine << 1) + 1, (vid->crtc[1] << 4) + 16, cols[0]);
                }
            }
        }

        if (vid->mode & 1)
            x = (vid->crtc[1] << 3) + 16;
        else
            x = (vid->crtc[1] << 4) + 16;
        if (!dev->is_sl2 && vid->composite) {
            Composite_Process(vid->mode, 0, x >> 2, buffer32->line[vid->displine << 1]);
            Composite_Process(vid->mode, 0, x >> 2, buffer32->line[(vid->displine << 1) + 1]);
        } else {
            video_process_8(x, vid->displine << 1);
            video_process_8(x, (vid->displine << 1) + 1);
        }
        vid->sc = oldsc;
        if (vid->vc == vid->crtc[7] && !vid->sc)
            vid->stat |= 8;
        vid->displine++;
        if (vid->displine >= 360)
            vid->displine = 0;
    } else {
        timer_advance_u64(&vid->timer, vid->dispontime);
        if (vid->dispon)
            vid->stat &= ~1;
        vid->linepos = 0;
        if (vid->vsynctime) {
            vid->vsynctime--;
            if (!vid->vsynctime)
                vid->stat &= ~8;
        }
        if (vid->sc == (vid->crtc[11] & 31) || ((vid->crtc[8] & 3) == 3 && vid->sc == ((vid->crtc[11] & 31) >> 1))) {
            vid->con  = 0;
            vid->coff = 1;
        }
        if (vid->vadj) {
            vid->sc++;
            vid->sc &= 31;
            vid->ma = vid->maback;
            vid->vadj--;
            if (!vid->vadj) {
                vid->dispon = 1;
                if (dev->is_sl2 && (vid->array[5] & 1))
                    vid->ma = vid->maback = vid->crtc[13] | (vid->crtc[12] << 8);
                else
                    vid->ma = vid->maback = (vid->crtc[13] | (vid->crtc[12] << 8)) & 0x3fff;
                vid->sc = 0;
            }
        } else if (vid->sc == vid->crtc[9] || ((vid->crtc[8] & 3) == 3 && vid->sc == (vid->crtc[9] >> 1))) {
            vid->maback = vid->ma;
            vid->sc     = 0;
            oldvc       = vid->vc;
            vid->vc++;
            if (dev->is_sl2)
                vid->vc &= 255;
            else
                vid->vc &= 127;
            if (vid->vc == vid->crtc[6])
                vid->dispon = 0;
            if (oldvc == vid->crtc[4]) {
                vid->vc   = 0;
                vid->vadj = vid->crtc[5];
                if (!vid->vadj)
                    vid->dispon = 1;
                if (!vid->vadj) {
                    if (dev->is_sl2 && (vid->array[5] & 1))
                        vid->ma = vid->maback = vid->crtc[13] | (vid->crtc[12] << 8);
                    else
                        vid->ma = vid->maback = (vid->crtc[13] | (vid->crtc[12] << 8)) & 0x3fff;
                }
                if ((vid->crtc[10] & 0x60) == 0x20)
                    vid->cursoron = 0;
                else
                    vid->cursoron = vid->blink & 16;
            }
            if (vid->vc == vid->crtc[7]) {
                vid->dispon    = 0;
                vid->displine  = 0;
                vid->vsynctime = 16;
                picint(1 << 5);
                if (vid->crtc[7]) {
                    if (vid->mode & 1)
                        x = (vid->crtc[1] << 3) + 16;
                    else
                        x = (vid->crtc[1] << 4) + 16;
                    vid->lastline++;

                    xs_temp = x;
                    ys_temp = (vid->lastline - vid->firstline) << 1;

                    if ((xs_temp > 0) && (ys_temp > 0)) {
                        if (xs_temp < 64)
                            xs_temp = 656;
                        if (ys_temp < 32)
                            ys_temp = 400;
                        if (!enable_overscan)
                            xs_temp -= 16;

                        if ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get()) {
                            xsize = xs_temp;
                            ysize = ys_temp;
                            set_screen_size(xsize, ysize + (enable_overscan ? 16 : 0));

                            if (video_force_resize_get())
                                video_force_resize_set(0);
                        }

                        if (enable_overscan) {
                            video_blit_memtoscreen(0, (vid->firstline - 4) << 1,
                                                   xsize, ((vid->lastline - vid->firstline) + 8) << 1);
                        } else {
                            video_blit_memtoscreen(8, vid->firstline << 1,
                                                   xsize, (vid->lastline - vid->firstline) << 1);
                        }
                    }

                    frames++;

                    video_res_x = xsize;
                    video_res_y = ysize;
                    if ((vid->array[3] & 0x10) && (vid->mode & 1)) { /*320x200x16*/
                        video_res_x /= 2;
                        video_bpp = 4;
                    } else if (vid->array[3] & 0x10) { /*160x200x16*/
                        video_res_x /= 4;
                        video_bpp = 4;
                    } else if (vid->array[3] & 0x08) { /*640x200x4 - this implementation is a complete guess!*/
                        video_bpp = 2;
                    } else if (vid->mode & 1) {
                        video_res_x /= 8;
                        video_res_y /= vid->crtc[9] + 1;
                        video_bpp = 0;
                    } else if (!(vid->mode & 2)) {
                        video_res_x /= 16;
                        video_res_y /= vid->crtc[9] + 1;
                        video_bpp = 0;
                    } else if (!(vid->mode & 16)) {
                        video_res_x /= 2;
                        video_bpp = 2;
                    } else {
                        video_bpp = 1;
                    }
                }
                vid->firstline = 1000;
                vid->lastline  = 0;
                vid->blink++;
            }
        } else {
            vid->sc++;
            vid->sc &= 31;
            vid->ma = vid->maback;
        }
        if (vid->sc == (vid->crtc[10] & 31) || ((vid->crtc[8] & 3) == 3 && vid->sc == ((vid->crtc[10] & 31) >> 1)))
            vid->con = 1;
    }
}

static void
vid_speed_changed(void *priv)
{
    tandy_t *dev = (tandy_t *) priv;

    recalc_timings(dev);
}

static void
vid_close(void *priv)
{
    tandy_t *dev = (tandy_t *) priv;

    free(dev->vid);
    dev->vid = NULL;
}

static void
vid_init(tandy_t *dev)
{
    int       display_type;
    t1kvid_t *vid;

    vid = calloc(1, sizeof(t1kvid_t));
    vid->memctrl = -1;

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_dram);

    display_type   = device_get_config_int("display_type");
    vid->composite = (display_type != TANDY_RGB);

    cga_comp_init(1);

    if (dev->is_sl2) {
        vid->b8000_limit = 0x8000;
        vid->planar_ctrl = 4;
        overscan_x = overscan_y = 16;

        io_sethandler(0x0065, 1, vid_in, NULL, NULL, vid_out, NULL, NULL, dev);
    } else
        vid->b8000_mask = 0x3fff;

    timer_add(&vid->timer, vid_poll, dev, 1);
    mem_mapping_add(&vid->mapping, 0xb8000, 0x08000,
                    vid_read, NULL, NULL, vid_write, NULL, NULL, NULL, 0, dev);
    io_sethandler(0x03d0, 16,
                  vid_in, NULL, NULL, vid_out, NULL, NULL, dev);

    dev->vid     = vid;
}

const device_config_t vid_config[] = {
  // clang-format off
    {
        .name = "display_type",
        .description = "Display type",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = TANDY_RGB,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "RGB",       .value = TANDY_RGB       },
            { .description = "Composite", .value = TANDY_COMPOSITE },
            { .description = ""                                    }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t vid_device = {
    .name          = "Tandy 1000",
    .internal_name = "tandy1000_video",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = vid_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = vid_speed_changed,
    .force_redraw  = NULL,
    .config        = vid_config
};

const device_t vid_device_hx = {
    .name          = "Tandy 1000 HX",
    .internal_name = "tandy1000_hx_video",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = vid_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = vid_speed_changed,
    .force_redraw  = NULL,
    .config        = vid_config
};

const device_t vid_device_sl = {
    .name          = "Tandy 1000SL2",
    .internal_name = "tandy1000_sl_video",
    .flags         = 0,
    .local         = 1,
    .init          = NULL,
    .close         = vid_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = vid_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

static void
eep_write(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    t1keep_t *eep = (t1keep_t *) priv;

    if ((val & 4) && !eep->clk)
        switch (eep->state) {
            case EEPROM_IDLE:
                switch (eep->count) {
                    case 0:
                        if (!(val & 3))
                            eep->count = 1;
                        else
                            eep->count = 0;
                        break;

                    case 1:
                        if ((val & 3) == 2)
                            eep->count = 2;
                        else
                            eep->count = 0;
                        break;

                    case 2:
                        if ((val & 3) == 3)
                            eep->state = EEPROM_GET_OPERATION;
                        eep->count = 0;
                        break;

                    default:
                        break;
                }
                break;

            case EEPROM_GET_OPERATION:
                eep->data = (eep->data << 1) | (val & 1);
                eep->count++;
                if (eep->count == 8) {
                    eep->count = 0;
                    eep->addr  = eep->data & 0x3f;
                    switch (eep->data & 0xc0) {
                        case 0x40:
                            eep->state = EEPROM_WRITE;
                            break;

                        case 0x80:
                            eep->state = EEPROM_READ;
                            eep->data  = eep->store[eep->addr];
                            break;

                        default:
                            eep->state = EEPROM_IDLE;
                            break;
                    }
                }
                break;

            case EEPROM_READ:
                eep_data_out = eep->data & 0x8000;
                eep->data <<= 1;
                eep->count++;
                if (eep->count == 16) {
                    eep->count = 0;
                    eep->state = EEPROM_IDLE;
                }
                break;

            case EEPROM_WRITE:
                eep->data = (eep->data << 1) | (val & 1);
                eep->count++;
                if (eep->count == 16) {
                    eep->count            = 0;
                    eep->state            = EEPROM_IDLE;
                    eep->store[eep->addr] = eep->data;
                }
                break;

            default:
                break;
        }

    eep->clk = val & 4;
}

static void *
eep_init(const device_t *info)
{
    t1keep_t *eep;
    FILE     *fp = NULL;

    eep = (t1keep_t *) calloc(1, sizeof(t1keep_t));

    switch (info->local) {
        case TYPE_TANDY1000HX:
            eep->path = "tandy1000hx.bin";
            break;

        case TYPE_TANDY1000SL2:
            eep->path = "tandy1000sl2.bin";
            break;

        default:
            break;
    }

    fp = nvr_fopen(eep->path, "rb");
    if (fp != NULL) {
        if (fread(eep->store, 1, 128, fp) != 128)
            fatal("eep_init(): Error reading Tandy EEPROM\n");
        (void) fclose(fp);
    } else
        memset(eep->store, 0x00, 128);

    io_sethandler(0x037c, 1, NULL, NULL, NULL, eep_write, NULL, NULL, eep);

    return eep;
}

static void
eep_close(void *priv)
{
    t1keep_t *eep = (t1keep_t *) priv;
    FILE     *fp  = NULL;

    fp = nvr_fopen(eep->path, "wb");
    if (fp != NULL) {
        (void) fwrite(eep->store, 128, 1, fp);
        (void) fclose(fp);
    }

    free(eep);
}

static const device_t eep_1000hx_device = {
    .name          = "Tandy 1000HX EEPROM",
    .internal_name = "eep_1000hx",
    .flags         = 0,
    .local         = TYPE_TANDY1000HX,
    .init          = eep_init,
    .close         = eep_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static const device_t eep_1000sl2_device = {
    .name          = "Tandy 1000SL2 EEPROM",
    .internal_name = "eep_1000sl2",
    .flags         = 0,
    .local         = TYPE_TANDY1000SL2,
    .init          = eep_init,
    .close         = eep_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static void
tandy_write(uint16_t addr, uint8_t val, void *priv)
{
    tandy_t *dev = (tandy_t *) priv;

    switch (addr) {
        case 0x00a0:
            if (dev->is_hx && (val & 0x10)) {
                dev->base = (mem_size - 256) * 1024;
                dev->mask = 0x3ffff;
                mem_mapping_set_addr(&ram_low_mapping, 0, dev->base);
                mem_mapping_set_addr(&dev->ram_mapping,
                                     (((val >> 1) & 7) - 1) * 128 * 1024, 0x40000);
            } else {
                dev->base = (mem_size - 128) * 1024;
                dev->mask = 0x1ffff;
                mem_mapping_set_addr(&ram_low_mapping, 0, dev->base);
                mem_mapping_set_addr(&dev->ram_mapping,
                                     ((val >> 1) & 7) * 128 * 1024, 0x20000);
            }
            if (dev->is_hx) {
                io_removehandler(0x03d0, 16,
                                 vid_in, NULL, NULL, vid_out, NULL, NULL, dev);
                if (val & 0x01)
                    mem_mapping_disable(&dev->vid->mapping);
                else {
                    io_sethandler(0x03d0, 16,
                                  vid_in, NULL, NULL, vid_out, NULL, NULL, dev);
                    mem_mapping_set_addr(&dev->vid->mapping, 0xb8000, 0x8000);
                }
            } else {
                if (val & 0x01)
                    mem_mapping_set_addr(&dev->vid->mapping, 0xc0000, 0x10000);
                else
                    mem_mapping_set_addr(&dev->vid->mapping, 0xb8000, 0x8000);
            }
            dev->ram_bank = val;
            break;

        case 0xffe8:
            if ((val & 0xe) == 0xe)
                mem_mapping_disable(&dev->ram_mapping);
            else
                mem_mapping_set_addr(&dev->ram_mapping,
                                     ((val >> 1) & 7) * 128 * 1024,
                                     0x20000);
            recalc_address_sl(dev);
            dev->ram_bank = val;
            break;

        case 0xffea:
            dev->rom_bank   = val;
            dev->rom_offset = ((val ^ 4) & 7) * 0x10000;
            mem_mapping_set_exec(&dev->rom_mapping,
                                 &dev->rom[dev->rom_offset]);
            break;

        default:
            break;
    }
}

static uint8_t
tandy_read(uint16_t addr, void *priv)
{
    const tandy_t *dev = (tandy_t *) priv;
    uint8_t        ret = 0xff;

    switch (addr) {
        case 0x00a0:
            ret = dev->ram_bank;
            break;

        case 0xffe8:
            ret = dev->ram_bank;
            break;

        case 0xffea:
            ret = (dev->rom_bank ^ 0x10);
            break;

        default:
            break;
    }

    return ret;
}

static void
write_ram(uint32_t addr, uint8_t val, void *priv)
{
    const tandy_t *dev = (tandy_t *) priv;

    ram[dev->base + (addr & dev->mask)] = val;
}

static uint8_t
read_ram(uint32_t addr, void *priv)
{
    const tandy_t *dev = (tandy_t *) priv;

    return (ram[dev->base + (addr & dev->mask)]);
}

static uint8_t
read_rom(uint32_t addr, void *priv)
{
    const tandy_t *dev   = (tandy_t *) priv;
    uint32_t       addr2 = (addr & 0xffff) + dev->rom_offset;

    return (dev->rom[addr2]);
}

static uint16_t
read_romw(uint32_t addr, void *priv)
{
    tandy_t *dev   = (tandy_t *) priv;
    uint32_t addr2 = (addr & 0xffff) + dev->rom_offset;

    return (*(uint16_t *) &dev->rom[addr2]);
}

static uint32_t
read_roml(uint32_t addr, void *priv)
{
    tandy_t *dev = (tandy_t *) priv;

    return (*(uint32_t *) &dev->rom[addr]);
}

static void
init_rom(tandy_t *dev)
{
    dev->rom = (uint8_t *) malloc(0x80000);

#if 1
    if (!rom_load_interleaved("roms/machines/tandy1000sl2/8079047.hu1",
                              "roms/machines/tandy1000sl2/8079048.hu2",
                              0x000000, 0x80000, 0, dev->rom)) {
        tandy_log("TANDY: unable to load BIOS for 1000/SL2 !\n");
        free(dev->rom);
        dev->rom = NULL;
        return;
    }
#else
    f  = rom_fopen("roms/machines/tandy1000sl2/8079047.hu1", "rb");
    ff = rom_fopen("roms/machines/tandy1000sl2/8079048.hu2", "rb");
    for (c = 0x0000; c < 0x80000; c += 2) {
        dev->rom[c]     = getc(f);
        dev->rom[c + 1] = getc(ff);
    }
    fclose(ff);
    fclose(f);
#endif

    mem_mapping_add(&dev->rom_mapping, 0xe0000, 0x10000,
                    read_rom, read_romw, read_roml, NULL, NULL, NULL,
                    dev->rom, MEM_MAPPING_EXTERNAL, dev);
}

static void
machine_tandy1k_init(const machine_t *model, int type)
{
    tandy_t *dev;

    dev = calloc(1, sizeof(tandy_t));

    machine_common_init(model);

    nmi_init();

    /*
     * Base 128K mapping is controlled via ports 0xA0 or
     * 0xFFE8 (SL2), so we remove it from the main mapping.
     */
    dev->base = (mem_size - 128) * 1024;
    dev->mask = 0x1ffff;
    mem_mapping_add(&dev->ram_mapping, 0x60000, 0x20000,
                    read_ram, NULL, NULL, write_ram, NULL, NULL, NULL,
                    MEM_MAPPING_INTERNAL, dev);
    mem_mapping_set_addr(&ram_low_mapping, 0, dev->base);

    device_add(&keyboard_tandy_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_xt_tandy_device);

    video_reset(gfxcard[0]);

    switch (type) {
        case TYPE_TANDY:
        case TYPE_TANDY1000SX:
            keyboard_set_table(scancode_tandy);
            io_sethandler(0x00a0, 1,
                          tandy_read, NULL, NULL, tandy_write, NULL, NULL, dev);
            device_context(&vid_device);
            vid_init(dev);
            device_context_restore();
            device_add_ex(&vid_device, dev);
            device_add((type == TYPE_TANDY1000SX) ? &ncr8496_device : &sn76489_device);
            break;

        case TYPE_TANDY1000HX:
            dev->is_hx = 1;
            keyboard_set_table(scancode_tandy);
            io_sethandler(0x00a0, 1,
                          tandy_read, NULL, NULL, tandy_write, NULL, NULL, dev);
            device_context(&vid_device_hx);
            vid_init(dev);
            device_context_restore();
            device_add_ex(&vid_device_hx, dev);
            device_add(&ncr8496_device);
            device_add(&eep_1000hx_device);
            break;

        case TYPE_TANDY1000SL2:
            dev->is_sl2 = 1;
            init_rom(dev);
            io_sethandler(0xffe8, 8,
                          tandy_read, NULL, NULL, tandy_write, NULL, NULL, dev);
            device_context(&vid_device_sl);
            vid_init(dev);
            device_context_restore();
            device_add_ex(&vid_device_sl, dev);
            device_add(&pssj_device);
            device_add(&eep_1000sl2_device);
            break;

        default:
            break;
    }

    standalone_gameport_type = &gameport_device;

    eep_data_out = 0x0000;
}

int
tandy1k_eeprom_read(void)
{
    return eep_data_out;
}

int
machine_tandy1000sx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr("roms/machines/tandy/tandy1t1.020",
                            0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_tandy1k_init(model, TYPE_TANDY1000SX);

    return ret;
}

int
machine_tandy1000hx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tandy1000hx/v020000.u12",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_tandy1k_init(model, TYPE_TANDY1000HX);

    return ret;
}

int
machine_tandy1000sl2_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/tandy1000sl2/8079047.hu1",
                                "roms/machines/tandy1000sl2/8079048.hu2",
                                0x000f0000, 65536, 0x18000);

    if (bios_only || !ret)
        return ret;

    machine_tandy1k_init(model, TYPE_TANDY1000SL2);

    return ret;
}
