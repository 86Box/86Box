/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tandy 2000 emulation ported from MAME.
 *
 * Authors: Curt Coder
 *          WNT50
 *
 *          Copyright 2010-2026 Curt Coder.
 *          Copyright 2026 WNT50.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/i8018x.h>
#include <86box/machine.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/fdc.h>
#include <86box/fdd.h>
#include <86box/hdc.h>
#include <86box/rom.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/serial.h>
#include <86box/plat_unused.h>
#include <86box/tandy_clkmouse.h>

/* The keyboard hangs off the second 8259's IR0, the 8251A off the first
   8259's IR2, the printer off the second 8259's IR3 and the coprocessor
   off its IR4. */
#define TANDY2K_KBD_IRQ  (1 << 8)
#define TANDY2K_SER_IRQ  (1 << 2)
#define TANDY2K_LPT_IRQ  (1 << 11)
#define TANDY2K_FPU_IRQ  (1 << 14)

/* 8255A-5 ports plus the Mode 1 handshake state of the printer port. */
typedef struct ppi_t {
    int     mode; /* group A mode from the last control word */
    int     inte;
    int     intr;
    int     obf;
    uint8_t pa;
    uint8_t pc;
    uint8_t ctrl;
} ppi_t;

/* Character RAM sits in the low byte lane of the upper memory
   window, the BIOS writes it with one byte every two addresses. */
typedef struct tandy2k_t {
    mem_mapping_t ram_map;
    fdc_t        *fdc;
    uint8_t       fdc_strap; /* strapped FDC lines already applied */
    uint8_t       port0;  /* speaker, clock gates, FDC reset */
    uint8_t       dmamux; /* DMA request multiplexer */

    uint8_t       kbdin;  /* last scan code taken from the keyboard */
    uint8_t       kbd_busy;  /* scan code not read by the guest yet */

    void         *uart;      /* 8251A */
    int           uart_div;  /* 8253 channel 1 reload for the 2 MHz clock */
    uint8_t       uart_gate;

    ppi_t         ppi;
    int           lpt_ack;
    int           lpt_busy;
    int           lpt_phase;
    pc_timer_t    lpt_timer;
} tandy2k_t;

static tandy2k_t tandy2k;

/* The Tandy 2000 keyboard encodes its own scan codes. Most keys match the IBM
   XT set, but 29h/2Bh/4Ah/4Eh are the arrow keys here, and the keypad digits
   carry a second character (7 = backslash, 4 = bar, 8 = tilde, 2 = grave;
   Shift plus a keypad key gives the digit). The alphanumeric block has no
   backslash or grave key of its own. A PC keyboard cannot type that, so
   the keypad keys are pointed at the alphanumeric digit keys and the
   characters they carried move to the keys noted below. */
static const scancode scancode_tandy2k[512] = {
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
    { .mk = {      0x50, 0 }, .brk = {             0xd0, 0 } }, /* 029 */
    { .mk = {      0x2a, 0 }, .brk = {             0xaa, 0 } }, /* 02a */
    { .mk = {      0x47, 0 }, .brk = {             0xc7, 0 } }, /* 02b */
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
    { .mk = {      0x08, 0 }, .brk = {             0x88, 0 } }, /* 047 */
    { .mk = {      0x09, 0 }, .brk = {             0x89, 0 } }, /* 048 */
    { .mk = {      0x0a, 0 }, .brk = {             0x8a, 0 } }, /* 049 */
    { .mk = {      0x48, 0 }, .brk = {             0xc8, 0 } }, /* 04a */
    { .mk = {      0x05, 0 }, .brk = {             0x85, 0 } }, /* 04b */
    { .mk = {      0x06, 0 }, .brk = {             0x86, 0 } }, /* 04c */
    { .mk = {      0x07, 0 }, .brk = {             0x87, 0 } }, /* 04d */
    { .mk = {      0x4b, 0 }, .brk = {             0xcb, 0 } }, /* 04e */
    { .mk = {      0x02, 0 }, .brk = {             0x82, 0 } }, /* 04f */
    { .mk = {      0x03, 0 }, .brk = {             0x83, 0 } }, /* 050 */
    { .mk = {      0x04, 0 }, .brk = {             0x84, 0 } }, /* 051 */
    { .mk = {      0x0b, 0 }, .brk = {             0x8b, 0 } }, /* 052 */
    { .mk = {      0x34, 0 }, .brk = {             0xb4, 0 } }, /* 053 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 054 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 055 */
    { .mk = {            0 }, .brk = {                   0 } }, /* 056 */
    { .mk = {      0x59, 0 }, .brk = {             0xd9, 0 } }, /* 057 */
    { .mk = {      0x5a, 0 }, .brk = {             0xda, 0 } }, /* 058 */
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
    { .mk = {      0x58, 0 }, .brk = {             0xd8, 0 } }, /* 147 */
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
    { .mk = {      0x55, 0 }, .brk = {             0xd5, 0 } }, /* 152 */
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

#ifdef ENABLE_TANDY2K_LOG
int tandy2k_do_log = ENABLE_TANDY2K_LOG;

static void
tandy2k_log(const char *fmt, ...)
{
    va_list ap;

    if (tandy2k_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define tandy2k_log(fmt, ...)
#endif

/* A scan code from the encoder is held in the latch and raises the
   keyboard interrupt until the BIOS reads it back through port B. */
static void
tandy2k_kbd_adddata(uint16_t val)
{
    tandy2k.kbdin    = (uint8_t) (val & 0xff);
    tandy2k.kbd_busy = 1;
    picint(TANDY2K_KBD_IRQ);
}

static void
tandy2k_kbd_send(uint16_t val)
{
    kbd_adddata_process(val, tandy2k_kbd_adddata);
}

static void
tandy2k_uart_clock(void)
{
    double hz = 0.0;

    /* RFRQGATE gates the 2 MHz baud clock, EXTCLK switches to the J1 line. */
    if (tandy2k.uart_gate && !(tandy2k.port0 & 0x02))
        hz = 2000000.0 / (double) tandy2k.uart_div;

    i8251_set_clock(tandy2k.uart, hz);
}

static void
tandy2k_pit1_load(UNUSED(uint8_t mode), int count)
{
    tandy2k.uart_div = count;
    tandy2k_uart_clock();
}

static void
tandy2k_lpt_intr(int state)
{
    if (tandy2k.ppi.intr == state)
        return;

    tandy2k.ppi.intr = state;
    if (state)
        picint(TANDY2K_LPT_IRQ);
    else
        picintc(TANDY2K_LPT_IRQ);
}

/* No printer is attached to the port, so it is modelled as one that always
   accepts the byte: BUSY for the transfer, then an ACK* pulse. */
static void
tandy2k_lpt_timer(void *priv)
{
    switch (tandy2k.lpt_phase) {
        case 1:
            tandy2k.lpt_ack   = 0;
            tandy2k.lpt_phase = 2;
            timer_set_delay_u64(&tandy2k.lpt_timer, 2 * TIMER_USEC);
            break;
        case 2:
            tandy2k.lpt_ack   = 1;
            tandy2k.lpt_busy  = 0;
            tandy2k.lpt_phase = 0;
            if (tandy2k.ppi.mode == 1) {
                tandy2k.ppi.obf = 1;
                if (tandy2k.ppi.inte)
                    tandy2k_lpt_intr(1);
            }
            break;
        default:
            break;
    }
}

static uint8_t
tandy2k_read(uint16_t addr, void *priv)
{
    switch (addr & 0x000f) {
        case 0x00:
            return tandy2k.port0;
        case 0x02:
            return tandy2k.dmamux;
        case 0x04:
            /* Reading the floppy terminal count acknowledges it. */
            return 0x00;
        default:
            break;
    }

    return 0xff;
}

static void tandy2k_fdc_strap(void);

static void
tandy2k_write(uint16_t addr, uint8_t val, void *priv)
{
    switch (addr & 0x000f) {
        case 0x00:
            /* FDCRESET* is active low, so asserting it is the falling edge. */
            if ((tandy2k.port0 & 0x20) && !(val & 0x20)) {
                fdc_reset(tandy2k.fdc);
                tandy2k_fdc_strap();
            }
            tandy2k.port0 = val;

            /* RFRQGATE gates the refresh and baud clocks, EXTCLK switches the
               baud clock source to the J1 line. */
            tandy2k.uart_gate = (val >> 4) & 0x01;
            pit_devs[0].set_gate(pit_devs[0].data, 1, tandy2k.uart_gate);
            pit_devs[0].set_gate(pit_devs[0].data, 2, tandy2k.uart_gate);
            tandy2k_uart_clock();
            break;
        case 0x02:
            tandy2k.dmamux = val;
            break;
        default:
            break;
    }
}

static uint8_t
ppi_pb_read(void)
{
    /* Port C bits 2-1 select what port B reads back. */
    switch ((tandy2k.ppi.pc >> 1) & 0x03) {
        case 0x01:
            /* KBDINEN: reading port B acknowledges the keyboard interrupt. */
            if (tandy2k.kbd_busy) {
                tandy2k.kbd_busy = 0;
                picintc(TANDY2K_KBD_IRQ);
            }
            return tandy2k.kbdin;
        case 0x02:
            /* PORTINEN: PCB revision. */
            return 0x03;
        default:
            /* LPINEN: _LPRACK, _LPRFLT and _LPRSEL are low when asserted. */
            return (tandy2k.lpt_ack ? 0x08 : 0x00) |
                   0x10 | /* not faulted */
                   0x20 | /* selected */
                   (tandy2k.lpt_busy ? 0x80 : 0x00);
    }
}

static uint8_t
ppi_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;

    switch (addr & 0x0006) {
        case 0x00:
            ret = (tandy2k.ppi.ctrl & 0x10) ? 0xff : tandy2k.ppi.pa;
            break;
        case 0x02:
            ret = ppi_pb_read();
            break;
        case 0x04:
            if ((tandy2k.ppi.mode == 1) && !(tandy2k.ppi.ctrl & 0x10))
                ret = (tandy2k.ppi.pc & 0x37) |
                      (tandy2k.ppi.intr ? 0x08 : 0x00) |
                      (tandy2k.ppi.inte ? 0x40 : 0x00) |
                      (tandy2k.ppi.obf ? 0x80 : 0x00);
            else
                ret = tandy2k.ppi.pc;
            break;
        case 0x06:
            ret = tandy2k.ppi.ctrl;
            break;
        default:
            break;
    }

    /* The printer interrupt is acknowledged by any read of the PPI. */
    if (tandy2k.ppi.mode == 1)
        tandy2k_lpt_intr(0);

    return ret;
}

static void
ppi_write(uint16_t addr, uint8_t val, void *priv)
{
    uint8_t bit;

    switch (addr & 0x0006) {
        case 0x00:
            tandy2k.ppi.pa = val;
            if ((tandy2k.ppi.mode == 1) && !(tandy2k.ppi.ctrl & 0x10)) {
                /* OBF* falls, which pulses STROBE and clears INTR. */
                tandy2k.ppi.obf   = 0;
                tandy2k.lpt_busy  = 1;
                tandy2k.lpt_phase = 1;
                tandy2k_lpt_intr(0);
                timer_set_delay_u64(&tandy2k.lpt_timer, 100 * TIMER_USEC);
            }
            break;
        case 0x04:
            if (tandy2k.ppi.mode == 1)
                /* PC3, PC6 and PC7 are driven by the PPI in mode 1. */
                tandy2k.ppi.pc = (tandy2k.ppi.pc & 0xf8) | (val & 0x07);
            else
                tandy2k.ppi.pc = val;
            break;
        case 0x06:
            if (val & 0x80) {
                tandy2k.ppi.ctrl = val;
                tandy2k.ppi.mode = (val >> 5) & 0x03;
                if ((tandy2k.ppi.mode == 1) && !(val & 0x10)) {
                    tandy2k.ppi.obf  = 1;
                    tandy2k.ppi.inte = 0;
                    tandy2k_lpt_intr(0);
                }
            } else {
                bit = (uint8_t) (1u << ((val >> 1) & 0x07));
                if (tandy2k.ppi.mode == 1) {
                    switch ((val >> 1) & 0x07) {
                        case 3:
                            tandy2k_lpt_intr(val & 0x01);
                            break;
                        case 6:
                            tandy2k.ppi.inte = val & 0x01;
                            if (tandy2k.ppi.inte && tandy2k.ppi.obf)
                                tandy2k_lpt_intr(1);
                            break;
                        case 7:
                            /* OBF* is an output pin in mode 1. */
                            break;
                        default:
                            /* PC4 and PC5 are inputs in mode 1. */
                            if (((val >> 1) & 0x07) <= 2) {
                                if (val & 0x01)
                                    tandy2k.ppi.pc |= bit;
                                else
                                    tandy2k.ppi.pc &= (uint8_t) ~bit;
                            }
                            break;
                    }
                } else if (val & 0x01)
                    tandy2k.ppi.pc |= bit;
                else
                    tandy2k.ppi.pc &= (uint8_t) ~bit;
            }
            break;
        default:
            break;
    }
}

/* The drive poll timers are rebuilt by fdd_reset(), which runs after this
   machine's init, so the strapped lines cannot be applied there: arming
   motors earlier leaves those timers linked twice. Apply them from the
   controller reset the guest pulses instead, with the first FDC access
   as a fallback for anything that does not reset the controller. */
static void
tandy2k_fdc_strap(void)
{
    fdc_t *fdc = tandy2k.fdc;

    if (tandy2k.fdc_strap)
        return;

    tandy2k.fdc_strap = 1;

    for (int i = 0; i < 4; i++)
        fdd_set_motor_enable(fdc->fdd[i], 0);

    /* There is no DOR on this board: the IRQ/DMA enable and the motors are
       strapped on, and releasing the emulated DOR reset instead would queue
       four reset status entries. */
    fdc_write(0x03f2, 0xf8, fdc);

    /* That write clears the status register, which the host reads as busy;
       with no DOR the controller is simply idle. */
    fdc->stat = 0x80;
}

/* The 8272 has only two host registers and no DOR: the status sits on 30h and
   the data/command register on 32h, while 80h is the same data register at
   the DMA acknowledge address. */
static uint8_t
tandy2k_fdc_read(uint16_t addr, void *priv)
{
    tandy2k_fdc_strap();

    return fdc_read((addr == 0x0030) ? 0x03f4 : 0x03f5, priv);
}

static void
tandy2k_fdc_write(uint16_t addr, uint8_t val, void *priv)
{
    fdc_t *fdc = (fdc_t *) priv;

    tandy2k_fdc_strap();

    /* With no DOR the command's unit field, always the first parameter, is
       only drive select this board has. Mirror it into the DOR so the core
       latches the right drive; RECALIBRATE runs inside this very write. */
    if ((fdc->pnum == 0) && (fdc->ptot > 0))
        fdc->dor = (fdc->dor & ~0x03) | (val & 0x03);

    /* Command and data share the 8272's second register. */
    fdc_write(0x03f5, val, priv);
}

static uint8_t
tandy2k_fdc_data_read(uint16_t addr, void *priv)
{
    return fdc_read(0x03f5, priv);
}

static void
tandy2k_fdc_data_write(uint16_t addr, uint8_t val, void *priv)
{
    fdc_write(0x03f5, val, priv);
}

/* The chip's cascade ack returns the vector the external 8259 puts on the bus. */
static int
tandy2k_8259(void *priv)
{
    return pic_irq_ack_instance((pic_t *) priv);
}

/* The 8259 output callback carries only the level, so each line needs its own
   adapter to name the 80186 interrupt input it drives. */
static void
tandy2k_icu_int0(void *priv, int level)
{
    i8018x_pcb_icu_int((struct i8018x_pcb_t *) priv, 0, level);
}

static void
tandy2k_icu_int1(void *priv, int level)
{
    i8018x_pcb_icu_int((struct i8018x_pcb_t *) priv, 1, level);
}

/* The coprocessor's error output reaches the board as MCPINT14 rather than
   through the CPU's NMI input, so the x87 core reports it here. */
static void
tandy2k_fpu_int(int level)
{
    if (level)
        picint(TANDY2K_FPU_IRQ);
    else
        picintc(TANDY2K_FPU_IRQ);
}

/* UCS covers F8000-FFFFF: the lower 16 KB is the character RAM that the
   video device maps, and the boot ROM sits at FE000. */
static const device_config_t tandy2000_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "tandy2000_020000",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "BOOT ROM version 01.00.00 (1983)",
                .internal_name = "tandy2000_010000",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/tandy2000/rev1_fc000_fe000.img", "" }
            },
            {
                .name          = "BOOT ROM version 02.00.00 (1984)",
                .internal_name = "tandy2000_020000",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/tandy2000/rev2_fc000_fe000.img", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t tandy_2000_device = {
    .name          = "Tandy 2000",
    .internal_name = "tandy2000",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = tandy2000_config
};

int
machine_tandy2000_init(const machine_t *model)
{
    int         ret = 0;
    fdc_t      *fdc;
    const char *fn;

    if (!device_available(model->device)) {
        /* No ROMs available. */
        return 0;
    }

    device_context(model->device);
    fn  = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000fe000, 8192, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    /* RAM is contiguous up to the top, unlike the PC layout that stops at
       A0000h; without this the BIOS sizes anything above 640 KB as absent. */
    if (mem_size > 640) {
        mem_set_mem_state_both(0xa0000, (uint32_t) (mem_size - 640) << 10,
                               MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        mem_mapping_add(&tandy2k.ram_map, 0xa0000,
                        (uint32_t) (mem_size - 640) << 10,
                        mem_read_ram, mem_read_ramw, mem_read_raml,
                        mem_write_ram, mem_write_ramw, mem_write_raml,
                        NULL, MEM_MAPPING_INTERNAL, NULL);
    }

    pic_init_tandy2k();

    fpu_int_line = tandy2k_fpu_int;

    /* There are no ISA slots, and a standalone COM2 would sit at 2F8h-2FFh,
       on top of the option board's chip select. */
    serial_set_next_inst(SERIAL_MAX - 1);

    /* The state block is static, so a hard reset has to put the registers the
       guest can see back to their power-on values; a stale FDCRESET* level
       would otherwise pulse the controller during POST. */
    tandy2k.port0  = 0x00;
    tandy2k.ppi.pa = 0x00;
    tandy2k.ppi.pc = 0x00;
    tandy2k.dmamux = 0x00;

    tandy2k.uart_gate = 1;   /* the 8253 leaves reset with its gates high */
    tandy2k.uart_div = 13;

    tandy2k.ppi.ctrl = 0x9b; /* all ports input, mode 0 */
    tandy2k.ppi.mode = 0;
    tandy2k.ppi.inte = 0;
    tandy2k.ppi.intr = 0;
    tandy2k.ppi.obf  = 1;
    tandy2k.lpt_ack  = 1;
    tandy2k.lpt_busy = 0;
    tandy2k.lpt_phase = 0;
    picintc(TANDY2K_LPT_IRQ);
    timer_add(&tandy2k.lpt_timer, tandy2k_lpt_timer, NULL, 0);

    /* The 8272 sits on the 16-bit bus, so its two registers appear on even
       addresses. There is no DOR, so the drive and motor lines are wired for
       drive 0 with its motor running; the BIOS moves sectors by PIO. */
    fdc = device_add(&fdc_xt_device);
    fdc_remove(fdc);
    io_sethandler(0x0030, 0x0004, tandy2k_fdc_read, NULL, NULL,
                  tandy2k_fdc_write, NULL, NULL, fdc);
    io_sethandler(0x0080, 0x0001, tandy2k_fdc_data_read, NULL, NULL,
                  tandy2k_fdc_data_write, NULL, NULL, fdc);
    fdc_set_irq(fdc, 4);
    fdc_set_dma_ch(fdc, 0);
    fdc_reset(fdc);
    tandy2k.fdc       = fdc;
    tandy2k.fdc_strap = 0;

    /* The turbo timing skips the seek and bit-cell paths this machine's BIOS
       depends on, so the drives never take it. */
    for (int i = 0; i < 4; i++)
        fdd_set_turbo(fdc->fdd[i], 0);

    device_add(&tandy_2000_video_device);

    if (hdc_current[0] == HDC_INTERNAL)
        device_add(&st506_tandy2k_wd1010_device);

    /* The board is an option: no internal mouse means it is not installed,
       and its clock goes with it. */
    if (mouse_type == MOUSE_TYPE_INTERNAL)
        device_add(&tandy_clkmouse_device);

    /* The board clocks the 8253 at 2 MHz and decodes its registers on
       even addresses, so replace the default 40h-43h mapping. */
    pit_t *pit = pit_common_init(PIT_8253, NULL, NULL);
    pit_set_clock(2000000);
    pit_handler(0, 0x0040, 0x0004, pit);
    io_sethandler(0x0040, 0x0008, pit_read_even, NULL, NULL,
                  pit_write_even, NULL, NULL, pit);
    pit_devs[0].set_load_func(pit_devs[0].data, 1, tandy2k_pit1_load);

    /* The 8251A takes its TxC/RxC from the 8253 channel 1 output. Its
       host backend is whatever character device is selected for COM1. */
    tandy2k.uart = device_add(&i8251_device);
    i8251_set_irq(tandy2k.uart, TANDY2K_SER_IRQ);
    io_sethandler(0x0010, 0x0004, i8251_read, NULL, NULL,
                  i8251_write, NULL, NULL, tandy2k.uart);
    tandy2k_uart_clock();

    io_sethandler(0x0050, 0x0008, ppi_read, NULL, NULL,
                  ppi_write, NULL, NULL, NULL);

    tandy2k.kbdin = 0x00;
    tandy2k.kbd_busy = 0;
    /* The keyboard has its own 8048 whose ROM is undumped, so send it
       the scan codes the encoder would have produced. */
    keyboard_set_table(scancode_tandy2k);
    keyboard_send = tandy2k_kbd_send;

    /* Both 8259As are slaves of the 80186's internal controller:
       the first drives INT0 and the second INT1, and each puts
       own vector on the bus during the acknowledge cycles. */
    struct i8018x_pcb_t *pcb = device_add(&i8018x_pcb_device);
    i8018x_pcb_set_dma_channels(pcb, 0, -1);
    i8018x_pcb_set_ext_ack(pcb, 0, tandy2k_8259, &pic);
    i8018x_pcb_set_ext_ack(pcb, 1, tandy2k_8259, &pic2);
    pic_set_output_callback(&pic, tandy2k_icu_int0, pcb);
    pic_set_output_callback(&pic2, tandy2k_icu_int1, pcb);

    io_sethandler(0x0000, 0x0010, tandy2k_read, NULL, NULL,
                  tandy2k_write, NULL, NULL, NULL);

    return ret;
}
