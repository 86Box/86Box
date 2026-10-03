/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tests of the Mach64 draw engine, the overlay and the 3D Rage
 *          II+'s engine timing.
 *
 *          The engine is included as C, for its private functions. The
 *          FIFO thread runs on the test's own thread, and a 1 GHz CPU
 *          clock makes a time stamp tick a nanosecond.
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
cpu_state_t cpu_state;
uint64_t    tsc;
double      cpuclock      = 1000000000.0;
int         cpu_pci_speed = 33333333;
int         cpu_use_dynarec;

static int       failures;
static mach64_t *fifo_owner;
static int       fifo_waits;

void
update_tsc(void)
{
}

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

/* No 3D engine: every register belongs to the draw engine. */
int
mach64_3d_write(mach64_t *mach64, uint32_t addr, uint32_t val, uint32_t type)
{
    (void) mach64;
    (void) addr;
    (void) val;
    (void) type;
    return 0;
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
    mach64_timing_close(mach64->timing);
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

/* A 24x16 color fill at the origin, set up as ATI's 5.24 driver does it. */
static void
composite_xor_fill(mach64_t *mach64, uint32_t frgd_clr_mix)
{
    write_reg(mach64, 0x2fc, 0x0010a058); /* DP_SET_GUI_ENGINE: 15 bpp, 1024 pixels a line, a color fill */
    write_reg(mach64, 0x330, 0x00000023); /* GUI_TRAJ_CNTL */
    write_reg(mach64, 0x2dc, frgd_clr_mix);
    write_reg(mach64, 0x10c, 0x00000000);
    write_reg(mach64, 0x118, (24 << 16) | 16);
    fifo_run(mach64);
}

/*
 * DP_FRGD_CLR_MIX (2DCh) and DP_FRGD_BKGD_CLR (2E0h) load colors and mixes
 * in one write (RRG-G03300 5-44, 5-45). ATI's 5.24 Windows 95 driver draws
 * disabled text with them at 15 bpp: an XOR fill with the color, an AND with
 * the text's mask from the host, and the XOR fill again, so the color lands
 * only where the mask is clear. These are its writes for 24x16 pixels of the
 * white part of disabled text over gray; with the registers ignored, the
 * fills painted all of it white (issue 8181). The mask here is clear on the
 * odd lines, so the result does not depend on the order of its bits.
 */
static void
composite_tests(void)
{
    const uint32_t face   = 0x5ef7;
    const uint32_t white  = 0x7fff;
    mach64_t      *mach64 = card_create();
    int            wrong  = 0;

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 32; x++)
            vram_write(mach64, 16, ((y * 1024) + x) * 2, face);
    }

    composite_xor_fill(mach64, 0x03057fff); /* white; XOR, else D */
    CHECK(mach64->dp_mix == 0x00050003, "DP_FRGD_CLR_MIX left DP_MIX at %08x", mach64->dp_mix);
    CHECK((mach64->dp_frgd_clr & 0xffff) == white, "DP_FRGD_CLR_MIX left DP_FRGD_CLR at %08x", mach64->dp_frgd_clr);

    write_reg(mach64, 0x2fc, 0x00312018); /* 15 bpp, a monochrome expansion from the host */
    write_reg(mach64, 0x330, 0x00000003);
    write_reg(mach64, 0x2d4, 0x000c000c); /* DP_MIX: D AND S */
    write_reg(mach64, 0x2a8, 23 << 16);   /* SC_LEFT_RIGHT */
    write_reg(mach64, 0x2c4, 0x40ffffff); /* DP_FRGD_CLR */
    write_reg(mach64, 0x2c0, 0x40000000); /* DP_BKGD_CLR */
    write_reg(mach64, 0x10c, 0x00000000);
    write_reg(mach64, 0x118, (32 << 16) | 16);
    for (int y = 0; y < 16; y++)
        write_reg(mach64, 0x200, (y & 1) ? 0x00000000 : 0xffffffff);
    fifo_run(mach64);

    composite_xor_fill(mach64, 0x03057fff);

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 32; x++) {
            uint32_t expect = ((x < 24) && (y & 1)) ? white : face;
            uint32_t got    = vram_read(mach64, 16, ((y * 1024) + x) * 2);

            if ((got != expect) && !wrong++)
                fprintf(stderr, "Disabled text: at %d,%d %04x, not %04x\n", x, y, got, expect);
        }
    }
    CHECK(!wrong, "disabled text: %d pixels wrong", wrong);

    /* DP_FRGD_BKGD_CLR: an expansion in blue on red. */
    write_reg(mach64, 0x2fc, 0x00312018);
    write_reg(mach64, 0x330, 0x00000003);
    write_reg(mach64, 0x2e0, 0x7c00001f);
    write_reg(mach64, 0x10c, 20);
    write_reg(mach64, 0x118, (32 << 16) | 2);
    write_reg(mach64, 0x200, 0xffffffff);
    write_reg(mach64, 0x200, 0x00000000);
    fifo_run(mach64);
    CHECK(((mach64->dp_frgd_clr & 0xffff) == 0x001f) && ((mach64->dp_bkgd_clr & 0xffff) == 0x7c00),
          "DP_FRGD_BKGD_CLR left the colors at %08x and %08x", mach64->dp_frgd_clr, mach64->dp_bkgd_clr);
    wrong = 0;
    for (int x = 0; x < 32; x++) {
        if (vram_read(mach64, 16, ((20 * 1024) + x) * 2) != 0x001f)
            wrong++;
        if (vram_read(mach64, 16, ((21 * 1024) + x) * 2) != 0x7c00)
            wrong++;
    }
    CHECK(!wrong, "DP_FRGD_BKGD_CLR: %d pixels wrong", wrong);
    card_close(mach64);
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

/*
 * The 3D Rage II+'s engine timing.
 */
static int
near(double a, double b, double tolerance)
{
    return ((a - b) <= tolerance) && ((b - a) <= tolerance);
}

static void
timing_push(mach64_timing_t *timing, uint64_t arrival, int queued, int fifo_idx, uint64_t cost)
{
    mach64_timing_entry_t *e = &timing->ring[timing->tail++ & TIMING_RING_MASK];

    e->arrival  = arrival;
    e->start    = 0;
    e->cost     = cost;
    e->fifo_idx = fifo_idx;
    e->queued   = queued;
}

/* The FIFO_STAT and GUI_STAT view at the given time. */
static uint32_t
timing_used(mach64_t *mach64, uint64_t now, int *busy)
{
    uint32_t used = 0;

    tsc   = now;
    *busy = 0;
    mach64_timing_status(mach64, &used, busy);
    return used;
}

static void
timing_fifo_tests(void)
{
    mach64_t        *mach64 = card_create();
    mach64_timing_t *timing = mach64->timing = mach64_timing_init();
    uint32_t         used;
    int              busy;

    /* The engine takes the first entry at once; the others wait for its
       10 ticks. */
    timing_push(timing, 0, 0, 0, 10);
    timing_push(timing, 0, 0, 0, 0);
    timing_push(timing, 0, 0, 0, 100);
    mach64_timing_fold(mach64);
    CHECK((timing->fold == 3) && (timing->free_at == 110), "fold %u, free at %llu", timing->fold,
          (unsigned long long) timing->free_at);
    used = timing_used(mach64, 5, &busy);
    CHECK((used == 2) && busy, "at 5: %u used, busy %d", used, busy);
    used = timing_used(mach64, 10, &busy);
    CHECK(used == 0, "at 10: %u used", used);
    timing_used(mach64, 109, &busy);
    CHECK(busy, "the engine is idle at 109");
    timing_used(mach64, 110, &busy);
    CHECK(!busy, "the engine is busy at 110");

    /* An entry written to an idle engine starts when it arrives. */
    timing_push(timing, 500, 0, 0, 20);
    mach64_timing_fold(mach64);
    CHECK((timing->ring[3].start == 500) && (timing->free_at == 520), "start %llu, free at %llu",
          (unsigned long long) timing->ring[3].start, (unsigned long long) timing->free_at);
    card_close(mach64);

    /* A draw engine FIFO entry's cost is known once the FIFO thread has run
       it; until then it holds the entries behind it. */
    mach64 = card_create();
    timing = mach64->timing = mach64_timing_init();
    timing_push(timing, 0, 1, 3, 0);
    timing_push(timing, 0, 0, 0, 5);
    used = timing_used(mach64, 1000, &busy);
    CHECK((timing->fold == 0) && (used == 2), "fold %u, %u used before the entry ran", timing->fold, used);
    timing->fifo_ns[3]    = 21.0;
    mach64->fifo_read_idx = mach64->fifo_write_idx = 4;
    mach64_timing_fold(mach64);
    CHECK((timing->fold == 2) && (timing->free_at == 26), "fold %u, free at %llu once it ran", timing->fold,
          (unsigned long long) timing->free_at);
    card_close(mach64);

    /* A write to a full FIFO waits for the engine to take an entry. */
    mach64 = card_create();
    timing = mach64->timing = mach64_timing_init();
    for (int i = 0; i <= TIMING_FIFO_DEPTH; i++)
        timing_push(timing, 0, 0, 0, 100);
    tsc    = 0;
    cycles = 0;
    mach64_timing_before_write(mach64);
    CHECK((timing->floor == 100) && (cycles == -100) && ((timing->tail - timing->head) == (TIMING_FIFO_DEPTH - 1)),
          "a full FIFO waited until %llu, %d cycles, %u used", (unsigned long long) timing->floor, cycles,
          timing->tail - timing->head);
    card_close(mach64);

    /* Entries that a reset of the draw engine FIFO took away are dropped. */
    mach64 = card_create();
    timing = mach64->timing = mach64_timing_init();
    for (int i = 0; i < TIMING_FIFO_DEPTH; i++)
        timing_push(timing, 0, 1, 100 + i, 0);
    mach64_timing_before_write(mach64);
    CHECK(timing->tail == timing->head, "%u lost entries kept", timing->tail - timing->head);
    card_close(mach64);
}

static void
timing_clock_tests(void)
{
    uint8_t                pll[64] = { 0 };
    mach64_timing_clocks_t c;
    double                 pll_mclk;
    mach64_t              *mach64;

    pll[2]   = 0x21; /* PLL_REF_DIV */
    pll[3]   = 0x10; /* MCLK_SRC_SEL: PLLMCLK / 2 */
    pll[4]   = 0x96; /* MCLK_FB_DIV */
    pll[11]  = 0x00; /* XCLK = MCLK = the source, 2 * MCLK_FB_DIV */
    c        = mach64_timing_pll_clocks(pll, 33333333.0);
    pll_mclk = PLL_REF_HZ * 2.0 * 0x96 / 0x21;
    CHECK(near(c.mclk, pll_mclk / 2.0, 1.0) && near(c.xclk, pll_mclk / 2.0, 1.0), "MCLK %.0f, XCLK %.0f", c.mclk, c.xclk);

    pll[11] = 0x04; /* MFB_TIMES_4_2B */
    pll[3]  = 0x20; /* PLLMCLK / 4 */
    c       = mach64_timing_pll_clocks(pll, 33333333.0);
    CHECK(near(c.mclk, pll_mclk / 2.0, 1.0), "MFB_TIMES_4_2B: MCLK %.0f", c.mclk);

    pll[11] = 0x06; /* XCLK_MCLK_RATIO 2: XCLK the source / 2, MCLK / 3 */
    c       = mach64_timing_pll_clocks(pll, 33333333.0);
    CHECK(near(c.xclk, pll_mclk / 4.0, 1.0) && near(c.mclk, pll_mclk / 6.0, 1.0), "ratio 2: XCLK %.0f, MCLK %.0f", c.xclk,
          c.mclk);

    /* An unprogrammed PLL is taken as 60 MHz. */
    memset(pll, 0, sizeof(pll));
    c = mach64_timing_pll_clocks(pll, 33333333.0);
    CHECK((c.mclk == 60000000.0) && (c.xclk == 60000000.0), "unprogrammed: MCLK %.0f, XCLK %.0f", c.mclk, c.xclk);

    /* 640x480 at 16 bpp and 25.175 MHz: 80 of 100 characters, 480 of 525
       lines, two bytes a pixel of the 8 a memory cycle of 60 MHz moves. */
    mach64                    = card_create();
    mach64->crtc_gen_cntl     = 1 << 24;
    mach64->crtc_h_total_disp = (79 << 16) | 99;
    mach64->crtc_v_total_disp = (479 << 16) | 524;
    mach64->pll_freq[0]       = 25175000.0;
    mach64->svga.bpp          = 16;
    c                         = mach64_timing_measure(mach64);
    CHECK(near(c.crtc_fraction, 25175000.0 * 2.0 * (80.0 * 480.0) / (100.0 * 525.0) / (60000000.0 * 8.0), 1e-9),
          "the display takes %.4f of the memory", c.crtc_fraction);
    card_close(mach64);
}

static void
timing_cost_tests(void)
{
    mach64_timing_clocks_t c = { 60000000.0, 60000000.0, 0.0 };
    mach64_timing_rect_t   r = { 0 };
    mach64_3d_work_t       t = { 0 };
    mach64_timing_work_t   w;
    mach64_timing_work_t   flat;
    mach64_t              *mach64;
    double                 take_ns;
    double                 fill_ns;
    uint64_t               expect;
    uint32_t               used;
    int                    busy;

    /* A 640x480 16 bpp fill with a 1280-byte pitch: 76,800 qwords of one
       cycle each, and 150 pages of 4 KiB opened once each (ATI 7.9.7). */
    r.width     = 640;
    r.height    = 480;
    r.dst_pitch = 1280;
    r.dst_bits  = 16;
    w           = mach64_timing_rect_work(&r);
    CHECK(near(w.memory_cycles, 76800.0 + 6.0 * 150.0, 0.5), "fill: %.1f memory cycles", w.memory_cycles);
    CHECK(near(mach64_timing_seconds(&w, &c), 77700.0 / 60000000.0, 1e-9), "fill: %.9f s", mach64_timing_seconds(&w, &c));

    /* The display's share of the memory slows memory-bound work. */
    c.crtc_fraction = 0.25;
    CHECK(near(mach64_timing_seconds(&w, &c), 77700.0 / 45000000.0, 1e-9), "fill with the display: %.9f s",
          mach64_timing_seconds(&w, &c));
    c.crtc_fraction = 0.0;

    /* An XOR blit reads the source and the destination and writes the
       destination: three accesses a qword in ATI's example. */
    r.width     = 160;
    r.height    = 120;
    r.dst_pitch = 1024;
    r.dst_bits  = 8;
    r.src_bits  = 8;
    r.dst_read  = 1;
    w           = mach64_timing_rect_work(&r);
    CHECK((w.memory_cycles > (120.0 * 20.0 * 3.0)) && (w.memory_cycles < (120.0 * 20.0 * 3.25 * 1.3)),
          "XOR blit: %.1f memory cycles", w.memory_cycles);

    /* An unaligned edge costs a read. */
    memset(&r, 0, sizeof(r));
    r.width     = 10;
    r.height    = 1;
    r.x         = 1;
    r.dst_pitch = 4096;
    r.dst_bits  = 16;
    w           = mach64_timing_rect_work(&r);
    CHECK(near(w.memory_cycles, 3.0 + 2.0 + 6.0, 0.5), "unaligned: %.1f memory cycles", w.memory_cycles);

    /* Nothing to draw: the set-up only. */
    r.width = 0;
    w       = mach64_timing_rect_work(&r);
    CHECK((w.memory_cycles == 0.0) && (w.engine_clocks > 0.0), "empty: %.1f memory cycles", w.memory_cycles);

    /* A flat 16 bpp draw without Z is held by the engine, a pixel a clock;
       a bilinear one with Z by the memory. */
    t.pixels   = 1000;
    t.rows     = 10;
    t.dst_bits = 16;
    flat       = mach64_timing_3d_work(&t);
    CHECK(flat.engine_clocks > flat.memory_cycles, "flat: %.1f engine clocks, %.1f memory cycles", flat.engine_clocks,
          flat.memory_cycles);
    t.z_read   = 1;
    t.z_write  = 1;
    t.tex_bits = 16;
    t.texels   = 4;
    w          = mach64_timing_3d_work(&t);
    CHECK(w.memory_cycles > w.engine_clocks, "bilinear with Z: %.1f engine clocks, %.1f memory cycles", w.engine_clocks,
          w.memory_cycles);
    CHECK(mach64_timing_seconds(&w, &c) > (mach64_timing_seconds(&flat, &c) * 1.5), "bilinear with Z is not slower");

    /* A fill through the FIFO: its four writes take an XCLK each at the 60
       MHz of an unprogrammed PLL, and the 256x128 fill on a 4096-byte pitch
       128 qwords a line and a page a line. */
    mach64         = card_create();
    mach64->timing = mach64_timing_init();
    tsc            = 0;
    write_reg(mach64, 0x2fc, 0x0010a070); /* 32 bpp, 1024 pixels a line, a color fill */
    write_reg(mach64, 0x2c4, 0x00c0c0c0);
    write_reg(mach64, 0x10c, 0x00000000);
    write_reg(mach64, 0x118, (256 << 16) | 128);
    fifo_run(mach64);
    take_ns = 1000000000.0 / 60000000.0;
    fill_ns = ((128.0 * 128.0) + (6.0 * 128.0)) * take_ns;
    expect  = (3 * mach64_timing_ticks(take_ns)) + mach64_timing_ticks(take_ns + fill_ns);
    used    = timing_used(mach64, 0, &busy);
    CHECK((used == 3) && busy && (mach64->timing->free_at == expect), "fill: %u used, busy %d, free at %llu, not %llu",
          used, busy, (unsigned long long) mach64->timing->free_at, (unsigned long long) expect);
    timing_used(mach64, expect, &busy);
    CHECK(!busy, "fill: busy at %llu", (unsigned long long) expect);
    card_close(mach64);
}

/*
 * An engine reset (GEN_GUI_EN to 0) runs the writes queued before it and
 * ends the operation in progress. ATI's Windows NT 3.5 driver for the 3D
 * Rage II+ sets the scissors to its 800x600 screen, opens them over all of
 * video memory and resets the engine at once, the opening still queued;
 * then it caches bitmaps from line 627 down. With the opening dropped,
 * those were clipped away (issue 8188). These are its writes at 8 bpp.
 */
static void
engine_reset_tests(void)
{
    mach64_t        *mach64 = card_create();
    mach64_timing_t *timing;
    uint32_t         used;
    int              busy;
    int              wrong = 0;

    write_reg(mach64, 0x100, 0x1a000000); /* DST_OFF_PITCH: 832 pixels a line */
    write_reg(mach64, 0x130, 0x00000003); /* DST_CNTL */
    write_reg(mach64, 0x2c8, 0xffffffff); /* DP_WRITE_MASK */
    write_reg(mach64, 0x2d0, 0x00020202); /* DP_PIX_WIDTH: 8 bpp */
    write_reg(mach64, 0x2d8, 0x00000100); /* DP_SRC: DP_FRGD_CLR */
    write_reg(mach64, 0x2d4, 0x00070003); /* DP_MIX: S, else D */
    write_reg(mach64, 0x2a8, 0x03200000); /* SC_LEFT_RIGHT: the screen */
    write_reg(mach64, 0x2b4, 0x02580000); /* SC_TOP_BOTTOM */
    fifo_run(mach64);

    write_reg(mach64, 0x2a8, 0x0fff0000);
    write_reg(mach64, 0x2b4, 0x13b10000); /* down to line 5041, the end of video memory */
    mach64_reset_engine(mach64);

    write_reg(mach64, 0x2c4, 0x00000007); /* DP_FRGD_CLR */
    write_reg(mach64, 0x10c, 627);        /* DST_Y_X */
    write_reg(mach64, 0x118, (8 << 16) | 2);
    fifo_run(mach64);
    for (int y = 627; y < 629; y++) {
        for (int x = 0; x < 8; x++) {
            if (vram_read(mach64, 8, (y * 832) + x) != 7)
                wrong++;
        }
    }
    CHECK(!wrong, "a fill at line 627 after the reset: %d of 16 pixels not drawn", wrong);

    /* A host data blit waiting for its data ends; what follows draws nothing. */
    write_reg(mach64, 0x2d8, 0x00000200); /* DP_SRC: host data */
    write_reg(mach64, 0x10c, 640);
    write_reg(mach64, 0x118, (8 << 16) | 1);
    fifo_run(mach64);
    CHECK(mach64->accel.busy, "the host data blit did not start");
    mach64_reset_engine(mach64);
    CHECK(!mach64->accel.busy, "the reset left the engine busy");
    write_reg(mach64, 0x200, 0x0f0f0f0f);
    write_reg(mach64, 0x200, 0x0f0f0f0f);
    fifo_run(mach64);
    wrong = 0;
    for (int x = 0; x < 8; x++) {
        if (vram_read(mach64, 8, (640 * 832) + x) != 0)
            wrong++;
    }
    CHECK(!wrong, "host data after the reset drew %d pixels", wrong);

    /* The GT-B's modeled FIFO is emptied and its engine idle. */
    timing = mach64->timing = mach64_timing_init();
    timing_push(timing, 0, 0, 0, 1000);
    timing_push(timing, 0, 0, 0, 1000);
    mach64_timing_fold(mach64);
    used = timing_used(mach64, 10, &busy);
    CHECK((used == 1) && busy, "before the reset: %u used, busy %d", used, busy);
    mach64_reset_engine(mach64);
    used = timing_used(mach64, 10, &busy);
    CHECK(!used && !busy, "after the reset: %u used, busy %d", used, busy);
    card_close(mach64);
}

int
main(void)
{
    key_tests();
    gui_engine_tests();
    composite_tests();
    overlay_tests();
    timing_fifo_tests();
    timing_clock_tests();
    timing_cost_tests();
    engine_reset_tests();

    if (failures) {
        fprintf(stderr, "Mach64 draw engine: %d checks failed\n", failures);
        return 1;
    }
    printf("Mach64 draw engine: all checks passed\n");
    return 0;
}
