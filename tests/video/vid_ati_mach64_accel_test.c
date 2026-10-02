/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tests of the Mach64 overlay.
 *
 *          The engine is included as C, for its private functions.
 *
 * Authors: Avastrap2, <https://github.com/Avastrap2>
 *
 *          Copyright 2026 Avastrap2.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/video/vid_ati_mach64_accel.c"

#define VRAM_SIZE (4 << 20)

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                   \
            fputc('\n', stderr);                            \
            failures++;                                     \
        }                                                   \
    } while (0)

/* As in vid_ati_mach64.c. */
int mach64_width[8] = { WIDTH_1BIT, WIDTH_4BIT, 0, 1, 1, 2, 2, 0 };

monitor_t   monitors[MONITORS_NUM];
int         monitor_index_global;

static int failures;

void
thread_set_event(event_t *event)
{
    (void) event;
}

void
thread_reset_event(event_t *event)
{
    (void) event;
}

int
thread_wait_event(event_t *event, int timeout)
{
    (void) event;
    (void) timeout;
    return 0;
}

int
thread_wait_mutex(mutex_t *mutex)
{
    (void) mutex;
    return 1;
}

int
thread_release_mutex(mutex_t *mutex)
{
    (void) mutex;
    return 1;
}

uint64_t
plat_timer_read(void)
{
    return 0;
}

void
pclog(const char *format, ...)
{
    (void) format;
}

static mach64_t *
card_create(void)
{
    mach64_t *mach64 = calloc(1, sizeof(mach64_t));

    if (mach64) {
        mach64->svga.vram        = calloc(1, VRAM_SIZE);
        mach64->svga.changedvram = calloc(VRAM_SIZE >> 12, 1);
        mach64->svga.monitor     = calloc(1, sizeof(monitor_t));
    }
    if (!mach64 || !mach64->svga.vram || !mach64->svga.changedvram || !mach64->svga.monitor) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    mach64->svga.priv = mach64;
    mach64->vram_size = VRAM_SIZE;
    mach64->vram_mask = VRAM_SIZE - 1;
    mach64->type      = MACH64_VT2;
    return mach64;
}

static void
card_close(mach64_t *mach64)
{
    free(mach64->svga.monitor);
    free(mach64->svga.changedvram);
    free(mach64->svga.vram);
    free(mach64);
}

static void
vram_write(mach64_t *mach64, int bpp, uint32_t addr, uint32_t val)
{
    for (int i = 0; i < (bpp >> 3); i++)
        mach64->svga.vram[(addr + i) & mach64->vram_mask] = val >> (i << 3);
}

/*
 * The overlay: a source of two lines of four pixels, drawn eight pixels wide
 * on four lines with the video everywhere, mixer function 2. What follows
 * the source in memory must never reach the screen.
 */
#define OVERLAY_SRC_W   4
#define OVERLAY_SRC_H   2
#define OVERLAY_PITCH   8 /* pixels */
#define OVERLAY_W       8
#define OVERLAY_H       4

#define SCALE_REPLICATE (SCALE_HORZ_MODE | SCALE_VERT_MODE)

static uint32_t overlay_screen[OVERLAY_H][OVERLAY_W];

/* ARGB8888, eight dwords a line. */
static const uint32_t overlay_rgb[(OVERLAY_SRC_H + 1) * OVERLAY_PITCH] = {
    0x100000, 0x200000, 0x300000, 0x400000, 0xbad001, 0xbad002, 0xbad003, 0xbad004,
    0x000100, 0x000200, 0x000300, 0x000400, 0xbad005, 0xbad006, 0xbad007, 0xbad008,
    0xbad009, 0xbad00a, 0xbad00b, 0xbad00c, 0xbad00d, 0xbad00e, 0xbad00f, 0xbad010
};

/* VYUY422, a pair (Y, U, Y, V) a dword and four dwords a line: grey, with
   luma 20h, 60h, A0h and E0h, then 10h more. */
static const uint32_t overlay_grey[(OVERLAY_SRC_H + 1) * (OVERLAY_PITCH / 2)] = {
    0x80608020, 0x80e080a0, 0x0000ff00, 0x00ff00ff,
    0x80708030, 0x80f080b0, 0x0000ff00, 0x00ff00ff,
    0x0000ff00, 0x00ff00ff, 0x0000ff00, 0x00ff00ff
};

/* Luma 80h, U 80h in the first pair and C0h in the second. */
static const uint32_t overlay_blue[(OVERLAY_SRC_H + 1) * (OVERLAY_PITCH / 2)] = {
    0x80808080, 0x8080c080, 0x0000ff00, 0x00ff00ff,
    0x80808080, 0x8080c080, 0x0000ff00, 0x00ff00ff,
    0x0000ff00, 0x00ff00ff, 0x0000ff00, 0x00ff00ff
};

static void
overlay_draw(int type, int format, const uint32_t *source, int dwords, int ecp_div, uint32_t scale_cntl, uint32_t scale_inc)
{
    mach64_t *mach64 = card_create();
    bitmap_t *bitmap = calloc(1, sizeof(bitmap_t));

    if (!bitmap) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    for (int i = 0; i < dwords; i++)
        vram_write(mach64, 32, i * 4, source[i]);
    for (int y = 0; y < OVERLAY_H; y++)
        bitmap->line[y] = overlay_screen[y];
    monitors[0].target_buffer = bitmap;
    monitor_index_global      = 0;

    mach64->type                         = type;
    mach64->svga.overlay.pitch           = OVERLAY_PITCH;
    mach64->svga.overlay_latch.cur_xsize = OVERLAY_W;
    mach64->scaler_format                = format;
    mach64->scaler_height_width          = (OVERLAY_SRC_W << 16) | OVERLAY_SRC_H;
    mach64->overlay_scale_cntl           = scale_cntl;
    mach64->overlay_scale_inc            = scale_inc;
    mach64->overlay_key_cntl             = 0x200;
    mach64->pll_regs[5]                  = ecp_div << 4;
    mach64->scaler_update                = 1;

    for (int y = 0; y < OVERLAY_H; y++)
        mach64_overlay_draw(&mach64->svga, y);

    monitors[0].target_buffer = NULL;
    free(bitmap);
    card_close(mach64);
}

static void
overlay_draw_rgb(int type, int ecp_div, uint32_t scale_inc)
{
    overlay_draw(type, 6, overlay_rgb, sizeof(overlay_rgb) / 4, ecp_div, 0, scale_inc);
}

static void
overlay_draw_yuv(int type, const uint32_t *source, uint32_t scale_cntl, uint32_t scale_inc)
{
    overlay_draw(type, 0xb, source, sizeof(overlay_grey) / 4, 0, scale_cntl, scale_inc);
}

static void
overlay_check(const char *card, const char *name, int y, const uint32_t expect[OVERLAY_W])
{
    int x = 0;

    while ((x < OVERLAY_W) && (overlay_screen[y][x] == expect[x]))
        x++;
    CHECK(x == OVERLAY_W, "%s overlay, %s: line %d pixel %d is %06x, not %06x", card, name, y, x, overlay_screen[y][x],
          expect[x]);
}

static void
overlay_tests(void)
{
    static const struct {
        int         type;
        const char *name;
    } cards[] = {
        { MACH64_VT2, "VT2" }
    };
    static const uint32_t pairs[OVERLAY_W] = { 0x100000, 0x100000, 0x200000, 0x200000, 0x300000, 0x300000, 0x400000, 0x400000 };
    static const uint32_t quads[OVERLAY_W] = { 0x100000, 0x100000, 0x100000, 0x100000, 0x300000, 0x300000, 0x300000, 0x300000 };
    static const uint32_t edge[OVERLAY_W]  = { 0x100000, 0x200000, 0x300000, 0x400000, 0x400000, 0x400000, 0x400000, 0x400000 };
    static const uint32_t last[OVERLAY_W]  = { 0x000100, 0x000200, 0x000300, 0x000400, 0x000400, 0x000400, 0x000400, 0x000400 };
    /* The grey source at 2x: its lines 0 and 1, the line halfway between,
       and the three with pixels replicated. */
    static const uint32_t grey_0[OVERLAY_W]     = { 0x202020, 0x404040, 0x606060, 0x808080, 0xa0a0a0, 0xc0c0c0, 0xe0e0e0, 0xe0e0e0 };
    static const uint32_t grey_1[OVERLAY_W]     = { 0x303030, 0x505050, 0x707070, 0x909090, 0xb0b0b0, 0xd0d0d0, 0xf0f0f0, 0xf0f0f0 };
    static const uint32_t grey_half[OVERLAY_W]  = { 0x282828, 0x484848, 0x686868, 0x888888, 0xa8a8a8, 0xc8c8c8, 0xe8e8e8, 0xe8e8e8 };
    static const uint32_t grey_0_rep[OVERLAY_W] = { 0x202020, 0x202020, 0x606060, 0x606060, 0xa0a0a0, 0xa0a0a0, 0xe0e0e0, 0xe0e0e0 };
    static const uint32_t grey_1_rep[OVERLAY_W] = { 0x303030, 0x303030, 0x707070, 0x707070, 0xb0b0b0, 0xb0b0b0, 0xf0f0f0, 0xf0f0f0 };
    static const uint32_t half_rep[OVERLAY_W]   = { 0x282828, 0x282828, 0x686868, 0x686868, 0xa8a8a8, 0xa8a8a8, 0xe8e8e8, 0xe8e8e8 };
    /* Halved: pixels 0 and 1 blended 50-50, then pixel 2 and the last. */
    static const uint32_t halved[OVERLAY_W] = { 0x404040, 0xa0a0a0, 0xe0e0e0, 0xe0e0e0, 0xe0e0e0, 0xe0e0e0, 0xe0e0e0, 0xe0e0e0 };
    /* U at half the rate of Y: 80h, 90h, A0h, B0h, then C0h. */
    static const uint32_t blue[OVERLAY_W] = { 0x808080, 0x807b9c, 0x8075b8, 0x8070d4, 0x806af1, 0x806af1, 0x806af1, 0x806af1 };

    for (size_t i = 0; i < (sizeof(cards) / sizeof(cards[0])); i++) {
        int         type = cards[i].type;
        const char *card = cards[i].name;

        /* A 2x zoom with ECP at VCLK / 2: the driver doubles HORZ_INC to
           1.0, and each step covers two pixels, as the Rage II+ Windows 95
           driver has it at 1280x960. RGB is replicated, blends or not. */
        overlay_draw_rgb(type, 1, 0x10000800);
        overlay_check(card, "ECP at VCLK / 2", 0, pairs);

        /* ECP at VCLK / 4: HORZ_INC four times as large, a step every four
           pixels. */
        overlay_draw_rgb(type, 2, 0x20000800);
        overlay_check(card, "ECP at VCLK / 4", 0, quads);

        /* Past the last pixel and line of the source, they are repeated. */
        overlay_draw_rgb(type, 0, 0x10001000);
        overlay_check(card, "last pixel", 0, edge);
        overlay_check(card, "last line", 2, last);
        overlay_check(card, "last line", 3, last);

        /* YUV at 2x, blended both ways. */
        overlay_draw_yuv(type, overlay_grey, 0, 0x08000800);
        overlay_check(card, "blends", 0, grey_0);
        overlay_check(card, "blends", 1, grey_half);
        overlay_check(card, "blends", 2, grey_1);
        overlay_check(card, "blends", 3, grey_1);

        /* Replicated both ways, then one way each. */
        overlay_draw_yuv(type, overlay_grey, SCALE_REPLICATE, 0x08000800);
        overlay_check(card, "replication", 0, grey_0_rep);
        overlay_check(card, "replication", 1, grey_0_rep);
        overlay_check(card, "replication", 2, grey_1_rep);
        overlay_draw_yuv(type, overlay_grey, SCALE_HORZ_MODE, 0x08000800);
        overlay_check(card, "pixel replication", 1, half_rep);
        overlay_draw_yuv(type, overlay_grey, SCALE_VERT_MODE, 0x08000800);
        overlay_check(card, "line replication", 1, grey_0);

        /* A step of two pixels takes the 50-50 blend. */
        overlay_draw_yuv(type, overlay_grey, 0, 0x20001000);
        overlay_check(card, "2:1", 0, halved);

        overlay_draw_yuv(type, overlay_blue, 0, 0x08000800);
        overlay_check(card, "U and V", 0, blue);
    }
}

int
main(void)
{
    overlay_tests();

    if (failures) {
        fprintf(stderr, "Mach64 overlay: %d checks failed\n", failures);
        return 1;
    }
    printf("Mach64 overlay: all checks passed\n");
    return 0;
}
