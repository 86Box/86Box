/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the DEC VAXmate I/O-Video module video controller:
 *          6845 CRTC with 64 KB video memory, a 4 KB programmable font RAM,
 *          a 16-entry video look-up table and a translation ROM in front of
 *          the industry-standard CRTC ports.
 *
 *          Register/port map (TRM Table 7-6):
 *            03D0 W     CRTC index (DIGITAL extended, direct)
 *            03D1 R/W   CRTC data  (DIGITAL extended, direct)
 *            03D4 W     CRTC index (industry-standard, through translation ROM)
 *            03D5 R/W   CRTC data  (industry-standard, through translation ROM)
 *            03D8 W     Control Register A
 *            03D9 W     Color Select Register
 *            03DA R     Status Register A
 *            03DD R     Status Register B
 *            03DE R     Write Data Register (last value written via 03D4/03D5)
 *            03DF W     Control Register B
 *            0C80 R/W   Special Purpose Register
 *
 *          Reference: AA-HD92A-TK VAXmate Technical Reference Manual
 *          Volume 1, Chapter 7 (Video Controller - Hardware Description);
 *          EK-PC500-TD VAXmate Technical Description, Chapter 4.
 *
 * Authors: WNT50
 *
 *          Copyright 2026 WNT50.
 */
#include <wchar.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pit.h>
#include <86box/video.h>
#include <86box/plat_unused.h>

/* Dot clock: the video gate array runs from the 22.367195 MHz crystal for every mode except
   VT240 emulation, which switches to 27.977244 MHz (TRM 4.2.7). The monitor then sees 26.40
   kHz horizontal / 60 Hz vertical with 848 dots per line at 640 pixels, and 26.49 kHz with
   1056 dots at 800 pixels (TRM chapter 6, "I/O - Video Module"). */
#define VAXMATE_CLK_NORMAL  22367195.0
#define VAXMATE_CLK_VT240   27977244.0

/* Dot counts that make up one scan line, active plus blanking. */
#define VAXMATE_DOTS_640    848
#define VAXMATE_DOTS_800    1056

/* Frame line counts. Both modes target the NTSC 59.94 Hz field rate
   rather than the TRM's rounded "60 Hz" (640x400: 22367195/848/440,
   800x252: 27977244/1056/442); 441 lines would give 60.076 Hz. */
#define VAXMATE_LINES_640   440
#define VAXMATE_LINES_800   442

#define VAXMATE_FONT_RAM    0x01000 /* 256 characters x 16 bytes */
#define VAXMATE_VRAM        0x10000 /* 64 KB memory, B0000-BFFFF */
#define VAXMATE_VLT         16

/* CRTC register indices (TRM Table 7-7) */
enum {
    CRTC_HTOTAL   = 0x00,
    CRTC_HDISP    = 0x01,
    CRTC_HSYNC    = 0x02,
    CRTC_SYNCW    = 0x03,
    CRTC_VTOTAL   = 0x04,
    CRTC_VADJ     = 0x05,
    CRTC_VDISP    = 0x06,
    CRTC_VSYNC    = 0x07,
    CRTC_ILACE    = 0x08,
    CRTC_MAXSCAN  = 0x09,
    CRTC_CURSTART = 0x0a,
    CRTC_CUREND   = 0x0b,
    CRTC_STARTHI  = 0x0c,
    CRTC_STARTLO  = 0x0d,
    CRTC_CURHI    = 0x0e,
    CRTC_CURLO    = 0x0f,
    CRTC_LPENHI   = 0x10,
    CRTC_LPENLO   = 0x11
};

/* Video modes, TRM Table 7-11 (control register A bits 2-0 + control B bit 7) */
enum {
    /* The number is the {bit4, bit1, bit0} pattern of control register A
       (A2 A1 A0 in TRM table 7-11). Bit 3 is display enable and bit 2 is
       always 0, so neither takes part in the code. The BIOS's D0/D1/D2
       are just aliases for these codes. */
    VMODE_TEXT40      = 0, /* 000: 40x25 text */
    VMODE_TEXT80      = 1, /* 001: 80x25 text */
    VMODE_TEXT_LAST   = VMODE_TEXT80, /* text mode */
    VMODE_G320x200x4  = 2, /* 010: 320x200 4-colour */
    VMODE_G320x200x16 = 3, /* 011: 320x200 16-colour */
    VMODE_G640x400x2  = 4, /* 100: 640x400 2-colour (BIOS mode D0) */
    VMODE_G640x200x4  = 5, /* 101: 640x200 4-colour */
    VMODE_G640x200x2  = 6, /* 110: 640x200 2-colour (BIOS mode 6) */
    VMODE_G640x400x4  = 7, /* 111: 640x400 4-colour (BIOS mode D1) */
    VMODE_G800x252x4  = 8  /* 111 + control B bit 7: 800x252 4-colour (D2) */
};

/* Status register A (03DA) */
#define STATA_DISPENSE    0x01
#define STATA_VSYNC       0x08

/* Status register B (03DD), TRM page 7-38 */
#define STATB_PORT_3D5    0x01
#define STATB_WRITE_CHECK 0x02

/* Control register A (03D8) */
#define CTLA_DISPLAY_EN   0x08
#define CTLA_BLINK        0x20

/* Control register B (03DF) */
#define CTLB_VLT_ENABLE   0x04
#define CTLB_DISPLAY_EN   0x08
#define CTLB_FONT_ENABLE  0x10
#define CTLB_CRA5_ENABLE  0x20
#define CTLB_SAVER        0x40
#define CTLB_MONITOR252   0x80

/* Special purpose register (0C80), TRM page 7-23 */
#define SPR_SPEED_SELECT  0x01
#define SPR_DISABLE_COMM  0x02
#define SPR_SPLIT_BAUD    0x04
#define SPR_VIDEO_ENABLE  0x08
#define SPR_SPEED_IND     0x10
#define SPR_INDEX         0x20
#define SPR_TRACK0        0x40
#define SPR_WRITE_PROTECT 0x80

typedef struct vaxmate_vid_t {
    mem_mapping_t mapping;

    uint8_t  crtc[32];     /* values the 6845 sees (after translation) */
    uint8_t  crtc_raw[32]; /* values the guest wrote to 03D5           */
    uint8_t  crtcreg;
    uint8_t  ctrl_a;
    uint8_t  ctrl_b;
    uint8_t  display_type;
    uint8_t  color_select;
    uint8_t  write_data;
    uint8_t  status_a;
    uint8_t  status_b;
    uint8_t  last_irgb;    /* RGBI of the last rendered pixel, reported on 03DA bits 7-4 */

    uint8_t  vlt[VAXMATE_VLT];
    uint8_t  vram[VAXMATE_VRAM];
    uint8_t  font_ram[VAXMATE_FONT_RAM];

    int      monitor_index;
    int      prev_monitor_index;
    int      monitor_mode; /* 0 = 400 line, 1 = 252 line (control B bit 7) */

    int      vc;
    int      sc;
    int      scanline;
    int      memaddr;
    int      memaddr_backup;
    int      text_base;
    int      displine;
    int      active_lines;
    int      hphase;
    int      firstline;
    int      lastline;
    int      linepos;
    int      dispon;
    int      blink;
    int      vadj;
    int      vsynctime;

    uint64_t dispontime;
    uint64_t dispofftime;
    uint64_t line_ticks;
    pc_timer_t timer;

    uint8_t  spr;
    int      mode;
    int      video_disabled;
} vaxmate_vid_t;

#define VAXMATE_MONITOR_PROLOGUE()                          \
    {                                                       \
        vid->prev_monitor_index = monitor_index_global;     \
        monitor_index_global    = vid->monitor_index;       \
    }
#define VAXMATE_MONITOR_EPILOGUE()                          \
    {                                                       \
        monitor_index_global = vid->prev_monitor_index;     \
    }

static video_timings_t timing_vaxmate = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32,
                                                              .read_b = 8, .read_w = 16, .read_l = 32 };

/* defined after the mode table; used by recalctimings() */
static int vaxmate_mode_width(int mode);

/* Translation ROM, TRM page 7-26 and Figure 7-1: the industry-standard CRTC
   window (03D4/03D5) goes through a 4Kx8 parameter translation ROM addressed
   by {register 0-15, data 7-0}, so only the ROM output reaches the 6845 and
   software that programs CGA timings still gets DIGITAL monitor timings.

   The ROM contents are not readable (it sits inside the gate array), so this
   is a behavioral model: crtc_raw[] keeps the guest's values and the vertical
   registers are rewritten for the frame we render: a fixed 400-line monitor
   (a 200-line field shown twice) with 8x16 text cells. R6 is only seven bits
   wide, so the scan lines per row are the smallest power of two that keeps
   the row count inside 127; blanking and sync offsets are kept so the field
   proportions do not change.

   DEC's own ROM BIOS programs the DIGITAL window (03D0/03D1) directly and
   never touches this path, so the translation only affects software
   written for an industry-standard CGA. */
static double
vaxmate_ticks_per_dot(vaxmate_vid_t *vid)
{
    double clk = (vid->mode == VMODE_G800x252x4) ? VAXMATE_CLK_VT240
                                                 : VAXMATE_CLK_NORMAL;

    return (cpuclock * (double) (1ULL << 32)) / clk;
}

static int
vaxmate_line_dots(vaxmate_vid_t *vid)
{
    return (vid->mode == VMODE_G800x252x4) ? VAXMATE_DOTS_800
                                           : VAXMATE_DOTS_640;
}

static void
vaxmate_vid_recalctimings(vaxmate_vid_t *vid)
{
    /* One scan line is a fixed number of dots at the crystal frequency, no
       matter what the guest programmed: the translation ROM keeps the field
       at exactly 26.40 kHz (26.49 in the 800-pixel mode) so the DEC monitor
       stays locked. The CRTC only decides where inside that line the active
       area starts. */
    int    dots   = vaxmate_line_dots(vid);
    int    active = vaxmate_mode_width(vid->mode);
    double ticks  = vaxmate_ticks_per_dot(vid);

    if (active < 320)
        active = 640;

    vid->dispontime  = (uint64_t) ((double) active * ticks);
    vid->dispofftime = (uint64_t) ((double) (dots - active) * ticks);
    vid->line_ticks  = (uint64_t) ((double) dots * ticks);
}

static int
vaxmate_vid_mode(vaxmate_vid_t *vid)
{
    int code = ((vid->ctrl_a & 0x10) >> 2) | (vid->ctrl_a & 0x03);

    /* Code 111 is split into D1 and D2 by the 252-line bit of control B. */
    if (code == 7)
        return (vid->ctrl_b & CTLB_MONITOR252) ? VMODE_G800x252x4 : VMODE_G640x400x4;

    return code;
}

/* TRM 2.5.1 and Figure 4-7: the 64 KB space is decoded as a whole, but only
   the RAM blocks the current mode uses are driven onto the bus. The BIOS's
   second presence probe (F2C15 with AL=1) depends on this: in text mode a
   write to B000:0000 must not read back, or the board is taken for an MDA
   adapter and the hand-off at F2BAD switches the DIGITAL video off. */
static int
vaxmate_vram_visible(vaxmate_vid_t *vid, uint32_t addr)
{
    uint32_t off = addr & (VAXMATE_VRAM - 1);

    switch (vid->mode) {
        case VMODE_TEXT40:
        case VMODE_TEXT80:
        case VMODE_G320x200x4:
        case VMODE_G640x200x2:
            /* 16 KB of static RAM at B8000-BBFFF */
            return (off >= 0x8000) && (off <= 0xbfff);

        case VMODE_G320x200x16:
        case VMODE_G640x400x2:
        case VMODE_G640x200x4:
            /* B8000-BFFFF: static RAM plus one 16 KB dynamic block */
            return off >= 0x8000;

        default:
            /* 640x400x4 / 800x252x4 start at B0000 and use the whole space */
            return 1;
    }
}

static uint8_t
vaxmate_vid_read(uint32_t addr, void *priv)
{
    vaxmate_vid_t *vid = (vaxmate_vid_t *) priv;

    /* 0C80 bit3 = 0: the whole DIGITAL block is off the bus. */
    if (vid->video_disabled)
        return 0xff;

    if (!vaxmate_vram_visible(vid, addr))
        return 0xff;

    if (!(addr & 1)) {
        if ((vid->ctrl_b & CTLB_FONT_ENABLE) && (vid->mode <= VMODE_TEXT_LAST)) {
            uint32_t off = (addr - 0xb8000) >> 1;
            if (off < VAXMATE_FONT_RAM)
                return vid->font_ram[off];
        }
        if ((vid->ctrl_b & CTLB_VLT_ENABLE) && (vid->mode <= VMODE_TEXT_LAST)) {
            if (addr >= 0xb8000 && addr < 0xb8020)
                return vid->vlt[(addr - 0xb8000) >> 1];
        }
    }

    return vid->vram[addr & (VAXMATE_VRAM - 1)];
}

/* Text buffer <-> font RAM/VLT shadowing, TRM pages 7-15 and 7-30:
   when control register B bit 4 (font) or bit 2 (VLT) is set, even text
   buffer addresses access the font RAM / VLT instead of video memory. */
static void
vaxmate_vid_write(uint32_t addr, uint8_t val, void *priv)
{
    vaxmate_vid_t *vid = (vaxmate_vid_t *) priv;

    /* 0C80 bit3 = 0: the whole DIGITAL block is off the bus. */
    if (vid->video_disabled)
        return;

    if (!vaxmate_vram_visible(vid, addr))
        return;

    if (addr & 1) {
        /* odd addresses always hit video memory */
        vid->vram[addr & (VAXMATE_VRAM - 1)] = val;
        return;
    }

    if ((vid->ctrl_b & CTLB_FONT_ENABLE) && (vid->mode <= VMODE_TEXT_LAST)) {
        /* B8000-B9FFF even addresses -> 4 KB font RAM */
        uint32_t off = (addr - 0xb8000) >> 1;
        if (off < VAXMATE_FONT_RAM) {
            vid->font_ram[off] = val;
            return;
        }
    }

    if ((vid->ctrl_b & CTLB_VLT_ENABLE) && (vid->mode <= VMODE_TEXT_LAST)) {
        if (addr >= 0xb8000 && addr < 0xb8020) {
            vid->vlt[(addr - 0xb8000) >> 1] = val;
            return;
        }
    }

    vid->vram[addr & (VAXMATE_VRAM - 1)] = val;
}

/* Graphics memory maps, TRM pages 7-11 .. 7-16 and Figures 7-3 .. 7-15: a
   logical scan line L lives in block (L mod P), where the block stride and
   the first block address differ per mode. 200-line modes are displayed
   twice on the 400-line monitor, 252-line mode is displayed as-is. */
typedef struct {
    uint16_t width;
    uint16_t lines;
    uint16_t rowbytes;
    uint8_t  planes;   /* number of memory blocks        */
    uint32_t stride;   /* bytes between blocks           */
    uint32_t origin;   /* first block, absolute address  */
    uint8_t  bpp;      /* bits per pixel                 */
} vaxmate_gfx_t;

static const vaxmate_gfx_t vaxmate_modes[9] = {
    { 640, 400, 80,  1, 0x2000, 0xb8000, 0 }, /* 000 40x25 text       */
    { 640, 400, 160, 1, 0x2000, 0xb8000, 0 }, /* 001 80x25 text       */
    { 320, 200, 80,  2, 0x2000, 0xb8000, 2 }, /* 010 320x200 4-color  */
    { 320, 200, 160, 4, 0x2000, 0xb8000, 4 }, /* 011 320x200 16-color */
    { 640, 400, 80,  4, 0x2000, 0xb8000, 1 }, /* 100 D0 640x400 2-col */
    { 640, 200, 160, 2, 0x4000, 0xb8000, 2 }, /* 101 640x200 4-color  */
    { 640, 200, 80,  2, 0x2000, 0xb8000, 1 }, /* 110 640x200 2-color  */
    { 640, 400, 160, 4, 0x4000, 0xb0000, 2 }, /* 111 D1 640x400 4-col */
    { 800, 252, 200, 4, 0x4000, 0xb0000, 2 }, /* 111 D2 800x252 4-col */
};

static void
vaxmate_crtc_translate(vaxmate_vid_t *vid)
{
    const vaxmate_gfx_t *m = &vaxmate_modes[vid->mode];
    int text = (vid->mode <= VMODE_TEXT_LAST);
    int lines, rows;
    int lpr = 1;
    int raw4, raw6, raw7, raw9;
    int guest_lpr, guest_lines, guest_total;
    int blank_lines, sync_lines;
    int i, v;

    lines = text ? 400 : m->lines;

    while ((lines / lpr) > 128)
        lpr <<= 1;
    if (text)
        lpr = 16;

    rows = lines / lpr;

    for (i = 0; i < 32; i++)
        vid->crtc[i] = vid->crtc_raw[i];

    raw4 = vid->crtc_raw[CRTC_VTOTAL] & 0x7f;
    raw6 = vid->crtc_raw[CRTC_VDISP] & 0x7f;
    raw7 = vid->crtc_raw[CRTC_VSYNC] & 0x7f;
    raw9 = vid->crtc_raw[CRTC_MAXSCAN] & 0x1f;

    /* Blanking and sync offsets as the guest meant them, counted in scan
       lines so they survive the change of scan lines per row. */
    guest_lpr   = raw9 + 1;
    guest_lines = (raw6 + 1) * guest_lpr;
    guest_total = (raw4 + 1) * guest_lpr;

    blank_lines = guest_total - guest_lines;
    if (blank_lines < 0)
        blank_lines = 0;

    sync_lines = ((raw7 + 1) - (raw6 + 1)) * guest_lpr;
    if (sync_lines < 0)
        sync_lines = 0;

    vid->crtc[CRTC_MAXSCAN] = (uint8_t) (lpr - 1);
    vid->crtc[CRTC_VDISP]   = (uint8_t) (rows - 1);

    v = rows + (blank_lines / lpr);
    vid->crtc[CRTC_VTOTAL] = (uint8_t) ((v > 0x7f) ? 0x7f : v);

    v = rows + (sync_lines / lpr);
    vid->crtc[CRTC_VSYNC] = (uint8_t) ((v > 0x7f) ? 0x7f : v);
}

static int
vaxmate_mode_width(int mode)
{
    if ((mode < 0) || (mode >= (int) (sizeof(vaxmate_modes) / sizeof(vaxmate_modes[0]))))
        return 0;

    return vaxmate_modes[mode].width;
}

/* CRTC parameter blocks, ROM F000:F13D -- 12 bytes each (R0..R11), R12-R15 = 0.
   Values cross-checked against TRM Table 7-8 "CRTC Register Values".
   Block index per internal mode:
     +00 40x25 text (IBM 0/1)   +10 80x25 text (IBM 2/3)
     +20 320x200 modes and D0   +30 IBM mode 7 (MDA timing)
     +40 D1 640x400x4           +50 D2 800x252x4 */
static const uint8_t vaxmate_crtc_blocks[6][12] = {
    { 0x34, 0x28, 0x2d, 0x54, 0x1a, 0x08, 0x19, 0x19, 0x40, 0x0f, 0x00, 0x07 },
    { 0x69, 0x50, 0x58, 0x58, 0x1a, 0x08, 0x19, 0x19, 0x40, 0x0f, 0x00, 0x07 },
    { 0x34, 0x28, 0x2c, 0x54, 0x6d, 0x00, 0x64, 0x66, 0x40, 0x03, 0x00, 0x07 },
    { 0x61, 0x50, 0x52, 0x0f, 0x19, 0x06, 0x19, 0x19, 0x02, 0x0d, 0x0b, 0x0c },
    { 0x69, 0x50, 0x58, 0x58, 0x6d, 0x00, 0x64, 0x66, 0x42, 0x03, 0x00, 0x07 },
    { 0x83, 0x64, 0x6d, 0x5a, 0x6d, 0x01, 0x3f, 0x53, 0x40, 0x03, 0x00, 0x07 },
};

/* VLT contents are RGBI codes (TRM page 7-19, Table 7-4): bit 3 = R, bit 2 = G,
   bit 1 = B, bit 0 = I.  Table 7-4's intensity column is just the code value,
   so 0 = black through 15 = white. The VAXmate monitor was sold with either
   an amber or a green phosphor CRT; amber is the default. */
static uint32_t
vaxmate_vlt_color(vaxmate_vid_t *vid, uint8_t v)
{
    int      y = (v & 0x0f) * 17;
    uint32_t r, g, b;

    switch (vid->display_type) {
        case 0: /* amber phosphor */
            r = (uint32_t) y;
            g = (uint32_t) ((y * 3) >> 2);
            b = (uint32_t) (y >> 4);
            break;

        case 1:
        default: /* green phosphor */
            r = (uint32_t) (y >> 3);
            g = (uint32_t) y;
            b = (uint32_t) (y >> 3);
            break;
    }

    return 0xff000000 | (r << 16) | (g << 8) | b;
}

/* Text attribute bytes carry the color as IRGB (bit 3 = I, 2 = R, 1 = G,
   0 = B) while the VLT is addressed in RGBI order, so the two middle bits
   swap and the intensity moves to bit 0.  TRM page 7-19: IRGB 0xC
   (intensified red) selects VLT entry 9. */
static uint8_t
vaxmate_attr_to_vlt(uint8_t irgb)
{
    return (uint8_t) (((irgb & 0x08) >> 3) | ((irgb & 0x01) << 1)
                    | ((irgb & 0x02) << 1) | ((irgb & 0x04) << 1));
}

/* 4-color palette selection, TRM Table 7-10 (CPS = bit 5, SIC = bit 4) */
static const uint8_t vaxmate_pal4[4][4] = {
    { 0x0, 0x2, 0x4, 0x6 },   /* CPS=0 SIC=0: black, green, red, brown       */
    { 0x0, 0x3, 0x5, 0xb },   /* CPS=1 SIC=0: black, cyan, magenta, l.cyan   */
    { 0x0, 0xa, 0xc, 0xe },   /* CPS=0 SIC=1: black, l.green, l.red, yellow  */
    { 0x0, 0x7, 0xd, 0xf },   /* CPS=1 SIC=1: black, white, l.magenta, white */
};

/* The column count comes from the mode code: the low three bits of control A
   are DEC's own encoding (bit 0 = 80 columns), not a BIOS mode number -- the
   BIOS keeps writing mode 1 for a whole 80-column DOS session (BDA
   0040:044A = 0x50, 160-byte text rows). */
static int
vaxmate_text_cols(vaxmate_vid_t *vid)
{
    return (vid->mode == VMODE_TEXT40) ? 40 : 80;
}

static void
vaxmate_vid_render_text(vaxmate_vid_t *vid, int line)
{
    int      cols = vaxmate_text_cols(vid);
    int      cw   = (cols == 40) ? 16 : 8;  /* 40-column modes: 16-dot cells */
    int      pix  = cw / 8;
    int      x;
    uint16_t cursoraddr = (uint16_t) (vid->crtc[CRTC_CURLO] | (vid->crtc[CRTC_CURHI] << 8));
    /* Cursor display mode and block size, TRM page 7-33: R10 bits 6-5 select
       non-blinking / hidden / 3.75 Hz / 1.875 Hz, R10 bits 4-0 the start scan
       line and R11 bits 4-0 the end scan line. R10 powers up hidden, so the
       cursor only appears once software programs it. */
    int      cursormode  = (vid->crtc[CRTC_CURSTART] >> 5) & 0x03;
    int      cursorstart = vid->crtc[CRTC_CURSTART] & 0x1f;
    int      cursorend   = vid->crtc[CRTC_CUREND] & 0x1f;
    int      cursorblink = 1;
    int      cursorscan  = 0;
    int      blink_en    = (vid->ctrl_a & CTLA_BLINK) && (vid->ctrl_b & CTLB_CRA5_ENABLE);
    int      blink       = (vid->blink >> 4) & 1;

    if (cursormode == 0x02)
        cursorblink = (vid->blink >> 3) & 1;   /* 3.75 Hz */
    else if (cursormode == 0x03)
        cursorblink = (vid->blink >> 4) & 1;   /* 1.875 Hz */

    if ((cursormode != 0x01) && cursorblink)
        cursorscan = (vid->scanline >= cursorstart) && (vid->scanline <= cursorend);

    for (x = 0; x < cols; x++) {
        /* text_base is the byte base (B0000->0, B8000->0x8000) and memaddr
           is the character address, so double it before adding the base. */
        uint32_t off  = ((uint32_t) vid->text_base
                       + (((uint32_t) vid->memaddr + (uint32_t) x) * 2)) & 0xffff;
        uint8_t  chr  = vid->vram[off & (VAXMATE_VRAM - 1)];
        uint8_t  attr = vid->vram[(off + 1) & (VAXMATE_VRAM - 1)];
        /* When blink is enabled, attribute bit 7 is the blink flag and the
           background only has the three color bits left (Table 7-2). */
        uint8_t  bg_irgb = (uint8_t) (blink_en ? ((attr >> 4) & 0x07) : ((attr >> 4) & 0x0f));
        uint8_t  fg_idx  = vaxmate_attr_to_vlt(attr & 0x0f);
        uint8_t  bg_idx  = vaxmate_attr_to_vlt(bg_irgb);
        int      is_cursor = cursorscan && ((uint16_t) (vid->memaddr + x) == cursoraddr);
        int      is_blink = blink_en && blink && (attr & 0x80);
        int      c;
        int      p;

        for (c = 0; c < 8; c++) {
            uint8_t  row = vid->font_ram[(chr * 16 + (vid->scanline & 15)) & (VAXMATE_FONT_RAM - 1)];
            int      on  = (row >> (7 - c)) & 1;
            uint8_t  idx = (on && !is_blink) ? fg_idx : bg_idx;
            uint32_t col;

            /* The cursor shows the character in reverse video (TRM 7-8). */
            if (is_cursor)
                idx = on ? bg_idx : fg_idx;

            vid->last_irgb = vid->vlt[idx] & 0x0f;
            col            = vaxmate_vlt_color(vid, vid->vlt[idx]);

            for (p = 0; p < pix; p++)
                buffer32->line[line][x * cw + c * pix + p] = col;
        }
    }
}

static void
vaxmate_vid_render_graphics(vaxmate_vid_t *vid, int line, int displine)
{
    const vaxmate_gfx_t *m = &vaxmate_modes[vid->mode];
    uint32_t off;
    int      l;
    int      x;

    if ((vid->active_lines > 0) && (m->lines != vid->active_lines))
        l = displine * m->lines / vid->active_lines;
    else
        l = displine;

    if (l >= m->lines)
        return;

    off = (m->origin - 0xb0000u) + (uint32_t) (l % m->planes) * m->stride
        + (uint32_t) (l / m->planes) * m->rowbytes;

    for (x = 0; x < m->width; x++) {
        uint8_t v;
        uint8_t idx;

        switch (m->bpp) {
            case 1:
                v   = vid->vram[(off + (uint32_t) (x >> 3)) & (VAXMATE_VRAM - 1)];
                idx = (v >> (7 - (x & 7))) & 1;
                /* TRM page 7-18: in 2-color modes the foreground comes from the low
                   four bits of the color select register and the background from VLT
                   entry 0. CPS/SIC only apply to the 4-color modes (Table 7-9). */
                v = idx ? vaxmate_attr_to_vlt(vid->color_select & 0x0f) : 0;
                vid->last_irgb = vid->vlt[v] & 0x0f;
                buffer32->line[line][x] = vaxmate_vlt_color(vid, vid->vlt[v]);
                break;

            case 2:
                v   = vid->vram[(off + (uint32_t) (x >> 2)) & (VAXMATE_VRAM - 1)];
                idx = (v >> (6 - 2 * (x & 3))) & 3;
                v   = vaxmate_pal4[((vid->color_select >> 4) & 3)][idx] & 0x0f;
                vid->last_irgb = vid->vlt[v] & 0x0f;
                buffer32->line[line][x] = vaxmate_vlt_color(vid, vid->vlt[v]);
                break;

            default:
                v   = vid->vram[(off + (uint32_t) (x >> 1)) & (VAXMATE_VRAM - 1)];
                idx = (v >> (4 - 4 * (x & 1))) & 0x0f;
                vid->last_irgb = vid->vlt[idx] & 0x0f;
                buffer32->line[line][x] = vaxmate_vlt_color(vid, vid->vlt[idx]);
                break;
        }
    }
}

static void
vaxmate_vid_poll(void *priv)
{
    vaxmate_vid_t *vid = (vaxmate_vid_t *) priv;
    int lpr, vdisp_rows, row, sub, start, active, total;

    VAXMATE_MONITOR_PROLOGUE()

    /* Frame height is fixed: 25 rows of an 8x16 cell in text (TRM 7-6), 400
       lines in graphics. R6/R9 hold the guest's "industry standard" values
       and only become real LAD values through the 4Kx8 translation ROM. */
    if (vid->mode <= VMODE_TEXT_LAST) {
        lpr        = 16;
        vdisp_rows = 25;
    } else {
        lpr        = 1;
        vdisp_rows = 400;
    }

    if (vid->mode == VMODE_G800x252x4) {
        /* VT240 raster: pin 7 of the video connector goes high and the monitor
           switches to 252-scan operation (TRM 4-2 "400/252 select"), so all
           252 scan lines held in memory (Figure 7-15, L = 0 to 251) are
           displayed inside the 441-line field. */
        active = 252;
        total  = VAXMATE_LINES_800;
    } else {
        active = vdisp_rows * lpr;
        total  = VAXMATE_LINES_640;
    }
    vid->active_lines = active;

    /* Two 03DA/03BA bits follow the scan. Each line is split into two
       halves: bit 0 is 1 during the display half and 0 during horizontal
       blanking, so it toggles at the line rate (26 kHz); bit 3 is vertical
       sync, in the last few lines of the frame. The BIOS scroll and cursor
       routines wait on these two bits for retrace. */
    if (vid->hphase == 0) {
        vid->status_a = 0;
        if (vid->displine < active)
            vid->status_a |= STATA_DISPENSE;
        if ((total > 3) && (vid->displine >= (total - 3)))
            vid->status_a |= STATA_VSYNC;

        vid->hphase = 1;
        timer_advance_u64(&vid->timer, vid->dispontime);

        VAXMATE_MONITOR_EPILOGUE()

        return;
    }

    /* Horizontal blanking: bit 0 drops and the line is over. */
    vid->status_a = 0;
    if ((total > 3) && (vid->displine >= (total - 3)))
        vid->status_a |= STATA_VSYNC;
    vid->hphase = 0;

    if (vid->displine >= active) {
        /* Vertical blanking: about 40 lines, to fill 440 lines at 60 Hz */
        vid->dispon = 0;
    } else {
        vid->dispon = 1;

        row = vid->displine / lpr;
        sub = vid->displine % lpr;

        if (vid->displine == 0)
            video_wait_for_buffer();

        /* R12/R13 are a 16-bit character address (the ROM BIOS writes the
           page start here), so both bytes are used in full. */
        start = (vid->crtc[CRTC_STARTHI] << 8) | vid->crtc[CRTC_STARTLO];

        vid->scanline = sub;
        vid->memaddr  = start + (row * (vaxmate_text_cols(vid) == 40 ? 40 : 80));

        if (vid->mode <= VMODE_TEXT_LAST)
            vaxmate_vid_render_text(vid, vid->displine);
        else
            vaxmate_vid_render_graphics(vid, vid->displine, vid->displine);
    }

    vid->displine++;

    if (vid->displine >= total) {
        /* End of frame: send it to the screen */
        int h = active;
        int w = vaxmate_modes[vid->mode].width;

        if (w < 320) w = 640;
        if (h < 200) h = 400;

        if ((xsize != w) || (ysize != h) || video_force_resize_get()) {
            xsize = w;
            ysize = h;
            set_screen_size(xsize, ysize);
            if (video_force_resize_get())
                video_force_resize_set(0);
        }

        video_blit_memtoscreen(0, 0, w, h);
        video_res_x = w;
        video_res_y = h;
        video_bpp   = 1;

        vid->displine = 0;
        vid->vc       = 0;
        vid->blink++;   /* 60 Hz frame rate / 32 = the 1.9 Hz text blink */
    }

    /* Rest of the line: the 848 (or 1056) dots minus the display half. The
       line rate is locked by the monitor and ignores whatever R0/R1 the
       guest writes to the CRTC. */
    timer_advance_u64(&vid->timer, vid->dispofftime);

    VAXMATE_MONITOR_EPILOGUE()
}

static uint8_t
vaxmate_vid_in(uint16_t addr, void *priv)
{
    vaxmate_vid_t *vid = (vaxmate_vid_t *) priv;

    /* 0C80 bit3 = 0: the whole DIGITAL block is off the bus, so 03D0-03DF
       stop responding as well. */
    if (vid->video_disabled)
        return 0xff;

    switch (addr) {
        case 0x3d0:
            return vid->crtcreg;

        case 0x3d1:
            return vid->crtc[vid->crtcreg];

        case 0x3d4:
            return vid->crtcreg;

        case 0x3d5:
            return vid->crtc[vid->crtcreg];

        case 0x3da:
            /* Bits 7-4 carry the IRGB of the pixel being output (TRM page
               7-37); the colour of the last rendered pixel is reported here. */
            return vid->status_a | (uint8_t) ((vid->last_irgb & 0x0f) << 4);

        case 0x3dd: {
            /* Bits 7-2 mirror the blanking state and the control registers,
               bits 1-0 the write and port check flags (TRM page 7-38). */
            uint8_t statb = 0;

            if ((vid->hphase != 0) || (vid->displine >= vid->active_lines))
                statb |= 0x80;                      /* VIDEO BLANK */
            if (vid->ctrl_b & CTLB_DISPLAY_EN)
                statb |= 0x40;                      /* CR-B3 */
            if (vid->ctrl_b & CTLB_CRA5_ENABLE)
                statb |= 0x20;                      /* CR-B5 */
            if (vid->ctrl_a & 0x10)
                statb |= 0x10;                      /* CR-A4, mode bit 2 */
            if (vid->ctrl_a & 0x02)
                statb |= 0x08;                      /* CR-A1, mode bit 1 */
            if (vid->ctrl_a & 0x01)
                statb |= 0x04;                      /* CR-A0, mode bit 0 */
            statb |= vid->status_b & (STATB_WRITE_CHECK | STATB_PORT_3D5);

            return statb;
        }

        case 0x3de:
            vid->status_b &= ~STATB_WRITE_CHECK;
            return vid->write_data;

        default:
            break;
    }

    return 0xff;
}

static void
vaxmate_vid_out(uint16_t addr, uint8_t val, void *priv)
{
    vaxmate_vid_t *vid = (vaxmate_vid_t *) priv;

    /* 0C80 bit3 = 0: the whole DIGITAL block is off the bus, so writes to
       03D0-03DF are dropped as well. */
    if (vid->video_disabled)
        return;

    switch (addr) {
        case 0x3d0: /* DIGITAL extended CRTC index, direct */
            vid->crtcreg = val & 0x1f;
            return;

        case 0x3d1: /* DIGITAL extended CRTC data, direct */
            if (vid->crtcreg < 32) {
                vid->crtc[vid->crtcreg] = val;
                vaxmate_vid_recalctimings(vid);
            }
            return;

        case 0x3d4: /* industry-standard CRTC index, through translation ROM */
            vid->crtcreg    = val & 0x1f;
            vid->status_b  |= STATB_WRITE_CHECK;
            vid->status_b  &= ~STATB_PORT_3D5;
            return;

        case 0x3d5: /* industry-standard CRTC data, through translation ROM */
            if (vid->crtcreg < 32) {
                vid->crtc_raw[vid->crtcreg] = val;
                vaxmate_crtc_translate(vid);
                vid->write_data         = val;
                vid->status_b          |= STATB_WRITE_CHECK | STATB_PORT_3D5;
                vaxmate_vid_recalctimings(vid);
            }
            return;

        case 0x3d8:
            /* DIGITAL/CGA text base is B8000 (offset 0x8000 in the 64 KB
               space); the MDA window does not exist on this board. */
            vid->text_base = 0x8000;
            vid->ctrl_a = val;
            vid->mode   = vaxmate_vid_mode(vid);
            vaxmate_crtc_translate(vid);
            vaxmate_vid_recalctimings(vid);
            return;

        case 0x3d9:
            vid->color_select = val & 0x3f;
            return;

        case 0x3df:
            vid->ctrl_b     = val;
            vid->monitor_mode = (val & CTLB_MONITOR252) ? 1 : 0;
            vid->mode       = vaxmate_vid_mode(vid);
            vaxmate_crtc_translate(vid);
            vaxmate_vid_recalctimings(vid);
            return;

        default:
            break;
    }
}

/* Special purpose register at 0C80 (TRM page 7-23) is a system register: the
   machine layer owns the I/O port and calls these two so the video window
   follows bit 3. The other bits (diskette status, split baud rates, ...)
   never reach this device. */
uint8_t
vaxmate_video_spr(void *priv)
{
    vaxmate_vid_t *vid = (vaxmate_vid_t *) priv;

    return vid ? vid->spr : 0xff;
}

void
vaxmate_video_spr_set(void *priv, uint8_t val)
{
    vaxmate_vid_t *vid = (vaxmate_vid_t *) priv;

    if (vid == NULL)
        return;

    vid->spr = val;
    /* Bit 3 = 1 keeps the video on the bus (power-on state); 0 takes
       whole block off. The mapping stays registered and the read/write
       handlers gate the access, so a disconnected window reads 0xFF and
       drops writes instead of falling through to anything behind it. */
    vid->video_disabled = (val & SPR_VIDEO_ENABLE) ? 0 : 1;
}

static void *
vaxmate_vid_init(UNUSED(const device_t *info))
{
    vaxmate_vid_t *vid = calloc(1, sizeof(vaxmate_vid_t));

    vid->display_type = (uint8_t) device_get_config_int("display_type");

    video_inform(VIDEO_FLAG_TYPE_MDA, &timing_vaxmate);

    vid->monitor_index = monitor_index_global;
    vid->ctrl_a        = CTLA_DISPLAY_EN | VMODE_TEXT80;   /* 09h: 80x25 text */
    vid->ctrl_b        = CTLB_DISPLAY_EN | CTLB_SAVER;
    vid->mode          = VMODE_TEXT80;
    vid->spr           = SPR_VIDEO_ENABLE; /* video enabled at power-on */

    vid->video_disabled = 0;
    vid->dispon         = 1;

    int c;

    /* power-on VLT: Table 7-4 default contents.  The table is the identity
       mapping with two entries exchanged: 7 (white) and 14 (light cyan). */
    for (c = 0; c < VAXMATE_VLT; c++)
        vid->vlt[c] = (uint8_t) (c & 0x0f);
    vid->vlt[0x07] = 0x0e;
    vid->vlt[0x0e] = 0x07;

    /* power-on font: MDA 8x16 ROM font */
    for (c = 0; c < 256; c++)
        memcpy(&vid->font_ram[c * 16], fontdatm[c], 16);

    mem_mapping_add(&vid->mapping, 0xb0000, VAXMATE_VRAM,
                    vaxmate_vid_read, NULL, NULL,
                    vaxmate_vid_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, vid);

    io_sethandler(0x03d0, 0x0010,
                  vaxmate_vid_in, NULL, NULL,
                  vaxmate_vid_out, NULL, NULL,
                  vid);

    /* Power-on CRTC = the BIOS's own 80x25 text block (ROM F000:F13D + 0x10,
       programmed by INT 10h AH=00 at F000:E4F1), so frames come out with real
       hardware timing before the BIOS touches the CRTC: 25 rows x 16 scan
       lines plus R5=8 vertical adjust = the 400-line field. */
    for (c = 0; c < 12; c++)
        vid->crtc_raw[c] = vaxmate_crtc_blocks[1][c];

    /* Power-on cursor state: hidden (R10 = 0x20), end scan line 15. Neither
       ROM BIOS revision programs these, so the cursor stays off until
       software enables it. */
    vid->crtc_raw[CRTC_CURSTART] = 0x20;
    vid->crtc_raw[CRTC_CUREND]   = 0x0f;
    vaxmate_crtc_translate(vid);

    vid->displine      = 0;
    vid->firstline     = 1000;
    vid->active_lines  = 400;
    vid->hphase        = 0;
    vid->memaddr       = 0;
    vid->text_base     = 0x8000;

    vaxmate_vid_recalctimings(vid);
    timer_add(&vid->timer, vaxmate_vid_poll, vid, 1);

    return vid;
}

static void
vaxmate_vid_close(void *priv)
{
    vaxmate_vid_t *vid = (vaxmate_vid_t *) priv;

    if (!vid)
        return;

    free(vid);
}

static void
vaxmate_vid_speed_changed(UNUSED(void *priv))
{
    vaxmate_vid_t *vid = (vaxmate_vid_t *) priv;

    vaxmate_vid_recalctimings(vid);
}

static const device_config_t vaxmate_vid_config[] = {
    // clang-format off
    {
        .name           = "display_type",
        .description    = "Display type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Amber", .value = 0 },
            { .description = "Green", .value = 1 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t vaxmate_video_device = {
    .name          = "DEC VAXmate Video",
    .internal_name = "vaxmate_video",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = vaxmate_vid_init,
    .close         = vaxmate_vid_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = vaxmate_vid_speed_changed,
    .force_redraw  = NULL,
    .config        = vaxmate_vid_config
};
