/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tests of the Mach64 draw engine and the overlay.
 *
 *          The engine is included as C, for its private functions. The
 *          FIFO thread runs on the test's own thread.
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

static int       failures;
static mach64_t *fifo_owner;
static int       fifo_waits;

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

/* The FIFO thread's loop runs once: its second wait ends it. */
int
thread_wait_event(event_t *event, int timeout)
{
    (void) event;
    (void) timeout;

    if (fifo_owner && (++fifo_waits > 1))
        fifo_owner->thread_run = 0;
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
    mach64->type      = MACH64_GTB;
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
write_reg(mach64_t *mach64, uint32_t addr, uint32_t val)
{
    mach64_queue(mach64, addr, val, FIFO_WRITE_DWORD);
}

/* Runs what is queued, as the FIFO thread does. */
static void
fifo_run(mach64_t *mach64)
{
    fifo_owner         = mach64;
    fifo_waits         = 0;
    mach64->thread_run = 1;
    mach64_fifo_thread(mach64);
    fifo_owner = NULL;
}

static uint32_t
vram_read(const mach64_t *mach64, int bpp, uint32_t addr)
{
    uint32_t val = 0;

    for (int i = 0; i < (bpp >> 3); i++)
        val |= (uint32_t) mach64->svga.vram[(addr + i) & mach64->vram_mask] << (i << 3);
    return val;
}

static void
vram_write(mach64_t *mach64, int bpp, uint32_t addr, uint32_t val)
{
    for (int i = 0; i < (bpp >> 3); i++)
        mach64->svga.vram[(addr + i) & mach64->vram_mask] = val >> (i << 3);
}

/*
 * Color compare: a write is inhibited where the compare is true. Each case
 * is a transparent blit as the Rage II+ DirectDraw driver writes it, from a
 * 128-pixel pitch to the screen: 128x128 pixels to (100, 100) at 640x480,
 * and at 16 bpp 64x64 pixels to (728, 8) at 800x600. The blit has two
 * regions, the left and right halves or the center and the rest.
 */
typedef struct key_case_t {
    const char *name;
    int         bpp;
    int         halves;
    uint32_t    cmp_cntl; /* CLR_CMP_CNTL */
    uint32_t    cmp_msk;  /* CLR_CMP_MSK */
    uint32_t    cmp_clr;  /* CLR_CMP_CLR */
    uint32_t    src[2];   /* in each region */
    uint32_t    dst[2];
    uint32_t    result[2]; /* the destination afterwards */
} key_case_t;

static const key_case_t key_cases[] = {
    { "source equal",                  32, 0, 0x01000005, 0xffffffff, 0x00ff00ff, { 0x0000ff00, 0x00ff00ff }, { 0x000000ff, 0x000000ff }, { 0x0000ff00, 0x000000ff } },
    { "source equal, 8 bpp",           8,  0, 0x01000005, 0xffffffff, 0x05,       { 0x02, 0x05 },             { 0x04, 0x04 },             { 0x02, 0x04 }             },
    { "destination not equal, 8 bpp",  8,  0, 0x00000004, 0xffffffff, 0x00,       { 0x01, 0x01 },             { 0x00, 0x04 },             { 0x01, 0x04 }             },
    { "source equal, 16 bpp",          16, 0, 0x01000005, 0xffffffff, 0x7c1f,     { 0x03e0, 0x7c1f },         { 0x001f, 0x001f },         { 0x03e0, 0x001f }         },
    { "destination not equal, 16 bpp", 16, 0, 0x00000004, 0xffffffff, 0x03e0,     { 0x7c00, 0x7c00 },         { 0x03e0, 0x001f },         { 0x7c00, 0x001f }         },
    { "source not equal, masked",      32, 1, 0x01000004, 0x00ffffff, 0x000000ff, { 0xa50000ff, 0x00ff0000 }, { 0x000000ff, 0x000000ff }, { 0xa50000ff, 0x000000ff } },
    { "false",                         32, 1, 0x00000000, 0xffffffff, 0x000000ff, { 0x0000ff00, 0x0000ff00 }, { 0x000000ff, 0x00ff0000 }, { 0x0000ff00, 0x0000ff00 } },
    { "true",                          32, 1, 0x00000001, 0xffffffff, 0x000000ff, { 0x0000ff00, 0x0000ff00 }, { 0x000000ff, 0x00ff0000 }, { 0x000000ff, 0x00ff0000 } },
    { "destination equal",             32, 1, 0x00000005, 0xffffffff, 0x000000ff, { 0x0000ff00, 0x0000ff00 }, { 0x000000ff, 0x00ff0000 }, { 0x000000ff, 0x0000ff00 } },
    { "destination not equal, masked", 32, 1, 0x00000004, 0x00ffffff, 0x000000ff, { 0x0000ff00, 0x0000ff00 }, { 0xa50000ff, 0x00ff0000 }, { 0x0000ff00, 0x00ff0000 } }
};

typedef struct key_blit_t {
    int      size;
    int      screen_w;
    int      screen_h;
    int      x;
    int      y;
    uint32_t src; /* bytes */
} key_blit_t;

static const key_blit_t key_blit_640 = { 128, 640, 480, 100, 100, 0x12f200 };
static const key_blit_t key_blit_800 = { 64, 800, 600, 728, 8, 0x0ec540 };

static int
key_region(const key_case_t *c, int size, int x, int y)
{
    if (c->halves)
        return x >= (size / 2);
    return (x < (size / 4)) || (x >= (size * 3 / 4)) || (y < (size / 4)) || (y >= (size * 3 / 4));
}

static void
key_blit(mach64_t *mach64, const key_case_t *c, const key_blit_t *b)
{
    uint32_t pix_width = (c->bpp == 8) ? 0x00020202 : ((c->bpp == 16) ? 0x30030303 : 0x60060606);

    write_reg(mach64, 0x130, (c->bpp == 16) ? 0 : 3);           /* DST_CNTL */
    write_reg(mach64, 0x2c8, 0xffffffff);                       /* DP_WRITE_MASK */
    write_reg(mach64, 0x2d0, pix_width);                        /* DP_PIX_WIDTH */
    write_reg(mach64, 0x2a8, (b->screen_w - 1) << 16);          /* SC_LEFT_RIGHT */
    write_reg(mach64, 0x2b4, (b->screen_h - 1) << 16);          /* SC_TOP_BOTTOM */
    write_reg(mach64, 0x180, ((128 / 8) << 22) | (b->src / 8)); /* SRC_OFF_PITCH */
    write_reg(mach64, 0x100, (b->screen_w / 8) << 22);          /* DST_OFF_PITCH */
    write_reg(mach64, 0x198, (b->size << 16) | b->size);        /* SRC_HEIGHT1_WIDTH1 */
    write_reg(mach64, 0x2d8, 0x00000300);                       /* DP_SRC: the blit source */
    write_reg(mach64, 0x2d4, 0x00070003);                       /* DP_MIX: S, else D */
    write_reg(mach64, 0x308, c->cmp_cntl);
    write_reg(mach64, 0x304, c->cmp_msk);
    write_reg(mach64, 0x300, c->cmp_clr);
    if (c->bpp == 16)
        write_reg(mach64, 0x330, 0x00000003);            /* GUI_TRAJ_CNTL */
    write_reg(mach64, 0x18c, 0x00000000);                /* SRC_Y_X */
    write_reg(mach64, 0x10c, (b->x << 16) | b->y);       /* DST_Y_X */
    write_reg(mach64, 0x118, (b->size << 16) | b->size); /* DST_HEIGHT_WIDTH */
    fifo_run(mach64);
}

static void
key_tests(void)
{
    for (size_t i = 0; i < (sizeof(key_cases) / sizeof(key_cases[0])); i++) {
        const key_case_t *c      = &key_cases[i];
        const key_blit_t *b      = (c->bpp == 16) ? &key_blit_800 : &key_blit_640;
        mach64_t         *mach64 = card_create();
        int               bytes  = c->bpp >> 3;
        int               wrong  = 0;

        for (int y = 0; y < b->size; y++) {
            for (int x = 0; x < b->size; x++) {
                int r = key_region(c, b->size, x, y);

                vram_write(mach64, c->bpp, b->src + ((y * 128) + x) * bytes, c->src[r]);
                vram_write(mach64, c->bpp, (((b->y + y) * b->screen_w) + b->x + x) * bytes, c->dst[r]);
            }
        }

        key_blit(mach64, c, b);

        for (int y = 0; y < b->size; y++) {
            for (int x = 0; x < b->size; x++) {
                int      r   = key_region(c, b->size, x, y);
                uint32_t src = vram_read(mach64, c->bpp, b->src + ((y * 128) + x) * bytes);
                uint32_t dst = vram_read(mach64, c->bpp, (((b->y + y) * b->screen_w) + b->x + x) * bytes);

                if (((src != c->src[r]) || (dst != c->result[r])) && !wrong++)
                    fprintf(stderr, "Color compare, %s: at %d,%d source %08x, destination %08x, not %08x\n",
                            c->name, x, y, src, dst, c->result[r]);
            }
        }
        CHECK(!wrong, "color compare, %s: %d pixels wrong", c->name, wrong);
        CHECK(!mach64->accel.busy && (mach64->fifo_read_idx == mach64->fifo_write_idx),
              "color compare, %s: the engine did not finish", c->name);
        card_close(mach64);
    }
}

/*
 * DP_SET_GUI_ENGINE (2FCh) sets the engine up in one write; it also turns
 * the color compare off. The Rage II+ Windows 95 driver writes it before
 * each solid fill and then only the color and the rectangle, so the
 * scissors it opens (SC_LEFT_RIGHT 1FFF0000h, "OPEN completely" in
 * RRG-G03300 5-54) must not clip the fill. An SC_RIGHT of 3FFFh, -1 in the GT-B's 14
 * bits, written afterwards still clips all of it. Returns the pixels the
 * 4x2 fill drew.
 */
static int
gui_engine_fill(uint32_t sc_left_right)
{
    mach64_t *mach64 = card_create();
    int       drawn  = 0;

    write_reg(mach64, 0x2fc, 0x0010a070); /* 32 bpp, 1024 pixels a line, a color fill */
    if (sc_left_right)
        write_reg(mach64, 0x2a8, sc_left_right);
    write_reg(mach64, 0x2c4, 0x00c0c0c0); /* DP_FRGD_CLR */
    write_reg(mach64, 0x10c, (16 << 16) | 8);
    write_reg(mach64, 0x118, (4 << 16) | 2);
    fifo_run(mach64);

    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 4; x++) {
            if (vram_read(mach64, 32, (((8 + y) * 1024) + 16 + x) * 4) == 0x00c0c0c0)
                drawn++;
        }
    }
    card_close(mach64);
    return drawn;
}

static void
gui_engine_tests(void)
{
    mach64_t *mach64 = card_create();
    int       drawn;

    /* The source-equal transparent blit of ATI's TBLIT sample. */
    mach64->clr_cmp_cntl = 0x01000005;
    mach64->clr_cmp_clr  = 0x00007c1f;
    mach64->clr_cmp_mask = 0xffffffff;
    write_reg(mach64, 0x2fc, 0x00000000);
    fifo_run(mach64);
    CHECK(mach64->fifo_read_idx == mach64->fifo_write_idx, "DP_SET_GUI_ENGINE stayed in the FIFO");
    CHECK(mach64->clr_cmp_cntl == 0, "DP_SET_GUI_ENGINE left CLR_CMP_CNTL at %08x", mach64->clr_cmp_cntl);
    card_close(mach64);

    drawn = gui_engine_fill(0);
    CHECK(drawn == 8, "a fill after DP_SET_GUI_ENGINE drew %d of 8 pixels", drawn);
    drawn = gui_engine_fill(0x3fff0000);
    CHECK(drawn == 0, "a fill with SC_RIGHT 3FFFh drew %d pixels", drawn);
}

/*
 * The overlay: a source of two lines of four pixels, drawn eight pixels wide
 * on four lines with the video everywhere. That is mixer function 2, or on
 * the GT-B, whose mixer has only functions 0 and Ch, both keys true and
 * function Ch. What follows the source in memory must never reach the
 * screen.
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
    mach64->overlay_key_cntl             = (type == MACH64_GTB) ? 0x111 : 0x200;
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
        { MACH64_VT2, "VT2" },
        { MACH64_GTB, "GT-B" }
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
    key_tests();
    gui_engine_tests();
    overlay_tests();

    if (failures) {
        fprintf(stderr, "Mach64 draw engine: %d checks failed\n", failures);
        return 1;
    }
    printf("Mach64 draw engine: all checks passed\n");
    return 0;
}
