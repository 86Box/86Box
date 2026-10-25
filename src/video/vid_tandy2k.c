/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tandy 2000 video emulation ported from MAME.
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
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/video.h>
#include <86box/device.h>
#include <86box/plat_unused.h>

/* The character RAM only uses the low byte lane, so the BIOS
   stores the n-th glyph byte at address 2n. */
#define TANDY2K_CHRAM_BASE  0xF8000
#define TANDY2K_CHRAM_SIZE  0x04000

/* Window the CPU sees the frame buffer through; port 101h selects it. */
#define TANDY2K_FB_WINDOW   0x08000
#define TANDY2K_FB_TEXT     0x06C00  /* last 5 KB of the window */

/* The graphics board carries its own video RAM, seen by the CPU as a 32 KB
   window at E0000h; port 1A0h picks which plane is visible. */
#define TANDY2K_VRAM_BASE   0xE0000
#define TANDY2K_VRAM_PLANE  0x08000
#define TANDY2K_VRAM_PLANES 3

#define TANDY2K_BOARD_NONE  0
#define TANDY2K_BOARD_MONO  1
#define TANDY2K_BOARD_COLO  2

#define TANDY2K_COLS        80
#define TANDY2K_ROWS        25
#define TANDY2K_XSIZE       (TANDY2K_COLS * 8)
#define TANDY2K_YSIZE       (TANDY2K_ROWS * 16)

/* VPAC timing as programmed by the BIOS: 106 characters of 8 dots per
   horizontal period, 440 scan lines per frame, 22.4 MHz dot clock. */
#define TANDY2K_FRAME_HZ    ((16000000.0 * 28.0 / 20.0) / ((106 * 8) * 440))

/* The standard VAC divides VSYNC by 32 for character blink (25% duty) and
   by 16 for the cursor (50% duty). */
#define TANDY2K_BLINK_FRAMES  32
#define TANDY2K_CURSOR_FRAMES 16

typedef struct t2kvid_t {
    uint8_t       chram[TANDY2K_CHRAM_SIZE];
    uint8_t       vram[TANDY2K_VRAM_PLANES][TANDY2K_VRAM_PLANE];
    uint8_t       vpac[0x20];
    uint8_t       pal[8];
    uint8_t       vbase;
    uint8_t       bank;   /* plane shown in the window */
    uint8_t       board;  /* graphics adapter type */
    uint8_t       hires;  /* port 1A0h bits 4-5 */
    uint8_t       vidout; /* port 101h bit 7 */

    uint64_t      frames;
    mem_mapping_t chram_map;
    mem_mapping_t vram_map;
    pc_timer_t    timer;
    uint64_t      frame_time;
} t2kvid_t;

/* Video memory sits on the main board, so CPU accesses add no bus cycles. */
static video_timings_t timing_tandy2k = { .type = VIDEO_BUS, .write_b = 0, .write_w = 0, .write_l = 0, .read_b = 0, .read_w = 0, .read_l = 0 };

static uint8_t
chram_read(uint32_t addr, void *priv)
{
    return ((uint8_t *) priv)[addr & (TANDY2K_CHRAM_SIZE - 1)];
}

static uint16_t
chram_readw(uint32_t addr, void *priv)
{
    const uint8_t *chram = (const uint8_t *) priv;
    uint32_t       off   = addr & (TANDY2K_CHRAM_SIZE - 1);

    return (uint16_t) (chram[off] | (chram[(off + 1) & (TANDY2K_CHRAM_SIZE - 1)] << 8));
}

static void
chram_write(uint32_t addr, uint8_t val, void *priv)
{
    ((uint8_t *) priv)[addr & (TANDY2K_CHRAM_SIZE - 1)] = val;
}

static void
chram_writew(uint32_t addr, uint16_t val, void *priv)
{
    uint8_t *chram = (uint8_t *) priv;
    uint32_t off   = addr & (TANDY2K_CHRAM_SIZE - 1);

    chram[off]                                     = (uint8_t) (val & 0x00ff);
    chram[(off + 1) & (TANDY2K_CHRAM_SIZE - 1)]    = (uint8_t) (val >> 8);
}

static uint8_t
vram_read(uint32_t addr, void *priv)
{
    t2kvid_t *vid = (t2kvid_t *) priv;

    if (vid->bank >= TANDY2K_VRAM_PLANES)
        return 0xff;

    return vid->vram[vid->bank][addr & (TANDY2K_VRAM_PLANE - 1)];
}

static uint16_t
vram_readw(uint32_t addr, void *priv)
{
    t2kvid_t *vid = (t2kvid_t *) priv;
    uint32_t  off = addr & (TANDY2K_VRAM_PLANE - 1);

    if (vid->bank >= TANDY2K_VRAM_PLANES)
        return 0xffff;

    return (uint16_t) (vid->vram[vid->bank][off] |
                       (vid->vram[vid->bank][(off + 1) & (TANDY2K_VRAM_PLANE - 1)] << 8));
}

static void
vram_write(uint32_t addr, uint8_t val, void *priv)
{
    t2kvid_t *vid = (t2kvid_t *) priv;

    if (vid->bank >= TANDY2K_VRAM_PLANES)
        return;

    vid->vram[vid->bank][addr & (TANDY2K_VRAM_PLANE - 1)] = val;
}

static void
vram_writew(uint32_t addr, uint16_t val, void *priv)
{
    t2kvid_t *vid = (t2kvid_t *) priv;
    uint32_t  off = addr & (TANDY2K_VRAM_PLANE - 1);

    if (vid->bank >= TANDY2K_VRAM_PLANES)
        return;

    vid->vram[vid->bank][off]                                  = (uint8_t) (val & 0x00ff);
    vid->vram[vid->bank][(off + 1) & (TANDY2K_VRAM_PLANE - 1)] = (uint8_t) (val >> 8);
}

static uint8_t
vpac_read(uint16_t addr, void *priv)
{
    t2kvid_t *vid = (t2kvid_t *) priv;

    if (addr == 0x0101)
        return vid->vbase;

    if (addr & 0x0001)
        return 0xff;

    return vid->vpac[(addr & 0x003f) >> 1];
}

static void
vpac_write(uint16_t addr, uint8_t val, void *priv)
{
    t2kvid_t *vid = (t2kvid_t *) priv;

    if (addr == 0x0101) {
        /* The controller fetches from main RAM at this base, and bit 7 picks
           which source drives the monochrome monitor. */
        vid->vbase   = val;
        vid->vidout = (val >> 7) & 0x01;
        return;
    }

    if (addr & 0x0001)
        return;

    vid->vpac[(addr & 0x003f) >> 1] = val;
}

static uint8_t
attr_read(uint16_t addr, void *priv)
{
    t2kvid_t *vid = (t2kvid_t *) priv;

    /* 180h reads the graphics board status port, which shares the write-only
       palette addresses. Bit 0 is 0 when a board is fitted, bit 1 is 0 for a
       one-plane board and 1 for the three-plane colour option. */
    if (addr == 0x0180) {
        switch (vid->board) {
            case TANDY2K_BOARD_MONO:
                return 0x00;
            case TANDY2K_BOARD_COLO:
                return 0x02;
            default:
                return 0xff;
        }
    }

    if (addr & 0x0001)
        return 0xff;

    return vid->pal[((addr - 0x0180) & 0x0f) >> 1];
}

static void
attr_write(uint16_t addr, uint8_t val, void *priv)
{
    t2kvid_t *vid = (t2kvid_t *) priv;

    if (addr & 0x0001)
        return;

    /* 180h-18Fh hold pens 0-7; 190h-19Fh mirror the same eight. */
    vid->pal[((addr - 0x0180) & 0x0f) >> 1] = val & 0x0f;
}

static uint8_t
hires_read(UNUSED(uint16_t addr), UNUSED(void *priv))
{
    return 0xff;
}

static void
hires_write(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    t2kvid_t *vid  = (t2kvid_t *) priv;
    uint8_t   bits = val & 0x07;

    /* Exactly one plane select picks a plane; anything else parks the window
       outside the video RAM. The one-plane board has no plane decode at all,
       so its writes always land in plane 0. */
    if (vid->board == TANDY2K_BOARD_MONO)
        vid->bank = 0;
    else if ((bits == 0x01) || (bits == 0x02) || (bits == 0x04))
        vid->bank = (bits == 0x01) ? 0 : (bits == 0x02) ? 1 : 2;
    else
        vid->bank = TANDY2K_VRAM_PLANES;

    vid->hires = (val >> 4) & 0x03;
}

static uint32_t
pen_green(uint8_t level)
{
    /* The palette decoder expands each colour to two bits,
       so the green gun only ever sees 0x00, 0x55, 0xAA or 0xFF. */
    const uint8_t idx = ((level & 0x07) ? 2 : 0) | ((level >> 3) & 1);

    return (uint32_t) (idx * 0x55) << 8;
}

static uint32_t
pen_color(uint8_t level)
{
    /* The palette decoder expands each colour to two bits: the colour bit sits
       above the intensity bit, so the guns see 0x00, 0x55, 0xAA or 0xFF. */
    const uint8_t i = (level >> 3) & 0x01;
    const uint8_t r = ((((level >> 2) & 0x01) << 1) | i) * 0x55;
    const uint8_t g = ((((level >> 1) & 0x01) << 1) | i) * 0x55;
    const uint8_t b = ((((level >> 0) & 0x01) << 1) | i) * 0x55;

    return ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
}

/* Text fetch: with a graphics board fitted the video base points into the
   board's RAM, which the CPU sees through the window at E0000h. */
static uint8_t *
vid_cell_ptr(t2kvid_t *vid, uint32_t addr)
{
    if ((vid->board != TANDY2K_BOARD_NONE) && (addr >= TANDY2K_VRAM_BASE) &&
        ((addr + 1) < (TANDY2K_VRAM_BASE + TANDY2K_FB_WINDOW))) {
        if (vid->bank >= TANDY2K_VRAM_PLANES)
            return NULL;

        return &vid->vram[vid->bank][addr - TANDY2K_VRAM_BASE];
    }

    if ((addr + 1) < ((uint32_t) mem_size << 10))
        return &ram[addr];

    return NULL;
}

static void
vid_render_hires(t2kvid_t *vid)
{
    for (int y = 0; y < 400; y++) {
        uint32_t *p = buffer32->line[y];

        for (int sx = 0; sx < TANDY2K_COLS; sx++) {
            uint32_t off = (uint32_t) ((y * TANDY2K_COLS) + sx);
            uint8_t  a   = vid->vram[0][off];
            uint8_t  b   = (vid->board == TANDY2K_BOARD_COLO) ? vid->vram[1][off] : 0x00;
            uint8_t  c   = (vid->board == TANDY2K_BOARD_COLO) ? vid->vram[2][off] : 0x00;

            for (int bit = 0; bit < 8; bit++) {
                const uint8_t color = (uint8_t) (((a >> (7 - bit)) & 1) |
                                                 (((b >> (7 - bit)) & 1) << 1) |
                                                 (((c >> (7 - bit)) & 1) << 2));

                p[(sx * 8) + bit] = pen_color(vid->pal[color]);
            }
        }
    }
}

static void
vid_render(t2kvid_t *vid)
{
    /* The three-plane board picks graphics with bit 5 of the plane select
       register. The one-plane board has neither the bit nor a palette, so
       the monitor follows VIDOUTSEL in port 101h instead: clear means the
       board's plane 0 drives it, set means the 9007 character generator. */
    int gfx = (vid->board == TANDY2K_BOARD_MONO) ? !vid->vidout : ((vid->hires & 0x02) != 0);

    if (gfx && (vid->board != TANDY2K_BOARD_NONE)) {
        vid_render_hires(vid);
        return;
    }

    /* Port 101h holds A19-A15 of the 32 KB window; the text buffer is its last 5 KB. */
    uint32_t fb    = ((uint32_t) (vid->vbase & 0x1f) << 15) + TANDY2K_FB_TEXT;
    int      blink_off = (int) ((vid->frames % TANDY2K_BLINK_FRAMES) < (TANDY2K_BLINK_FRAMES / 4));
    int      cursor_on = (int) ((vid->frames % TANDY2K_CURSOR_FRAMES) < (TANDY2K_CURSOR_FRAMES / 2));

    for (int row = 0; row < TANDY2K_ROWS; row++) {
        for (int line = 0; line < 16; line++) {
            uint32_t *p = buffer32->line[(row * 16) + line];

            for (int col = 0; col < TANDY2K_COLS; col++) {
                uint32_t cell = (uint32_t) ((row * TANDY2K_COLS) + col) * 2;
                uint8_t *cellp = vid_cell_ptr(vid, fb + cell);
                uint8_t  code = 0x00;
                uint8_t  attr = 0x00;
                uint8_t  bits = 0x00;
                uint8_t  pen  = 4;
                uint32_t fg   = pen_green(0x0f);
                uint32_t bg   = pen_green(0x00);

                if (cellp != NULL) {
                    code = cellp[0];
                    attr = cellp[1];
                    bits = vid->chram[((code & 0x80) ? 0x1000 : 0x0000) +
                                      ((code & 0x7f) * 32) + (line * 2)];
                    /* Attribute bits follow the CRT 9021 wiring: 7 = REVID,
                       6 = INT, 5 = BLINK, 2 = CHABL. INT set means full
                       intensity, so it picks the high pens 4/5. */
                    pen = (uint8_t) (4 | ((attr & 0x40) ? 0 : 2));
                    fg  = pen_green(vid->pal[pen | 1]);
                    bg  = pen_green(vid->pal[pen]);

                    if (attr & 0x04)
                        bits = 0x00;
                    if (attr & 0x80)
                        bits = (uint8_t) ~bits;
                    if ((attr & 0x20) && blink_off)
                        bits = 0x00;
                }

                /* CURSOR stays active for the whole data row and the VAC
                   inverts the character, so every scan line is covered. */
                if (cursor_on && (row == vid->vpac[0x18]) && (col == vid->vpac[0x19]))
                    bits = (uint8_t) ~bits;

                for (int dot = 0; dot < 8; dot++)
                    p[(col * 8) + dot] = (bits & (0x80 >> dot)) ? fg : bg;
            }
        }
    }
}

static void
vid_poll(void *priv)
{
    t2kvid_t *vid = (t2kvid_t *) priv;

    timer_advance_u64(&vid->timer, vid->frame_time);

    /* The OSD can reset the screen size behind the driver's back; honour
       its resize request even when the mode itself has not changed. */
    if ((TANDY2K_XSIZE != xsize) || (TANDY2K_YSIZE != ysize) ||
        video_force_resize_get()) {
        xsize = TANDY2K_XSIZE;
        ysize = TANDY2K_YSIZE;
        set_screen_size(xsize, ysize);

        if (video_force_resize_get())
            video_force_resize_set(0);
    }

    video_wait_for_buffer();

    vid_render(vid);

    video_blit_memtoscreen(0, 0, xsize, ysize);

    vid->frames++;
    frames++;

    video_res_x = xsize / 8;
    video_res_y = ysize / 16;
    video_bpp   = 0;
}

static void *
tandy2k_vid_init(UNUSED(const device_t *info))
{
    t2kvid_t *vid = (t2kvid_t *) calloc(1, sizeof(t2kvid_t));

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_tandy2k);

    overscan_x = overscan_y = 0;

    vid->board = (uint8_t) device_get_config_int("graphics");

    /* The BIOS only programs the palette when the high resolution board is
       reported present, so the on-board colours it relies on must be there
       from the start: background 0, character 15 and 7 for the two
       intensities. The monochrome adapter drives its pixels
       straight from pen 0 and 1. */
    vid->pal[4] = 0x00;
    vid->pal[5] = 0x0f;
    vid->pal[6] = 0x00;
    vid->pal[7] = 0x07;

    if (vid->board == TANDY2K_BOARD_MONO) {
        vid->pal[0] = 0x00;
        vid->pal[1] = 0x0f;
    }

    mem_mapping_add(&vid->chram_map, TANDY2K_CHRAM_BASE, TANDY2K_CHRAM_SIZE,
                    chram_read, chram_readw, NULL,
                    chram_write, chram_writew, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, vid->chram);

    if (vid->board != TANDY2K_BOARD_NONE)
        mem_mapping_add(&vid->vram_map, TANDY2K_VRAM_BASE, TANDY2K_FB_WINDOW,
                        vram_read, vram_readw, NULL,
                        vram_write, vram_writew, NULL,
                        NULL, MEM_MAPPING_EXTERNAL, vid);

    io_sethandler(0x0100, 0x0040, vpac_read, NULL, NULL, vpac_write, NULL, NULL, vid);
    io_sethandler(0x0180, 0x0020, attr_read, NULL, NULL, attr_write, NULL, NULL, vid);
    io_sethandler(0x01a0, 0x0001, hires_read, NULL, NULL, hires_write, NULL, NULL, vid);

    vid->frame_time = (uint64_t) ((cpuclock * (double) (1ULL << 32)) / TANDY2K_FRAME_HZ);

    timer_add(&vid->timer, vid_poll, vid, 0);
    timer_set_delay_u64(&vid->timer, vid->frame_time);

    return vid;
}

static void
tandy2k_vid_close(void *priv)
{
    t2kvid_t *vid = (t2kvid_t *) priv;

    free(vid);
}

static void
tandy2k_vid_speed_changed(void *priv)
{
    t2kvid_t *vid = (t2kvid_t *) priv;

    vid->frame_time = (uint64_t) ((cpuclock * (double) (1ULL << 32)) / TANDY2K_FRAME_HZ);
}

static const device_config_t tandy2k_video_config[] = {
    // clang-format off
    {
        .name           = "graphics",
        .description    = "Graphics Card",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = TANDY2K_BOARD_NONE,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "None",                  .value = TANDY2K_BOARD_NONE },
            { .description = "Graphics Adapter",      .value = TANDY2K_BOARD_MONO },
            { .description = "Color Graphics Option", .value = TANDY2K_BOARD_COLO },
            { .description = ""                                                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "",
        .description    = "",
        .type           = CONFIG_END
    }
    // clang-format on
};

const device_t tandy_2000_video_device = {
    .name          = "Tandy 2000 (Video)",
    .internal_name = "tandy2000_video",
    .flags         = 0,
    .local         = 0,
    .init          = tandy2k_vid_init,
    .close         = tandy2k_vid_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = tandy2k_vid_speed_changed,
    .force_redraw  = NULL,
    .config        = tandy2k_video_config
};
