/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tests of the 3D Rage II+ 3D engine and front-end scaler.
 *
 *          The engine is included as C, for its private functions, with
 *          the draw engine FIFO and the timing model as stubs: register
 *          streams go to mach64_3d_write and the results come from video
 *          memory. The expected images are worked out here, not with the
 *          engine's own helpers.
 *
 * Authors: Avastrap2, <https://github.com/Avastrap2>
 *
 *          Copyright 2026 Avastrap2.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/video/vid_ati_mach64_3d.c"

#define VRAM_SIZE (4 << 20)

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                \
        }                                                              \
    } while (0)

monitor_t monitors[MONITORS_NUM];
int       monitor_index_global;

static int failures;
static int card_type = MACH64_GTB;
static uint8_t test_agp[0x10000];

/* Host services used by the Rage Pro path in the shared engine. */
uint8_t
log2i(uint32_t value)
{
    uint8_t result = 0;
    while (value >>= 1)
        result++;
    return result;
}

void
pclog(const char *fmt, ...)
{
    (void) fmt;
}

const uint8_t *
agpgart_bus_ptr(uint32_t addr, uint32_t len)
{
    return (addr <= sizeof(test_agp) && len <= sizeof(test_agp) - addr) ? test_agp + addr : NULL;
}

void
dma_bm_read(uint32_t addr, uint8_t *dst, uint32_t len, int transfer_size)
{
    (void) addr;
    (void) transfer_size;
    memset(dst, 0, len);
}

/* The draw engine FIFO: a drain runs what is queued, and an ordering test
   can have a queued DP_SRC and DST_CNTL take effect at the next drain. */
static unsigned fifo_waits;
static int      fifo_shared_update;
static uint32_t fifo_shared_source;
static uint32_t fifo_shared_cntl;

void
mach64_wait_fifo_idle(mach64_t *mach64)
{
    fifo_waits++;
    if (fifo_shared_update) {
        mach64->dp_src     = fifo_shared_source;
        mach64->dst_cntl   = fifo_shared_cntl;
        fifo_shared_update = 0;
    }
    mach64->fifo_read_idx = mach64->fifo_write_idx;
}

void
mach64_wake_fifo_thread(mach64_t *mach64)
{
    (void) mach64;
}

void
mach64_timing_3d(mach64_t *mach64, const mach64_3d_work_t *work)
{
    (void) mach64;
    (void) work;
}

int
mach64_timing_status(mach64_t *mach64, uint32_t *used, int *busy)
{
    (void) mach64;
    (void) used;
    (void) busy;
    return 0;
}

/* Run helper jobs synchronously at wakeup. This deliberately draws helper
   lanes before lane zero, checking that the result does not depend on order. */
uint64_t timer_freq;

uint64_t
plat_timer_read(void)
{
    return 0;
}

thread_t *
thread_create_named(void (*thread_func)(void *param), void *param, const char *name)
{
    (void) thread_func;
    (void) name;
    mach64_3d_helper_t *helper = param;
    *(mach64_3d_helper_t **) helper->go = helper;
    atomic_store(&helper->sleeping, 1);
    return NULL;
}

int
thread_wait(thread_t *arg)
{
    (void) arg;
    return 0;
}

event_t *
thread_create_event(void)
{
    return calloc(1, sizeof(mach64_3d_helper_t *));
}

void
thread_set_event(event_t *arg)
{
    mach64_3d_helper_t *helper = *(mach64_3d_helper_t **) arg;
    mach64_3d_t *ctx = helper->ctx;

    if (ctx->helpers_quit)
        return;
    mach64_rp_draw_spans(ctx, ctx->rp_job, helper->index, ctx->job_threads, &helper->dither, &helper->agp);
    atomic_fetch_sub(&ctx->job_left, 1);
}

void
thread_reset_event(event_t *arg)
{
    (void) arg;
}

int
thread_wait_event(event_t *arg, int timeout)
{
    (void) arg;
    (void) timeout;
    return 0;
}

void
thread_destroy_event(event_t *arg)
{
    free(arg);
}

static void
expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        fprintf(stderr, "%s: %08x, not %08x\n", name, actual, expected);
        failures++;
    }
}

static void
expect_int(const char *name, int actual, int expected)
{
    if (actual != expected) {
        fprintf(stderr, "%s: %d, not %d\n", name, actual, expected);
        failures++;
    }
}

static mach64_t *
card_create(void)
{
    mach64_t *mach64 = calloc(1, sizeof(mach64_t));

    if (mach64) {
        mach64->svga.vram        = calloc(1, VRAM_SIZE);
        mach64->svga.changedvram = calloc(VRAM_SIZE >> 12, sizeof(*mach64->svga.changedvram));
        mach64->svga.monitor     = calloc(1, sizeof(monitor_t));
    }
    if (!mach64 || !mach64->svga.vram || !mach64->svga.changedvram || !mach64->svga.monitor) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    mach64->vram_size = VRAM_SIZE >> 20; /* MiB */
    mach64->vram_mask = VRAM_SIZE - 1;
    mach64->type      = card_type;
    mach64->gt3d      = mach64_3d_init(mach64);
    return mach64;
}

static void
card_close(mach64_t *mach64)
{
    mach64_3d_close(mach64->gt3d);
    free(mach64->svga.monitor);
    free(mach64->svga.changedvram);
    free(mach64->svga.vram);
    free(mach64);
}

/* Fixed-point fields of the 3D registers. */

static void
field_tests(void)
{
    static const int32_t  periods[]   = { -1073741824, -4, -1, 0, 1, 4, 1073741824 };
    static const uint32_t fractions[] = { 0, 1, 15, 16, 32768, 65535 };
    int                   wrong       = 0;

    /* Real driver/log style values: high reserved bits must not affect fields. */
    CHECK(mach64_3d_u10_11_decode(UINT32_C(0xfc08c853)) == (int32_t) UINT32_C(0x0008c840));
    CHECK(mach64_3d_u10_11_decode(UINT32_C(0x03f98ab4)) == (int32_t) UINT32_C(0x03f98aa0));

    CHECK(mach64_3d_s10_16_decode(UINT32_C(0xffffff6e)) == -146);
    CHECK(mach64_3d_s10_16_decode(UINT32_C(0xfffffdb8)) == -584);
    CHECK(mach64_3d_s11_16_decode(UINT32_C(0xffdb3460)) == -2411424);

    /* S.10.16 is sign + 10 integer + 16 fraction = 27 physical bits. */
    CHECK(mach64_3d_s10_16_decode(UINT32_C(0x024119b0)) == 37820848);
    CHECK(mach64_3d_s10_16_decode(UINT32_C(0x0489c338)) == -58080456);
    CHECK(mach64_3d_s10_16_decode(UINT32_C(0x04000000)) == -(INT32_C(1) << 26));

    /* S.11.16 is sign + 11 integer + 16 fraction = 28 physical bits. */
    CHECK(mach64_3d_s11_16_decode(UINT32_C(0xf9a69d88)) == -106521208);
    CHECK(mach64_3d_s11_16_decode(UINT32_C(0x0aa4ac18)) == -89871336);
    CHECK(mach64_3d_s11_16_decode(UINT32_C(0x08000000)) == -(INT32_C(1) << 27));

    /* Lead/trail Bresenham terms are signed 18-bit physical fields. */
    CHECK(mach64_3d_sign_extend(UINT32_C(0xffffffff), 18) == -1);
    CHECK(mach64_3d_sign_extend(UINT32_C(0x0001ffff), 18) == 131071);
    CHECK(mach64_3d_sign_extend(UINT32_C(0x00020000), 18) == -131072);

    /* S.8.12 lives at bits 24:4: high reserved and low four bits vanish. */
    CHECK(mach64_3d_s8_12_decode(UINT32_C(0x00ff000f)) == (int32_t) UINT32_C(0x00ff0000));
    CHECK(mach64_3d_s8_12_decode(UINT32_C(0xfe000010)) == 16);
    CHECK(mach64_3d_s8_12_decode(UINT32_C(0x01fffff0)) == -16);

    /* Color conversion retains the physical signed field, not an unsigned
     * 8-bit wrap or a clamp of the unbounded host interpolation sum. */
    CHECK(mach64_3d_s8_12_color(-16) == 0);
    CHECK(mach64_3d_s8_12_color(INT64_C(0x00ffffff)) == 255);
    CHECK(mach64_3d_s8_12_color(INT64_C(0x01000000)) == 0);
    CHECK(mach64_3d_s8_12_color(INT64_C(0x01ffffff)) == 0);
    CHECK(mach64_3d_s8_12_color(INT64_C(0x02000000)) == 0);
    CHECK(mach64_3d_s8_12_color((INT64_C(197) - 512) * 65536) == 197);
    CHECK(mach64_3d_s8_12_color((INT64_C(123) + 512) * 65536) == 123);
    CHECK(mach64_3d_s8_12_color(INT64_MIN) == 0);
    CHECK(mach64_3d_s8_12_color(INT64_MAX) == 0);
    /* Any sum wraps into the field: an integer part of 256 or more is
       negative, so black, and the color is what the field decodes to. */
    for (unsigned p = 0; p < sizeof(periods) / sizeof(periods[0]); p++) {
        for (unsigned integer = 0; integer < 512; integer++) {
            for (unsigned f = 0; f < sizeof(fractions) / sizeof(fractions[0]); f++) {
                int64_t value    = (int64_t) periods[p] * INT64_C(0x02000000) + (int64_t) integer * 65536 + fractions[f];
                int     expected = (integer < 256) ? (int) integer : 0;
                int32_t decoded  = mach64_3d_s8_12_decode((uint32_t) value);

                if ((mach64_3d_s8_12_color(value) != expected) || (mach64_3d_s8_12_color(value) != ((decoded < 0) ? 0 : (decoded / 65536))))
                    wrong++;
            }
        }
    }
    CHECK(wrong == 0);

    /* Z signs from bit 28, not bit 31. */
    CHECK(mach64_3d_s16_12_decode(UINT32_C(0xffffffff)) == -1);
    CHECK(mach64_3d_s16_12_decode(UINT32_C(0x10000000)) == -(INT32_C(1) << 28));
    CHECK(mach64_3d_s16_12_decode(UINT32_C(0xe0001000)) == 4096);

    CHECK(mach64_3d_s16_12_depth(-1) == 0);
    CHECK(mach64_3d_s16_12_depth(INT64_C(0x0fffffff)) == 65535);
    CHECK(mach64_3d_s16_12_depth(INT64_C(0x10000000)) == 0);
    CHECK(mach64_3d_s16_12_depth(INT64_C(0x1fffffff)) == 0);
    CHECK(mach64_3d_s16_12_depth((INT64_C(48443) + 131072) * 4096) == 48443);
    CHECK(mach64_3d_s16_12_depth((INT64_C(23823) - 131072) * 4096) == 23823);
    CHECK(mach64_3d_s16_12_depth(INT64_MIN) == 0);
    CHECK(mach64_3d_s16_12_depth(INT64_MAX) == 0);
    wrong = 0;
    for (unsigned p = 0; p < sizeof(periods) / sizeof(periods[0]); p++) {
        for (unsigned integer = 0; integer < 131072; integer++) {
            for (unsigned fractional = 0; fractional <= 4095; fractional += 4095) {
                int64_t  value    = (int64_t) periods[p] * INT64_C(0x20000000) + (int64_t) integer * 4096 + fractional;
                unsigned expected = (integer < 65536) ? integer : 0;

                if (mach64_3d_s16_12_depth(value) != expected)
                    wrong++;
            }
        }
    }
    CHECK(wrong == 0);

    CHECK(mach64_3d_s10_16_encode(-146) == UINT32_C(0x07ffff6e));
    CHECK(mach64_3d_s11_16_encode(-146) == UINT32_C(0x0fffff6e));
    CHECK(mach64_3d_s8_12_encode(-16) == UINT32_C(0x01fffff0));
    CHECK(mach64_3d_s16_12_encode(-1) == UINT32_C(0x1fffffff));
}

/* The data path: the source select and the destination compare. */

static void
data_path_tests(void)
{
    const uint32_t sentinel       = 0xf83ef83eu;
    const uint32_t foreground     = 0x12345678u;
    const uint32_t filtered_texel = 0x00abcdefu;
    const uint32_t compare_mask   = 0x7fff7fffu;

    /* ATI's first repair pass uses DP_SRC=0x00000005: the always-one
     * monochrome selector chooses DP_FRGD_SRC=0, which is DP_BKGD_CLR. */
    expect_u32("repair first-pass sentinel",
               mach64_3d_dp_select_source(
                   0x00000005u, sentinel, foreground, filtered_texel),
               sentinel);

    /* The second pass uses DP_SRC=0x00000505 and restores Scaler/3D data. */
    expect_u32("repair second-pass texel",
               mach64_3d_dp_select_source(
                   0x00000505u, sentinel, foreground, filtered_texel),
               filtered_texel);
    expect_u32("foreground-color source",
               mach64_3d_dp_select_source(
                   0x00000105u, sentinel, foreground, filtered_texel),
               foreground);
    expect_u32("nontrivial mono source remains 3D",
               mach64_3d_dp_select_source(
                   0x00010505u, sentinel, foreground, filtered_texel),
               filtered_texel);

    /* CLR_CMP_CNTL=4 inhibits pixels whose existing destination is not the
     * first-pass sentinel, leaving the filtered second pass inside the mask. */
    expect_int("sentinel permits second pass",
               mach64_3d_destination_compare_inhibits(
                   0x00000004u, sentinel, compare_mask, sentinel, 0xffffu),
               0);
    expect_int("other destination inhibits second pass",
               mach64_3d_destination_compare_inhibits(
                   0x00000004u, sentinel, compare_mask, 0x001fu, 0xffffu),
               1);
    expect_int("ARGB1555 alpha bits are masked",
               mach64_3d_destination_compare_inhibits(
                   0x00000004u, sentinel, compare_mask, 0x783eu, 0xffffu),
               0);
    expect_int("equality function inhibits sentinel",
               mach64_3d_destination_compare_inhibits(
                   0x00000005u, sentinel, compare_mask, sentinel, 0xffffu),
               1);
    expect_int("texel selector is not a destination compare",
               mach64_3d_destination_compare_inhibits(
                   0x02000004u, sentinel, compare_mask, 0x001fu, 0xffffu),
               0);
    expect_int("disabled compare avoids destination read",
               mach64_3d_destination_compare_enabled(0x00000000u), 0);
    expect_int("destination inequality compare enabled",
               mach64_3d_destination_compare_enabled(0x00000004u), 1);
    expect_int("texel compare is not destination compare",
               mach64_3d_destination_compare_enabled(0x02000004u), 0);
}

/* Trapezoid spans: which edge owns a pixel, and the scissors. */

static void
expect_span(const char *name, int lead, int trail, int fill_lr,
            int sc_left, int sc_right,
            int expected_valid, int expected_first, int expected_end)
{
    int first = -1;
    int end   = -1;
    int valid = mach64_3d_trapezoid_clip_span(
        lead, trail, fill_lr, sc_left, sc_right, &first, &end);

    if (valid != expected_valid || (valid && (first != expected_first || end != expected_end))) {
        fprintf(stderr,
                "%s: valid %d span [%d,%d), not valid %d span [%d,%d)\n",
                name, valid, first, end,
                expected_valid, expected_first, expected_end);
        failures++;
    }
}

static void
trapezoid_tests(void)
{
    /* Leading is included and trailing is excluded in either fill direction. */
    expect_span("left-to-right ownership", 10, 13, 1,
                0, 31, 1, 10, 13);
    expect_span("right-to-left ownership", 13, 10, 0,
                0, 31, 1, 11, 14);

    /* Adjacent lead/lead boundaries retain both neighboring pixels. */
    expect_span("left triangle leading edge", 95, 86, 0,
                0, 127, 1, 87, 96);
    expect_span("right triangle leading edge", 96, 106, 1,
                0, 127, 1, 96, 106);

    /* An identical trailing/leading shared edge has exactly one owner. */
    expect_span("left triangle trailing edge", 1, 21, 1,
                0, 127, 1, 1, 21);
    expect_span("right triangle leading edge", 21, 54, 1,
                0, 127, 1, 21, 54);

    /* A collapsed lead/trail pair has zero horizontal coverage. */
    expect_span("collapsed span", 10, 10, 1,
                0, 31, 0, 0, 0);
    expect_span("collapsed reverse span", 10, 10, 0,
                0, 31, 0, 0, 0);

    /* Scissor coordinates remain inclusive. */
    expect_span("left-to-right clipped", 5, 40, 1,
                8, 31, 1, 8, 32);
    expect_span("right-to-left clipped", 40, 5, 0,
                8, 31, 1, 8, 32);
    expect_span("crossed left-to-right", 13, 10, 1,
                0, 31, 0, 0, 0);
    expect_span("crossed right-to-left", 10, 13, 0,
                0, 31, 0, 0, 0);

    /* A span the scissors cut to nothing is empty. */
    expect_span("clipped to nothing", 5, 40, 1,
                40, 63, 0, 0, 0);
    expect_span("clipped to nothing right-to-left", 40, 5, 0,
                0, 5, 0, 0, 0);
}

/* Mip map level of detail, and the filter it selects. */

static int
check_lod(int64_t dsdx, int64_t dtdx, int64_t dsdy, int64_t dtdy,
          int expected_minifying, int expected_floor, int expected_nearest)
{
    mach64_3d_mip_lod_t lod = mach64_3d_mip_lod(dsdx, dtdx, dsdy, dtdy, 20, 6);

    return lod.minifying == expected_minifying && lod.floor_lod == expected_floor && lod.nearest_lod == expected_nearest && lod.fraction >= 0 && lod.fraction <= 255;
}

static int
check_filter(int minifying, unsigned blend, int bilinear,
             mach64_3d_texture_filter_t expected)
{
    return mach64_3d_texture_filter(minifying, blend, bilinear) == expected;
}

static int
check_lowest_level(const uint32_t offsets[11], int largest,
                   int expected_lowest)
{
    return mach64_3d_mip_lowest_populated_level(offsets, largest) == expected_lowest;
}

static void
mipmap_tests(void)
{
    const int64_t  texel              = INT64_C(1) << 20;
    const uint32_t complete_chain[11] = {
        0x1180, 0x1160, 0x1120, 0x10a0, 0x1000, 0x0800, 0x0000
    };
    const uint32_t final_reality_chain[11] = {
        0x2151c0, 0x2151c0, 0x2151c0, 0x2151c0,
        0x2151c0, 0x214980, 0x212940
    };
    mach64_3d_mip_lod_t lod;

    CHECK(check_lod(texel / 4, 0, 0, 0, 0, 0, 0));
    CHECK(check_lod(texel, 0, 0, 0, 0, 0, 0));
    CHECK(check_lod(texel + texel / 4, 0, 0, 0, 1, 0, 0));
    CHECK(check_lod(texel + texel / 2, 0, 0, 0, 1, 0, 1));
    CHECK(check_lod(texel * 2, 0, 0, 0, 1, 1, 1));
    CHECK(check_lod(texel * 3, 0, 0, 0, 1, 1, 2));
    CHECK(check_lod(texel * 8, 0, 0, 0, 1, 3, 3));
    CHECK(check_lod(0, 0, -(texel * 8), 0, 1, 3, 3));
    CHECK(check_lod(texel * 128, 0, 0, 0, 1, 6, 6));

    /* Final Reality uploads 64x64, 32x32, and 16x16 maps, then repeats the
     * 16x16 byte pointer for nominally smaller levels. */
    CHECK(check_lowest_level(complete_chain, 6, 0));
    CHECK(check_lowest_level(final_reality_chain, 6, 4));
    lod = mach64_3d_mip_lod(texel * 128, 0, 0, 0, 20, 2);
    CHECK(lod.minifying && (lod.floor_lod == 2) && (lod.nearest_lod == 2));

    /* Magnification follows BILINEAR_TEX_EN, except ATI's documented
     * multipass suppression for the two 2x2 minification modes. */
    CHECK(check_filter(0, 0, 0, MACH64_3D_TEXTURE_FILTER_NEAREST));
    CHECK(check_filter(0, 0, 1, MACH64_3D_TEXTURE_FILTER_BILINEAR));
    CHECK(check_filter(0, 2, 0, MACH64_3D_TEXTURE_FILTER_NONE));
    CHECK(check_filter(0, 3, 0, MACH64_3D_TEXTURE_FILTER_NONE));
    CHECK(check_filter(0, 2, 1, MACH64_3D_TEXTURE_FILTER_BILINEAR));

    /* Minification ignores BILINEAR_TEX_EN: TEX_BLEND_FCN selects whether
     * the chosen map is sampled nearest or with a 2x2 blend. */
    CHECK(check_filter(1, 0, 1, MACH64_3D_TEXTURE_FILTER_NEAREST));
    CHECK(check_filter(1, 1, 1, MACH64_3D_TEXTURE_FILTER_NEAREST));
    CHECK(check_filter(1, 2, 0, MACH64_3D_TEXTURE_FILTER_BILINEAR));
    CHECK(check_filter(1, 3, 0, MACH64_3D_TEXTURE_FILTER_BILINEAR));
}

/* YUV. */

static void
expect_rgb(const char *name, mach64_yuv_rgb_t actual,
           int r, int g, int b)
{
    if (actual.r != r || actual.g != g || actual.b != b) {
        fprintf(stderr, "%s: %u,%u,%u, not %d,%d,%d\n",
                name, actual.r, actual.g, actual.b, r, g, b);
        failures++;
    }
}

static void
yuv_tests(void)
{
    const uint32_t yuyv = 0xDC963C28u; /* Y0=40 U=60 Y1=150 V=220 */

    expect_int("YUYV Y0", mach64_yuyv_y(yuyv, 0), 40);
    expect_int("YUYV Y1", mach64_yuyv_y(yuyv, 1), 150);
    expect_int("YUYV U", mach64_yuyv_u(yuyv), 60);
    expect_int("YUYV V", mach64_yuyv_v(yuyv), 220);

    expect_int("unsigned neutral U", mach64_yuv_chroma(0x80, 0), 0);
    expect_int("unsigned zero byte", mach64_yuv_chroma(0x00, 0), -128);
    expect_int("signed neutral U", mach64_yuv_chroma(0x00, 1), 0);
    expect_int("signed negative U", mach64_yuv_chroma(0x80, 1), -128);
    expect_int("signed positive U", mach64_yuv_chroma(0x7f, 1), 127);

    expect_rgb("unsigned neutral grey",
               mach64_yuv_to_rgb(100, 128, 128, 0), 100, 100, 100);
    expect_rgb("APPLE signed neutral grey",
               mach64_yuv_to_rgb(100, 0, 0, 1), 100, 100, 100);
}

/* The texture palette, and the CI4 banks. */

static void
palette_tests(void)
{
    mach64_3d_t ctx = { 0 };
    uint32_t palette[256] = { 0 };
    uint32_t rgb;
    uint32_t ci4_low;
    uint32_t ci4_high;

    /* Values captured from the Windows 95 Direct3D HAL Globe upload. */
    mach64_3d_texture_palette_store(palette, 0x30bda54au);
    mach64_3d_texture_palette_store(palette, 0x01181821u);

    expect_u32("entry 0x30", palette[0x30], 0x00bda54au);
    expect_u32("entry 0x01", palette[0x01], 0x00181821u);
    expect_u32("unwritten entry", palette[0x02], 0);

    mach64_3d_expand_table_init();

    /* The scaler's palette image swaps the two 128-entry halves in its
       second 0x400-byte row. */
    mach64_3d_texture_palette_cache_byte(&ctx, 0x000, 0x00);
    mach64_3d_texture_palette_cache_byte(&ctx, 0x001, 0xf8);
    expect_u32("palette cache first-half entry", ctx.texture_palette[0], 0xff0000u);
    mach64_3d_texture_palette_cache_byte(&ctx, 0x004, 0xe0);
    mach64_3d_texture_palette_cache_byte(&ctx, 0x005, 0x07);
    expect_u32("palette cache first-row high entry", ctx.texture_palette[128], 0x00ff00u);
    mach64_3d_texture_palette_cache_byte(&ctx, 0x400, 0x1f);
    mach64_3d_texture_palette_cache_byte(&ctx, 0x401, 0x00);
    expect_u32("palette cache second-row high entry", ctx.texture_palette[128], 0x0000ffu);
    mach64_3d_texture_palette_cache_byte(&ctx, 0x404, 0xff);
    mach64_3d_texture_palette_cache_byte(&ctx, 0x405, 0xff);
    expect_u32("palette cache second-row low entry", ctx.texture_palette[0], 0xffffffu);

    rgb = palette[0x30];
    expect_u32("red", (rgb >> 16) & 0xff, 0xbdu);
    expect_u32("green", (rgb >> 8) & 0xff, 0xa5u);
    expect_u32("blue", rgb & 0xff, 0x4au);

    /*
     * ATI documents CI4 as CI8 lookup with a selected nibble and a 4-bit
     * palette bank in DP_CI4_RGB_INDEX.  For bank A and byte 0x3c, low-nibble
     * mode selects AC and high-nibble mode selects A3.
     */
    ci4_low  = (0xau << CI4_RGB_INDEX_SHIFT) | CI4_RGB_LOW_NIBBLE;
    ci4_high = (0xau << CI4_RGB_INDEX_SHIFT) | CI4_RGB_HIGH_NIBBLE;
    expect_u32("CI4 low nibble bank",
               mach64_3d_texture_palette_texel_index(ci4_low, 0x3cu), 0xacu);
    expect_u32("CI4 high nibble bank",
               mach64_3d_texture_palette_texel_index(ci4_high, 0x3cu), 0xa3u);
    expect_u32("CI8 full byte",
               mach64_3d_texture_palette_texel_index(0, 0x3cu), 0x3cu);
    expect_u32("invalid dual CI4 selector stays CI8",
               mach64_3d_texture_palette_texel_index(
                   CI4_RGB_LOW_NIBBLE | CI4_RGB_HIGH_NIBBLE,
                   0x3cu),
               0x3cu);
}

/* Texel keys, component expansion and other controls. */

/* The IIC HAL asks for 128 free entries before its first draw. Reporting
   the II+'s 48 makes the real driver spin forever at initialization. */
static void
fifo_identity_tests(void)
{
    mach64_t *m = card_create();
    uint32_t value;
    const unsigned depths[] = { 48, 128, 192, 128, 64 };
    for (unsigned profile = 0; profile < 5; profile++) {
        m->rage2c = profile == 1;
        m->ragepro = profile >= 2;
        m->rp.gui_cntl = profile >= 2 ? profile - 2 : 0;
        expect_u32("FIFO physical capacity", mach64_fifo_depth(m), depths[profile]);
        for (unsigned used = 0; used <= depths[profile]; used++) {
            m->fifo_read_idx = 0;
            m->fifo_write_idx = used;
            CHECK(mach64_3d_read(m, GUI_STAT, &value));
            expect_u32("GUI_STAT free entries", (value >> 16) & 0x1ff, depths[profile] - used);
            expect_u32("GUI_STAT active", value & 1, used != 0);
        }
    }
    card_close(m);
}

static void
control_tests(void)
{
    const uint32_t equality = 0x02000005u;
    const uint32_t key      = 0x00f800f8u;
    const uint32_t mask     = 0x00f8fcf8u;

    /* Captured HAL values: expanded RGB565 magenta masks to the key. */
    expect_int("captured magenta equality", mach64_3d_texel_key_match(equality, key, mask, 0xff, 0x00, 0xff), 1);
    expect_int("green is not the key", mach64_3d_texel_key_match(equality, key, mask, 0x00, 0xff, 0x00), 0);
    expect_int("2D source selector is ignored", mach64_3d_texel_key_match(0x01000005u, key, mask, 0xff, 0x00, 0xff), 0);
    expect_int("texel inequality", mach64_3d_texel_key_match(0x02000004u, key, mask, 0x00, 0xff, 0x00), 1);
    expect_int("disabled comparator", mach64_3d_texel_key_match(0x02000000u, key, mask, 0xff, 0x00, 0xff), 0);

    /* Pseudo-color texels are keyed on the low-order source index, not the
     * palette-expanded RGB value. */
    expect_int("CI8 indexed equality", mach64_3d_texel_key_match_index(equality, 0x0000007cu, 0x000000ffu, 0x7c), 1);
    expect_int("CI8 indexed mismatch", mach64_3d_texel_key_match_index(equality, 0x0000007cu, 0x000000ffu, 0x3c), 0);
    expect_int("CI8 masked key", mach64_3d_texel_key_match_index(equality, 0xdead007cu, 0x000000ffu, 0x7c), 1);

    /* NEAREST_TEX_VIS selects the nearest contributor instead of the OR of
     * all contributors participating in a texture filter. */
    expect_int("all contributors inhibit", mach64_3d_texel_visibility_inhibits(0, 0, 1), 1);
    expect_int("nearest contributor visible", mach64_3d_texel_visibility_inhibits(1, 0, 1), 0);
    expect_int("nearest contributor inhibits", mach64_3d_texel_visibility_inhibits(1, 1, 1), 1);

    /* SCALE_PIX_EXPAND=0 zero-extends source components.  Dynamic correction
     * repeats source bits into the low positions used by the 24-bit pipeline. */
    expect_int("RGB555 zero max", mach64_3d_expand_component(31, 5, 0), 0xf8);
    expect_int("RGB555 dynamic max", mach64_3d_expand_component(31, 5, 1), 0xff);
    expect_int("RGB555 dynamic pattern", mach64_3d_expand_component(3, 5, 1), 0x18);
    expect_int("RGB565 green zero max", mach64_3d_expand_component(63, 6, 0), 0xfc);
    expect_int("RGB565 green dynamic max", mach64_3d_expand_component(63, 6, 1), 0xff);
    expect_int("RGB332 red dynamic pattern", mach64_3d_expand_component(2, 3, 1), 0x49);
    expect_int("RGB332 blue zero max", mach64_3d_expand_component(3, 2, 0), 0xc0);
    expect_int("RGB332 blue dynamic max", mach64_3d_expand_component(3, 2, 1), 0xff);
    expect_int("ARGB4444 dynamic pattern", mach64_3d_expand_component(10, 4, 1), 0xaa);

    /* RED_DITHER_MAX reserves the top 32 RGB8 palette entries only when
     * dithering is active. */
    expect_int("RGB8 red unrestricted", mach64_3d_rgb8_red_code(7, 1, 0), 7);
    expect_int("RGB8 red dither max", mach64_3d_rgb8_red_code(7, 1, 1), 6);
    expect_int("RGB8 red max ignored without dither", mach64_3d_rgb8_red_code(7, 0, 1), 7);

    /* TEX_BLEND_FCN=3 supplies mip-distance alpha specifically for the
     * alpha-blending pipeline. */
    expect_int("multipass LOD alpha", mach64_3d_uses_lod_alpha(1, 3), 1);
    expect_int("no LOD alpha without blending", mach64_3d_uses_lod_alpha(0, 3), 0);
    expect_int("no LOD alpha for nearest-map mode", mach64_3d_uses_lod_alpha(1, 2), 0);
}

/* The texel path's packed forms against the component ones. */

static void
texel_path_tests(void)
{
    mach64_t *m     = card_create();
    uint32_t  mask  = m->vram_mask;
    int       wrong = 0;

    /* The 2x2 filter's blend, two components in each 32-bit lane. */
    for (unsigned fraction = 0; fraction < 256; fraction++) {
        for (unsigned a = 0; a < 256; a++) {
            for (unsigned b = 0; b < 256; b++) {
                uint32_t pa  = (a << 24) | ((255 - a) << 16) | (b << 8) | (a ^ b);
                uint32_t pb  = (b << 24) | ((255 - b) << 16) | (a << 8) | ((a + b) & 0xff);
                uint32_t got = mach64_3d_lerp_texel(pa, pb, fraction);

                for (int shift = 0; shift < 32; shift += 8) {
                    unsigned ca = (pa >> shift) & 0xff;
                    unsigned cb = (pb >> shift) & 0xff;

                    if (((got >> shift) & 0xff) != ((ca * (256 - fraction) + cb * fraction + 128) >> 8))
                        wrong++;
                }
            }
        }
    }
    CHECK(wrong == 0);

    /* The 16-bit texel types, for every pixel, with either expansion. */
    wrong = 0;
    for (int dynamic = 0; dynamic < 2; dynamic++) {
        for (uint32_t raw = 0; raw < 0x10000; raw++) {
            uint32_t argb1555 = ((raw & 0x8000) ? 0xff000000 : 0) | (mach64_3d_expand_component(raw >> 10, 5, dynamic) << 16) |
                                (mach64_3d_expand_component(raw >> 5, 5, dynamic) << 8) | mach64_3d_expand_component(raw, 5, dynamic);
            uint32_t argb565  = 0xff000000 | (mach64_3d_expand_component(raw >> 11, 5, dynamic) << 16) |
                               (mach64_3d_expand_component(raw >> 5, 6, dynamic) << 8) | mach64_3d_expand_component(raw, 5, dynamic);
            uint32_t argb4444 = ((((raw >> 12) & 15) * 17) << 24) | (mach64_3d_expand_component(raw >> 8, 4, dynamic) << 16) |
                                (mach64_3d_expand_component(raw >> 4, 4, dynamic) << 8) | mach64_3d_expand_component(raw, 4, dynamic);

            wrong += (mach64_3d_argb1555(raw, dynamic) != argb1555);
            wrong += (mach64_3d_argb565(raw, dynamic) != argb565);
            wrong += (mach64_3d_argb4444(raw, dynamic) != argb4444);
        }
    }
    CHECK(wrong == 0);

    /* Reads at the end of video memory wrap a byte at a time. */
    for (uint32_t i = 0; i < 8; i++) {
        m->svga.vram[i]        = 0x10 + i;
        m->svga.vram[mask - i] = 0xa0 + i;
    }
    wrong = 0;
    for (uint32_t addr = mask - 6; addr != mask + 6; addr++) {
        const uint8_t *vram = m->svga.vram;
        uint16_t       w    = vram[addr & mask] | (vram[(addr + 1) & mask] << 8);
        uint32_t       d    = w | (vram[(addr + 2) & mask] << 16) | ((uint32_t) vram[(addr + 3) & mask] << 24);

        wrong += (mach64_3d_vram_read16(m, addr) != w);
        wrong += (mach64_3d_vram_read32(m, addr) != d);
    }
    CHECK(wrong == 0);
    card_close(m);
}

/* Scaler arithmetic: the accumulators, the blends and the mixes. */

static void
scaler_math_tests(void)
{
    static const int      expected_2x[8]     = { 0, 0, 1, 1, 2, 2, 3, 3 };
    static const uint32_t quadrant_colors[4] = {
        0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0x00ffffffu
    };

    for (int destination = 0; destination < 8; destination++) {
        int source = mach64_scaler_accum_pixel(destination * 0x8000);
        expect_int("2x replicated source coordinate", source,
                   expected_2x[destination]);
    }

    expect_int("negative accumulator floor", mach64_scaler_accum_pixel(-1), -1);
    expect_int("one-to-one accumulator", mach64_scaler_accum_pixel(7 * 0x10000), 7);
    expect_int("half-pixel 4-bit coefficient", (int) mach64_scaler_accum_fraction4(0x8000), 8);
    expect_int("quarter-pixel 4-bit coefficient", (int) mach64_scaler_accum_fraction4(0x4000), 4);
    expect_int("half-pixel 4-bit blend", mach64_scaler_lerp4(0, 255, 8), 128);
    expect_int("quarter-pixel 4-bit blend", mach64_scaler_lerp4(0, 255, 4), 64);

    expect_int("signed chroma zero crossing", mach64_scaler_lerp4_signed(-64, 64, 8), 0);
    expect_int("signed chroma negative half", mach64_scaler_lerp4_signed(-128, 0, 8), -64);
    expect_int("signed chroma positive half", mach64_scaler_lerp4_signed(0, 126, 8), 63);

    /*
     * For 1:1 YUV422 output geometry the UV stream advances at half the luma
     * rate: sample 0, midpoint 0->1, sample 1, midpoint 1->2.  These are the
     * documented U0,(U0+U1)/2 positions when UV_HACC starts at zero.
     */
    for (int destination = 0; destination < 4; destination++) {
        int32_t uv = destination * 0x8000;
        expect_int("YUV422 UV sample coordinate", mach64_scaler_accum_pixel(uv), destination / 2);
        expect_int("YUV422 UV sample fraction", (int) mach64_scaler_accum_fraction4(uv),
                   (destination & 1) ? 8 : 0);
    }

    /* Exact geometry used by the DDTEST reproduction: 32x32 to 64x64. */
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            int      source_x          = mach64_scaler_accum_pixel(x * 0x8000);
            int      source_y          = mach64_scaler_accum_pixel(y * 0x8000);
            unsigned source_quadrant   = (source_x >= 16 ? 1u : 0u) | (source_y >= 16 ? 2u : 0u);
            unsigned expected_quadrant = (x >= 32 ? 1u : 0u) | (y >= 32 ? 2u : 0u);

            if (quadrant_colors[source_quadrant] != quadrant_colors[expected_quadrant]) {
                fprintf(stderr, "32x32 to 64x64 quadrant mismatch at %d,%d\n",
                        x, y);
                failures++;
            }
        }
    }

    expect_u32("SRCCOPY", mach64_scaler_mix(0x00ff00ffu, 0x000000ffu, 7),
               0x00ff00ffu);
    expect_u32("SRCINVERT", mach64_scaler_mix(0x00ff00ffu, 0x000000ffu, 5),
               0x00ff0000u);
    expect_u32("SRCAND", mach64_scaler_mix(0x00ff00ffu, 0x000000ffu, 12),
               0x000000ffu);
    expect_u32("BLACKNESS", mach64_scaler_mix(0xffffffffu, 0xffffffffu, 1),
               0x00000000u);
    expect_u32("WHITENESS", mach64_scaler_mix(0, 0, 2), 0xffffffffu);
}

/* The scaler, through its registers. */

enum {
    SOURCE_OFFSET    = 0x10000,
    SOURCE_SIZE      = 32,
    DESTINATION_SIZE = 64
};

static uint32_t *
pixel32(mach64_t *mach64, uint32_t address)
{
    return (uint32_t *) &mach64->svga.vram[address & mach64->vram_mask];
}

static void
write_yuyv_pair(mach64_t *mach64, uint32_t address,
                uint8_t y0, uint8_t u, uint8_t y1, uint8_t v)
{
    mach64->svga.vram[(address + 0) & mach64->vram_mask] = y0;
    mach64->svga.vram[(address + 1) & mach64->vram_mask] = u;
    mach64->svga.vram[(address + 2) & mach64->vram_mask] = y1;
    mach64->svga.vram[(address + 3) & mach64->vram_mask] = v;
}

static uint32_t
argb_from_centered_yuv(int y, int u, int v)
{
    mach64_yuv_rgb_t rgb = mach64_yuv_centered_to_rgb(y, u, v);

    return 0xff000000u | ((uint32_t) rgb.r << 16) | ((uint32_t) rgb.g << 8) | rgb.b;
}

static int
write_scaler_register(mach64_t *mach64, uint32_t address, uint32_t value)
{
    if (!mach64_3d_write(mach64, address, value, FIFO_WRITE_DWORD)) {
        fprintf(stderr, "scaler register %03x was not claimed\n", address);
        return 1;
    }
    return 0;
}

static int
run_rgb_scaler_test(mach64_t *mach64)
{
    static const uint32_t colors[4] = {
        0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0x00ffffffu
    };
    int wrong = 0;

    for (int y = 0; y < SOURCE_SIZE; y++) {
        for (int x = 0; x < SOURCE_SIZE; x++) {
            unsigned quadrant                                                       = (x >= 16 ? 1u : 0u) | (y >= 16 ? 2u : 0u);
            *pixel32(mach64, SOURCE_OFFSET + (uint32_t) (y * SOURCE_SIZE + x) * 4u) = colors[quadrant];
        }
    }

    mach64->dp_pix_width  = 0x60000606u;
    mach64->dp_src        = 0x00000500u;
    mach64->dp_mix        = 0x00070007u;
    mach64->write_mask    = 0xffffffffu;
    mach64->dst_off_pitch = 0x02000000u; /* 64 pixels */
    mach64->dst_y_x       = 0;
    mach64->dst_cntl      = DST_X_DIR | DST_Y_DIR;
    mach64->sc_left_right = 0x003f0000u;
    mach64->sc_top_bottom = 0x003f0000u;

    wrong += write_scaler_register(mach64, 0x1c0, SOURCE_OFFSET);
    wrong += write_scaler_register(mach64, 0x1dc, SOURCE_SIZE);
    wrong += write_scaler_register(mach64, 0x1e0, SOURCE_SIZE);
    wrong += write_scaler_register(mach64, 0x1ec, SOURCE_SIZE);
    wrong += write_scaler_register(mach64, 0x1f0, 0x00008000u);
    wrong += write_scaler_register(mach64, 0x1f4, 0x00008000u);
    wrong += write_scaler_register(mach64, 0x1f8, 0);
    wrong += write_scaler_register(mach64, 0x3c8, 0);
    wrong += write_scaler_register(mach64, 0x1fc, 0x00000140u);

    if (!mach64_3d_write(mach64, 0x118, 0x00400040u,
                         FIFO_WRITE_DWORD)) {
        fprintf(stderr, "scaler destination trigger was not claimed\n");
        wrong++;
    }

    for (int y = 0; y < DESTINATION_SIZE; y++) {
        for (int x = 0; x < DESTINATION_SIZE; x++) {
            unsigned quadrant = (x >= 32 ? 1u : 0u) | (y >= 32 ? 2u : 0u);
            uint32_t actual   = *pixel32(mach64,
                                         (uint32_t) (y * DESTINATION_SIZE + x) * 4u);

            if (actual != colors[quadrant]) {
                fprintf(stderr,
                        "scaled pixel %d,%d: expected %08x, got %08x\n",
                        x, y, colors[quadrant], actual);
                wrong++;
                y = DESTINATION_SIZE;
                break;
            }
        }
    }
    return wrong;
}

static int
run_yuyv_scaler_test(mach64_t *mach64)
{
    static const int expected_u[4] = { 0, 32, 64, 64 };
    int              wrong         = 0;

    memset(mach64->svga.vram, 0, 64u * 4u);
    write_yuyv_pair(mach64, SOURCE_OFFSET + 0, 100, 128, 100, 128);
    write_yuyv_pair(mach64, SOURCE_OFFSET + 4, 100, 192, 100, 128);

    mach64->dp_pix_width  = 0xb0000606u; /* SCALE=YUYV, DST=ARGB8888 */
    mach64->dst_y_x       = 0;
    mach64->sc_left_right = 0x00030000u;
    mach64->sc_top_bottom = 0x00000000u;

    wrong += write_scaler_register(mach64, 0x1c0, SOURCE_OFFSET);
    wrong += write_scaler_register(mach64, 0x1dc, 4);
    wrong += write_scaler_register(mach64, 0x1e0, 1);
    wrong += write_scaler_register(mach64, 0x1ec, 4);
    wrong += write_scaler_register(mach64, 0x1f0, 0x00010000u);
    wrong += write_scaler_register(mach64, 0x1f4, 0x00010000u);
    wrong += write_scaler_register(mach64, 0x1f8, 0);
    wrong += write_scaler_register(mach64, 0x3c8, 0);
    wrong += write_scaler_register(mach64, 0x3d8, 0x00008000u);
    wrong += write_scaler_register(mach64, 0x3e0, 0);
    wrong += write_scaler_register(mach64, 0x1fc, 0x00000040u);

    if (!mach64_3d_write(mach64, 0x118, 0x00040001u,
                         FIFO_WRITE_DWORD)) {
        fprintf(stderr, "YUYV scaler destination trigger was not claimed\n");
        wrong++;
    }

    for (int x = 0; x < 4; x++) {
        uint32_t expected = argb_from_centered_yuv(100, expected_u[x], 0);
        uint32_t actual   = *pixel32(mach64, (uint32_t) x * 4u);

        if (actual != expected) {
            fprintf(stderr,
                    "YUYV pixel %d: expected %08x, got %08x\n",
                    x, expected, actual);
            wrong++;
        }
    }
    return wrong;
}

static int
run_apple_yuv_test(mach64_t *mach64)
{
    int            wrong    = 0;
    const uint32_t expected = argb_from_centered_yuv(100, 0, 0);

    memset(mach64->svga.vram, 0, 64u * 4u);
    write_yuyv_pair(mach64, SOURCE_OFFSET + 0, 100, 0, 100, 0);
    write_yuyv_pair(mach64, SOURCE_OFFSET + 4, 100, 0, 100, 0);

    wrong += write_scaler_register(mach64, 0x1f8, 0);
    wrong += write_scaler_register(mach64, 0x3c8, 0);
    wrong += write_scaler_register(mach64, 0x3e0, 0);
    wrong += write_scaler_register(mach64, 0x1fc, 0x00000440u);

    if (!mach64_3d_write(mach64, 0x118, 0x00040001u,
                         FIFO_WRITE_DWORD)) {
        fprintf(stderr, "APPLE YUV scaler destination trigger was not claimed\n");
        wrong++;
    }

    for (int x = 0; x < 4; x++) {
        uint32_t actual = *pixel32(mach64, (uint32_t) x * 4u);

        if (actual != expected) {
            fprintf(stderr,
                    "APPLE YUV pixel %d: expected %08x, got %08x\n",
                    x, expected, actual);
            wrong++;
        }
    }
    return wrong;
}

static int
run_scaler_color_compare_test(mach64_t *mach64)
{
    const uint32_t source      = 0x00ff0000u;
    const uint32_t destination = 0x0000ff00u;
    int            wrong       = 0;

    *pixel32(mach64, SOURCE_OFFSET) = source;
    *pixel32(mach64, 0)             = destination;

    mach64->dp_pix_width  = 0x60000606u; /* SCALE=ARGB8888, DST=ARGB8888 */
    mach64->dp_src        = 0x00000500u;
    mach64->dp_mix        = 0x00070007u;
    mach64->write_mask    = 0xffffffffu;
    mach64->dst_off_pitch = 0x02000000u; /* 64 pixels */
    mach64->dst_y_x       = 0;
    mach64->dst_cntl      = DST_X_DIR | DST_Y_DIR;
    mach64->sc_left_right = 0;
    mach64->sc_top_bottom = 0;

    /* GT CLR_CMP_SRC=2 selects the scaler source.  Put ignored bits in the
     * key to verify that the comparison mask applies to CLR_CMP_CLR too. */
    mach64->clr_cmp_clr  = 0xaaff0000u;
    mach64->clr_cmp_mask = 0x00ffffffu;
    mach64->clr_cmp_cntl = 0x02000005u;

    wrong += write_scaler_register(mach64, 0x1c0, SOURCE_OFFSET);
    wrong += write_scaler_register(mach64, 0x1dc, 1);
    wrong += write_scaler_register(mach64, 0x1e0, 1);
    wrong += write_scaler_register(mach64, 0x1ec, 1);
    wrong += write_scaler_register(mach64, 0x1f0, 0);
    wrong += write_scaler_register(mach64, 0x1f4, 0);
    wrong += write_scaler_register(mach64, 0x1f8, 0);
    wrong += write_scaler_register(mach64, 0x3c8, 0);
    wrong += write_scaler_register(mach64, 0x1fc, 0x00000140u);

    if (!mach64_3d_write(mach64, 0x118, 0x00010001u,
                         FIFO_WRITE_DWORD)) {
        fprintf(stderr, "color-key scaler destination trigger was not claimed\n");
        wrong++;
    }

    if (*pixel32(mach64, 0) != destination) {
        fprintf(stderr,
                "CLR_CMP_SRC=2 failed: expected inhibited destination %08x, got %08x\n",
                destination, *pixel32(mach64, 0));
        wrong++;
    }

    mach64->clr_cmp_cntl = 0;
    return wrong;
}

static int
run_scaler_visibility_test(mach64_t *mach64)
{
    const uint32_t key         = 0x00ff0000u;
    const uint32_t other       = 0x0000ff00u;
    const uint32_t destination = 0x000000ffu;
    int            wrong       = 0;

    /* The top-left contributor is keyed red.  With 12/16 horizontal and
     * vertical fractions the nearest contributor is bottom-right green. */
    *pixel32(mach64, SOURCE_OFFSET + 0u)  = key;
    *pixel32(mach64, SOURCE_OFFSET + 4u)  = other;
    *pixel32(mach64, SOURCE_OFFSET + 8u)  = other;
    *pixel32(mach64, SOURCE_OFFSET + 12u) = other;

    mach64->dp_pix_width  = 0x60000606u; /* SCALE=ARGB8888, DST=ARGB8888 */
    mach64->dp_src        = 0x00000500u;
    mach64->dp_mix        = 0x00070007u;
    mach64->write_mask    = 0xffffffffu;
    mach64->dst_off_pitch = 0x02000000u;
    mach64->dst_y_x       = 0;
    mach64->dst_cntl      = DST_X_DIR | DST_Y_DIR;
    mach64->sc_left_right = 0;
    mach64->sc_top_bottom = 0;
    mach64->clr_cmp_clr   = key;
    mach64->clr_cmp_mask  = 0x00ffffffu;
    mach64->clr_cmp_cntl  = 0x02000005u;

    wrong += write_scaler_register(mach64, 0x1c0, SOURCE_OFFSET);
    wrong += write_scaler_register(mach64, 0x1dc, 2);
    wrong += write_scaler_register(mach64, 0x1e0, 2);
    wrong += write_scaler_register(mach64, 0x1ec, 2);
    wrong += write_scaler_register(mach64, 0x1f0, 0);
    wrong += write_scaler_register(mach64, 0x1f4, 0);
    wrong += write_scaler_register(mach64, 0x1f8, 0x0000c000u);
    wrong += write_scaler_register(mach64, 0x3c8, 0x0000c000u);

    /* bit 9 clear: all four source pixels take part in chroma-key visibility. */
    *pixel32(mach64, 0) = destination;
    wrong += write_scaler_register(mach64, 0x1fc, 0x00000040u);
    if (!mach64_3d_write(mach64, 0x118, 0x00010001u,
                         FIFO_WRITE_DWORD)) {
        fprintf(stderr, "all-source visibility trigger was not claimed\n");
        wrong++;
    }
    if (*pixel32(mach64, 0) != destination) {
        fprintf(stderr,
                "NEAREST_TEX_VIS=0 failed: expected inhibited %08x, got %08x\n",
                destination, *pixel32(mach64, 0));
        wrong++;
    }

    /* bit 9 set: only bottom-right, the nearest source pixel, is tested. */
    *pixel32(mach64, 0) = destination;
    wrong += write_scaler_register(mach64, 0x1f8, 0x0000c000u);
    wrong += write_scaler_register(mach64, 0x3c8, 0x0000c000u);
    wrong += write_scaler_register(mach64, 0x1fc, 0x00000240u);
    if (!mach64_3d_write(mach64, 0x118, 0x00010001u,
                         FIFO_WRITE_DWORD)) {
        fprintf(stderr, "nearest-source visibility trigger was not claimed\n");
        wrong++;
    }
    if (*pixel32(mach64, 0) == destination) {
        fprintf(stderr,
                "NEAREST_TEX_VIS=1 failed: nearest unkeyed source was inhibited\n");
        wrong++;
    }

    mach64->clr_cmp_cntl = 0;
    return wrong;
}

/*
 * ATI's DirectDraw MPEG motion compensation on a packed surface (macxdd32
 * 4.11): DP_PIX_WIDTH C00000CCh, YVYU 422 in and out, scaled 1:1. A copy
 * keeps every byte, SCALE_3D_CNTL 98A40h adds the error block ONE/ONE with
 * saturation, and DP_MIX 17h averages the two predictions byte by byte.
 */
static int
yuv422_mc_blit(mach64_t *mach64, uint32_t src, uint32_t dst, uint32_t cntl, uint32_t mix)
{
    int wrong = 0;

    mach64->dp_pix_width  = 0xc00000ccu;
    mach64->dp_src        = 0x00000505u;
    mach64->dp_mix        = mix;
    mach64->write_mask    = 0xffffffffu;
    mach64->clr_cmp_cntl  = 0;
    mach64->dst_off_pitch = (1u << 22) | (dst >> 3); /* 8 pixels of 2 bytes */
    mach64->dst_y_x       = 0;
    mach64->dst_cntl      = DST_X_DIR | DST_Y_DIR;
    mach64->sc_left_right = 0x0fff0000u;
    mach64->sc_top_bottom = 0x3fff0000u;

    wrong += write_scaler_register(mach64, 0x1c0, src);
    wrong += write_scaler_register(mach64, 0x1dc, 4);
    wrong += write_scaler_register(mach64, 0x1e0, 1);
    wrong += write_scaler_register(mach64, 0x1ec, 8);
    wrong += write_scaler_register(mach64, 0x1f0, 0x00010000u);
    wrong += write_scaler_register(mach64, 0x1f4, 0x00010000u);
    wrong += write_scaler_register(mach64, 0x1f8, 0);
    wrong += write_scaler_register(mach64, 0x3c8, 0);
    wrong += write_scaler_register(mach64, 0x3d8, 0x00008000u);
    wrong += write_scaler_register(mach64, 0x3e0, 0);
    wrong += write_scaler_register(mach64, 0x1fc, cntl);
    if (!mach64_3d_write(mach64, 0x118, 0x00040001u, FIFO_WRITE_DWORD)) {
        fprintf(stderr, "YUV 422 MC destination trigger was not claimed\n");
        wrong++;
    }
    return wrong;
}

static int
run_yuv422_motion_comp_test(mach64_t *mach64)
{
    static const uint8_t ref[8]      = { 16, 90, 235, 200, 120, 30, 60, 250 };
    static const uint8_t error[8]    = { 10, 10, 30, 70, 0, 255, 5, 10 };
    static const uint8_t ref2[8]     = { 20, 100, 135, 101, 121, 32, 61, 249 };
    const uint32_t       src         = 0x10000;
    const uint32_t       err         = 0x10100;
    const uint32_t       src2        = 0x10200;
    const uint32_t       dst         = 0x20000;
    int                  wrong       = 0;
    uint8_t              expected[8];

    memcpy(mach64->svga.vram + src, ref, 8);
    memcpy(mach64->svga.vram + err, error, 8);
    memcpy(mach64->svga.vram + src2, ref2, 8);
    memset(mach64->svga.vram + dst, 0x55, 8);

    /* The prediction: a byte-exact copy. */
    wrong += yuv422_mc_blit(mach64, src, dst, 0x00000040u, 0x00070007u);
    if (memcmp(mach64->svga.vram + dst, ref, 8)) {
        fprintf(stderr, "YUV 422 MC copy: got %02x %02x %02x %02x %02x %02x %02x %02x\n", mach64->svga.vram[dst],
                mach64->svga.vram[dst + 1], mach64->svga.vram[dst + 2], mach64->svga.vram[dst + 3], mach64->svga.vram[dst + 4],
                mach64->svga.vram[dst + 5], mach64->svga.vram[dst + 6], mach64->svga.vram[dst + 7]);
        wrong++;
    }

    /* The error block, added ONE/ONE with saturation. */
    wrong += yuv422_mc_blit(mach64, err, dst, 0x00098a40u, 0x00070003u);
    for (int i = 0; i < 8; i++)
        expected[i] = (ref[i] + error[i] > 255) ? 255 : (uint8_t) (ref[i] + error[i]);
    if (memcmp(mach64->svga.vram + dst, expected, 8)) {
        for (int i = 0; i < 8; i++)
            fprintf(stderr, "YUV 422 MC add byte %d: expected %02x, got %02x\n", i, expected[i], mach64->svga.vram[dst + i]);
        wrong++;
    }

    /* A bidirectional prediction: the second reference averaged in. */
    memcpy(mach64->svga.vram + dst, ref, 8);
    wrong += yuv422_mc_blit(mach64, src2, dst, 0x00000040u, 0x00170017u);
    for (int i = 0; i < 8; i++)
        expected[i] = (uint8_t) ((ref[i] + ref2[i]) >> 1);
    if (memcmp(mach64->svga.vram + dst, expected, 8)) {
        for (int i = 0; i < 8; i++)
            fprintf(stderr, "YUV 422 MC average byte %d: expected %02x, got %02x\n", i, expected[i], mach64->svga.vram[dst + i]);
        wrong++;
    }
    return wrong;
}

/* XL DP_PIX_WIDTH: RGB8 intensity uses green on writes; packed 24-bit
   destinations occupy three bytes and may not overwrite their neighbour. */
static int
run_scaler_intensity_and_24_test(mach64_t *m)
{
    int wrong = 0;
    m->dp_src = 0x500;
    m->dp_mix = 0x70007;
    m->write_mask = ~0u;
    m->dst_off_pitch = 1u << 22;
    m->dst_y_x = 0;
    m->dst_cntl = DST_X_DIR | DST_Y_DIR;
    m->sc_left_right = 7u << 16;
    m->sc_top_bottom = 0;
    m->clr_cmp_cntl = 0;
    wrong += write_scaler_register(m, 0x1c0, SOURCE_OFFSET);
    wrong += write_scaler_register(m, 0x1dc, 2);
    wrong += write_scaler_register(m, 0x1e0, 1);
    wrong += write_scaler_register(m, 0x1ec, 8);
    wrong += write_scaler_register(m, 0x1f0, 0x10000);
    wrong += write_scaler_register(m, 0x1f4, 0x10000);
    wrong += write_scaler_register(m, 0x1fc, 0x140);
    for (unsigned mode = 0; mode < 3; mode++) {
        memset(m->svga.vram, 0x7e, 32);
        *pixel32(m, SOURCE_OFFSET) = 0x0012ab34;
        *pixel32(m, SOURCE_OFFSET + 4) = 0x00ed5607;
        m->dp_pix_width = mode == 0 ? 0x60000005 : mode == 1 ? 0x60000009 : 0x90000006;
        if (mode == 2) {
            m->svga.vram[SOURCE_OFFSET] = 17;
            m->svga.vram[SOURCE_OFFSET+1] = 231;
        }
        wrong += write_scaler_register(m, 0x1f8, 0);
        wrong += write_scaler_register(m, 0x3c8, 0);
        wrong += !mach64_3d_write(m, 0x118, 0x00020001, FIFO_WRITE_DWORD);
        if (mode == 0) {
            const uint8_t expected[] = { 0x34,0xab,0x12,0x07,0x56,0xed,0x7e };
            wrong += memcmp(m->svga.vram, expected, sizeof(expected)) != 0;
        } else if (mode == 1)
            wrong += m->svga.vram[0] != 0xab || m->svga.vram[1] != 0x56 || m->svga.vram[2] != 0x7e;
        else
            wrong += *pixel32(m,0) != 0x00111111 || *pixel32(m,4) != 0x00e7e7e7;
    }
    if (wrong) fprintf(stderr, "scaler intensity/24-bit: %d failures\n", wrong);
    return wrong;
}

static void
scaler_tests(void)
{
    mach64_t *mach64 = card_create();

    failures += run_rgb_scaler_test(mach64);
    failures += run_scaler_intensity_and_24_test(mach64);
    failures += run_yuyv_scaler_test(mach64);
    failures += run_apple_yuv_test(mach64);
    failures += run_scaler_color_compare_test(mach64);
    failures += run_scaler_visibility_test(mach64);
    failures += run_yuv422_motion_comp_test(mach64);
    card_close(mach64);
}

static void
xl_scaler_alias_tests(void)
{
    mach64_t *m = card_create();
    m->ragepro = 1;
    m->idct = mach64_idct_init();
    m->dp_pix_width = 0x80000808;
    m->dp_src = 0x500;
    m->dp_mix = 0x70007;
    m->write_mask = ~0u;
    m->dst_off_pitch = 1u << 22;
    m->dst_cntl = DST_X_DIR | DST_Y_DIR;
    m->sc_left_right = 7u << 16;
    m->sc_top_bottom = 7u << 16;
    for (unsigned i = 0; i < 64; i++) m->svga.vram[SOURCE_OFFSET+i] = 40+i;
    write_scaler_register(m, 0x1dc, 8);
    write_scaler_register(m, 0x1e0, 8);
    write_scaler_register(m, 0x1f0, 0x10000);
    write_scaler_register(m, 0x1f4, 0x10000);
    write_scaler_register(m, 0x1fc, 0x140);
    write_scaler_register(m, RP_SCALE_PITCH_BOTH, 0x00080008);
    write_scaler_register(m, RP_SCALE_OFF_ACC, SOURCE_OFFSET);
    write_scaler_register(m, RP_SCALE_DST_Y_X, 0x00010002);
    write_scaler_register(m, XL_SCALE_DST_HEIGHT_WIDTH, 0x00020002);
    for (unsigned y = 0; y < 2; y++)
        for (unsigned x = 0; x < 2; x++)
            if (m->svga.vram[(y+2)*8+x+1] != 40+y*8+x) {
                fprintf(stderr, "XL native scaler launch: wrong pixel %u,%u\n",x,y);
                failures++;
            }
    mach64_idct_close(m->idct);
    m->idct = NULL;
    card_close(m);
}

/* Native DVD submits Y/U/V rectangles before its six coefficient blocks. */
static void
xl_idct_reconstruction_test(void)
{
    mach64_t *m = card_create();
    mach64_3d_t *ctx = m->gt3d;
    m->ragepro = 1;
    m->idct = mach64_idct_init();
    m->rp.gui_cntl = 1u << 16;
    m->dp_pix_width = 0x80080888;
    m->dp_src = 0x500;
    m->dp_mix = 0x70007;
    m->write_mask = ~0u;
    m->dst_cntl = DST_X_DIR | DST_Y_DIR;
    m->sc_left_right = m->sc_top_bottom = 31u << 16;
    ctx->regs[SCALE_WIDTH >> 2] = ctx->regs[SCALE_HEIGHT >> 2] = 16;
    ctx->regs[SCALE_Y_PITCH >> 2] = 16;
    ctx->regs[SCALE_3D_CNTL >> 2] = 0x8240;
    ctx->regs[RP_ALPHA_TST_CNTL >> 2] = RP_IDCT_EN;
    mach64_idct_bm_write(m, 0x3bc, 0x420);
    for (unsigned plane = 0; plane < 3; plane++) {
        m->dst_off_pitch = ((plane ? 1u : 2u) << 22) | (plane * 512 / 8);
        m->dst_height_width = plane ? 0x00080008 : 0x00100010;
        mach64_scaler_submit(ctx);
    }
    expect_u32("IDCT waits for coefficients", m->svga.vram[0], 0);
    const int dc[] = {-568, -552, -408, -248, 80, -80};
    for (unsigned block = 0; block < 6; block++) {
        mach64_idct_bm_write(m, 0x380, 0x00ffffff);
        mach64_idct_bm_write(m, 0x384, 0);
        mach64_idct_bm_write(m, 0x384, (uint32_t)(uint16_t)dc[block] << 16);
        if (block < 3)
            expect_u32("IDCT waits for whole Y rectangle", m->svga.vram[0], 0);
    }
    for (unsigned y = 0; y < 16; y++)
        for (unsigned x = 0; x < 16; x++) {
            const unsigned expected[] = {57, 59, 77, 97};
            expect_u32("IDCT Y quadrant", m->svga.vram[y*16+x], expected[(y/8)*2+x/8]);
        }
    expect_u32("IDCT U bias", m->svga.vram[512], 138);
    expect_u32("IDCT V bias", m->svga.vram[1024], 118);
    expect_u32("IDCT restores latest destination", m->dst_off_pitch, (1u << 22) | 128);
    mach64_idct_close(m->idct);
    m->idct = NULL;
    card_close(m);
}

/* ATID3DR3's palette upload: Y8 samples 32 bytes apart, bilinearly
   filtered over two rows, with destination writes disabled. */
static void
ragepro_palette_upload_tests(void)
{
    mach64_t *m = card_create();
    mach64_3d_t *ctx = m->gt3d;
    const uint32_t staging = 0x20800;

    m->ragepro = 1;
    m->dp_pix_width = 0x80000003;
    m->dp_src = 0x505;
    m->dp_mix = 0x70007;
    m->write_mask = 0;
    m->dst_off_pitch = 0x10000000;
    m->dst_cntl = 0x8e023;
    m->sc_left_right = 0x1fff0000;
    m->sc_top_bottom = 0x3fff0000;
    for (unsigned pass = 0; pass < 2; pass++) {
        for (unsigned i = 0; i < 128; i++) {
            uint16_t low = ((i + pass) << 8) | i;
            uint16_t high = 0xffffu ^ low;
            uint16_t words[] = {low, low, high, high, high, high, low, low};
            memcpy(m->svga.vram + staging + i * 8, words, 8);
            memcpy(m->svga.vram + staging + 0x400 + i * 8, words + 4, 8);
        }
        mach64_3d_write(m, SCALE_3D_CNTL, 0x40, FIFO_WRITE_DWORD);
        mach64_3d_write(m, RP_TEX_CNTL, 0x40800000, FIFO_WRITE_DWORD);
        mach64_3d_write(m, SCALE_Y_OFF, staging + 4, FIFO_WRITE_DWORD);
        mach64_3d_write(m, SCALE_Y_PITCH, 0x3fc, FIFO_WRITE_DWORD);
        mach64_3d_write(m, SCALE_WIDTH, 0x400, FIFO_WRITE_DWORD);
        mach64_3d_write(m, SCALE_HEIGHT, 2, FIFO_WRITE_DWORD);
        mach64_3d_write(m, SCALE_HACC, 0, FIFO_WRITE_DWORD);
        mach64_3d_write(m, SCALE_VACC, 0x8000, FIFO_WRITE_DWORD);
        mach64_3d_write(m, SCALE_X_INC, 0x200000, FIFO_WRITE_DWORD);
        mach64_3d_write(m, SCALE_Y_INC, 0x10000, FIFO_WRITE_DWORD);
        mach64_3d_write(m, 0x118, 0x200001, FIFO_WRITE_DWORD);
        mach64_3d_write(m, RP_TEX_CNTL, RP_TEX_CACHE_FLUSH, FIFO_WRITE_DWORD);
        for (unsigned i = 0; i < 256; i++) {
            uint16_t packed = (((i & 127) + pass) << 8) | (i & 127);
            if (i >= 128)
                packed ^= 0xffff;
            uint32_t r = (packed >> 11) & 31, g = (packed >> 5) & 63, b = packed & 31;
            uint32_t expected = ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
            expect_u32("driver palette upload", ctx->texture_palette[i], expected);
            m->svga.vram[0x30000 + i] = i;
            mach64_3d_texture_t map = {.base = 0x30000, .width = 256, .bpp = 1, .format = 2};
            int inhibits;
            expect_u32("uploaded CI8 texel", mach64_3d_texel_at(ctx, &map, i, 0, &inhibits), 0xff000000 | expected);
            test_agp[i] = i;
            map.base = 0;
            expect_u32("uploaded AGP CI8 texel", mach64_rp_texel_agp(ctx, &ctx->agp, &map, i, 0, &inhibits), 0xff000000 | expected);
        }
        expect_u32("palette upload leaves destination intact", *pixel32(m, 0), 0);
    }
    /* Ordinary scaler reads and older cards must not load texture RAM. */
    memset(m->svga.vram + staging, 0, 0x800);
    uint32_t saved[256];
    memcpy(saved, ctx->texture_palette, sizeof(saved));
    for (unsigned i = 0; i < 0x800; i += 32)
        (void) mach64_scaler_read_source(ctx, 8, staging, 0x400, i, 0);
    CHECK(!memcmp(saved, ctx->texture_palette, sizeof(saved)));
    m->ragepro = 0;
    ctx->regs[RP_TEX_CNTL >> 2] = RP_TEX_CACHE_SIZE_SELECT;
    for (unsigned i = 0; i < 0x800; i += 32)
        (void) mach64_scaler_read_source(ctx, 8, staging, 0x400, i, 0);
    CHECK(!memcmp(saved, ctx->texture_palette, sizeof(saved)));
    m->ragepro = 1;
    uint32_t value;
    mach64_3d_write(m, RP_TEX_PALETTE_INDEX, 0xfffffc00u, FIFO_WRITE_DWORD);
    CHECK(mach64_3d_read(m, RP_TEX_PALETTE_INDEX, &value));
    expect_u32("palette index masks reserved bits", value, 0);
    for (unsigned i = 0; i < 1024; i++) {
        unsigned entry = (i & 0x1ff) >> 2;
        uint16_t packed = ((entry + 1) << 8) | entry;
        if (((i >> 1) ^ (i >> 9)) & 1)
            packed ^= 0xffff;
        CHECK(mach64_3d_read(m, TEX_PALETTE, &value));
        expect_u32("palette register reads uploaded RGB565 word", value, packed);
    }
    CHECK(mach64_3d_read(m, RP_TEX_PALETTE_INDEX, &value));
    expect_u32("palette index wraps after 1024 reads", value, 0);

    mach64_3d_write(m, RP_TEX_PALETTE_INDEX, 0x34, FIFO_WRITE_BYTE);
    mach64_3d_write(m, RP_TEX_PALETTE_INDEX + 1, 0xfe, FIFO_WRITE_BYTE);
    mach64_3d_write(m, TEX_PALETTE, 0x9a123456, FIFO_WRITE_DWORD);
    CHECK(mach64_3d_read(m, RP_TEX_PALETTE_INDEX, &value));
    expect_u32("palette index accepts byte writes and is stable on read", value, 0x234);
    CHECK(!memcmp(saved, ctx->texture_palette, sizeof(saved)));
    CHECK(mach64_3d_read(m, TEX_PALETTE, &value));
    expect_u32("read-only palette RAM word survives write", value, 0xf1f2);
    CHECK(mach64_3d_read(m, RP_TEX_PALETTE_INDEX, &value));
    expect_u32("palette read increments the index once", value, 0x235);

    m->ragepro = 0;
    mach64_3d_write(m, S_X_INC2, 7, FIFO_WRITE_DWORD);
    CHECK(mach64_3d_read(m, S_X_INC2, &value));
    expect_u32("older chip keeps S_X_INC2 at 0x340", value, 7);
    mach64_3d_write(m, TEX_PALETTE, 0x9a123456, FIFO_WRITE_DWORD);
    expect_u32("packed palette keeps its explicit index", ctx->texture_palette[0x9a], 0x123456);
    card_close(m);
}

/* J5.30 alternates palette-cache uploads with CI8 scaler expansion for
   its display-property controls. The offset/accumulator alias must replace
   that upload's source and fractions, including split register writes. */
static void
ragepro_scaler_offset_acc_tests(void)
{
    mach64_t *m = card_create();
    mach64_3d_t *ctx = m->gt3d;
    const uint32_t source = 0x18000;
    const uint32_t colors[] = { 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffffff };

    m->ragepro = 1;
    m->dp_pix_width = 0x20000006;
    m->dp_src = 0x500;
    m->dp_mix = 0x70007;
    m->write_mask = ~0u;
    m->dst_off_pitch = 0x02000000; /* 64 pixels */
    m->dst_y_x = 0;
    m->dst_cntl = DST_X_DIR | DST_Y_DIR;
    m->sc_left_right = 0x003f0000;
    m->sc_top_bottom = 0x003f0000;
    ctx->regs[SCALE_3D_CNTL >> 2] = 0x140;
    ctx->regs[SCALE_Y_PITCH >> 2] = 4;
    ctx->regs[SCALE_WIDTH >> 2] = 4;
    ctx->regs[SCALE_HEIGHT >> 2] = 2;
    ctx->regs[SCALE_X_INC >> 2] = 0x10000;
    ctx->regs[SCALE_Y_INC >> 2] = 0x10000;
    for (unsigned i = 0; i < 4; i++)
        ctx->texture_palette[0x10 + i] = colors[i];
    for (unsigned i = 0; i < 8; i++)
        m->svga.vram[source + i] = 0x10 + (i & 3);

    for (unsigned bytes = 1; bytes <= 4; bytes *= 2) {
        uint32_t type = bytes == 1 ? FIFO_WRITE_BYTE : bytes == 2 ? FIFO_WRITE_WORD : FIFO_WRITE_DWORD;
        ctx->regs[SCALE_Y_OFF >> 2] = 0x20804; /* stale palette staging */
        ctx->regs[SCALE_HACC >> 2] = 0x8000;
        ctx->regs[SCALE_VACC >> 2] = 0x18000;
        memset(m->svga.vram, 0xa5, 64 * 2 * 4);
        for (unsigned offset = 0; offset < 4; offset += bytes)
            CHECK(mach64_3d_write(m, RP_SCALE_OFF_ACC + offset, source >> (offset * 8), type));
        expect_u32("scaler alias source", ctx->regs[SCALE_Y_OFF >> 2], source);
        expect_u32("scaler alias horizontal restart", ctx->regs[SCALE_HACC >> 2], 0);
        expect_u32("scaler alias vertical restart", ctx->regs[SCALE_VACC >> 2], 0);
        CHECK(mach64_3d_write(m, 0x118, 0x00040002, FIFO_WRITE_DWORD));
        for (unsigned y = 0; y < 2; y++)
            for (unsigned x = 0; x < 4; x++)
                expect_u32("CI8 control bitmap", *pixel32(m, (y * 64 + x) * 4), colors[x]);
    }
    card_close(m);
}

static void
ragepro_ci8_tiling_tests(void)
{
    mach64_t *m = card_create();
    mach64_3d_t *ctx = m->gt3d;
    mach64_rp_map_t map = {0};
    /* Fixed coordinate-bit positions from a byte-for-byte comparison of
       the driver's 256x256 AGP texture upload and its source bitmap. */
    const unsigned xb[] = {0, 1, 3, 6, 8, 10, 12, 14};
    const unsigned yb[] = {2, 4, 5, 7, 9, 11, 13, 15};

    m->ragepro = 1;
    mach64_rp_map_level(ctx, &map, 2, 0x30000, 8, 8, 0, 0);
    mach64_rp_map_tiled(&map);
    for (unsigned i = 0; i < 256; i++)
        ctx->texture_palette[i] = i * 0x010101;
    for (unsigned y = 0; y < 256; y++)
        for (unsigned x = 0; x < 256; x++) {
            unsigned addr = ((x >> 6) & 1) << 11;
            for (unsigned b = 0; b < 8; b++)
                addr ^= (((x >> b) & 1) << xb[b]) | (((y >> b) & 1) << yb[b]);
            uint8_t texel = (x * 17 + y * 31) & 255;
            m->svga.vram[0x30000 + addr] = texel;
            test_agp[addr] = texel;
            expect_u32("CI8 tiled address", map.tile_u[x] ^ map.tile_v[y], addr);
        }
    for (int agp = 0; agp < 2; agp++) {
        map.agp = agp;
        map.tex.base = agp ? 0 : 0x30000;
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) {
                uint32_t expected = 0xff000000 | (((x * 17 + y * 31) & 255) * 0x010101);
                int inhibits;
                expect_u32("CI8 tiled texel", mach64_rp_texel(ctx, &ctx->agp, &map, x, y, &inhibits), expected);
            }
    }
    card_close(m);
}

/*
 * VQ textures. The codebook goes in as ATI's driver loads a palette (a Y8
 * scaler pass over 0x800 staged bytes), the index maps are written to video
 * and AGP memory, and the texels come out of the Rage Pro fetch path the
 * triangle uses. Expected texels are worked out here from the layout: a
 * codebook block is four 16-bit texels, upper left, upper right, lower left,
 * lower right; an index covers the 2x2 texels at (2x, 2y).
 */
static uint16_t
vq_block_texel(unsigned block, unsigned k)
{
    return (uint16_t) (block * 0x0101u + k * 0x2467u + (block >> 3) * 0x0840u);
}

static uint8_t
vq_index(unsigned x, unsigned y)
{
    return (uint8_t) (x * 29 + y * 7 + 3);
}

/*
 * The 0x800 byte image ATI3DCIF stages for a codebook: for k below 128, offset 8k
 * holds the upper two words of block k and the lower two of block k + 128, and
 * offset 0x400 + 8k the upper two of block k + 128 and the lower two of block k
 * (read from the driver; a real card reads the codebook back unswizzled, see
 * ragepro_vq_real_card_tests).
 */
static void
vq_driver_image(const uint8_t *codebook, uint8_t *image)
{
    for (unsigned k = 0; k < 128; k++) {
        memcpy(image + 8 * k, codebook + 8 * k, 4);
        memcpy(image + 8 * k + 4, codebook + 8 * (k + 128) + 4, 4);
        memcpy(image + 0x400 + 8 * k, codebook + 8 * (k + 128), 4);
        memcpy(image + 0x400 + 8 * k + 4, codebook + 8 * k + 4, 4);
    }
}

/* The codebook through the Y8 scaler pass, from vram at staging. */
static void
vq_load_codebook(mach64_t *m, uint32_t staging, const uint8_t *codebook)
{
    memcpy(m->svga.vram + staging, codebook, 0x800);
    m->dp_pix_width = 0x80000003;
    m->dp_src = 0x505;
    m->dp_mix = 0x70007;
    m->write_mask = 0;
    m->dst_off_pitch = 0x10000000;
    m->dst_cntl = 0x8e023;
    m->sc_left_right = 0x1fff0000;
    m->sc_top_bottom = 0x3fff0000;
    mach64_3d_write(m, SCALE_3D_CNTL, 0x40, FIFO_WRITE_DWORD);
    mach64_3d_write(m, RP_TEX_CNTL, 0x40800000, FIFO_WRITE_DWORD);
    mach64_3d_write(m, SCALE_Y_OFF, staging + 4, FIFO_WRITE_DWORD);
    mach64_3d_write(m, SCALE_Y_PITCH, 0x3fc, FIFO_WRITE_DWORD);
    mach64_3d_write(m, SCALE_WIDTH, 0x400, FIFO_WRITE_DWORD);
    mach64_3d_write(m, SCALE_HEIGHT, 2, FIFO_WRITE_DWORD);
    mach64_3d_write(m, SCALE_HACC, 0, FIFO_WRITE_DWORD);
    mach64_3d_write(m, SCALE_VACC, 0x8000, FIFO_WRITE_DWORD);
    mach64_3d_write(m, SCALE_X_INC, 0x200000, FIFO_WRITE_DWORD);
    mach64_3d_write(m, SCALE_Y_INC, 0x10000, FIFO_WRITE_DWORD);
    mach64_3d_write(m, 0x118, 0x200001, FIFO_WRITE_DWORD);
    mach64_3d_write(m, RP_TEX_CNTL, RP_TEX_CACHE_FLUSH, FIFO_WRITE_DWORD);
}

/* 0xAARRGGBB of a 16-bit texel, zero extended (SCALE_PIX_EXPAND clear). */
static uint32_t
vq_expected(unsigned format, uint16_t t)
{
    uint32_t r, g, b, a = 0xff;

    if (format == 4) {
        r = (t >> 11) << 3;
        g = ((t >> 5) & 63) << 2;
        b = (t & 31) << 3;
    } else if (format == 3) {
        a = (t & 0x8000) ? 0xff : 0;
        r = ((t >> 10) & 31) << 3;
        g = ((t >> 5) & 31) << 3;
        b = (t & 31) << 3;
    } else {
        a = (t >> 12) * 17;
        r = ((t >> 8) & 15) << 4;
        g = ((t >> 4) & 15) << 4;
        b = (t & 15) << 4;
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Where an index byte is for an 8 x 4 index map tiled as 8 bpp. This is an
   assumed layout: the tiled VQ cases stage data the way the reader expects
   it, so they check the reader against that assumption, not against a card.
   No real-card capture of a tiled VQ index map exists. */
static unsigned
vq_tiled_8x4(unsigned x, unsigned y)
{
    return (x & 1) | (((x >> 1) & 1) << 1) | ((y & 1) << 2) | (((x >> 2) & 1) << 3) | (((y >> 1) & 1) << 4);
}

static void
vq_texel_checks(mach64_t *m, const char *what, unsigned format, int tiled, int agp)
{
    mach64_3d_t        *ctx = m->gt3d;
    mach64_rp_texture_t tx;
    const uint32_t      base = 0x30000;

    /* A 16 x 8 texel VQ map: an 8 x 4 index map at TEX_4_OFF. */
    for (unsigned y = 0; y < 4; y++)
        for (unsigned x = 0; x < 8; x++) {
            unsigned off = tiled ? vq_tiled_8x4(x, y) : y * 8 + x;

            m->svga.vram[base + off] = vq_index(x, y);
            test_agp[0x100 + off]    = vq_index(x, y);
        }
    m->dp_pix_width = format << 28;
    ctx->regs[SCALE_3D_CNTL >> 2]        = MIP_MAP_DISABLE;
    ctx->regs[RP_TEX_CNTL >> 2]          = RP_TEX_VQ | (tiled ? RP_TEXTURE_TILING : 0) | (agp ? RP_TEX_SRC_AGP : 0);
    ctx->regs[TEX_SIZE_PITCH >> 2]       = 4 | (4 << 4) | (3 << 8);
    ctx->regs[(TEX_0_OFF + 16) >> 2]     = agp ? 0x100 : base;
    CHECK(mach64_rp_texture_init(ctx, &tx));
    expect_int("VQ one level", tx.levels, 1);
    memset(ctx->agp.tag, 0, sizeof(ctx->agp.tag));
    for (unsigned v = 0; v < 8; v++)
        for (unsigned u = 0; u < 16; u++) {
            int      inhibits;
            unsigned block = vq_index(u >> 1, v >> 1);
            char     name[96];

            snprintf(name, sizeof(name), "%s VQ texel %u,%u%s", what, u, v, tiled ? " (tiling: assumed layout, no card data)" : "");
            expect_u32(name, mach64_rp_texel(ctx, &ctx->agp, &tx.maps[0], u, v, &inhibits),
                       vq_expected(format, vq_block_texel(block, ((v & 1) << 1) | (u & 1))));
        }
    /* The nearest sample at a texel's center is that texel. */
    {
        int      inhibits;
        unsigned u = 9, v = 5;

        expect_u32("VQ sampled texel", mach64_rp_sample_map(ctx, &ctx->agp, &tx.maps[0], 0, (u + 0.5) / 16, (v + 0.5) / 8, &inhibits),
                   vq_expected(format, vq_block_texel(vq_index(u >> 1, v >> 1), ((v & 1) << 1) | (u & 1))));
    }
}

#include "vid_ati_mach64_vq_vectors.h"

/*
 * What a real Rage Pro fetched for a 64x64 VQ texture whose codebook words say which block and
 * word they are (vid_ati_mach64_vq_vectors.h, a VQ diagnostic): the guest stages the image
 * ATI3DCIF stages, and every texel of the 1:1 quad is compared with the real card's pixel.
 */
static void
ragepro_vq_real_card_tests(void)
{
    mach64_t           *m   = card_create();
    mach64_3d_t        *ctx = m->gt3d;
    mach64_rp_texture_t tx;
    uint8_t             codebook[0x800];
    uint8_t             image[0x800];
    int                 bad = 0;

    m->ragepro = 1;
    for (unsigned i = 0; i < 256; i++)
        for (unsigned p = 0; p < 4; p++) {
            unsigned word = ((i >> 3) << 11) | ((((i & 7) << 2 | p) << 1) << 5) | 21;

            codebook[i * 8 + p * 2]     = word & 255;
            codebook[i * 8 + p * 2 + 1] = word >> 8;
        }
    vq_driver_image(codebook, image);
    vq_load_codebook(m, 0x20800, image);
    for (unsigned n = 0; n < 1024; n++)
        m->svga.vram[0x30000 + n] = vq_a_seq[n & 255];
    m->dp_pix_width = 2u << 28;
    ctx->regs[SCALE_3D_CNTL >> 2]    = MIP_MAP_DISABLE;
    ctx->regs[RP_TEX_CNTL >> 2]      = RP_TEX_VQ;
    ctx->regs[TEX_SIZE_PITCH >> 2]   = 6 | (6 << 4) | (6 << 8);
    ctx->regs[(TEX_0_OFF + 24) >> 2] = 0x30000;
    CHECK(mach64_rp_texture_init(ctx, &tx));
    for (unsigned y = 0; y < 64; y++)
        for (unsigned x = 0; x < 64; x++) {
            int      inhibits;
            uint32_t argb = mach64_rp_texel(ctx, &ctx->agp, &tx.maps[0], x, y, &inhibits);
            unsigned p555 = (((argb >> 19) & 31) << 10) | (((argb >> 11) & 31) << 5) | ((argb >> 3) & 31);

            if (p555 != vq_a_real_pixels[y * 64 + x])
                bad++;
        }
    expect_int("VQ texels differing from a real Rage Pro", bad, 0);
    card_close(m);
}

static void
ragepro_vq_tests(void)
{
    mach64_t *m = card_create();
    mach64_3d_t *ctx = m->gt3d;
    uint8_t codebook[0x800];
    static const unsigned formats[] = {4, 3, 15};

    m->ragepro = 1;
    for (unsigned b = 0; b < 256; b++)
        for (unsigned k = 0; k < 4; k++) {
            codebook[b * 8 + k * 2]     = vq_block_texel(b, k) & 255;
            codebook[b * 8 + k * 2 + 1] = vq_block_texel(b, k) >> 8;
        }
    {
        uint8_t image[0x800];

        vq_driver_image(codebook, image);
        vq_load_codebook(m, 0x20800, image);
        CHECK(!memcmp(ctx->texture_palette_ram, image, 0x800));
    }
    for (unsigned f = 0; f < 3; f++)
        for (int tiled = 0; tiled < 2; tiled++)
            for (int agp = 0; agp < 2; agp++)
                vq_texel_checks(m, "primary", formats[f], tiled, agp);
    /* A format that is not a 16-bit one leaves the codebook as RGB565. */
    {
        mach64_rp_texture_t tx;
        int                 inhibits;

        memset(m->svga.vram + 0x30000, 5, 32);
        m->dp_pix_width = 2u << 28;
        ctx->regs[(TEX_0_OFF + 16) >> 2] = 0x30000;
        ctx->regs[RP_TEX_CNTL >> 2]      = RP_TEX_VQ;
        CHECK(mach64_rp_texture_init(ctx, &tx));
        expect_u32("VQ default texel type", mach64_rp_texel(ctx, &ctx->agp, &tx.maps[0], 1, 1, &inhibits),
                   vq_expected(4, vq_block_texel(5, 3)));
    }
    /* Two mip levels: the smaller map has an index map of its own. */
    {
        mach64_rp_texture_t tx;
        int                 inhibits;

        for (unsigned i = 0; i < 8; i++)
            m->svga.vram[0x34000 + i] = 200 + i;
        m->dp_pix_width = 4u << 28;
        ctx->regs[SCALE_3D_CNTL >> 2]    = 0;
        ctx->regs[(TEX_0_OFF + 16) >> 2] = 0x30000;
        ctx->regs[(TEX_0_OFF + 12) >> 2] = 0x34000;
        ctx->regs[RP_TEX_CNTL >> 2]      = RP_TEX_VQ;
        /* a 16x16 map: the levels follow the size field, and 15:12 names the smallest (TEX_3) */
        ctx->regs[TEX_SIZE_PITCH >> 2]   = 4 | (4 << 4) | (4 << 8) | (3 << 12);
        CHECK(mach64_rp_texture_init(ctx, &tx));
        expect_int("VQ two levels", tx.levels, 2);
        expect_u32("VQ level 1 texel", mach64_rp_texel(ctx, &ctx->agp, &tx.maps[1], 5, 3, &inhibits),
                   vq_expected(4, vq_block_texel(200 + 1 * 4 + 2, 3)));
    }
    /* Without TEX_VQ the same bytes are an ordinary 565 map. */
    {
        mach64_rp_texture_t tx;
        int                 inhibits;

        m->dp_pix_width = 4u << 28;
        ctx->regs[SCALE_3D_CNTL >> 2] = MIP_MAP_DISABLE;
        ctx->regs[RP_TEX_CNTL >> 2]   = 0;
        CHECK(mach64_rp_texture_init(ctx, &tx));
        CHECK(!tx.maps[0].vq);
        expect_u32("non-VQ map is not decoded as VQ", mach64_rp_texel(ctx, &ctx->agp, &tx.maps[0], 0, 0, &inhibits),
                   vq_expected(4, m->svga.vram[0x30000] | (m->svga.vram[0x30001] << 8)));
    }
    card_close(m);
}

/*
 * Rage Pro 16 bpp dither and Gouraud color, against a real card.
 *
 * The expected pixels come from raw frame buffer dumps of a PCI Rage Pro
 * (1002:4750, ATI driver 5.30, 3DCIF 4.11.1.2692) drawing, into a 640x480 555
 * surface, flat gray 16x16 patches (gray 0..255, 32 per row from (64,32)),
 * and a gray ramp over the full width and one over the full height. With the
 * dither on, a component v comes out as min(31, (scale[v] + rank) >> 4):
 * the tables below are the scale, 9 bits per v, read out of the patches (each
 * v shows two outputs across the 16 ranks, and where they change is scale[v]
 * mod 16), and the rank is a 4 by 8 table at the absolute screen X and Y.
 * Dither off, a component is v >> 3. The ramps show the interpolated color is
 * sampled at the pixel center and truncated to 8 bits: v = floor(255 * (p +
 * 0.5) / extent). The real vertical ramp is one 8-bit step lower than that
 * on three rows, which the engine does not reproduce; they are skipped.
 */
static const uint16_t real_scale_red_blue[256] = {
      0,   2,   4,   6,   8,  10,  12,  14,  16,  18,  20,  22,  24,  26,  28,  30,
     31,  33,  35,  37,  39,  41,  43,  45,  47,  49,  51,  53,  55,  57,  59,  61,
     62,  64,  66,  68,  70,  72,  74,  76,  78,  80,  82,  84,  86,  88,  90,  92,
     93,  95,  97,  99, 101, 103, 105, 107, 109, 111, 113, 115, 117, 119, 121, 123,
    124, 126, 128, 130, 132, 134, 136, 138, 140, 142, 144, 146, 148, 150, 152, 154,
    155, 157, 159, 161, 163, 165, 167, 169, 171, 173, 175, 177, 179, 181, 183, 185,
    186, 188, 190, 192, 194, 196, 198, 200, 202, 204, 206, 208, 210, 212, 214, 216,
    217, 219, 221, 223, 225, 227, 229, 231, 233, 235, 237, 239, 241, 243, 245, 247,
    249, 251, 253, 255, 257, 259, 261, 263, 265, 267, 269, 271, 273, 275, 277, 279,
    280, 282, 284, 286, 288, 290, 292, 294, 296, 298, 300, 302, 304, 306, 308, 310,
    311, 313, 315, 317, 319, 321, 323, 325, 327, 329, 331, 333, 335, 337, 339, 341,
    342, 344, 346, 348, 350, 352, 354, 356, 358, 360, 362, 364, 366, 368, 370, 372,
    373, 375, 377, 379, 381, 383, 385, 387, 389, 391, 393, 395, 397, 399, 401, 403,
    404, 406, 408, 410, 412, 414, 416, 418, 420, 422, 424, 426, 428, 430, 432, 434,
    435, 437, 439, 441, 443, 445, 447, 449, 451, 453, 455, 457, 459, 461, 463, 465,
    466, 468, 470, 472, 474, 476, 478, 480, 482, 484, 486, 488, 490, 492, 494, 496,
};

static const uint16_t real_scale_green[256] = {
      0,   2,   4,   6,   8,  10,  12,  14,  15,  17,  19,  21,  23,  25,  27,  29,
     31,  33,  35,  37,  39,  41,  43,  45,  46,  48,  50,  52,  54,  56,  58,  60,
     62,  64,  66,  68,  70,  72,  74,  76,  77,  79,  81,  83,  85,  87,  89,  91,
     93,  95,  97,  99, 101, 103, 105, 107, 108, 110, 112, 114, 116, 118, 120, 122,
    124, 126, 128, 130, 132, 134, 136, 138, 140, 142, 144, 146, 148, 150, 152, 154,
    155, 157, 159, 161, 163, 165, 167, 169, 171, 173, 175, 177, 179, 181, 183, 185,
    186, 188, 190, 192, 194, 196, 198, 200, 202, 204, 206, 208, 210, 212, 214, 216,
    217, 219, 221, 223, 225, 227, 229, 231, 233, 235, 237, 239, 241, 243, 245, 247,
    249, 251, 253, 255, 257, 259, 261, 263, 264, 266, 268, 270, 272, 274, 276, 278,
    280, 282, 284, 286, 288, 290, 292, 294, 295, 297, 299, 301, 303, 305, 307, 309,
    311, 313, 315, 317, 319, 321, 323, 325, 326, 328, 330, 332, 334, 336, 338, 340,
    342, 344, 346, 348, 350, 352, 354, 356, 357, 359, 361, 363, 365, 367, 369, 371,
    373, 375, 377, 379, 381, 383, 385, 387, 389, 391, 393, 395, 397, 399, 401, 403,
    404, 406, 408, 410, 412, 414, 416, 418, 420, 422, 424, 426, 428, 430, 432, 434,
    435, 437, 439, 441, 443, 445, 447, 449, 451, 453, 455, 457, 459, 461, 463, 465,
    466, 468, 470, 472, 474, 476, 478, 480, 482, 484, 486, 488, 490, 492, 494, 496,
};

static const uint8_t real_rank[8][4] = {
    { 15, 7, 13, 5 }, { 3, 11, 1, 9 }, { 12, 4, 14, 6 }, { 0, 8, 2, 10 },
    { 13, 5, 15, 7 }, { 1, 9, 3, 11 }, { 14, 6, 12, 4 }, { 2, 10, 0, 8 }
};

#define DITHER_W 640
#define DITHER_H 480

#include "vid_ati_mach64_dither_vectors.h"

static void
dither_vertex(mach64_t *m, int n, double x, double y, int gray)
{
    uint32_t argb = 0xff000000u | (uint32_t) gray * 0x010101u;
    uint32_t xy4  = ((uint32_t) (int) (x * 4.0) << 16) | ((uint32_t) (int) (y * 4.0) & 0xffffu);

    mach64_3d_setup_write(m, 0x90 + n * 8 + 5, argb);
    mach64_3d_setup_write(m, 0x90 + n * 8 + 6, xy4);
}

/* One triangle of three vertices (x, y, gray), 1/area as the driver gives it. */
static void
dither_triangle(mach64_t *m, const double v[3][3])
{
    double   area = (v[0][0] - v[2][0]) * (v[1][1] - v[2][1]) - (v[0][1] - v[2][1]) * (v[1][0] - v[2][0]);
    float    ooa  = (float) (1.0 / area);
    uint32_t raw;

    for (int i = 0; i < 3; i++)
        dither_vertex(m, i, v[i][0], v[i][1], (int) v[i][2]);
    memcpy(&raw, &ooa, 4);
    mach64_3d_setup_write(m, 0x90 + 7, raw);
}

/* The rectangle [x0,x1) x [y0,y1) as the two triangles the diagnostic sends. */
static void
dither_rect(mach64_t *m, double x0, double y0, double x1, double y1, int tl, int tr, int bl, int br)
{
    const double a[3][3] = { { x0, y0, tl }, { x1, y0, tr }, { x0, y1, bl } };
    const double b[3][3] = { { x1, y0, tr }, { x1, y1, br }, { x0, y1, bl } };

    dither_triangle(m, a);
    dither_triangle(m, b);
}

static void
dither_surface(mach64_t *m, int dither)
{
    mach64_3d_t *ctx = m->gt3d;

    memset(m->svga.vram, 0, (size_t) DITHER_W * DITHER_H * 2);
    m->dp_pix_width  = 3; /* ARGB 1555 */
    m->dp_src        = 0x00000500u;
    m->dp_mix        = 0x00070007u;
    m->write_mask    = 0xffffffffu;
    m->dst_off_pitch = (DITHER_W / 8) << 22;
    m->sc_left_right = (DITHER_W - 1) << 16;
    m->sc_top_bottom = (DITHER_H - 1) << 16;
    m->clr_cmp_cntl  = 0;
    ctx->regs[Z_CNTL >> 2]        = 0;
    ctx->regs[SCALE_3D_CNTL >> 2] = (FCN_SHADE << SCALE_3D_FCN_SHIFT) | (dither ? (DITHER_EN | SCALE_DITHER) : 0);
    ctx->setup_cntl               = 0;
}

static unsigned
dither_pixel(const mach64_t *m, int x, int y)
{
    return (m->svga.vram[(y * DITHER_W + x) * 2] | (m->svga.vram[(y * DITHER_W + x) * 2 + 1] << 8)) & 0x7fffu;
}

static unsigned
dither_pixel_fmt(const mach64_t *m, int x, int y)
{
    return m->svga.vram[(y * DITHER_W + x) * 2] | (m->svga.vram[(y * DITHER_W + x) * 2 + 1] << 8);
}

static unsigned
dither_expected(int v, int x, int y)
{
    int r = (real_scale_red_blue[v] + real_rank[y & 7][x & 3]) >> 4;
    int g = (real_scale_green[v] + real_rank[y & 7][x & 3]) >> 4;

    if (r > 31)
        r = 31;
    if (g > 31)
        g = 31;
    return (unsigned) ((r << 10) | (g << 5) | r);
}

static void
ragepro_dither_tests(void)
{
    mach64_t *m = card_create();
    int       bad;

    m->ragepro = 1;

    /* Flat patches, dither on: every pixel of every gray. */
    dither_surface(m, 1);
    for (int i = 0; i < 256; i++) {
        double x0 = 64 + 16 * (i % 32);
        double y0 = 32 + 16 * (i / 32);

        dither_rect(m, x0, y0, x0 + 16, y0 + 16, i, i, i, i);
    }
    bad = 0;
    for (int i = 0; i < 256; i++)
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                int px = 64 + 16 * (i % 32) + x;
                int py = 32 + 16 * (i / 32) + y;

                bad += (dither_pixel(m, px, py) != dither_expected(i, px, py));
            }
    expect_int("dithered flat patches differing from a real Rage Pro", bad, 0);

    /* Flat patches, dither off: truncation. */
    dither_surface(m, 0);
    for (int i = 0; i < 256; i++) {
        double x0 = 64 + 16 * (i % 32);
        double y0 = 32 + 16 * (i / 32);

        dither_rect(m, x0, y0, x0 + 16, y0 + 16, i, i, i, i);
    }
    bad = 0;
    for (int i = 0; i < 256; i++)
        bad += (dither_pixel(m, 64 + 16 * (i % 32) + 5, 32 + 16 * (i / 32) + 9) != (unsigned) (((i >> 3) << 10) | ((i >> 3) << 5) | (i >> 3)));
    expect_int("undithered flat patches differing from a real Rage Pro", bad, 0);

    /* Ramps, dither off: expectations are the real card's pixels. */
    dither_surface(m, 0);
    dither_rect(m, 0, 0, DITHER_W, 240, 0, 255, 0, 255);
    dither_rect(m, 0, 240, DITHER_W, DITHER_H, 0, 0, 255, 255);
    bad = 0;
    for (int x = 0; x < DITHER_W; x++)
        bad += (dither_pixel(m, x, 100) != real_ramp_h_off[0][x]);
    expect_int("undithered horizontal ramp differing from a real Rage Pro", bad, 0);
    bad = 0;
    for (int y = 240; y < DITHER_H; y++)
        bad += (dither_pixel(m, 300, y) != real_ramp_v_off[y - 240]);
    expect_int("undithered vertical ramp differing from a real Rage Pro", bad, 0);

    /* Ramps, dither on: every pixel against the real card's. */
    dither_surface(m, 1);
    dither_rect(m, 0, 0, DITHER_W, 240, 0, 255, 0, 255);
    dither_rect(m, 0, 240, DITHER_W, DITHER_H, 0, 0, 255, 255);
    bad = 0;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < DITHER_W; x++)
            bad += (dither_pixel(m, x, y) != real_ramp_h_on[y & 7][x]);
    expect_int("dithered horizontal ramp differing from a real Rage Pro", bad, 0);

    /* The vertical ramp: three rows differ from the card, one 8-bit step
       lower there, and the engine does not reproduce that. They are known
       divergences: each must still differ from the card, in exactly the
       pinned pixels, so that a change either way is noticed. */
    {
        static const struct {
            int      row;
            unsigned emu[4]; /* what the engine draws at x = 0..3 (pinned, not measured) */
        } known[] = {
            { 240 + 136, { 0x4a52, 0x4a52, 0x4a52, 0x4631 } },
            { 240 + 152, { 0x5294, 0x5294, 0x5294, 0x5294 } },
            { 240 + 168, { 0x5ad6, 0x5ad6, 0x5ad6, 0x5ad6 } },
        };
        int unexpected = 0;
        int matching   = 0;
        int moved      = 0;

        /* A pin that equals the card is no divergence at all. */
        for (int i = 0; i < 3; i++) {
            int same = 1;

            for (int x = 0; x < 4; x++)
                same &= (known[i].emu[x] == real_ramp_v_on[known[i].row - 240][x]);
            matching += same;
        }

        for (int y = 240; y < DITHER_H; y++) {
            int k = -1;

            for (int i = 0; i < 3; i++)
                if (known[i].row == y)
                    k = i;
            for (int x = 0; x < DITHER_W; x++) {
                unsigned got = dither_pixel(m, x, y);

                if (k < 0) {
                    unexpected += (got != real_ramp_v_on[y - 240][x & 3]);
                } else {
                    moved += (got != known[k].emu[x & 3]);
                }
            }
        }
        expect_int("dithered vertical ramp differing from a real Rage Pro", unexpected, 0);
        expect_int("known-divergent vertical ramp rows pinned equal to the card", matching, 0);
        expect_int("known-divergent vertical ramp pixels moved from the pinned values", moved, 0);
    }
    card_close(m);
}


/*
 * Outside what was measured, SCALE_DITHER keeps the earlier 4x4 Bayer stand-in:
 * a chip that is not a Rage Pro, and a Rage Pro writing a surface other than
 * 555 or 565 (both have been measured). The cases here are the chip that is
 * not a Rage Pro. The expectation is that stand-in's formula, written out
 * independently of the engine: add (bayer * (2^drop - 1) + 7) / 15, drop the
 * low bits.
 */
static void
dither_bayer_tests_one(const char *what, int ragepro, unsigned pix_width)
{
    static const uint8_t bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    mach64_t             *m        = card_create();
    int                   bad      = 0;
    int                   same_as_rage_pro = 0;

    m->ragepro = ragepro;
    dither_surface(m, 1);
    m->dp_pix_width = pix_width;
    for (int i = 0; i < 256; i++) {
        double x0 = 64 + 16 * (i % 32);
        double y0 = 32 + 16 * (i / 32);

        dither_rect(m, x0, y0, x0 + 16, y0 + 16, i, i, i, i);
    }
    for (int i = 0; i < 256; i++) {
        for (int yy = 0; yy < 16; yy++) {
            for (int xx = 0; xx < 16; xx++) {
                int      x    = 64 + 16 * (i % 32) + xx;
                int      y    = 32 + 16 * (i / 32) + yy;
                int      add5 = (bayer[((y & 3) << 2) | (x & 3)] * 7 + 7) / 15;
                int      add6 = (bayer[((y & 3) << 2) | (x & 3)] * 3 + 7) / 15;
                int      r    = (i + add5) >> 3;
                int      g    = (pix_width == 4) ? (i + add6) >> 2 : r;
                unsigned want;

                if (r > 31)
                    r = 31;
                if (g > (pix_width == 4 ? 63 : 31))
                    g = pix_width == 4 ? 63 : 31;
                want = (pix_width == 4) ? (unsigned) ((r << 11) | (g << 5) | r) : (unsigned) ((r << 10) | (g << 5) | r);
                bad += (dither_pixel_fmt(m, x, y) != want);
                same_as_rage_pro += (dither_expected(i, x, y) == want);
            }
        }
    }
    expect_int(what, bad, 0);
    /* The check above is not vacuous: the two tables disagree on many pixels. */
    CHECK(same_as_rage_pro < 256 * 256 * 9 / 10);
    card_close(m);
}

static void
dither_bayer_tests(void)
{
    dither_bayer_tests_one("flat patches on a chip that is not a Rage Pro, differing from the Bayer stand-in", 0, 3);
}

/*
 * Rage Pro 565 dither and blend-then-dither order, against a real card.
 *
 * vid_ati_mach64_blend_vectors.h holds, for the 555 and 565 captures of the
 * diagnostic's flat patches (B dither on, C dither off) and blend tests (G..R,
 * see the header), the CRC-32 of each 16x16 patch's words as
 * read back from the card. The scenes are drawn here the way the diagnostic
 * draws them: a black clear, an opaque gray destination layer, then for the
 * blend tests a translucent gray layer of vertex alpha over it, and every
 * patch must reproduce the card's words.
 */
#include "vid_ati_mach64_blend_vectors.h"

static uint32_t
crc32_words(const uint16_t *w, int n)
{
    uint32_t c = 0xffffffffu;

    for (int i = 0; i < n * 2; i++) {
        c ^= ((const uint8_t *) w)[i]; /* little-endian host */
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

static void
blend_rect(mach64_t *m, double x0, double y0, double x1, double y1, int gray, int alpha)
{
    const double v[2][3][3] = { { { x0, y0, gray }, { x1, y0, gray }, { x0, y1, gray } },
                                { { x1, y0, gray }, { x1, y1, gray }, { x0, y1, gray } } };

    for (int t = 0; t < 2; t++) {
        double   area = (v[t][0][0] - v[t][2][0]) * (v[t][1][1] - v[t][2][1]) - (v[t][0][1] - v[t][2][1]) * (v[t][1][0] - v[t][2][0]);
        float    ooa  = (float) (1.0 / area);
        uint32_t raw;

        for (int i = 0; i < 3; i++) {
            uint32_t argb = ((uint32_t) alpha << 24) | (uint32_t) gray * 0x010101u;
            uint32_t xy4  = ((uint32_t) (int) (v[t][i][0] * 4.0) << 16) | ((uint32_t) (int) (v[t][i][1] * 4.0) & 0xffffu);

            mach64_3d_setup_write(m, 0x90 + i * 8 + 5, argb);
            mach64_3d_setup_write(m, 0x90 + i * 8 + 6, xy4);
        }
        memcpy(&raw, &ooa, 4);
        mach64_3d_setup_write(m, 0x90 + 7, raw);
    }
}

/* Draw one diagnostic scene into a fresh surface. test is the index into
   real_blend_crc's second dimension: 0 B, 1 C, then G..R. */
static void
blend_scene(mach64_t *m, int test, unsigned pix_width)
{
    static const int  bd[8] = { 16, 56, 96, 128, 160, 192, 224, 248 };
    static const int  bs[4] = { 24, 104, 184, 248 };
    static const int  ba[8] = { 16, 48, 80, 112, 144, 176, 208, 240 };
    /* G H I J K L M N O P Q R: destination layer dithered, final dither, blend (src, dst code), sweep dst / src */
    static const struct {
        int dst_dither, dither, src_code, dst_code, sweep_d, sweep_s;
    } cfg[12] = {
        { 0, 1, 4, 5, -1, 0 },   { 0, 0, 4, 5, -1, 0 },   { 1, 1, 4, 5, -1, 0 },   { 1, 0, 4, 5, -1, 0 },
        { 0, 1, 4, 1, -1, 0 },   { 0, 0, 4, 1, -1, 0 },   { 0, 1, 1, 1, -1, 0 },   { 0, 0, 1, 1, -1, 0 },
        { 0, 1, 4, 5, 0, 255 },  { 0, 1, 4, 5, 248, 0 }, { 0, 1, 4, 5, 128, 0 }, { 0, 1, 4, 5, 128, 255 },
    };
    mach64_3d_t *ctx = m->gt3d;

    dither_surface(m, 0);
    m->dp_pix_width = pix_width;
    if (test < 2) {
        dither_surface(m, test == 0);
        m->dp_pix_width = pix_width;
        for (int i = 0; i < 256; i++) {
            double x0 = 64 + 16 * (i % 32);
            double y0 = 32 + 16 * (i / 32);

            blend_rect(m, x0, y0, x0 + 16, y0 + 16, i, 255);
        }
        return;
    }
    {
        int c = test - 2;

        dither_surface(m, cfg[c].dst_dither);
        m->dp_pix_width = pix_width;
        for (int i = 0; i < 256; i++) {
            double x0 = 64 + 16 * (i % 32);
            double y0 = 32 + 16 * (i / 32);

            blend_rect(m, x0, y0, x0 + 16, y0 + 16, (cfg[c].sweep_d >= 0) ? cfg[c].sweep_d : bd[i / 32], 255);
        }
        ctx->regs[SCALE_3D_CNTL >> 2] = (FCN_SHADE << SCALE_3D_FCN_SHIFT) | (cfg[c].dither ? (DITHER_EN | SCALE_DITHER) : 0) |
                                        (ALPHA_FOG_BLEND << ALPHA_FOG_SHIFT) | ((uint32_t) cfg[c].src_code << ALPHA_BLEND_SRC) |
                                        ((uint32_t) cfg[c].dst_code << ALPHA_BLEND_DST);
        for (int i = 0; i < 256; i++) {
            double x0 = 64 + 16 * (i % 32);
            double y0 = 32 + 16 * (i / 32);
            int    g  = (cfg[c].sweep_d >= 0) ? cfg[c].sweep_s : bs[(i % 32) & 3];
            int    a  = (cfg[c].sweep_d >= 0) ? i : ba[(i % 32) >> 2];

            blend_rect(m, x0, y0, x0 + 16, y0 + 16, g, a);
        }
    }
}

static int
blend_scene_bad_patches(mach64_t *m, int test, int fmt)
{
    unsigned keep = (fmt == 0) ? 0x7fffu : 0xffffu;
    int      bad  = 0;

    for (int i = 0; i < 256; i++) {
        uint16_t w[256];

        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++)
                w[y * 16 + x] = (uint16_t) (dither_pixel_fmt(m, 64 + 16 * (i % 32) + x, 32 + 16 * (i / 32) + y) & keep);
        }
        bad += (crc32_words(w, 256) != real_blend_crc[fmt][test][i]);
    }
    return bad;
}

static void
ragepro_blend_tests(void)
{
    static const char names[] = "BCGHIJKLMNOPQR";

    for (int fmt = 0; fmt < 2; fmt++) {
        for (int test = 0; test < 14; test++) {
            mach64_t *m = card_create();
            char      what[96];

            m->ragepro = 1;
            blend_scene(m, test, fmt ? 4 : 3);
            snprintf(what, sizeof(what), "%s test %c patches differing from a real Rage Pro", fmt ? "565" : "555", names[test]);
            expect_int(what, blend_scene_bad_patches(m, test, fmt), 0);
            card_close(m);
        }
    }
}

/*
 * Rage Pro mip level selection, against a real card.
 *
 * The vectors (vid_ati_mach64_mip_vectors.h) are triangles drawn by a
 * 256 x 256 RGB565 mip-mapped texture (9 levels, a solid color per level, or
 * a gray ramp where the filter blends levels) through ATI3DCIF 4.11.2692 (mip-disable bug
 * patched) on a PCI Rage Pro 1002:4750, with the LOG_MAX_INC the card
 * reported after each and the pixels it drew at four points inside. The
 * level the card shows is ceil of LOG_MAX_INC less the LOD bias plus the
 * pixel's -log2 W, LOG_MAX_INC being the steepest screen gradient of the
 * texture coordinates times the largest W of the triangle: see
 * mach64_rp_log_max_inc, mach64_rp_w_term and mach64_rp_sample_lmi.
 *
 * The 1/area is the driver's float. The card's LOG_MAX_INC for a triangle whose
 * step is a power of two falls a hair short of it; the model takes the 1/area
 * a step down when it is above the true one, which reproduces all but two of the
 * vectors the generator was given; those two stay in the file as known
 * divergences.
 */
#include "vid_ati_mach64_mip_vectors.h"

#define MIP_TEX_BASE 0x100000u

static unsigned
mip_filter_scale_3d_cntl(int filter)
{
    /* what the driver wrote for each TMAP_FILTER (the card's SCALE_3D_CNTL) */
    static const unsigned cntl[7] = { 0x0001008bu, 0x0a01008bu, 0x0a01008bu, 0x0401008bu, 0x0601008bu, 0x0e01008bu, 0x0801008bu };

    return cntl[filter];
}

static void
mip_vertex_value(mach64_t *m, int reg, double v)
{
    float    f = (float) v;
    uint32_t raw;

    memcpy(&raw, &f, 4);
    mach64_3d_setup_write(m, reg, raw);
}

static void
ragepro_mip_tests(void)
{
    static const uint16_t colour[9] = { 0xf800, 0x07e0, 0x001f, 0xffe0, 0x07ff, 0xf81f, 0xffff, 0xfc00, 0x8410 };
    mach64_t             *m   = card_create();
    mach64_3d_t          *ctx = m->gt3d;
    int                   bad_pixels = 0;
    int                   bad_lmi    = 0;
    int                   shown      = 0;
    int                   known_seen = 0;
    int                   known_bad  = 0;

    m->ragepro = 1;
    /* level k of each texture: tex 0 at MIP_TEX_BASE, tex 1 a MiB above */
    for (int tex = 0; tex < 2; tex++) {
        uint32_t base = MIP_TEX_BASE + (uint32_t) tex * 0x100000u;

        for (int k = 0; k < 9; k++) {
            unsigned side  = 256u >> k;
            unsigned gray  = 3u * (unsigned) k + 1;
            unsigned value = tex ? ((gray << 11) | ((gray << 1) << 5) | gray) : colour[k];

            ctx->regs[(TEX_0_OFF + (8 - k) * 4) >> 2] = base;
            for (unsigned i = 0; i < side * side; i++) {
                m->svga.vram[base + i * 2]     = value & 0xff;
                m->svga.vram[base + i * 2 + 1] = value >> 8;
            }
            base += side * side * 2;
        }
    }
    for (unsigned i = 0; i < sizeof(mip_vectors) / sizeof(mip_vectors[0]); i++) {
        const struct mip_vector *v    = &mip_vectors[i];
        uint32_t                 base = MIP_TEX_BASE + (uint32_t) v->tex * 0x100000u;
        double                   area = (v->x[0] - v->x[2]) * (v->y[1] - v->y[2]) - (v->y[0] - v->y[2]) * (v->x[1] - v->x[2]);
        float                    ooa  = (float) (1.0 / area);
        uint32_t                 raw;

        memset(m->svga.vram, 0, (size_t) DITHER_W * DITHER_H * 2);
        m->dp_pix_width  = (4u << 28) | 3;
        m->dp_src        = 0x00000500u;
        m->dp_mix        = 0x00070007u;
        m->write_mask    = 0xffffffffu;
        m->dst_off_pitch = (DITHER_W / 8) << 22;
        m->sc_left_right = (DITHER_W - 1) << 16;
        m->sc_top_bottom = (DITHER_H - 1) << 16;
        m->clr_cmp_cntl  = 0;
        ctx->setup_cntl  = 0;
        ctx->regs[Z_CNTL >> 2]        = 0;
        ctx->regs[SCALE_3D_CNTL >> 2] = mip_filter_scale_3d_cntl(v->filter);
        ctx->regs[TEX_SIZE_PITCH >> 2] = 0x888;
        ctx->regs[RP_TEX_CNTL >> 2]    = (v->filter == 5 ? 0x40000880u : 0x40000080u) | (unsigned) v->bias;
        for (int k = 0, off = 0; k < 9; k++) {
            ctx->regs[(TEX_0_OFF + (8 - k) * 4) >> 2] = base + (uint32_t) off;
            off += (int) (((256u >> k) * (256u >> k)) * 2);
        }
        for (int n = 0; n < 3; n++) {
            uint32_t xy4 = ((uint32_t) (int) (v->x[n] * 4.0) << 16) | ((uint32_t) (int) (v->y[n] * 4.0) & 0xffffu);

            mach64_3d_setup_write(m, 0x90 + n * 8 + 5, 0xffffffffu);
            mach64_3d_setup_write(m, 0x90 + n * 8 + 6, xy4);
            mip_vertex_value(m, 0xab + 3 * n, v->u[n]);
            mip_vertex_value(m, 0xab + 3 * n + 1, v->v[n]);
            mip_vertex_value(m, 0xab + 3 * n + 2, v->w[n]);
        }
        memcpy(&raw, &ooa, 4);
        mach64_3d_setup_write(m, 0x90 + 7, raw);

        if (v->known_divergent) {
            unsigned emu = ctx->regs[RP_LOG_MAX_INC >> 2] & 0xfff000u;

            known_seen++;
            if (emu == v->lmi) {
                fprintf(stderr, "mip %c vector %u: now matches the card's LOG_MAX_INC %06x; remove it from the known divergences\n", v->test, i, emu);
                known_bad++;
            } else if (emu != v->emu_lmi) {
                fprintf(stderr, "mip %c vector %u: known divergence changed, LOG_MAX_INC %06x, pinned %06x, the card's %06x\n", v->test, i, emu, v->emu_lmi, v->lmi);
                known_bad++;
            }
            /* Only the register is a known divergence: the pixels of the vector are checked below. */
        } else if (v->has_lmi && (ctx->regs[RP_LOG_MAX_INC >> 2] & 0xfff000u) != v->lmi) {
            bad_lmi++;
            if (shown++ < 40)
                fprintf(stderr, "mip %c vector %u: LOG_MAX_INC %06x, the card's %06x\n", v->test, i,
                        ctx->regs[RP_LOG_MAX_INC >> 2] & 0xfff000u, v->lmi);
        }
        for (int p = 0; p < v->nprobe; p++) {
            unsigned got = dither_pixel(m, v->probe[p].x, v->probe[p].y);

            if (got != v->probe[p].pixel555) {
                bad_pixels++;
                if (shown++ < 40)
                    fprintf(stderr, "mip %c vector %u (%d,%d): pixel %04x, the card's %04x\n", v->test, i, v->probe[p].x,
                            v->probe[p].y, got, v->probe[p].pixel555);
            }
        }
    }
    expect_int("mip pixels differing from a real Rage Pro", bad_pixels, 0);
    expect_int("mip LOG_MAX_INC differing from a real Rage Pro", bad_lmi, 0);
    /* The known divergences: LOG_MAX_INC the model still gets wrong, each at
       the value pinned in the vector file. A model that starts to match, or
       moves, fails here so the entry is dealt with on purpose. */
    expect_int("mip known divergences present", known_seen, 2);
    expect_int("mip known divergences matching or moved", known_bad, 0);
    card_close(m);
}

/*
 * Rage Pro mip level cap, against a real card: truncated chains.
 *
 * Diagnostic tests 1 (256x256, 4 of 9 levels supplied), 2 (128x128, 6 of 8)
 * and 5 (128x32, 6 of 8) drawn with the three ways a driver can pass the
 * levels it lacks: pointers into poison-filled memory (real), the last
 * supplied level's pointer again (last), NULL (null). TEX_n_OFF are the
 * values the driver wrote, so a missing level reads whatever its register
 * holds. The levels follow TEX_SIZE_PITCH, not the registers.
 *
 * Memory at address 0 is zero here, so a level that reads address 0 and one
 * that is not fetched at all both come out black: the null mode checks the
 * black the card showed, not that address 0 is read.
 */
#include "vid_ati_mach64_mipchain_vectors.h"

#define MIP_CHAIN_FRAME 0x300000u

static void
mip_chain_fill(mach64_t *m, uint32_t off, unsigned w, unsigned h, unsigned value)
{
    for (unsigned i = 0; i < w * h; i++) {
        m->svga.vram[off + i * 2]     = value & 0xff;
        m->svga.vram[off + i * 2 + 1] = value >> 8;
    }
}

static void
ragepro_mip_chain_tests(void)
{
    static const uint16_t colour[9] = { 0xf800, 0x07e0, 0x001f, 0xffe0, 0x07ff, 0xf81f, 0xffff, 0xfc00, 0x8410 };
    static const int      kcol[9][3] = { { 31, 0, 0 }, { 0, 31, 0 }, { 0, 0, 31 }, { 31, 31, 0 }, { 0, 31, 31 },
                                         { 31, 0, 31 }, { 31, 31, 31 }, { 31, 16, 0 }, { 16, 16, 16 } };
    mach64_t             *m   = card_create();
    mach64_3d_t          *ctx = m->gt3d;
    int                   bad[3]   = { 0, 0, 0 };
    int                   total[3] = { 0, 0, 0 };
    int                   shown    = 0;

    m->ragepro = 1;
    for (unsigned i = 0; i < sizeof(mip_chain_vectors) / sizeof(mip_chain_vectors[0]); i++) {
        const struct mip_chain_vector *v   = &mip_chain_vectors[i];
        const struct mip_chain_layout *lay = &mip_chain_layouts[v->layout];
        int                            mode = !strcmp(lay->mode, "real") ? 0 : (!strcmp(lay->mode, "last") ? 1 : 2);
        double                         area = (v->x[0] - v->x[2]) * (v->y[1] - v->y[2]) - (v->y[0] - v->y[2]) * (v->x[1] - v->x[2]);
        float                          ooa  = (float) (1.0 / area);
        uint32_t                       raw;
        int                            bad_here = 0;

        {
            int nfull = ((lay->wl2 > lay->hl2) ? lay->wl2 : lay->hl2) + 1;
            int s     = nfull - 1;

            memset(m->svga.vram, 0, MIP_CHAIN_FRAME);
            for (int k = 0; k < nfull; k++) {
                unsigned w = 1u << ((lay->wl2 - k) > 0 ? (lay->wl2 - k) : 0);
                unsigned h = 1u << ((lay->hl2 - k) > 0 ? (lay->hl2 - k) : 0);
                unsigned val;
                uint32_t off = lay->tex_off[s - k];

                if (k < lay->supplied) {
                    unsigned gray = 3u * (unsigned) k + 1;

                    val = v->tex ? ((gray << 11) | ((gray << 1) << 5) | gray) : colour[k];
                } else if (mode == 0) {
                    /* poison: the level's colour with 0, 16, 31 read as 4, 12, 24; a ramp's r = b = 3k + 1, g = 3k + 5 */
                    unsigned r, g, b;

                    if (v->tex) {
                        r = b = 3u * (unsigned) k + 1;
                        g     = 3u * (unsigned) k + 5;
                    } else {
                        r = (kcol[k][0] == 0) ? 4 : ((kcol[k][0] == 16) ? 12 : 24);
                        g = (kcol[k][1] == 0) ? 4 : ((kcol[k][1] == 16) ? 12 : 24);
                        b = (kcol[k][2] == 0) ? 4 : ((kcol[k][2] == 16) ? 12 : 24);
                    }
                    val = (r << 11) | ((g << 1) << 5) | b;
                } else
                    continue;
                mip_chain_fill(m, off, w, h, val);
            }
        }
        m->dp_pix_width  = (4u << 28) | 3;
        m->dp_src        = 0x00000500u;
        m->dp_mix        = 0x00070007u;
        m->write_mask    = 0xffffffffu;
        m->dst_off_pitch = ((DITHER_W / 8) << 22) | (MIP_CHAIN_FRAME / 8);
        m->sc_left_right = (DITHER_W - 1) << 16;
        m->sc_top_bottom = (DITHER_H - 1) << 16;
        m->clr_cmp_cntl  = 0;
        ctx->setup_cntl  = 0;
        ctx->regs[Z_CNTL >> 2]         = 0;
        ctx->regs[SCALE_3D_CNTL >> 2]  = v->scale_3d_cntl;
        ctx->regs[TEX_SIZE_PITCH >> 2] = lay->tex_size_pitch;
        ctx->regs[RP_TEX_CNTL >> 2]    = v->tex_cntl;
        for (int n = 0; n < 9; n++)
            ctx->regs[(TEX_0_OFF + n * 4) >> 2] = lay->tex_off[n];
        memset(m->svga.vram + MIP_CHAIN_FRAME, 0, (size_t) DITHER_W * DITHER_H * 2);
        for (int n = 0; n < 3; n++) {
            uint32_t xy4 = ((uint32_t) (int) (v->x[n] * 4.0) << 16) | ((uint32_t) (int) (v->y[n] * 4.0) & 0xffffu);

            mach64_3d_setup_write(m, 0x90 + n * 8 + 5, 0xffffffffu);
            mach64_3d_setup_write(m, 0x90 + n * 8 + 6, xy4);
            mip_vertex_value(m, 0xab + 3 * n, v->u[n]);
            mip_vertex_value(m, 0xab + 3 * n + 1, v->v[n]);
            mip_vertex_value(m, 0xab + 3 * n + 2, v->w[n]);
        }
        memcpy(&raw, &ooa, 4);
        mach64_3d_setup_write(m, 0x90 + 7, raw);

        for (int p = 0; p < 3; p++) {
            const uint8_t *px = m->svga.vram + MIP_CHAIN_FRAME + (v->probe[p].y * DITHER_W + v->probe[p].x) * 2;
            unsigned       got = (px[0] | (px[1] << 8)) & 0x7fffu;

            if (got != v->probe[p].pixel555)
                bad_here = 1;
        }
        total[mode]++;
        if (bad_here) {
            bad[mode]++;
            if (shown++ < 25)
                fprintf(stderr, "mip chain %s test %d vector %u (tex %d filter %d): pixel mismatch\n", lay->mode, lay->test, i, v->tex, v->filter);
        }
    }
    printf("mip chain triangles differing from a real Rage Pro: real %d of %d, last %d of %d, null %d of %d\n", bad[0], total[0], bad[1],
           total[1], bad[2], total[2]);
    expect_int("mip chain triangles differing (real)", bad[0], 0);
    expect_int("mip chain triangles differing (last)", bad[1], 0);
    expect_int("mip chain triangles differing (null)", bad[2], 0);
    card_close(m);
}

/*
 * Rage Pro composite (second) texture, against a real card.
 *
 * Composite states (blend factors, modulate, add specular, factor from
 * alpha, the secondary filters, the secondary's own coordinates, a secondary
 * registered as a mip chain), replayed from the card's registers through the
 * triangle path: the vertex registers as the driver left them, the 3D state
 * registers, and the 64x64 textures the probe built. The compared pixels are
 * every second one of the triangle's interior, within one unit of a 5 bit
 * component (the card's own rounding is not known to the bit).
 */
#include "vid_ati_mach64_composite_vectors.h"

#define COMP_FRAME 0x300000u

static unsigned
composite_texel(int which, int x, int y)
{
    int xs = x * 31 / 63;
    int ys = y * 31 / 63;

    switch (which) {
        case 0: /* primary: r = x, g = y, b = 0 */
            return (xs << 11) | ((ys << 1) << 5);
        case 1:
            return (8 << 11) | ((8 << 1) << 5) | xs;
        case 2:
            return (8 << 11) | ((8 << 1) << 5) | ys;
        case 3: /* 4444: alpha = x / 2, r = g = 4, b = y / 2 */
            return ((xs >> 1) << 12) | (4 << 8) | (4 << 4) | (ys >> 1);
        case 4: /* a mip chain whose level 0 is red */
            return 0xf800;
        default: /* one texel checker */
            return (8 << 11) | ((8 << 1) << 5) | (((x + y) & 1) ? 31 : 0);
    }
}

static void
composite_tests(void)
{
    mach64_t    *m   = card_create();
    mach64_3d_t *ctx = m->gt3d;
    int          bad_total = 0;
    int          exact     = 0;
    int          total     = 0;
    int          shown     = 0;

    m->ragepro = 1;
    for (unsigned i = 0; i < sizeof(composite_vectors) / sizeof(composite_vectors[0]); i++) {
        const struct composite_vector *v = &composite_vectors[i];
        int                            bad = 0;

        memset(m->svga.vram, 0, COMP_FRAME + (size_t) DITHER_W * DITHER_H * 2);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                unsigned p = composite_texel(0, x, y);
                unsigned c = composite_texel(v->sec_tex, x, y);
                uint32_t po = v->tex_6_off + (y * 64 + x) * 2;
                uint32_t co = v->secondary_tex_off + (y * 64 + x) * 2;

                m->svga.vram[po] = p & 0xff;
                m->svga.vram[po + 1] = p >> 8;
                m->svga.vram[co] = c & 0xff;
                m->svga.vram[co + 1] = c >> 8;
            }
        m->dp_pix_width  = (4u << 28) | 3 | ((v->sec_tex == 3 ? 15u : 4u) << 4);
        m->dp_src        = 0x00000500u;
        m->dp_mix        = 0x00070007u;
        m->write_mask    = 0xffffffffu;
        m->dst_off_pitch = ((DITHER_W / 8) << 22) | (COMP_FRAME / 8);
        m->sc_left_right = (DITHER_W - 1) << 16;
        m->sc_top_bottom = (DITHER_H - 1) << 16;
        m->clr_cmp_cntl  = 0;
        for (int r = 0; r < 256; r++)
            ctx->regs[r] = 0;
        ctx->regs[SCALE_3D_CNTL >> 2]            = v->scale_3d_cntl;
        ctx->regs[TEX_SIZE_PITCH >> 2]           = v->tex_size_pitch;
        ctx->regs[RP_TEX_CNTL >> 2]              = v->tex_cntl;
        ctx->regs[RP_SECONDARY_TEX_OFF >> 2]     = v->secondary_tex_off;
        ctx->regs[(TEX_0_OFF + 6 * 4) >> 2]      = v->tex_6_off;
        ctx->regs[Z_CNTL >> 2]                   = 0;
        ctx->regs[RP_ALPHA_TST_CNTL >> 2]        = v->alpha_tst_cntl;
        for (int n = 0; n < v->nsetup; n++)
            mach64_3d_setup_write(m, v->setup[n].idx, v->setup[n].val);
        mach64_3d_setup_write(m, 0xc0, v->one_over_area); /* starts the triangle */

        for (int p = 0; p < v->npix; p++) {
            const uint8_t *px  = m->svga.vram + COMP_FRAME + ((8 + v->pix[p].dy) * DITHER_W + 60 + v->pix[p].dx) * 2;
            unsigned       got = (px[0] | (px[1] << 8)) & 0x7fffu;
            unsigned       exp = v->pix[p].pixel555;
            int            off = 0;

            for (int sh = 0; sh <= 10; sh += 5) {
                int d = (int) ((got >> sh) & 31) - (int) ((exp >> sh) & 31);

                if (d > 1 || d < -1)
                    off = 1;
            }
            total++;
            exact += (got == exp);
            bad += off;
        }
        bad_total += bad;
        if (bad && shown++ < 40)
            fprintf(stderr, "composite %s: %d of %d pixels more than one unit from the card\n", v->name, bad, v->npix);
    }
    printf("composite pixels: %d of %d exact, %d beyond one unit\n", exact, total, bad_total);
    expect_int("composite pixels more than one unit from a real Rage Pro", bad_total, 0);
    /* The vectors are 40 states of one primary and diffuse colour and a few secondary
       values: a summary of how close the model sits, not a check on the modulate rule,
       which ragepro_modulate_tests checks pixel by pixel. */
    printf("composite exact count (diagnostic only): %d of %d\n", exact, total);
    card_close(m);
}

/*
 * VQ through the triangle path. The texel-level checks above call the texel
 * reader directly; these draw triangles through the setup registers, as the
 * mip and dither tests do, and read the frame buffer back, so the sampling
 * connection of the triangle rasterizer is part of what is checked.
 *
 * Test A: the 64x64 VQ texture and 1:1 point-sampled quad of the lookup test,
 * all 64 x 64 pixels against the real card's. Test M: a 7 level VQ chain whose
 * level k is one colour, 27 small triangles of rising level of detail, five
 * interior pixels of each against the real card's.
 */
static void
vq_tri_frame(mach64_t *m, unsigned cntl)
{
    mach64_3d_t *ctx = m->gt3d;

    memset(m->svga.vram, 0, (size_t) DITHER_W * DITHER_H * 2);
    m->dp_pix_width  = (2u << 28) | 3;
    m->dp_src        = 0x00000500u;
    m->dp_mix        = 0x00070007u;
    m->write_mask    = 0xffffffffu;
    m->dst_off_pitch = (DITHER_W / 8) << 22;
    m->sc_left_right = (DITHER_W - 1) << 16;
    m->sc_top_bottom = (DITHER_H - 1) << 16;
    m->clr_cmp_cntl  = 0;
    ctx->setup_cntl  = 0;
    ctx->regs[Z_CNTL >> 2]        = 0;
    ctx->regs[SCALE_3D_CNTL >> 2] = cntl;
}

static void
vq_tri_draw(mach64_t *m, const double x[3], const double y[3], const double u[3], const double v[3], const double w[3])
{
    double   area = (x[0] - x[2]) * (y[1] - y[2]) - (y[0] - y[2]) * (x[1] - x[2]);
    float    ooa  = (float) (1.0 / area);
    uint32_t raw;

    for (int n = 0; n < 3; n++) {
        uint32_t xy4 = ((uint32_t) (int) (x[n] * 4.0) << 16) | ((uint32_t) (int) (y[n] * 4.0) & 0xffffu);

        mach64_3d_setup_write(m, 0x90 + n * 8 + 5, 0xffffffffu);
        mach64_3d_setup_write(m, 0x90 + n * 8 + 6, xy4);
        mip_vertex_value(m, 0xab + 3 * n, u[n]);
        mip_vertex_value(m, 0xab + 3 * n + 1, v[n]);
        mip_vertex_value(m, 0xab + 3 * n + 2, w[n]);
    }
    memcpy(&raw, &ooa, 4);
    mach64_3d_setup_write(m, 0x90 + 7, raw);
}

static unsigned
vq_tri_pixel(const mach64_t *m, int x, int y)
{
    return dither_pixel(m, x, y);
}

static void
ragepro_vq_triangle_tests(void)
{
    mach64_t    *m   = card_create();
    mach64_3d_t *ctx = m->gt3d;
    uint8_t      codebook[0x800];
    uint8_t      image[0x800];
    int          bad = 0;
    int          bad_mip = 0;
    int          shown = 0;

    m->ragepro = 1;

    /* Test A */
    for (unsigned i = 0; i < 256; i++)
        for (unsigned p = 0; p < 4; p++) {
            unsigned word = ((i >> 3) << 11) | ((((i & 7) << 2 | p) << 1) << 5) | 21;

            codebook[i * 8 + p * 2]     = word & 255;
            codebook[i * 8 + p * 2 + 1] = word >> 8;
        }
    vq_driver_image(codebook, image);
    vq_load_codebook(m, 0x20800, image);
    vq_tri_frame(m, mip_filter_scale_3d_cntl(0));
    for (unsigned n = 0; n < 1024; n++)
        m->svga.vram[0x30000 + n] = vq_a_seq[n & 255];
    ctx->regs[RP_TEX_CNTL >> 2]      = 0x40000080u | RP_TEX_VQ;
    ctx->regs[TEX_SIZE_PITCH >> 2]   = 6 | (6 << 4) | (6 << 8);
    ctx->regs[(TEX_0_OFF + 24) >> 2] = 0x30000;
    {
        const double ax[3] = { 16, 80, 16 }, ay[3] = { 16, 16, 80 }, au[3] = { 0, 1, 0 }, av[3] = { 0, 0, 1 }, aw[3] = { 1, 1, 1 };
        const double bx[3] = { 80, 80, 16 }, by[3] = { 16, 80, 80 }, bu[3] = { 1, 1, 0 }, bv[3] = { 0, 1, 1 };

        vq_tri_draw(m, ax, ay, au, av, aw);
        vq_tri_draw(m, bx, by, bu, bv, aw);
    }
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            if (vq_tri_pixel(m, 16 + x, 16 + y) != vq_a_real_pixels[y * 64 + x]) {
                if (bad++ < 5)
                    fprintf(stderr, "VQ A (%d,%d): %04x, the card's %04x\n", x, y, vq_tri_pixel(m, 16 + x, 16 + y), vq_a_real_pixels[y * 64 + x]);
            }
    expect_int("VQ triangle pixels differing from a real Rage Pro (lookup)", bad, 0);

    /* Test M */
    {
        static const unsigned colour[7] = { 0xf800, 0x07e0, 0x001f, 0xffe0, 0x07ff, 0xf81f, 0xffff };
        uint32_t              off = 0;

        memset(codebook, 0, sizeof(codebook));
        for (unsigned k = 0; k < 7; k++)
            for (unsigned p = 0; p < 4; p++) {
                codebook[k * 8 + p * 2]     = colour[k] & 255;
                codebook[k * 8 + p * 2 + 1] = colour[k] >> 8;
            }
        vq_driver_image(codebook, image);
        vq_load_codebook(m, 0x20800, image);
        for (unsigned i = 0; i < sizeof(vq_mip_vectors) / sizeof(vq_mip_vectors[0]); i++) {
            const struct vq_mip_vector *v = &vq_mip_vectors[i];

            vq_tri_frame(m, mip_filter_scale_3d_cntl(0));
            memset(m->svga.vram + 0x30000, 0, 0x1000);
            off = 0;
            for (unsigned k = 0; k < 7; k++) {
                unsigned d = (64u >> k) / 2;

                if (d < 1)
                    d = 1;
                /* The 1x1 texel level shares the 1x1 index map before it. */
                if (k < 6)
                    memset(m->svga.vram + 0x30000 + off, (int) k, d * d);
                ctx->regs[(TEX_0_OFF + (6 - k) * 4) >> 2] = 0x30000 + off - ((k == 6) ? d * d : 0);
                if (k < 6)
                    off += d * d;
            }
            ctx->regs[RP_TEX_CNTL >> 2]    = 0x40000080u | RP_TEX_VQ;
            ctx->regs[TEX_SIZE_PITCH >> 2] = 6 | (6 << 4) | (6 << 8);
            vq_tri_draw(m, v->x, v->y, v->u, v->v, v->w);
            for (int p = 0; p < 5; p++)
                if (vq_tri_pixel(m, v->probe[p].x, v->probe[p].y) != v->probe[p].pixel555) {
                    bad_mip++;
                    if (shown++ < 12)
                        fprintf(stderr, "VQ M vector %u (%d,%d): %04x, the card's %04x\n", i, v->probe[p].x, v->probe[p].y,
                                vq_tri_pixel(m, v->probe[p].x, v->probe[p].y), v->probe[p].pixel555);
                }
        }
        expect_int("VQ mip triangle pixels differing from a real Rage Pro", bad_mip, 0);
    }
    card_close(m);
}

/*
 * The Rage Pro's bilinear filter, against a real card.
 *
 * 64x64 textures of two colours per channel, drawn as quads (two triangles
 * through the setup registers, white vertex colour, no perspective
 * correction, dither off) into a 32 bit frame, so the filtered value keeps its
 * 8 bits per channel. Magnified 128 pixels per texel, a probe's fraction of a
 * texel follows its position, and the card's 16 steps (8 for VQ) show in the
 * stairs of each strip. The d groups are minified 2:1 and 4:1 with a
 * minifying filter, which gives one bilinear sample at the pixel centre.
 */
#include "vid_ati_mach64_bilinear_vectors.h"

#define BL_FRAME 0x100000u
#define BL_TEX   0x300000u
#define BL_CODE  0x340000u

static uint16_t
bilinear_texel(int format, const struct bilinear_group *g, int kind, int x, int y)
{
    int c[3];

    for (int i = 0; i < 3; i++)
        c[i] = g->lo[i];
    if (kind == 0) {
        if (x & 1)
            for (int i = 0; i < 3; i++)
                c[i] = g->hi[i];
    } else if (kind == 1) {
        if (y & 1)
            for (int i = 0; i < 3; i++)
                c[i] = g->hi[i];
    } else {
        if ((x & 1) && !(y & 1))
            c[0] = g->hi[0];
        if (!(x & 1) && (y & 1))
            c[1] = g->hi[1];
        if ((x & 1) && (y & 1))
            c[2] = g->hi[2];
    }
    if (format == 1)
        return (uint16_t) (0x8000 | (c[0] << 10) | (c[1] << 5) | c[2]);
    if (format == 2)
        return (uint16_t) (0xf000 | (c[0] << 8) | (c[1] << 4) | c[2]);
    return (uint16_t) ((c[0] << 11) | (c[1] << 5) | c[2]);
}

static void
bilinear_texture(mach64_t *m, const struct bilinear_group *g, int kind)
{
    mach64_3d_t *ctx = m->gt3d;

    if (g->format == 3) {
        uint8_t codebook[0x800];
        uint8_t image[0x800];

        for (unsigned i = 0; i < 256; i++)
            for (unsigned p = 0; p < 4; p++) {
                uint16_t w = bilinear_texel(3, g, kind, (int) (p & 1), (int) (p >> 1));

                codebook[i * 8 + p * 2]     = w & 255;
                codebook[i * 8 + p * 2 + 1] = w >> 8;
            }
        vq_driver_image(codebook, image);
        vq_load_codebook(m, BL_CODE, image);
        memset(m->svga.vram + BL_TEX, 0, 32 * 32);
    } else {
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                uint16_t w = bilinear_texel(g->format, g, kind, x, y);

                m->svga.vram[BL_TEX + (y * 64 + x) * 2]     = w & 255;
                m->svga.vram[BL_TEX + (y * 64 + x) * 2 + 1] = w >> 8;
            }
    }
    ctx->regs[RP_TEX_CNTL >> 2]      = 0x40000080u | ((g->format == 3) ? RP_TEX_VQ : 0);
    ctx->regs[TEX_SIZE_PITCH >> 2]   = 0x6666;
    ctx->regs[(TEX_0_OFF + 24) >> 2] = BL_TEX;
}

static void
ragepro_bilinear_tests(void)
{
    static const int src_format[4] = { 4, 3, 15, 4 };
    mach64_t        *m   = card_create();
    mach64_3d_t     *ctx = m->gt3d;
    int              bad_total = 0;
    int              probes_total = 0;

    m->ragepro = 1;
    for (unsigned gi = 0; gi < sizeof(bilinear_groups) / sizeof(bilinear_groups[0]); gi++) {
        const struct bilinear_group *g   = &bilinear_groups[gi];
        int                          bad = 0;
        int                          n   = 0;
        int                          kind = -1;

        memset(m->svga.vram + BL_FRAME, 0, (size_t) DITHER_W * DITHER_H * 4);
        for (int qi = 0; qi < g->nquad; qi++) {
            const struct bilinear_quad *q = &g->quads[qi];
            const double x[3] = { q->x0, q->x1, q->x0 }, y[3] = { q->y0, q->y0, q->y1 };
            const double u[3] = { q->u0 / 64.0, q->u1 / 64.0, q->u0 / 64.0 }, v[3] = { q->v0 / 64.0, q->v0 / 64.0, q->v1 / 64.0 };
            const double bx[3] = { q->x1, q->x1, q->x0 }, by[3] = { q->y0, q->y1, q->y1 };
            const double bu[3] = { q->u1 / 64.0, q->u1 / 64.0, q->u0 / 64.0 }, bv[3] = { q->v0 / 64.0, q->v1 / 64.0, q->v1 / 64.0 };
            const double w[3] = { 1, 1, 1 };

            if (q->kind != kind) {
                kind = q->kind;
                bilinear_texture(m, g, kind);
            }
            m->dp_pix_width  = ((uint32_t) src_format[g->format] << 28) | 6;
            m->dp_src        = 0x00000500u;
            m->dp_mix        = 0x00070007u;
            m->write_mask    = 0xffffffffu;
            m->dst_off_pitch = ((DITHER_W / 8) << 22) | (BL_FRAME / 8);
            m->sc_left_right = (DITHER_W - 1) << 16;
            m->sc_top_bottom = (DITHER_H - 1) << 16;
            m->clr_cmp_cntl  = 0;
            ctx->setup_cntl  = 0;
            ctx->regs[Z_CNTL >> 2]        = 0;
            ctx->regs[SCALE_3D_CNTL >> 2] = mip_filter_scale_3d_cntl(2);
            vq_tri_draw(m, x, y, u, v, w);
            vq_tri_draw(m, bx, by, bu, bv, w);
            for (int p = 0; p < q->count; p++) {
                const struct bilinear_probe *pr  = &bilinear_probes[q->first + p];
                const uint8_t               *px  = m->svga.vram + BL_FRAME + ((size_t) pr->y * DITHER_W + pr->x) * 4;
                unsigned                     got = ((unsigned) px[2] << 16) | ((unsigned) px[1] << 8) | px[0];

                n++;
                if (got != pr->rgb) {
                    if (bad++ < 5)
                        fprintf(stderr, "bilinear %s quad %d (%u,%u): %06x, the card's %06x\n", g->name, qi, pr->x, pr->y, got, pr->rgb);
                }
            }
        }
        printf("bilinear %s: %d of %d probes exact\n", g->name, n - bad, n);
        bad_total += bad;
        probes_total += n;
    }
    expect_int("bilinear filter probes differing from a real Rage Pro", bad_total, 0);
    expect_int("bilinear filter probes checked", probes_total, (int) (sizeof(bilinear_probes) / sizeof(bilinear_probes[0])));
    card_close(m);
}


#include "vid_ati_mach64_modulate_vectors.h"

/*
 * Rage Pro texture formats that are not 16 bit RGB, and the modulate combine of
 * a composite texture, against a real card (see the header's comment for what
 * each group holds). Strips of the CI8, CI4, RGB332 and ARGB8888 formats go
 * through the primary texture, the composite strips and the ramps through the
 * composite. The ramps (non-white primary and diffuse, every secondary channel
 * value) are the data the modulate rule is fitted to; the strips are not.
 */
static uint32_t
modulate_word(int format, const int c[3])
{
    switch (format) {
        case 0:
            return (uint32_t) ((c[0] << 11) | (c[1] << 5) | c[2]);
        case 1:
            return (uint32_t) (0x8000 | (c[0] << 10) | (c[1] << 5) | c[2]);
        case 2:
            return (uint32_t) (0xf000 | (c[0] << 8) | (c[1] << 4) | c[2]);
        case 6:
            return (uint32_t) ((c[0] << 5) | (c[1] << 2) | c[2]);
        default:
            return 0xff000000u | ((uint32_t) c[0] << 16) | ((uint32_t) c[1] << 8) | (uint32_t) c[2];
    }
}

/* The register code (DP_PIX_WIDTH) of a texture format of the group. */
static unsigned
modulate_code(int format)
{
    static const unsigned code[8] = { 4, 3, 15, 4, 2, 2, 7, 6 };

    return code[format];
}

static void
modulate_texture(mach64_t *m, const struct mod_group *g, int kind, uint32_t base)
{
    int bytes = (g->format == 7) ? 4 : ((g->format >= 4 && g->format != 6) || g->format == 6) ? 1 : 2;

    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int      c[3];
            int      hi = (kind == 0) ? (x & 1) : (y & 1);
            uint32_t w;

            for (int i = 0; i < 3; i++)
                c[i] = hi ? g->hi[i] : g->lo[i];
            if (kind == 4) {
                if (g->format == 2) {
                    c[0] = x & 15;
                    c[1] = (x * 3) & 15;
                    c[2] = 15 - (x & 15);
                } else if (g->format == 1) {
                    c[0] = x & 31;
                    c[1] = (x * 7) & 31;
                    c[2] = 31 - (x & 31);
                } else {
                    c[0] = x & 31;
                    c[1] = x & 63;
                    c[2] = 31 - (x & 31);
                }
            }
            w = (g->format == 4 || g->format == 5) ? (uint32_t) hi : modulate_word(g->format, c);
            for (int b = 0; b < bytes; b++)
                m->svga.vram[base + (y * 64 + x) * bytes + b] = (uint8_t) (w >> (8 * b));
        }
}

static void
modulate_solid(mach64_t *m, uint32_t base, const int p[3])
{
    uint32_t w = modulate_word(0, p);

    for (int i = 0; i < 64 * 64; i++) {
        m->svga.vram[base + i * 2]     = (uint8_t) w;
        m->svga.vram[base + i * 2 + 1] = (uint8_t) (w >> 8);
    }
}

static void
modulate_vertices(mach64_t *m, const struct mod_quad *q, int composite, unsigned diffuse)
{
    const double x[2][3] = { { q->x0, q->x1, q->x0 }, { q->x1, q->x1, q->x0 } };
    const double y[2][3] = { { q->y0, q->y0, q->y1 }, { q->y0, q->y1, q->y1 } };
    const double u[2][3] = { { q->u0, q->u1, q->u0 }, { q->u1, q->u1, q->u0 } };
    const double v[2][3] = { { q->v0, q->v0, q->v1 }, { q->v0, q->v1, q->v1 } };

    for (int tri = 0; tri < 2; tri++) {
        double   area = (x[tri][0] - x[tri][2]) * (y[tri][1] - y[tri][2]) - (y[tri][0] - y[tri][2]) * (x[tri][1] - x[tri][2]);
        float    ooa  = (float) (1.0 / area);
        uint32_t raw;

        for (int n = 0; n < 3; n++) {
            uint32_t xy4 = ((uint32_t) (int) (x[tri][n] * 4.0) << 16) | ((uint32_t) (int) (y[tri][n] * 4.0) & 0xffffu);
            double   su  = u[tri][n] / 64.0;
            double   sv  = v[tri][n] / 64.0;

            mach64_3d_setup_write(m, 0x90 + n * 8 + 5, diffuse);
            mach64_3d_setup_write(m, 0x90 + n * 8 + 6, xy4);
            if (composite) {
                /* the primary is a solid texture: any coordinate; the composite has its own */
                mip_vertex_value(m, 0xab + 3 * n, 0.5);
                mip_vertex_value(m, 0xab + 3 * n + 1, 0.5);
                mip_vertex_value(m, 0xab + 3 * n + 2, 1.0);
                mip_vertex_value(m, (n == 2) ? 0xa8 : (0xca + 3 * n), su);
                mip_vertex_value(m, (n == 2) ? 0xa9 : (0xca + 3 * n + 1), sv);
                mip_vertex_value(m, (n == 2) ? 0xaa : (0xca + 3 * n + 2), 1.0);
            } else {
                mip_vertex_value(m, 0xab + 3 * n, su);
                mip_vertex_value(m, 0xab + 3 * n + 1, sv);
                mip_vertex_value(m, 0xab + 3 * n + 2, 1.0);
            }
        }
        memcpy(&raw, &ooa, 4);
        mach64_3d_setup_write(m, 0x90 + 7, raw);
    }
}

static void
ragepro_modulate_tests(void)
{
    mach64_t    *m   = card_create();
    mach64_3d_t *ctx = m->gt3d;
    int          bad_group[16] = { 0 };
    int          total[16]     = { 0 };

    m->ragepro = 1;
    for (unsigned gi = 0; gi < sizeof(mod_groups) / sizeof(mod_groups[0]); gi++) {
        const struct mod_group *g = &mod_groups[gi];
        int                     kind = -1;

        memset(m->svga.vram + BL_FRAME, 0, (size_t) DITHER_W * DITHER_H * 4);
        for (int qi = 0; qi < g->nquad; qi++) {
            const struct mod_quad *q = &g->quads[qi];

            memset(ctx->regs, 0, sizeof(ctx->regs));
            ctx->setup_cntl = 0;
            m->write_mask   = 0xffffffffu;
            m->dp_src       = 0x00000500u;
            m->dp_mix       = 0x00070007u;
            m->dst_off_pitch = ((DITHER_W / 8) << 22) | (BL_FRAME / 8);
            m->sc_left_right = (DITHER_W - 1) << 16;
            m->sc_top_bottom = (DITHER_H - 1) << 16;
            m->clr_cmp_cntl  = 0;
            if (g->secondary) {
                const int white[3] = { 31, 63, 31 };
                const int prim[3] = { q->prim[0] ? q->prim[0] : white[0], q->prim[1] ? q->prim[1] : white[1],
                                      q->prim[2] ? q->prim[2] : white[2] };

                if (q->kind != kind) {
                    kind = q->kind;
                    modulate_texture(m, g, kind, BL_TEX + 0x2000);
                }
                modulate_solid(m, BL_TEX, q->kind == 4 ? prim : white);
                m->dp_pix_width = (4u << 28) | 6 | (modulate_code(g->format) << 4);
                ctx->regs[SCALE_3D_CNTL >> 2]        = 0x0541028bu;
                ctx->regs[TEX_SIZE_PITCH >> 2]       = 0x06660666u;
                ctx->regs[RP_TEX_CNTL >> 2]          = 0xc0080380u | (q->kind == 4 ? 0 : RP_COMP_FILTER_BILIN);
                ctx->regs[RP_SECONDARY_TEX_OFF >> 2] = BL_TEX + 0x2000;
                ctx->regs[(TEX_0_OFF + 6 * 4) >> 2]  = BL_TEX;
                modulate_vertices(m, q, 1, q->kind == 4 ? (0xff000000u | q->dif) : 0xffffffffu);
            } else {
                if (q->kind != kind) {
                    kind = q->kind;
                    modulate_texture(m, g, kind, BL_TEX);
                    if (g->format == 4 || g->format == 5) {
                        ctx->texture_palette[0] = ((uint32_t) g->lo[0] << 16) | ((uint32_t) g->lo[1] << 8) | (uint32_t) g->lo[2];
                        ctx->texture_palette[1] = ((uint32_t) g->hi[0] << 16) | ((uint32_t) g->hi[1] << 8) | (uint32_t) g->hi[2];
                    }
                }
                m->dp_pix_width = ((uint32_t) modulate_code(g->format) << 28) | 6 | ((g->format == 5) ? CI4_RGB_LOW_NIBBLE : 0);
                ctx->regs[SCALE_3D_CNTL >> 2]    = mip_filter_scale_3d_cntl(2);
                ctx->regs[RP_TEX_CNTL >> 2]      = 0x40000080u;
                ctx->regs[TEX_SIZE_PITCH >> 2]   = 0x6666;
                ctx->regs[(TEX_0_OFF + 24) >> 2] = BL_TEX;
                modulate_vertices(m, q, 0, 0xffffffffu);
            }
            for (int p = 0; p < q->count; p++) {
                const struct mod_probe *pr  = &mod_probes[q->first + p];
                const uint8_t          *px  = m->svga.vram + BL_FRAME + ((size_t) pr->y * DITHER_W + pr->x) * 4;
                unsigned                got = ((unsigned) px[2] << 16) | ((unsigned) px[1] << 8) | px[0];

                total[gi]++;
                if (got != pr->rgb) {
                    if (bad_group[gi]++ < 4)
                        fprintf(stderr, "modulate %s quad %d (%u,%u): %06x, the card's %06x\n", g->name, qi, pr->x, pr->y, got, pr->rgb);
                }
            }
        }
        printf("modulate %s: %d of %d probes exact\n", g->name, total[gi] - bad_group[gi], total[gi]);
        expect_int(g->name, bad_group[gi], 0);
    }
    card_close(m);
}

/*
 * The 2x2 filter's fraction bits by texture format. The formats the card was
 * measured with take 4 bits (VQ 3); the others keep the 8 bit fraction of the
 * filter before the measurement. A point halfway between two texels' centres'
 * 3/10 gives weight 64 (4 bits) or 76 (8 bits) of 256.
 */
static void
ragepro_filter_format_tests(void)
{
    static const struct { int format; int bits; } fmt[] = {
        { 2, 4 }, { 3, 4 }, { 4, 4 }, { 6, 4 }, { 7, 4 }, { 15, 4 }, { 8, 8 }, { 11, 8 }, { 14, 8 }
    };
    mach64_t    *m   = card_create();
    mach64_3d_t *ctx = m->gt3d;

    m->ragepro = 1;
    ctx->texture_palette[0] = 0x00102030;
    ctx->texture_palette[1] = 0x00f0e0d0;
    for (unsigned i = 0; i < sizeof(fmt) / sizeof(fmt[0]); i++) {
        mach64_rp_map_t map;
        int             inh;
        int             bpp;
        uint32_t        c0, c1, expect, got;
        unsigned        weight = (fmt[i].bits == 4) ? 64 : 76;

        mach64_rp_map_level(ctx, &map, fmt[i].format, 0x4000, 3, 0, 0, 0);
        bpp = map.tex.bpp;
        /* texels 0 and 1 of row 0: two different values of whatever the format holds */
        for (int b = 0; b < bpp; b++) {
            m->svga.vram[0x4000 + b]       = (uint8_t) (fmt[i].format == 2 ? 0 : 0x20 + b * 0x31);
            m->svga.vram[0x4000 + bpp + b] = (uint8_t) (fmt[i].format == 2 ? 1 : 0xe0 - b * 0x23);
        }
        c0 = mach64_3d_texel_at(ctx, &map.tex, 0, 0, &inh);
        c1 = mach64_3d_texel_at(ctx, &map.tex, 1, 0, &inh);
        expect = mach64_3d_lerp_texel(c0, c1, weight);
        /* x = 0.5 + 0.3 texel: s = (0.8) / 8 of a map eight texels wide; the row is
           the same in the second row of the filter */
        got = mach64_rp_sample_map(ctx, &ctx->agp, &map, 1, 0.8 / 8.0, 0.5 / 1.0, &inh);
        if (got != expect) {
            fprintf(stderr, "filter format %d: %08x, expected %08x (weight %u of 256)\n", fmt[i].format, got, expect, weight);
            failures++;
        }
        CHECK(c0 != c1);
        CHECK(mach64_rp_filter_bits(&map) == fmt[i].bits);
    }
    {
        mach64_rp_map_t map;

        mach64_rp_map_level(ctx, &map, 4, 0, 3, 3, 0, 0);
        map.vq = 1;
        CHECK(mach64_rp_filter_bits(&map) == 3);
    }
    card_close(m);
}

/*
 * The cell of the 2x2 filter at coordinates on and either side of a cell edge.
 * The expected cells come from exact integer arithmetic on a coordinate of
 * 1 / 2^20 texel. An edge, and a coordinate inside the 1 / 4096 of a cell the
 * filter rounds to, is on the lower side; one beyond it is not.
 */
static void
ragepro_filter_cell_tests(void)
{
    for (int bits = 3; bits <= 4; bits++) {
        const int64_t unit = (int64_t) 1 << 20;
        const int64_t half = (unit >> bits) / 8192; /* half of 1 / 4096 of a cell, in units of 2^-20 texel */

        for (int n = -40; n <= 200; n++) {
            int64_t edge = (int64_t) n * (unit >> bits);

            for (int d = -4; d <= 4; d++) {
                int64_t  a_int = edge + d * half / 2; /* a, in units of 2^-20 texel */
                double   a     = (double) a_int / (double) unit;
                int      texel;
                unsigned weight;
                int64_t  k = (a_int * ((int64_t) 1 << bits) * 4096 + (unit >> 1)) >> 20;
                int64_t  mm = ((k + 4095) >> 12) - 1;
                int64_t  m_edge = n - 1; /* the lower cell of the edge */

                mach64_rp_filter_cell(a, bits, &texel, &weight);
                if (texel != (int) (mm >> bits) || weight != (unsigned) (mm & ((1 << bits) - 1)) << (8 - bits)) {
                    if (failures < 24)
                        fprintf(stderr, "filter cell bits %d, a = %lld/2^20: texel %d weight %u, expected %d %u\n", bits,
                                (long long) a_int, texel, weight, (int) (mm >> bits),
                                (unsigned) (mm & ((1 << bits) - 1)) << (8 - bits));
                    failures++;
                }
                /* the stated behaviour at the edge: the edge itself and a coordinate within the rounding
                   of it, on either side, are the lower cell; two roundings beyond is not */
                if (d >= -2 && d <= 1 && mm != m_edge) {
                    if (failures < 24)
                        fprintf(stderr, "filter cell bits %d: %d near-edge units from edge %d is cell %lld, expected %lld\n", bits, d, n,
                                (long long) mm, (long long) m_edge);
                    failures++;
                }
                if (d >= 2 && mm != m_edge + 1) {
                    if (failures < 24)
                        fprintf(stderr, "filter cell bits %d: %d units beyond edge %d is cell %lld, expected %lld\n", bits, d, n,
                                (long long) mm, (long long) (m_edge + 1));
                    failures++;
                }
            }
        }
    }
    /* hand-checked edge cases at 4 bits: exactly on texel centre 1 (a = 1) is texel 0, weight 15/16; a = 2^-18 is the
       edge of cell 0 and, inside the rounding, the lower cell, texel -1; 2^-16 is beyond it, texel 0 weight 0 */
    {
        int      texel;
        unsigned weight;

        mach64_rp_filter_cell(1.0, 4, &texel, &weight);
        CHECK(texel == 0 && weight == 240);
        mach64_rp_filter_cell(0.0, 4, &texel, &weight);
        CHECK(texel == -1 && weight == 240);
        mach64_rp_filter_cell(1.0 / 262144.0, 4, &texel, &weight);
        CHECK(texel == -1 && weight == 240);
        mach64_rp_filter_cell(1.0 / 65536.0, 4, &texel, &weight);
        CHECK(texel == 0 && weight == 0);
        mach64_rp_filter_cell(-1.0 / 262144.0, 4, &texel, &weight);
        CHECK(texel == -1 && weight == 240);
        mach64_rp_filter_cell(0.3, 8, &texel, &weight);
        CHECK(texel == 0 && weight == 76);
    }
}

/*
 * Edges through the triangle path, for quads of different size and slope: a
 * 16-step strip in which a pixel's centre is placed exactly on a cell edge
 * (lower cell) or 2^-16 texel beyond it (upper cell), at 16 to 256 pixels
 * per texel and in a few quad sizes. The expected value is the 8 bit lerp of
 * the two texels with the weight the cell gives.
 */
static void
ragepro_filter_edge_triangle_tests(void)
{
    static const int ppt[4]  = { 16, 32, 64, 256 };
    static const int rows[3] = { 4, 8, 40 };
    mach64_t        *m   = card_create();
    mach64_3d_t     *ctx = m->gt3d;
    int              bad = 0;

    m->ragepro = 1;
    for (int pi = 0; pi < 4; pi++)
        for (int ri = 0; ri < 3; ri++)
            for (int n = 1; n < 16; n += 2)
                for (int beyond = 0; beyond <= 1; beyond++) {
                    /* texel columns 20, 21, 22: lo, hi, lo (565 pair 1 of the card's strips) */
                    const int lo[3] = { 6, 49, 18 }, hi[3] = { 27, 9, 31 };
                    struct mod_quad q;
                    struct mod_group g;
                    const int probe_x = 4 + 5;
                    double s_edge = 20.5 + n / 16.0;   /* a = 20 + n / 16 */
                    double s_probe = s_edge + (beyond ? 1.0 / 65536.0 : 0.0);
                    double u0 = s_probe - 5.5 / ppt[pi];
                    int    cell_m = beyond ? (20 * 16 + n) : (20 * 16 + n - 1);
                    int    left = cell_m >> 4, wt = cell_m & 15;
                    uint32_t w0, w1;
                    uint32_t expect = 0;

                    memset(&q, 0, sizeof(q));
                    memset(&g, 0, sizeof(g));
                    q.x0 = 4; q.y0 = 4; q.x1 = 4 + 64; q.y1 = 4 + rows[ri];
                    q.u0 = u0; q.v0 = 0.0; q.u1 = u0 + 64.0 / ppt[pi]; q.v1 = 1.0;
                    for (int i = 0; i < 3; i++) { g.lo[i] = lo[i]; g.hi[i] = hi[i]; }
                    g.format = 0;
                    memset(ctx->regs, 0, sizeof(ctx->regs));
                    ctx->setup_cntl = 0;
                    m->write_mask = 0xffffffffu; m->dp_src = 0x500u; m->dp_mix = 0x70007u;
                    m->dst_off_pitch = ((DITHER_W / 8) << 22) | (BL_FRAME / 8);
                    m->sc_left_right = (DITHER_W - 1) << 16; m->sc_top_bottom = (DITHER_H - 1) << 16;
                    m->clr_cmp_cntl = 0;
                    memset(m->svga.vram + BL_FRAME, 0, (size_t) DITHER_W * DITHER_H * 4);
                    modulate_texture(m, &g, 0, BL_TEX);
                    m->dp_pix_width = (4u << 28) | 6;
                    ctx->regs[SCALE_3D_CNTL >> 2]    = mip_filter_scale_3d_cntl(2);
                    ctx->regs[RP_TEX_CNTL >> 2]      = 0x40000080u;
                    ctx->regs[TEX_SIZE_PITCH >> 2]   = 0x6666;
                    ctx->regs[(TEX_0_OFF + 24) >> 2] = BL_TEX;
                    modulate_vertices(m, &q, 0, 0xffffffffu);
                    w0 = (left & 1) ? 1u : 0u;
                    w1 = 1u - w0;
                    {
                        uint32_t t0 = 0, t1 = 0;

                        for (int ch = 0; ch < 3; ch++) {
                            int bitsn = (ch == 1) ? 6 : 5;
                            int v0c = w0 ? hi[ch] : lo[ch], v1c = w1 ? hi[ch] : lo[ch];
                            int e0 = (v0c << (8 - bitsn)) | (v0c >> (2 * bitsn - 8));
                            int e1 = (v1c << (8 - bitsn)) | (v1c >> (2 * bitsn - 8));
                            int out = (e0 * (16 - wt) + e1 * wt + 8) >> 4;

                            expect |= (uint32_t) out << (16 - 8 * ch);
                            (void) t0; (void) t1;
                        }
                    }
                    {
                        const uint8_t *px = m->svga.vram + BL_FRAME + ((size_t) (4 + rows[ri] / 2) * DITHER_W + probe_x) * 4;
                        unsigned got = ((unsigned) px[2] << 16) | ((unsigned) px[1] << 8) | px[0];

                        if (got != expect) {
                            if (bad++ < 8)
                                fprintf(stderr, "filter edge: %d px/texel, %d rows, n %d, beyond %d: %06x, expected %06x\n", ppt[pi],
                                        rows[ri], n, beyond, got, expect);
                        }
                    }
                }
    expect_int("filter edge cases through triangles differing from the formula", bad, 0);
    card_close(m);
}

/*
 * Draw commands through the registers: trapezoids, lines and the state they
 * leave. The legacy registers they share with the draw engine are set
 * directly. The commands are those of RRG-G02700 4-43 to 4-48, and the
 * DST_BRES_LNTH bits 31 and 15 of the table on 4-46. The expected images
 * and interpolants are closed forms, not the engine's edge, clipping or
 * interpolation helpers.
 */
enum {
    SIZE           = 64,
    Z_BASE         = 0x10000,
    TEX_BASE       = 0x20000,
    TEX_3_OFF      = TEX_0_OFF + 3 * 4,
    SHADE          = 3u << 6,
    TEXTURE        = (2u << 6) | (1u << 24), /* Single map, nearest sampling. */
    Z_LEQUAL_WRITE = 0x121
};

#define BACKGROUND 0x19324b64u
#define INITIAL_Z  60000u

static unsigned cases;
static char     case_name[160];

static void
case_expect(const char *what, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        if (failures < 24)
            fprintf(stderr, "%s: %s: %08x, not %08x\n", case_name, what, actual, expected);
        failures++;
    }
}

static void
write_reg(mach64_t *m, uint32_t address, uint32_t value)
{
    case_expect("3D register claimed", mach64_3d_write(m, address, value, FIFO_WRITE_DWORD), 1);
}

static uint32_t
read_reg(mach64_t *m, uint32_t address)
{
    uint32_t value = 0;
    case_expect("3D read claimed", mach64_3d_read(m, address, &value), 1);
    return value;
}

static uint32_t
load_pixel(mach64_t *m, unsigned address, unsigned bytes)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; i++)
        value |= (uint32_t) m->svga.vram[address + i] << (8 * i);
    return value;
}

static void
store_pixel(mach64_t *m, unsigned address, unsigned bytes, uint32_t value)
{
    for (unsigned i = 0; i < bytes; i++)
        m->svga.vram[address + i] = (uint8_t) (value >> (8 * i));
}

static uint32_t
pack_color(uint32_t color, int format)
{
    if (format == 3) /* ARGB1555: alpha occupies the otherwise unused high bit. */
        return ((color >> 16) & 0x8000u) | ((color >> 9) & 0x7c00u) | ((color >> 6) & 0x03e0u) | ((color >> 3) & 0x001fu);
    if (format == 4) /* RGB565, dithering/rounding disabled. */
        return ((color >> 8) & 0xf800u) | ((color >> 5) & 0x07e0u) | ((color >> 3) & 0x001fu);
    return color;
}

static unsigned
destination_bytes(int format)
{
    return format == 6 ? 4 : 2;
}

static uint32_t
texture_color(int u, int v)
{
    return 0xff000000u | (uint32_t) (40 + 20 * u) << 16 | (uint32_t) (30 + 24 * v) << 8 | (uint32_t) (16 + 8 * (u + v));
}

static void
clear_surfaces(mach64_t *m, int format)
{
    unsigned bytes = destination_bytes(format);
    for (unsigned i = 0; i < SIZE * SIZE; i++) {
        store_pixel(m, i * bytes, bytes, pack_color(BACKGROUND, format));
        store_pixel(m, Z_BASE + i * 2, 2, INITIAL_Z);
    }
}

static void
setup(mach64_t *m, int format)
{
    mach64_3d_close(m->gt3d);
    m->gt3d          = mach64_3d_init(m);
    m->dp_pix_width  = 0x60000600u | (unsigned) format;
    m->dp_src        = 0x00000500u;
    m->dp_mix        = 0x00070007u;
    m->write_mask    = 0xffffffffu;
    m->clr_cmp_cntl  = 0;
    m->dst_off_pitch = (SIZE / 8) << 22;
    m->dst_cntl      = DST_X_DIR | DST_Y_DIR | TRAIL_X_DIR | TRAP_FILL_DIR;
    m->sc_left_right = (SIZE - 1) << 16;
    m->sc_top_bottom = (SIZE - 1) << 16;
    m->dst_bres_err  = (uint32_t) -1;
    m->dst_bres_inc  = 0;
    m->dst_bres_dec  = (uint32_t) -1;
    write_reg(m, TRAIL_BRES_ERR, (uint32_t) -1);
    write_reg(m, TRAIL_BRES_INC, 0);
    write_reg(m, TRAIL_BRES_DEC, (uint32_t) -1);
    write_reg(m, Z_OFF_PITCH, ((SIZE / 8) << 22) | (Z_BASE >> 3));
    write_reg(m, Z_CNTL, Z_LEQUAL_WRITE);
    write_reg(m, SCALE_3D_CNTL, SHADE);
    /* Explicitly reprogram all live interpolators between independent cases. */
    for (unsigned a = S_X_INC2; a <= T_START; a += 4)
        write_reg(m, a, 0);
    for (unsigned a = RED_X_INC; a <= ALPHA_START; a += 4)
        write_reg(m, a, 0);
    write_reg(m, RED_START, 32u << 16);
    write_reg(m, GREEN_START, 64u << 16);
    write_reg(m, BLUE_START, 96u << 16);
    write_reg(m, ALPHA_START, 255u << 16);
    write_reg(m, Z_START, 1000u << 12);
    write_reg(m, TEX_SIZE_PITCH, 0x333);
    write_reg(m, TEX_3_OFF, TEX_BASE);
    for (int v = 0; v < 8; v++)
        for (int u = 0; u < 8; u++)
            store_pixel(m, TEX_BASE + (v * 8 + u) * 4, 4, texture_color(u, v));
    clear_surfaces(m, format);
}

static uint32_t
xy(int x, int y)
{
    return ((uint32_t) x & 0x1fffu) << 16 | ((uint32_t) y & 0x7fffu);
}

/* Coefficients are raw fixed-point register images. Include low derivative
 * bits so continuation checks also exercise hidden S/T fractional precision. */
static const int s_coeff[6] = { 0x200000, 0x400003, 0x100007,
                                0x040001, 0x020003, 0x010005 };
static const int t_coeff[6] = { 0x200000, 0x080005, 0x400009,
                                0x020001, 0x040003, 0x010007 };

static int64_t
polynomial(const int *c, int x, int y)
{
    return c[0] + (int64_t) x * c[1] + (int64_t) y * c[2] + (int64_t) x * (x - 1) / 2 * c[3] + (int64_t) y * (y - 1) / 2 * c[4] + (int64_t) x * y * c[5];
}

static void
set_texture_polynomial(mach64_t *m, unsigned base, const int *c)
{
    write_reg(m, base + 0, c[3]);
    write_reg(m, base + 4, c[4]);
    write_reg(m, base + 8, c[5]);
    write_reg(m, base + 12, c[1]);
    write_reg(m, base + 16, c[2]);
    write_reg(m, base + 20, c[0]);
}

static void
check_continuation(mach64_t *m, int ox, int oy, int dx, int dy,
                   int slope, int row)
{
    int n = slope * row;
    case_expect("live destination", m->dst_y_x, xy(ox + dx * n, oy + dy * row));
    case_expect("lead error", m->dst_bres_err, (uint32_t) -1);
    case_expect("trail error", read_reg(m, TRAIL_BRES_ERR), (uint32_t) -1);
    case_expect("red continuation", read_reg(m, RED_START), (32 + 2 * n + row) << 16);
    case_expect("green continuation", read_reg(m, GREEN_START), (64 + n) << 16);
    case_expect("blue continuation", read_reg(m, BLUE_START), (96 + 2 * row) << 16);
    case_expect("alpha continuation", read_reg(m, ALPHA_START), 255u << 16);
    case_expect("depth continuation", read_reg(m, Z_START), (1000 + 3 * n + 5 * row) << 12);
    case_expect("S continuation", read_reg(m, S_START),
                (uint32_t) polynomial(s_coeff, n, row) & 0x03ffffe0u);
    case_expect("T continuation", read_reg(m, T_START),
                (uint32_t) polynomial(t_coeff, n, row) & 0x03ffffe0u);
    case_expect("S X derivative", read_reg(m, S_XINC_START), s_coeff[1] + n * s_coeff[3] + row * s_coeff[5]);
    case_expect("S Y derivative", read_reg(m, S_Y_INC), s_coeff[2] + n * s_coeff[5] + row * s_coeff[4]);
    case_expect("T X derivative", read_reg(m, T_XINC_START), t_coeff[1] + n * t_coeff[3] + row * t_coeff[5]);
    case_expect("T Y derivative", read_reg(m, T_Y_INC), t_coeff[2] + n * t_coeff[5] + row * t_coeff[4]);
}

static void
triangle_case(mach64_t *m, int format, int textured, int dx, int dy,
              int clip, int slope, int additive)
{
    int      ox = dx > 0 ? 8 : 32, oy = dy > 0 ? 8 : 32;
    int      left = 0, right = SIZE - 1, top = 0, bottom = SIZE - 1;
    unsigned bytes = destination_bytes(format);
    snprintf(case_name, sizeof(case_name), "triangle format=%d tex=%d dir=%d,%d clip=%d slope=%d add=%d",
             format, textured, dx, dy, clip, slope, additive);
    cases++;
    setup(m, format);
    if (clip == 1) {
        left   = ox + dx * (dx > 0 ? 1 : 9);
        right  = ox + dx * (dx > 0 ? 9 : 1);
        top    = oy + dy * (dy > 0 ? 2 : 5);
        bottom = oy + dy * (dy > 0 ? 5 : 2);
    } else if (clip == 2) {
        left  = 48; /* Both halves invisible, but trajectory must still advance. */
        right = 56;
    }
    m->sc_left_right = (unsigned) left | (unsigned) right << 16;
    m->sc_top_bottom = (unsigned) top | (unsigned) bottom << 16;
    m->dst_cntl      = (dx > 0 ? DST_X_DIR | TRAIL_X_DIR | TRAP_FILL_DIR : 0) | (dy > 0 ? DST_Y_DIR : 0);
    m->dst_bres_inc  = slope;
    write_reg(m, TRAIL_BRES_INC, slope + 2);
    write_reg(m, DST_Y_X_ALIAS, xy(ox, oy));
    write_reg(m, RED_X_INC, 2u << 16);
    write_reg(m, RED_Y_INC, 1u << 16);
    write_reg(m, GREEN_X_INC, 1u << 16);
    write_reg(m, BLUE_Y_INC, 2u << 16);
    write_reg(m, Z_X_INC, 3u << 12);
    write_reg(m, Z_Y_INC, 5u << 12);
    set_texture_polynomial(m, S_X_INC2, s_coeff);
    set_texture_polynomial(m, T_X_INC2, t_coeff);
    write_reg(m, SCALE_3D_CNTL, (textured ? TEXTURE : SHADE) | (additive ? (1u << 11) | (1u << 16) | (1u << 19) : 0));
    if (additive) {
        /* ONE + ONE onto black exposes double coverage at the join. Disable
         * Z so a depth rejection cannot hide a duplicate color write. */
        memset(m->svga.vram, 0, SIZE * SIZE * bytes);
        write_reg(m, Z_CNTL, 0);
    }

    /* Vertices in local coordinates: (0,0), (4*slope+8,4), (8*slope,8).
     * The leading edge continues across both four-scanline trapezoids. */
    write_reg(m, DST_BRES_LNTH, LINE_DIS | (unsigned) ox << 16 | DRAW_TRAP | 4);
    check_continuation(m, ox, oy, dx, dy, slope, 4);
    m->dst_cntl ^= TRAIL_X_DIR;
    write_reg(m, TRAIL_BRES_INC, 2 - slope);
    write_reg(m, LEAD_BRES_LNTH, DRAW_TRAP | 4); /* Retain the live trailing X. */
    check_continuation(m, ox, oy, dx, dy, slope, 8);

    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            int      r = (y - oy) * dy, n = (x - ox) * dx;
            int      width  = r < 4 ? 2 * r : 2 * (8 - r);
            int      inside = r >= 0 && r < 8 && n >= slope * r && n < slope * r + width && x >= left && x <= right && y >= top && y <= bottom;
            uint32_t color  = additive ? 0 : BACKGROUND;
            uint32_t depth  = INITIAL_Z;
            char     label[64];
            if (inside) {
                if (textured) {
                    int u = (int) (polynomial(s_coeff, n, r) >> 23) & 7;
                    int v = (int) (polynomial(t_coeff, n, r) >> 23) & 7;
                    color = texture_color(u, v);
                } else {
                    color = 0xff000000u | (unsigned) (32 + 2 * n + r) << 16 | (unsigned) (64 + n) << 8 | (unsigned) (96 + 2 * r);
                }
                if (!additive)
                    depth = 1000 + 3 * n + 5 * r;
            }
            snprintf(label, sizeof(label), "color at %d,%d", x, y);
            case_expect(label, load_pixel(m, (y * SIZE + x) * bytes, bytes), pack_color(color, format));
            snprintf(label, sizeof(label), "depth at %d,%d", x, y);
            case_expect(label, load_pixel(m, Z_BASE + (y * SIZE + x) * 2, 2), depth);
        }
    }
}

static void
check_surface_unchanged(mach64_t *m)
{
    for (unsigned i = 0; i < SIZE * SIZE; i++) {
        case_expect("preload leaves color untouched", load_pixel(m, i * 4, 4), BACKGROUND);
        case_expect("preload leaves Z untouched", load_pixel(m, Z_BASE + i * 2, 2), INITIAL_Z);
    }
}

static void
preload_case(mach64_t *m, unsigned address, unsigned bytes, int shaded, int stale)
{
    uint32_t command = LINE_DIS | (12u << 16) | 2;
    uint32_t type    = bytes == 4 ? FIFO_WRITE_DWORD : bytes == 2 ? FIFO_WRITE_WORD
                                                                  : FIFO_WRITE_BYTE;
    snprintf(case_name, sizeof(case_name), "preload alias=%03x bytes=%u shaded=%d stale=%d",
             address, bytes, shaded, stale);
    cases++;
    setup(m, 6);
    if (stale) {
        write_reg(m, DST_Y_X_ALIAS, xy(8, 8));
        write_reg(m, LEAD_BRES_LNTH, LINE_DIS | (10u << 16) | DRAW_TRAP | 1);
        clear_surfaces(m, 6);
    }
    write_reg(m, DST_Y_X_ALIAS, xy(8, 8));
    write_reg(m, SCALE_3D_CNTL, shaded ? SHADE : 0);
    for (unsigned lane = 0; lane < 4; lane += bytes) {
        /* An ordinary/shared 0x120 write can be unclaimed. No legacy draw is
         * requested: the completing high byte has LINE_DIS set. */
        mach64_3d_write(m, address + lane, command >> (8 * lane), type);
        check_surface_unchanged(m);
        case_expect("preload does not move lead", m->dst_y_x, xy(8, 8));
    }
    write_reg(m, SCALE_3D_CNTL, SHADE);
    /* Deliberately supply a different upper field: bit 31=0/bit 15=1 must
     * retain the preloaded X=12, not load X=2 or reuse a previous X=10. */
    command = (2u << 16) | DRAW_TRAP | 2;
    for (unsigned lane = 0; lane < 4; lane += bytes) {
        unsigned other_alias = address == LEAD_BRES_LNTH ? DST_BRES_LNTH : LEAD_BRES_LNTH;
        int      claimed     = mach64_3d_write(m, other_alias + lane, command >> (8 * lane), type);
        if (lane + bytes == 4)
            case_expect("completed trapezoid claimed", claimed, 1);
        else
            check_surface_unchanged(m);
    }
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            int  inside = y >= 8 && y < 10 && x >= 8 && x < 12;
            char label[64];
            snprintf(label, sizeof(label), "preloaded span at %d,%d", x, y);
            case_expect(label, load_pixel(m, (y * SIZE + x) * 4, 4),
                        inside ? 0xff204060u : BACKGROUND);
            case_expect("preloaded depth", load_pixel(m, Z_BASE + (y * SIZE + x) * 2, 2),
                        inside ? 1000u : INITIAL_Z);
        }
    }
}

static void
rectangle(mach64_t *m)
{
    write_reg(m, DST_Y_X_ALIAS, xy(8, 8));
    write_reg(m, DST_BRES_LNTH, LINE_DIS | (12u << 16) | DRAW_TRAP | 1);
    write_reg(m, LEAD_BRES_LNTH, DRAW_TRAP | 1);
}

static void
depth_case(mach64_t *m, int format, int enabled, int write_enabled,
           unsigned function, unsigned relation)
{
    /* Comparison truth tables for source <, ==, > destination respectively.
     * Bits name the eight documented Z_TEST functions, not engine helpers. */
    static const unsigned passes[3]   = { 0xc6, 0x9c, 0xf0 };
    unsigned              source_z    = 999 + relation;
    unsigned              bytes       = destination_bytes(format);
    int                   passes_test = !enabled || ((passes[relation] >> function) & 1u);
    snprintf(case_name, sizeof(case_name), "depth format=%d enabled=%d write=%d fn=%u relation=%u",
             format, enabled, write_enabled, function, relation);
    cases++;
    setup(m, format);
    write_reg(m, Z_CNTL, (unsigned) enabled | (function << 4) | ((unsigned) write_enabled << 8));
    write_reg(m, Z_START, source_z << 12);
    for (int y = 8; y < 10; y++)
        for (int x = 8; x < 12; x++)
            store_pixel(m, Z_BASE + (y * SIZE + x) * 2, 2, 1000);
    rectangle(m);
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            int      inside = y >= 8 && y < 10 && x >= 8 && x < 12;
            uint32_t color  = inside && passes_test ? 0xff204060u : BACKGROUND;
            unsigned depth  = inside ? 1000 : INITIAL_Z;
            if (inside && passes_test && enabled && write_enabled)
                depth = source_z;
            case_expect("depth-gated color", load_pixel(m, (y * SIZE + x) * bytes, bytes), pack_color(color, format));
            case_expect("depth write enable", load_pixel(m, Z_BASE + (y * SIZE + x) * 2, 2), depth);
        }
    }
}

static void
visibility_case(mach64_t *m, int format, int mode)
{
    unsigned bytes   = destination_bytes(format);
    unsigned control = mode == 2 ? SHADE : TEXTURE;
    snprintf(case_name, sizeof(case_name), "visibility format=%d mode=%d", format, mode);
    cases++;
    setup(m, format);
    write_reg(m, S_XINC_START, 1u << 23); /* One texel per pixel in an 8-wide map. */
    for (int u = 0; u < 4; u++) {
        uint32_t color = (u & 1) ? 0xff20c040u : 0xffff00ffu;
        if (mode == 1 && !(u & 1))
            color &= 0x00ffffffu; /* Alpha LSB clear: inhibit color and Z. */
        store_pixel(m, TEX_BASE + u * 4, 4, color);
    }
    if (mode == 0) {
        m->clr_cmp_cntl = 0x02000005u; /* Equal texel RGB inhibits. */
        m->clr_cmp_clr  = 0x00ff00ffu;
        m->clr_cmp_mask = 0x00ffffffu;
    } else if (mode == 1) {
        control |= (1u << 30) | (1u << 28); /* Texture alpha + alpha mask. */
    } else {
        m->clr_cmp_cntl = 5; /* Equal packed destination inhibits. */
        m->clr_cmp_clr  = pack_color(BACKGROUND, format);
        m->clr_cmp_mask = bytes == 2 ? 0xffffu : 0xffffffffu;
        for (int y = 8; y < 10; y++)
            for (int x = 9; x < 12; x += 2)
                store_pixel(m, (y * SIZE + x) * bytes, bytes, 0);
    }
    write_reg(m, SCALE_3D_CNTL, control);
    for (int pass = 0; pass < 2; pass++) {
        if (pass) {
            /* Disable inhibition without resetting the engine or its VRAM. */
            m->clr_cmp_cntl = 0;
            write_reg(m, SCALE_3D_CNTL, mode == 2 ? SHADE : TEXTURE);
            write_reg(m, Z_START, 900u << 12);
            write_reg(m, S_START, 0);
        }
        rectangle(m);
        for (int y = 0; y < SIZE; y++) {
            for (int x = 0; x < SIZE; x++) {
                int      inside  = y >= 8 && y < 10 && x >= 8 && x < 12;
                int      visible = inside && (pass || (x & 1));
                uint32_t color   = BACKGROUND;
                unsigned depth   = INITIAL_Z;
                if (visible) {
                    color = mode == 2 ? 0xff204060u : (x & 1) ? 0xff20c040u
                                                              : 0xffff00ffu;
                    depth = pass ? 900 : 1000;
                }
                case_expect("visibility color", load_pixel(m, (y * SIZE + x) * bytes, bytes), pack_color(color, format));
                case_expect("inhibited texel must not occlude later geometry",
                            load_pixel(m, Z_BASE + (y * SIZE + x) * 2, 2), depth);
            }
        }
    }
}

static void
depth_field_case(mach64_t *m, int format, unsigned function, int writes, int dir, int split)
{
    static const unsigned passes[3] = { 0xc6, 0x9c, 0xf0 };
    const unsigned        stored_z  = 48928; /* Equal to column 3 on the first row. */
    int                   ox = dir > 0 ? 8 : 24, oy = dir > 0 ? 8 : 24;
    int                   left = ox + dir * (dir > 0 ? 2 : 6), right = ox + dir * (dir > 0 ? 6 : 2);
    unsigned              bytes = destination_bytes(format);
    snprintf(case_name, sizeof(case_name), "depth field format=%d fn=%u write=%d dir=%d split=%d",
             format, function, writes, dir, split);
    cases++;
    setup(m, format);
    m->dst_cntl      = dir > 0 ? DST_X_DIR | DST_Y_DIR | TRAIL_X_DIR | TRAP_FILL_DIR : 0;
    m->sc_left_right = (unsigned) left | (unsigned) right << 16;
    write_reg(m, Z_CNTL, 1u | function << 4 | (unsigned) writes << 8);
    write_reg(m, Z_START, 30000u << 12);
    write_reg(m, Z_X_INC, 50000u << 12);
    write_reg(m, Z_Y_INC, (uint32_t) (-40000 * 4096));
    for (unsigned i = 0; i < SIZE * SIZE; i++)
        store_pixel(m, Z_BASE + i * 2, 2, stored_z);
    write_reg(m, DST_Y_X_ALIAS, xy(ox, oy));
    write_reg(m, DST_BRES_LNTH, LINE_DIS | (unsigned) (ox + dir * 8) << 16 | DRAW_TRAP | (split ? 2 : 4));
    if (split)
        write_reg(m, LEAD_BRES_LNTH, DRAW_TRAP | 2);
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            int      row = (y - oy) * dir, column = (x - ox) * dir;
            int      inside = row >= 0 && row < 4 && column >= 2 && column <= 6;
            unsigned depth  = stored_z;
            uint32_t color  = BACKGROUND;
            if (inside) {
                int physical = (30000 + column * 50000 - row * 40000) % 131072;
                if (physical < 0)
                    physical += 131072;
                unsigned incoming = physical < 65536 ? (unsigned) physical : 0;
                unsigned relation = incoming < stored_z ? 0 : incoming == stored_z ? 1
                                                                                   : 2;
                if ((passes[relation] >> function) & 1u) {
                    color = 0xff204060u;
                    if (writes)
                        depth = incoming;
                }
            }
            case_expect("physical Z compare gates color", load_pixel(m, (y * SIZE + x) * bytes, bytes),
                        pack_color(color, format));
            case_expect("physical Z conditional write", load_pixel(m, Z_BASE + (y * SIZE + x) * 2, 2), depth);
        }
    }
    case_expect("physical Z continuation", read_reg(m, Z_START), (uint32_t) (-130000 * 4096) & 0x1fffffffu);
}

/* Scissors are inclusive signed 13/15-bit bounds. Empty rectangles inhibit
 * writes but must still allow the command's live trajectory to advance. */
static void
scissor_case(mach64_t *m, int mode, int line)
{
    static const int bounds[][4] = {
        { 12, 8,  0,  63 },
        { 0,  63, 10, 8  },
        { 0,  -1, 0,  63 },
        { 0,  63, 0,  -1 },
        { -4, 9,  0,  63 },
        { 0,  63, -4, 8  },
        { 9,  9,  8,  8  },
        { 70, 75, 0,  63 },
        { 0,  63, 0,  63 }
    };
    int left = bounds[mode][0], right = bounds[mode][1];
    int top = bounds[mode][2], bottom = bounds[mode][3];
    snprintf(case_name, sizeof(case_name), "signed/empty scissor mode=%d line=%d", mode, line);
    cases++;
    setup(m, 6);
    m->sc_left_right = ((uint32_t) left & 0x3fffu) | ((uint32_t) right & 0x3fffu) << 16;
    m->sc_top_bottom = ((uint32_t) top & 0x7fffu) | ((uint32_t) bottom & 0x7fffu) << 16;
    write_reg(m, DST_Y_X_ALIAS, xy(8, 8));
    unsigned expected_status = (8 < left ? 0x10u : 0) | (8 > right ? 0x20u : 0) | (8 < top ? 0x40u : 0) | (8 > bottom ? 0x80u : 0);
    case_expect("signed GUI scissor comparisons", read_reg(m, 0x338) & 0xf0u, expected_status);
    if (line == 1) {
        m->dst_cntl |= DST_LAST_PEL;
        write_reg(m, DST_BRES_LNTH, 4);
        case_expect("clipped line still advances", m->dst_y_x, xy(11, 8));
    } else if (line == 2) {
        store_pixel(m, TEX_BASE, 4, 0xff204060u);
        write_reg(m, 0x1c0, TEX_BASE);
        write_reg(m, 0x1dc, 1);
        write_reg(m, 0x1e0, 1);
        write_reg(m, 0x1ec, 1);
        write_reg(m, 0x1f0, 1u << 16);
        write_reg(m, 0x1f4, 1u << 16);
        write_reg(m, 0x1f8, 0);
        write_reg(m, 0x3c8, 0);
        write_reg(m, SCALE_3D_CNTL, 0x140);
        write_reg(m, 0x118, (4u << 16) | 2);
        case_expect("clipped scaler still advances", read_reg(m, 0x1f8), 2u << 16);
    } else {
        write_reg(m, RED_Y_INC, 1u << 16);
        write_reg(m, Z_Y_INC, 1u << 12);
        rectangle(m);
        case_expect("clipped trapezoid still advances", m->dst_y_x, xy(8, 10));
        case_expect("clipped color still advances", read_reg(m, RED_START), 34u << 16);
        case_expect("clipped Z still advances", read_reg(m, Z_START), 1002u << 12);
    }
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            int      inside = x >= 8 && x < 12 && y >= 8 && y < (line == 1 ? 9 : 10) && x >= left && x <= right && y >= top && y <= bottom;
            unsigned red    = 32 + (line ? 0 : y - 8);
            uint32_t color  = inside ? 0xff004060u | red << 16 : BACKGROUND;
            case_expect("scissor write inhibition", load_pixel(m, (y * SIZE + x) * 4, 4), color);
            case_expect("scissor Z inhibition", load_pixel(m, Z_BASE + (y * SIZE + x) * 2, 2),
                        inside && line != 2 ? 1000u + (unsigned) (line ? 0 : y - 8) : INITIAL_Z);
        }
    }
}

/* The FIFO thread applies shared 2D state before dispatching the next 3D
 * command. Classification must use this ordered state. */
static void
line_dispatch_case(mach64_t *m, int mode, unsigned alias)
{
    snprintf(case_name, sizeof(case_name), "ordered line dispatch mode=%d alias=%03x", mode, alias);
    cases++;
    setup(m, 6);
    m->dst_cntl |= DST_LAST_PEL;
    write_reg(m, DST_Y_X_ALIAS, xy(8, 8));
    if (mode == 1)
        m->dp_src = 0x00000100u;
    if (mode == 2)
        m->dst_cntl |= DST_POLYGON_EN;
    fifo_shared_source = mode == 0 ? 0x00000100u : 0x00000500u;
    fifo_shared_cntl   = mode == 3 ? m->dst_cntl | DST_POLYGON_EN : m->dst_cntl & ~DST_POLYGON_EN;
    fifo_shared_update = 1;
    case_expect("source change is queued", mach64_3d_write(m, 0x2d8, fifo_shared_source, FIFO_WRITE_DWORD), 0);
    case_expect("polygon change is queued", mach64_3d_write(m, 0x130, fifo_shared_cntl, FIFO_WRITE_DWORD), 0);
    /* Model the 2D fallbacks in mach64_fifo_run_one, rather than the old
       CPU-side drain that was removed when 3D writes joined the FIFO. */
    m->dp_src = fifo_shared_source;
    m->dst_cntl = fifo_shared_cntl;
    fifo_shared_update = 0;
    int      shaded = mode == 1 || mode == 2;
    unsigned result = mach64_3d_write(m, alias, 4, FIFO_WRITE_DWORD);
    /* Alias 0x144 belongs to the 3D front end even for non-drawing state. */
    case_expect("dispatch uses ordered shared state", result, shaded || alias == LEAD_BRES_LNTH);
    case_expect("pending state was consumed before dispatch", fifo_shared_update, 0);
    case_expect("line dispatch pixel", load_pixel(m, (8 * SIZE + 8) * 4, 4),
                shaded ? 0xff204060u : BACKGROUND);
    case_expect("line dispatch depth", load_pixel(m, Z_BASE + (8 * SIZE + 8) * 2, 2),
                shaded ? 1000 : INITIAL_Z);
    fifo_shared_update = 0;
}

/* An additive black texel or a fully transparent source must not accumulate
 * RGB noise in the destination. Exercise every table phase and framebuffer
 * component level, including partial-channel (red-only) contributions. */
static void
ordered_blend_identity_case(mach64_t *m, int format, int mode)
{
    uint32_t rgb_mask = format == 3 ? 0x7fffu : 0xffffu;
    uint32_t red_mask = format == 3 ? 0x7c00u : 0xf800u;
    unsigned levels   = format == 3 ? 32 : 64;
    snprintf(case_name, sizeof(case_name), "ordered blend identity format=%d mode=%d", format, mode);
    cases++;
    setup(m, format);
    write_reg(m, Z_CNTL, 0);
    write_reg(m, SCALE_3D_CNTL, SHADE | 6u | (1u << 11) | ((mode == 1 ? 4u : 1u) << 16) | ((mode == 1 ? 5u : 1u) << 19));
    write_reg(m, RED_START, (mode ? 255u : 0u) << 16);
    write_reg(m, GREEN_START, (mode == 1 ? 255u : 0u) << 16);
    write_reg(m, BLUE_START, (mode == 1 ? 255u : 0u) << 16);
    write_reg(m, ALPHA_START, 0);
    for (unsigned level = 0; level < levels; level++) {
        uint32_t background = format == 3 ? ((level << 10) | ((31 - level) << 5) | ((level * 7) & 31)) : (((level & 31) << 11) | ((63 - level) << 5) | ((level * 7) & 31));
        for (int y = 8; y < 12; y++)
            for (int x = 8; x < 12; x++)
                store_pixel(m, (y * SIZE + x) * 2, 2, background);
        for (int pass = 0; pass < 4; pass++) {
            write_reg(m, DST_Y_X_ALIAS, xy(8, 8));
            write_reg(m, DST_BRES_LNTH, LINE_DIS | (12u << 16) | DRAW_TRAP | 4);
            for (int y = 8; y < 12; y++)
                for (int x = 8; x < 12; x++)
                    case_expect("unchanged blend components", load_pixel(m, (y * SIZE + x) * 2, 2) & rgb_mask,
                                mode == 2 ? (background | red_mask) : background);
        }
    }
}

static void
blend_transition_case(mach64_t *m, int format)
{
    static const uint32_t source[3] = { 0xff102030u, 0xff203040u, 0xff406080u };
    unsigned              bytes     = destination_bytes(format);
    snprintf(case_name, sizeof(case_name), "opaque/additive/opaque format=%d", format);
    cases++;
    setup(m, format);
    for (int pass = 0; pass < 3; pass++) {
        write_reg(m, RED_START, ((source[pass] >> 16) & 255u) << 16);
        write_reg(m, GREEN_START, ((source[pass] >> 8) & 255u) << 16);
        write_reg(m, BLUE_START, (source[pass] & 255u) << 16);
        write_reg(m, SCALE_3D_CNTL, SHADE | (pass == 1 ? (1u << 11) | (1u << 16) | (1u << 19) : 0));
        write_reg(m, Z_CNTL, pass == 1 ? 0x21 : Z_LEQUAL_WRITE);
        write_reg(m, Z_START, (pass == 2 ? 500u : 1000u) << 12);
        rectangle(m);
        for (int y = 0; y < SIZE; y++) {
            for (int x = 0; x < SIZE; x++) {
                int inside = y >= 8 && y < 10 && x >= 8 && x < 12;
                /* For RGB565 the first pass expands to (16,32,49), so
                 * the additive blue sum is 113, which still packs to 112. */
                uint32_t color = inside ? (pass == 1 ? 0xff305070u : source[pass]) : BACKGROUND;
                unsigned depth = inside ? (pass == 2 ? 500 : 1000) : INITIAL_Z;
                case_expect("blend transition color", load_pixel(m, (y * SIZE + x) * bytes, bytes), pack_color(color, format));
                case_expect("blend transition depth", load_pixel(m, Z_BASE + (y * SIZE + x) * 2, 2), depth);
            }
        }
    }
}

/* Exercise color-field crossings through the actual fragment consumers, not
 * only a conversion helper. White texels isolate modulation from sampling;
 * the blend case uses SRC_ALPHA and ZERO. */
static void
color_field_case(mach64_t *m, int format, int mode, int dx, int dy, int clip, int split)
{
    static const unsigned registers[] = { RED_X_INC, GREEN_X_INC, BLUE_X_INC, ALPHA_X_INC };
    static const int      start[]     = { 220, 100, 10, 128 };
    static const int      x_inc[]     = { 100, -100, 200, 200 };
    static const int      y_inc[]     = { -192, 160, -64, 100 };
    int                   ox = dx > 0 ? 8 : 24, oy = dy > 0 ? 8 : 24;
    int                   left = 0, right = SIZE - 1;
    unsigned              bytes = destination_bytes(format);

    snprintf(case_name, sizeof(case_name), "color field format=%d mode=%d dir=%d,%d clip=%d split=%d",
             format, mode, dx, dy, clip, split);
    cases++;
    setup(m, format);
    m->dst_cntl = (dx > 0 ? DST_X_DIR | TRAIL_X_DIR | TRAP_FILL_DIR : 0) | (dy > 0 ? DST_Y_DIR : 0);
    if (clip) {
        left             = ox + dx * (dx > 0 ? 2 : 6);
        right            = ox + dx * (dx > 0 ? 6 : 2);
        m->sc_left_right = (unsigned) left | (unsigned) right << 16;
    }
    write_reg(m, SCALE_3D_CNTL, (mode ? TEXTURE | (1u << 22) : SHADE) | (mode == 2 ? (1u << 11) | (4u << 16) : 0));
    for (unsigned i = 0; i < 64; i++)
        store_pixel(m, TEX_BASE + i * 4, 4, 0xffffffffu);
    for (unsigned c = 0; c < 4; c++) {
        write_reg(m, registers[c], (uint32_t) (x_inc[c] * 65536));
        write_reg(m, registers[c] + 4, (uint32_t) (y_inc[c] * 65536));
        write_reg(m, registers[c] + 8, (uint32_t) (start[c] * 65536));
    }
    write_reg(m, DST_Y_X_ALIAS, xy(ox, oy));
    write_reg(m, DST_BRES_LNTH, LINE_DIS | (unsigned) (ox + dx * 8) << 16 | DRAW_TRAP | (split ? 2 : 4));
    if (split)
        write_reg(m, LEAD_BRES_LNTH, DRAW_TRAP | 2);

    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            int      row = (y - oy) * dy, column = (x - ox) * dx;
            int      inside = row >= 0 && row < 4 && column >= 0 && column < 8 && x >= left && x <= right;
            uint32_t color  = BACKGROUND;
            if (inside) {
                unsigned component[4];
                for (unsigned c = 0; c < 4; c++) {
                    int value = (start[c] + column * x_inc[c] + row * y_inc[c]) % 512;
                    if (value < 0)
                        value += 512;
                    component[c] = value < 256 ? (unsigned) value : 0;
                }
                if (mode == 2) {
                    unsigned alpha = component[3];
                    for (unsigned c = 0; c < 4; c++)
                        component[c] = (component[c] * alpha + 127) / 255;
                }
                color = component[3] << 24 | component[0] << 16 | component[1] << 8 | component[2];
            }
            case_expect("color-field pipeline", load_pixel(m, (y * SIZE + x) * bytes, bytes),
                        pack_color(color, format));
            case_expect("color-field depth unchanged", load_pixel(m, Z_BASE + (y * SIZE + x) * 2, 2),
                        inside ? 1000 : INITIAL_Z);
        }
    }
    for (unsigned c = 0; c < 4; c++)
        case_expect("color-field START readback", read_reg(m, registers[c] + 8),
                    (uint32_t) ((start[c] + 4 * y_inc[c]) * 65536) & 0x01fffff0u);
}

static void
hidden_fraction_case(mach64_t *m)
{
    snprintf(case_name, sizeof(case_name), "hidden S fraction across split and readback");
    cases++;
    setup(m, 6);
    write_reg(m, SCALE_3D_CNTL, TEXTURE);
    write_reg(m, S_START, (1u << 23) - 32);
    write_reg(m, S_Y_INC, 7);
    write_reg(m, DST_Y_X_ALIAS, xy(8, 8));
    write_reg(m, DST_BRES_LNTH, LINE_DIS | (12u << 16) | DRAW_TRAP | 4);
    case_expect("readback excludes hidden fraction", read_reg(m, S_START), (1u << 23) - 32);
    write_reg(m, LEAD_BRES_LNTH, DRAW_TRAP | 4);
    for (int row = 0; row < 8; row++)
        for (int x = 8; x < 12; x++)
            case_expect("fraction crosses texel boundary after split",
                        load_pixel(m, ((row + 8) * SIZE + x) * 4, 4), texture_color(row >= 5, 0));
    /* A new START write must replace, not retain, the internal accumulator. */
    write_reg(m, S_START, 0);
    rectangle(m);
    case_expect("guest START replaces hidden state", load_pixel(m, (8 * SIZE + 8) * 4, 4), texture_color(0, 0));
}

static void
line_to_triangle_case(mach64_t *m, unsigned address, int last_pixel)
{
    snprintf(case_name, sizeof(case_name), "shaded line loads trailing X alias=%03x last=%d", address, last_pixel);
    cases++;
    setup(m, 6);
    if (last_pixel)
        m->dst_cntl |= DST_LAST_PEL;
    write_reg(m, DST_Y_X_ALIAS, xy(2, 2));
    write_reg(m, address, (12u << 16) | 2); /* Bit 31=0, bit 15=0. */
    case_expect("shaded line still draws", load_pixel(m, (2 * SIZE + 2) * 4, 4), 0xff204060u);
    case_expect("last-pixel selection preserved", load_pixel(m, (2 * SIZE + 3) * 4, 4),
                last_pixel ? 0xff204060u : BACKGROUND);
    case_expect("line endpoint preserved", m->dst_y_x, xy(3, 2));
    clear_surfaces(m, 6);
    write_reg(m, DST_Y_X_ALIAS, xy(8, 8));
    write_reg(m, LEAD_BRES_LNTH, (2u << 16) | DRAW_TRAP | 1);
    case_expect("triangle retains X loaded by line", load_pixel(m, (8 * SIZE + 11) * 4, 4), 0xff204060u);
    case_expect("trailing edge excluded", load_pixel(m, (8 * SIZE + 12) * 4, 4), BACKGROUND);
}

/* The Windows 95 HAL draws wireframe triangle edges as textured lines:
 * SCALE_3D_FCN 2 with the 3D foreground source.  Like a trapezoid span, an
 * X step adds the S/T X increments and a Y step the Y increments, so a line
 * stepping one texel per pixel reads a row (X-major) or column (Y-major). */
static void
textured_line_case(mach64_t *m, unsigned address, int ymajor)
{
    snprintf(case_name, sizeof(case_name), "textured line alias=%03x ymajor=%d", address, ymajor);
    cases++;
    setup(m, 6);
    write_reg(m, Z_CNTL, 0);
    write_reg(m, SCALE_3D_CNTL, TEXTURE);
    m->dst_cntl |= DST_LAST_PEL | (ymajor ? DST_Y_MAJOR : 0);
    write_reg(m, S_XINC_START, 1u << 23); /* One texel of the 8x8 map. */
    write_reg(m, T_Y_INC, 1u << 23);
    write_reg(m, DST_Y_X_ALIAS, xy(8, 8));
    write_reg(m, address, 4);
    for (int k = 0; k < 4; k++) {
        int x = ymajor ? 8 : 8 + k, y = ymajor ? 8 + k : 8;
        case_expect("textured line texel", load_pixel(m, (y * SIZE + x) * 4, 4),
                    ymajor ? texture_color(0, k) : texture_color(k, 0));
    }
    case_expect("pixel past the line untouched",
                load_pixel(m, ((ymajor ? 12 : 8) * SIZE + (ymajor ? 8 : 12)) * 4, 4), BACKGROUND);
    case_expect("textured line endpoint", m->dst_y_x, ymajor ? xy(8, 11) : xy(11, 8));
}

static void
legacy_barrier_case(mach64_t *m)
{
    unsigned before;
    snprintf(case_name, sizeof(case_name), "ordinary 2D commands do not add FIFO waits");
    cases++;
    setup(m, 6);
    write_reg(m, SCALE_3D_CNTL, 0);
    before = fifo_waits;
    /* Writes now run on the FIFO thread, in order with shared state.
       Only CPU register reads need to drain the FIFO. */
    case_expect("shared write falls through", mach64_3d_write(m, 0x2d8, m->dp_src, FIFO_WRITE_DWORD), 0);
    for (unsigned i = 0; i < 16; i++)
        case_expect("ordinary line remains legacy", mach64_3d_write(m, DST_BRES_LNTH, LINE_DIS | (i << 16), FIFO_WRITE_DWORD), 0);
    case_expect("no extra waits for ordinary line state", fifo_waits, before);
    write_reg(m, Z_CNTL, Z_LEQUAL_WRITE);
    case_expect("FIFO-ordered 3D write does not drain itself", fifo_waits, before);
    write_reg(m, Z_CNTL, Z_LEQUAL_WRITE);
    case_expect("subsequent 3D write does not drain itself", fifo_waits, before);
}

static void
pixie_profile_case(mach64_t *m, int textured, int dx, int dy, int slope, int clip)
{
    int ox = dx > 0 ? 8 : 32, oy = dy > 0 ? 8 : 32;
    int left = 0, right = SIZE - 1, top = 0, bottom = SIZE - 1;
    snprintf(case_name, sizeof(case_name), "RGB555 recorded control tex=%d dir=%d,%d slope=%d clip=%d",
             textured, dx, dy, slope, clip);
    cases++;
    setup(m, 3);
    /* These mode words occur in the saved Pixie captures. Geometry, colors,
     * and maps are deliberately synthetic, not a replay of the failing scene. */
    m->dp_pix_width = 0x30030203u;
    m->dp_src       = 0x00000503u;
    m->dp_mix       = 0x00070003u;
    write_reg(m, SCALE_3D_CNTL, textured ? 0x06410287u : 0x064102c7u);
    write_reg(m, TEX_SIZE_PITCH, 0x777);
    for (unsigned level = 0; level <= 7; level++) {
        unsigned base = TEX_BASE + (level << 16);
        unsigned side = 1u << level;
        write_reg(m, 0x1c0 + level * 4, base);
        for (unsigned i = 0; i < side * side; i++)
            store_pixel(m, base + i * 2, 2, 0xffff);
    }
    /* White at every mip level makes the expected texture-modulated color
     * independent of undocumented fractional LOD and filter rounding. */
    set_texture_polynomial(m, S_X_INC2, s_coeff);
    set_texture_polynomial(m, T_X_INC2, t_coeff);
    write_reg(m, RED_START, 200u << 16);
    write_reg(m, GREEN_START, 180u << 16);
    write_reg(m, BLUE_START, 160u << 16);
    write_reg(m, RED_X_INC, (uint32_t) (-7 * 65536));
    write_reg(m, RED_Y_INC, (uint32_t) (-9 * 65536));
    write_reg(m, GREEN_X_INC, (uint32_t) (-5 * 65536));
    write_reg(m, GREEN_Y_INC, (uint32_t) (-3 * 65536));
    write_reg(m, BLUE_X_INC, (uint32_t) (-2 * 65536));
    write_reg(m, BLUE_Y_INC, 4u << 16);
    write_reg(m, Z_X_INC, (uint32_t) (-3 * 4096));
    write_reg(m, Z_Y_INC, 5u << 12);
    if (clip) {
        left   = ox + dx * (dx > 0 ? 1 : 9);
        right  = ox + dx * (dx > 0 ? 9 : 1);
        top    = oy + dy * (dy > 0 ? 2 : 5);
        bottom = oy + dy * (dy > 0 ? 5 : 2);
    }
    m->sc_left_right = (unsigned) left | (unsigned) right << 16;
    m->sc_top_bottom = (unsigned) top | (unsigned) bottom << 16;
    m->dst_cntl      = (dx > 0 ? DST_X_DIR | TRAIL_X_DIR | TRAP_FILL_DIR : 0) | (dy > 0 ? DST_Y_DIR : 0);
    m->dst_bres_inc  = slope;
    write_reg(m, TRAIL_BRES_INC, slope + 2);
    write_reg(m, DST_Y_X_ALIAS, xy(ox, oy));
    write_reg(m, DST_BRES_LNTH, LINE_DIS | (unsigned) ox << 16 | DRAW_TRAP | 4);
    m->dst_cntl ^= TRAIL_X_DIR;
    write_reg(m, TRAIL_BRES_INC, 2 - slope);
    write_reg(m, LEAD_BRES_LNTH, DRAW_TRAP | 4);
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            int      r = (y - oy) * dy, n = (x - ox) * dx;
            int      width  = r < 4 ? 2 * r : 2 * (8 - r);
            int      inside = r >= 0 && r < 8 && n >= slope * r && n < slope * r + width && x >= left && x <= right && y >= top && y <= bottom;
            uint32_t pixel  = load_pixel(m, (y * SIZE + x) * 2, 2);
            unsigned depth  = INITIAL_Z;
            if (inside) {
                int channels[3] = { 200 - 7 * n - 9 * r, 180 - 5 * n - 3 * r,
                                    160 - 2 * n + 4 * r };
                case_expect("opaque RGB555 alpha", pixel >> 15, 1);
                for (int channel = 0; channel < 3; channel++) {
                    int quantized = (pixel >> (10 - 5 * channel)) & 31;
                    /* Not a Rage Pro 555 surface: the Bayer stand-in table,
                     * whose codes lie within one of the 5-bit truncation. */
                    int lower     = channels[channel] >> 3;
                    case_expect("lit component within quantization bounds",
                                quantized >= lower && quantized <= lower + 1, 1);
                }
                depth = 1000 - 3 * n + 5 * r;
            } else {
                case_expect("RGB555 profile coverage", pixel, pack_color(BACKGROUND, 3));
            }
            case_expect("RGB555 profile depth", load_pixel(m, Z_BASE + (y * SIZE + x) * 2, 2), depth);
        }
    }
}

/* Generated numeric inputs and MMIO outputs from an isolated execution of
 * ATI3DCIF 4.03.2510, SHA256
 * 5d0c8227afb4d37e277c14c7566b297d9360e02a36bed2b8b023f72533c0d222.
 * Entry 0x100074f0, internal device code 0x201; Gouraud RGB and Z enabled.
 * This contains no driver machine code or guest capture. The input vertices
 * define the independent expected color/depth planes used by the test.
 * MMIO offsets are relative to the 400h-byte register block.
 */
static const struct {
    double   vertex[3][6]; /* x, y, z, r, g, b */
    unsigned command_count;
    uint32_t command[32][2];
} ati_driver_triangles[] = {
    { /* Deterministic offline case 2. */
      {
          { 55.50, 34.00, 897.00, 216.00, 151.00, 153.00 },
          { 14.75, 47.75, 947.00, 239.00, 181.00, 150.00 },
          { 16.00, 33.00, 1010.00, 178.00, 211.00, 152.00 },
      },
     27,
     {
          { 0x194, 0x00000503u },
          { 0x124, 0xffffff95u },
          { 0x128, 0x00000014u },
          { 0x12c, 0xffffff14u },
          { 0x130, 0x0000e822u },
          { 0x134, 0x00100021u },
          { 0x138, 0x00000134u },
          { 0x13c, 0x00000278u },
          { 0x140, 0xfffffff0u },
          { 0x3c0, 0xffff24ffu },
          { 0x3c4, 0x00043546u },
          { 0x3c8, 0x00b48824u },
          { 0x3cc, 0x000176e0u },
          { 0x3d0, 0xfffdd78eu },
          { 0x3d4, 0x00d13057u },
          { 0x3d8, 0xfffff8a8u },
          { 0x3dc, 0xffffdde9u },
          { 0x3e0, 0x0097f2a1u },
          { 0x3e4, 0x00002bf3u },
          { 0x3e8, 0xffffb7f0u },
          { 0x3ec, 0x003ee5ffu },
          { 0x120, 0x80108001u },
          { 0x130, 0x0000c822u },
          { 0x138, 0x0000006bu },
          { 0x13c, 0x0000028cu },
          { 0x140, 0xffffff24u },
          { 0x120, 0x8037800eu },
      } },
    { /* Deterministic offline case 21. */
      {
          { 48.25, 37.25, 811.00, 163.00, 198.00, 151.00 },
          { 37.50, 13.75, 1320.00, 224.00, 162.00, 172.00 },
          { 52.00, 41.25, 789.00, 94.00, 146.00, 195.00 },
      },
     27,
     {
          { 0x194, 0x00000503u },
          { 0x124, 0x000000aeu },
          { 0x128, 0x000000e8u },
          { 0x12c, 0xfffffe48u },
          { 0x130, 0x0000a823u },
          { 0x134, 0x0024000eu },
          { 0x138, 0x00000081u },
          { 0x13c, 0x000000acu },
          { 0x140, 0xfffffe88u },
          { 0x3c0, 0xffe17943u },
          { 0x3c4, 0x000b5e51u },
          { 0x3c8, 0x01070d79u },
          { 0x3cc, 0xffe1ba81u },
          { 0x3d0, 0x000f6127u },
          { 0x3d4, 0x00cbce5cu },
          { 0x3d8, 0x0018c68fu },
          { 0x3dc, 0xfff3c5dau },
          { 0x3e0, 0x008a0dd4u },
          { 0x3e4, 0x00021a98u },
          { 0x3e8, 0xfffdaf12u },
          { 0x3ec, 0x004ea8b6u },
          { 0x120, 0x80248017u },
          { 0x130, 0x0000a823u },
          { 0x138, 0xffffffffu },
          { 0x13c, 0x0000003cu },
          { 0x140, 0xffffffc0u },
          { 0x120, 0x802f8004u },
      } },
    { /* Deterministic offline case 74. */
      {
          { 14.00, 43.50, 1079.00, 203.00, 78.00, 75.00 },
          { 6.00, 47.00, 1169.00, 124.00, 78.00, 191.00 },
          { 57.00, 28.75, 990.00, 89.00, 55.00, 180.00 },
      },
     27,
     {
          { 0x194, 0x00000503u },
          { 0x124, 0x000001d3u },
          { 0x128, 0x00000330u },
          { 0x12c, 0xfffffedcu },
          { 0x130, 0x00008822u },
          { 0x134, 0x0038001du },
          { 0x138, 0x0000018fu },
          { 0x13c, 0x000002b0u },
          { 0x140, 0xffffff14u },
          { 0x3c0, 0x0030217au },
          { 0x3c4, 0xff7b6a57u },
          { 0x3c8, 0x000da07eu },
          { 0x3cc, 0x00027a18u },
          { 0x3d0, 0xfffa56a5u },
          { 0x3d4, 0x0033fe08u },
          { 0x3d8, 0xffc00bd1u },
          { 0x3dc, 0x00b352b5u },
          { 0x3e0, 0x011a83f0u },
          { 0x3e4, 0xfffe0bd1u },
          { 0x3e8, 0x000612b5u },
          { 0x3ec, 0x004173f0u },
          { 0x120, 0x8038800eu },
          { 0x130, 0x00008822u },
          { 0x138, 0xffffffe5u },
          { 0x13c, 0x00000080u },
          { 0x140, 0xffffffc8u },
          { 0x120, 0x800d8004u },
      } },
    { /* Deterministic offline case 90. */
      {
          { 39.75, 29.50, 1311.00, 212.00, 181.00, 230.00 },
          { 7.25, 35.50, 1253.00, 161.00, 233.00, 169.00 },
          { 28.50, 39.75, 978.00, 122.00, 196.00, 103.00 },
      },
     27,
     {
          { 0x194, 0x00000503u },
          { 0x124, 0xffffffd8u },
          { 0x128, 0x000000b4u },
          { 0x12c, 0xffffff5cu },
          { 0x130, 0x00008822u },
          { 0x134, 0x0027001du },
          { 0x138, 0xffffffe9u },
          { 0x13c, 0x00000208u },
          { 0x140, 0xffffffa0u },
          { 0x3c0, 0x000010a0u },
          { 0x3c4, 0xfff725f3u },
          { 0x3c8, 0x00d40428u },
          { 0x3cc, 0x0001aaf3u },
          { 0x3d0, 0xffffa208u },
          { 0x3d4, 0x00b56abdu },
          { 0x3d8, 0x000083cbu },
          { 0x3dc, 0xfff30b72u },
          { 0x3e0, 0x00e620f3u },
          { 0x3e4, 0x0000548au },
          { 0x3e8, 0xfffd9b68u },
          { 0x3ec, 0x00520523u },
          { 0x120, 0x80278006u },
          { 0x130, 0x0000a822u },
          { 0x138, 0xffffffefu },
          { 0x13c, 0x00000154u },
          { 0x140, 0xffffffbcu },
          { 0x120, 0x80068005u },
      } },
    { /* Deterministic offline case 258: color-field crossings. */
      {
          { 29.25, 23.50, 1311.00, 197.00, 98.00, 83.00 },
          { 24.50, 27.50, 607.00, 80.00, 180.00, 78.00 },
          { 41.00, 14.75, 573.00, 90.00, 216.00, 240.00 },
      },
     27,
     {
          { 0x194, 0x00000503u },
          { 0x124, 0x00000061u },
          { 0x128, 0x00000108u },
          { 0x12c, 0xffffff34u },
          { 0x130, 0x00008822u },
          { 0x134, 0x0028000fu },
          { 0x138, 0x00000048u },
          { 0x13c, 0x000000bcu },
          { 0x140, 0xffffff74u },
          { 0x3c0, 0x010afd10u },
          { 0x3c4, 0xfea5b37eu },
          { 0x3c8, 0xffdbc526u },
          { 0x3cc, 0xff253dcau },
          { 0x3d0, 0x0118469eu },
          { 0x3d4, 0x013cd3dcu },
          { 0x3d8, 0xff948d3cu },
          { 0x3dc, 0x007e5848u },
          { 0x3e0, 0x011908d4u },
          { 0x3e4, 0x0068bc53u },
          { 0x3e8, 0xff78a05eu },
          { 0x3ec, 0xfff2a670u },
          { 0x120, 0x80288008u },
          { 0x130, 0x00008822u },
          { 0x138, 0xffffffd1u },
          { 0x13c, 0x0000004cu },
          { 0x140, 0xffffffc0u },
          { 0x120, 0x801c8004u },
      } },
    { /* Deterministic offline case 269: color-field crossings. */
      {
          { 22.00, 41.00, 670.00, 112.00, 130.00, 125.00 },
          { 34.75, 15.75, 789.00, 224.00, 140.00, 198.00 },
          { 35.00, 15.75, 590.00, 65.00, 54.00, 94.00 },
      },
     23,
     {
          { 0x194, 0x00000503u },
          { 0x124, 0x00000035u },
          { 0x128, 0x000000ccu },
          { 0x12c, 0xfffffe6cu },
          { 0x130, 0x0000c822u },
          { 0x134, 0x00230010u },
          { 0x3c0, 0x027c0000u },
          { 0x3c4, 0xfeba6a74u },
          { 0x3c8, 0xfe0ecfd7u },
          { 0x3cc, 0x01580000u },
          { 0x3d0, 0xff51e6a7u },
          { 0x3d4, 0xff076cfdu },
          { 0x3d8, 0x01a00000u },
          { 0x3dc, 0xff2b0cacu },
          { 0x3e0, 0xfeee4981u },
          { 0x3e4, 0x0031c000u },
          { 0x3e8, 0xffe6958bu },
          { 0x3ec, 0xfff8f028u },
          { 0x130, 0x0000c822u },
          { 0x138, 0xffffffd3u },
          { 0x13c, 0x000000d0u },
          { 0x140, 0xfffffe6cu },
          { 0x120, 0x80238019u },
      } },
    { /* Offline depth-range case 23: Z-field crossings. */
      {
          { 33.50, 33.00, 12080.00, 117.00, 185.00, 204.00 },
          { 53.25, 57.00, 41495.00, 67.00, 240.00, 177.00 },
          { 19.25, 16.75, 55731.00, 119.00, 172.00, 76.00 },
      },
     27,
     {
          { 0x194, 0x00000503u },
          { 0x124, 0x000000f7u },
          { 0x128, 0x00000220u },
          { 0x12c, 0xfffffd7cu },
          { 0x130, 0x0000e823u },
          { 0x134, 0x00130011u },
          { 0x138, 0x0000006au },
          { 0x13c, 0x000000e4u },
          { 0x140, 0xfffffefcu },
          { 0x3c0, 0x00244bf7u },
          { 0x3c4, 0xffe00c28u },
          { 0x3c8, 0x00681c1bu },
          { 0x3cc, 0xffe4613cu },
          { 0x3d0, 0x00190551u },
          { 0x3d4, 0x00b7dc4cu },
          { 0x3d8, 0x00a6aeb8u },
          { 0x3dc, 0xff75b58eu },
          { 0x3e0, 0x000df3d8u },
          { 0x3e4, 0xee50f1bau },
          { 0x3e8, 0x0ed9fdb0u },
          { 0x3ec, 0x1452eab3u },
          { 0x120, 0x80138010u },
          { 0x130, 0x0000e823u },
          { 0x138, 0x0000009eu },
          { 0x13c, 0x0000013cu },
          { 0x140, 0xfffffe80u },
          { 0x120, 0x80218018u },
      } },
    { /* Offline depth-range case 219: Z-field crossings. */
      {
          { 55.00, 26.75, 13474.00, 231.00, 50.00, 180.00 },
          { 55.50, 27.00, 56088.00, 93.00, 82.00, 147.00 },
          { 17.75, 44.00, 2367.00, 219.00, 192.00, 141.00 },
      },
     23,
     {
          { 0x194, 0x00000503u },
          { 0x124, 0x00000136u },
          { 0x128, 0x00000254u },
          { 0x12c, 0xfffffeecu },
          { 0x130, 0x0000e822u },
          { 0x134, 0x0037001bu },
          { 0x3c0, 0x00848b26u },
          { 0x3c4, 0xfee1164cu },
          { 0x3c8, 0xffcd8b26u },
          { 0x3cc, 0xffe334a0u },
          { 0x3d0, 0x00466942u },
          { 0x3d4, 0x007534a1u },
          { 0x3d8, 0x001f310fu },
          { 0x3dc, 0xffba621eu },
          { 0x3e0, 0x0070310fu },
          { 0x3e4, 0xf5f507ccu },
          { 0x3e8, 0x15878fadu },
          { 0x3ec, 0x187547dcu },
          { 0x130, 0x0000c822u },
          { 0x138, 0x0000001fu },
          { 0x13c, 0x0000025cu },
          { 0x140, 0xfffffef0u },
          { 0x120, 0x80378011u },
      } },
};

static void
initial_edge_texture_case(mach64_t *m, int dx, int dy, int zero_negative,
                          int split)
{
    int ox = dx > 0 ? 8 : 48, oy = dy > 0 ? 8 : 48;
    int leading_steps  = zero_negative ? 2 : 3;
    int trailing_steps = zero_negative ? 1 : 2;
    snprintf(case_name, sizeof(case_name), "initial edges texture dx=%d dy=%d zero=%d split=%d",
             dx, dy, zero_negative, split);
    cases++;
    setup(m, 6);
    write_reg(m, SCALE_3D_CNTL, TEXTURE);
    m->dst_cntl     = (dx > 0 ? DST_X_DIR | TRAIL_X_DIR | TRAP_FILL_DIR : 0) | (dy > 0 ? DST_Y_DIR : 0) | (zero_negative ? (1u << 11) | (1u << 15) : 0);
    m->dst_bres_err = 4;
    m->dst_bres_inc = 0;
    m->dst_bres_dec = (uint32_t) -2;
    write_reg(m, TRAIL_BRES_ERR, 2);
    write_reg(m, TRAIL_BRES_INC, 0);
    write_reg(m, TRAIL_BRES_DEC, (uint32_t) -2);
    write_reg(m, S_START, 0);
    write_reg(m, S_XINC_START, 1u << 21);
    write_reg(m, S_Y_INC, 1u << 21);
    write_reg(m, S_X_INC2, 1u << 20);
    write_reg(m, S_Y_INC2, 1u << 19);
    write_reg(m, S_XY_INC2, 1u << 19);
    write_reg(m, T_START, (1u << 23) - 32);
    write_reg(m, T_XINC_START, 7);
    write_reg(m, T_Y_INC, 5);
    write_reg(m, T_X_INC2, 3);
    write_reg(m, T_Y_INC2, 2);
    write_reg(m, T_XY_INC2, 1);
    write_reg(m, Z_X_INC, (uint32_t) (-3 * 4096));
    write_reg(m, Z_Y_INC, 5u << 12);
    write_reg(m, DST_Y_X_ALIAS, xy(ox, oy));
    write_reg(m, DST_BRES_LNTH, LINE_DIS | (unsigned) (ox + dx * 4) << 16 | DRAW_TRAP | (split ? 2 : 4));
    if (split) {
        (void) read_reg(m, T_START); /* Reading must not discard internal bits. */
        write_reg(m, LEAD_BRES_LNTH, DRAW_TRAP | 2);
    }
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            int      row = (y - oy) * dy, n = (x - ox) * dx;
            int      inside   = row >= 0 && row < 4 && n >= leading_steps && n < 4 + trailing_steps;
            uint32_t expected = BACKGROUND;
            unsigned z        = INITIAL_Z;
            if (inside) {
                int64_t s = ((int64_t) n + row) * (1 << 21) + (int64_t) n * (n - 1) / 2 * (1 << 20) + ((int64_t) row * (row - 1) / 2 + n * row) * (1 << 19);
                int64_t t = (1 << 23) - 32 + 7 * n + 5 * row + 3 * n * (n - 1) / 2 + row * (row - 1) + n * row;
                expected  = texture_color((int) (s >> 23) & 7, (int) (t >> 23) & 7);
                z         = 1000 - 3 * n + 5 * row;
            }
            case_expect("initial edge texture and coverage", load_pixel(m, (y * SIZE + x) * 4, 4), expected);
            case_expect("initial edge depth", load_pixel(m, Z_BASE + (y * SIZE + x) * 2, 2), z);
        }
    }
    case_expect("initial X steps do not advance Y", m->dst_y_x, xy(ox + dx * leading_steps, oy + dy * 4));
}

/* Check actual ATI-generated setup against input-vertex planes. No driver DLL
 * or x86 interpreter is needed to run it. Stay strictly
 * inside the triangle so this does not impose an unverified edge-tie rule. */
static void
driver_triangle_case(mach64_t *m, unsigned fixture, int format)
{
    const double (*v)[6] = ati_driver_triangles[fixture].vertex;
    double   area        = (v[1][0] - v[0][0]) * (v[2][1] - v[0][1]) - (v[2][0] - v[0][0]) * (v[1][1] - v[0][1]);
    unsigned interior    = 0;
    unsigned bytes       = destination_bytes(format);

    snprintf(case_name, sizeof(case_name), "ATI setup fixture=%u format=%d", fixture, format);
    cases++;
    setup(m, format);
    m->dp_mix = 0x00070003;
    for (unsigned i = 0; i < ati_driver_triangles[fixture].command_count; i++) {
        uint32_t address = ati_driver_triangles[fixture].command[i][0];
        uint32_t value   = ati_driver_triangles[fixture].command[i][1];
        switch (address) {
            case 0x124:
                m->dst_bres_err = value;
                break;
            case 0x128:
                m->dst_bres_inc = value;
                break;
            case 0x12c:
                m->dst_bres_dec = value;
                break;
            case 0x130:
                m->dst_cntl = value;
                break;
            case 0x194:
                m->dp_src = value;
                break;
            default:
                write_reg(m, address, value);
                break;
        }
    }
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            double px = x + 0.5 - v[0][0], py = y + 0.5 - v[0][1];
            double w1 = (px * (v[2][1] - v[0][1]) - (v[2][0] - v[0][0]) * py) / area;
            double w2 = ((v[1][0] - v[0][0]) * py - px * (v[1][1] - v[0][1])) / area;
            double w0 = 1.0 - w1 - w2;
            if (w0 < 0.01 || w1 < 0.01 || w2 < 0.01)
                continue;
            interior++;
            uint32_t pixel = load_pixel(m, (y * SIZE + x) * bytes, bytes);
            case_expect("ATI triangle interior written", pixel != pack_color(BACKGROUND, format), 1);
            for (unsigned channel = 0; channel < 3; channel++) {
                double   expected = w0 * v[0][3 + channel] + w1 * v[1][3 + channel] + w2 * v[2][3 + channel];
                unsigned shift    = format == 6 ? 16 - 8 * channel : (format == 3 ? 10 - 5 * channel : (channel == 0 ? 11 : (channel == 1 ? 5 : 0)));
                unsigned bits     = format == 6 ? 8 : (format == 4 && channel == 1 ? 6 : 5);
                unsigned actual   = (pixel >> shift) & ((1u << bits) - 1);
                double   error    = format == 6 ? (double) actual - expected : (double) actual - ((unsigned) expected >> (8 - bits));
                case_expect("ATI vertex-plane color", error > -1.1 && error < 1.1, 1);
            }
            double expected_z = w0 * v[0][2] + w1 * v[1][2] + w2 * v[2][2];
            double error_z    = load_pixel(m, Z_BASE + (y * SIZE + x) * 2, 2) - expected_z;
            case_expect("ATI vertex-plane depth", error_z > -1.1 && error_z < 1.1, 1);
        }
    }
    case_expect("ATI fixture has interior samples", interior > 0, 1);
}

/* One white texel among black ones exposes both the 8-bit binary fraction
 * (not an endpoint-inclusive /255 alpha weight) and the 2x2 filter's texel
 * centers, half a texel past each integer coordinate.  The white texel is
 * fully weighted only at its center and half weighted at either integer
 * edge.  Exercise both axes, the wrap boundary, and the command path. */
static void
bilinear_fraction_case(mach64_t *m, int axis, int wrap)
{
    snprintf(case_name, sizeof(case_name), "bilinear fraction axis=%d wrap=%d", axis, wrap);
    cases++;
    setup(m, 6);
    write_reg(m, Z_CNTL, 0);
    write_reg(m, SCALE_3D_CNTL, TEXTURE | (1u << 25));
    for (int v = 0; v < 8; v++)
        for (int u = 0; u < 8; u++) {
            unsigned level = (axis ? v : u) == (wrap ? 7 : 0) ? 255 : 0;
            store_pixel(m, TEX_BASE + (v * 8 + u) * 4, 4, 0xff000000u | level * 0x010101u);
        }
    for (unsigned fraction = 0; fraction < 256; fraction++) {
        /* 8x8 map: one texel is 2^23 in the normalized S/T domain. */
        write_reg(m, axis ? T_START : S_START, ((wrap ? 7u : 0u) << 23) | (fraction << 15));
        write_reg(m, DST_Y_X_ALIAS, xy(8, 8));
        write_reg(m, DST_BRES_LNTH, LINE_DIS | (9u << 16) | DRAW_TRAP | 1);
        /* Weight of the white texel at distance |fraction-128|/256 from its center. */
        unsigned weight   = fraction < 128 ? fraction + 128 : 384 - fraction;
        unsigned expected = (255u * weight + 128) / 256;
        case_expect("binary texture fraction", load_pixel(m, (8 * SIZE + 8) * 4, 4),
                    0xff000000u | expected * 0x010101u);
    }
}

/* Final Reality's neon entrance bevel samples T=0 of a map whose only bright
 * row is the last one.  With texel-center weighting its rim blends the last
 * and first rows equally; at the first row's center only that row remains.
 * Nearest sampling selects the first row throughout. */
static void
bilinear_wrap_edge_case(mach64_t *m, int bilinear)
{
    static const unsigned offsets[]         = { 0, 1u << 21, 1u << 22 };
    static const unsigned bilinear_levels[] = { 128, 64, 0 };

    snprintf(case_name, sizeof(case_name), "texture wrap edge bilinear=%d", bilinear);
    cases++;
    setup(m, 6);
    write_reg(m, Z_CNTL, 0);
    write_reg(m, SCALE_3D_CNTL, TEXTURE | (bilinear ? 1u << 25 : 0));
    for (int v = 0; v < 8; v++)
        for (int u = 0; u < 8; u++)
            store_pixel(m, TEX_BASE + (v * 8 + u) * 4, 4,
                        0xff000000u | (v == 7 ? 0xff2020u : 0u));
    for (unsigned i = 0; i < 3; i++) {
        /* T=0, a quarter texel, then half a texel (the first row's center). */
        write_reg(m, T_START, offsets[i]);
        write_reg(m, DST_Y_X_ALIAS, xy(8, 8));
        write_reg(m, DST_BRES_LNTH, LINE_DIS | (9u << 16) | DRAW_TRAP | 1);
        unsigned level = bilinear ? bilinear_levels[i] : 0;
        unsigned red = (255u * level + 128) / 256, other = (0x20u * level + 128) / 256;
        case_expect("wrapped edge row weight", load_pixel(m, (8 * SIZE + 8) * 4, 4),
                    0xff000000u | red << 16 | other << 8 | other);
    }
}

/* Both edge walkers have signed 18-bit error/step fields. A caller can
 * zero-extend a negative field or leave arbitrary reserved high bits. The
 * four rows below move one pixel after rows zero and two, in either
 * direction; splitting the command must preserve the same coverage. */
static void
trapezoid_edge_field_case(mach64_t *m, int dx, int dy, int encoding, int split)
{
    int ox = dx > 0 ? 8 : 24, oy = dy > 0 ? 8 : 24;
    uint32_t reserved = encoding == 1 ? 0xfffc0000u : encoding == 2 ? 0xa5a80000u : 0;

    snprintf(case_name, sizeof(case_name), "trapezoid edge fields dir=%d,%d encoding=%d split=%d",
             dx, dy, encoding, split);
    cases++;
    setup(m, 6);
    m->dst_cntl = (dx > 0 ? DST_X_DIR | TRAIL_X_DIR | TRAP_FILL_DIR : 0) | (dy > 0 ? DST_Y_DIR : 0);
    m->dst_bres_err = reserved | 0x3ffff;
    m->dst_bres_inc = reserved | 2;
    m->dst_bres_dec = reserved | 0x3fffc;
    write_reg(m, TRAIL_BRES_ERR, reserved | 0x3ffff);
    write_reg(m, TRAIL_BRES_INC, reserved | 2);
    write_reg(m, TRAIL_BRES_DEC, reserved | 0x3fffc);
    write_reg(m, DST_Y_X_ALIAS, xy(ox, oy));
    write_reg(m, DST_BRES_LNTH, LINE_DIS | (unsigned) (ox + dx * 4) << 16 | DRAW_TRAP | (split ? 2 : 4));
    if (split)
        write_reg(m, LEAD_BRES_LNTH, DRAW_TRAP | 2);
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            int row = (y - oy) * dy, column = (x - ox) * dx;
            int shift = (row + 1) / 2;
            int inside = row >= 0 && row < 4 && column >= shift && column < shift + 4;
            case_expect("18-bit edge coverage", load_pixel(m, (y * SIZE + x) * 4, 4),
                        inside ? 0xff204060u : BACKGROUND);
            case_expect("18-bit edge depth", load_pixel(m, Z_BASE + (y * SIZE + x) * 2, 2),
                        inside ? 1000u : INITIAL_Z);
        }
    }
    case_expect("18-bit leading edge continuation", m->dst_y_x, xy(ox + dx * 2, oy + dy * 4));
}

static void
draw_tests(void)
{
    static const int formats[] = { 3, 4, 6 };
    mach64_t        *m         = card_create();
    for (int dx = -1; dx <= 1; dx += 2)
        for (int dy = -1; dy <= 1; dy += 2)
            for (int encoding = 0; encoding < 3; encoding++)
                for (int split = 0; split <= 1; split++)
                    trapezoid_edge_field_case(m, dx, dy, encoding, split);
    for (int axis = 0; axis < 2; axis++)
        for (int wrap = 0; wrap < 2; wrap++)
            bilinear_fraction_case(m, axis, wrap);
    for (int bilinear = 0; bilinear <= 1; bilinear++)
        bilinear_wrap_edge_case(m, bilinear);
    for (unsigned fi = 0; fi < sizeof(formats) / sizeof(formats[0]); fi++) {
        int format = formats[fi];
        for (int textured = 0; textured <= 1; textured++)
            for (int dx = -1; dx <= 1; dx += 2)
                for (int dy = -1; dy <= 1; dy += 2)
                    for (int clip = 0; clip < 3; clip++)
                        for (int slope = 0; slope <= 1; slope++)
                            for (int additive = 0; additive <= 1; additive++)
                                triangle_case(m, format, textured, dx, dy, clip, slope, additive);
    }
    for (unsigned alias = 0; alias < 2; alias++)
        for (unsigned bytes = 1; bytes <= 4; bytes *= 2)
            for (int shaded = 0; shaded <= 1; shaded++)
                for (int stale = 0; stale <= 1; stale++)
                    preload_case(m, alias ? LEAD_BRES_LNTH : DST_BRES_LNTH, bytes, shaded, stale);
    for (unsigned fi = 0; fi < sizeof(formats) / sizeof(formats[0]); fi++) {
        int format = formats[fi];
        for (int enabled = 0; enabled <= 1; enabled++)
            for (int write_enabled = 0; write_enabled <= 1; write_enabled++)
                for (unsigned function = 0; function < 8; function++)
                    for (unsigned relation = 0; relation < 3; relation++)
                        depth_case(m, format, enabled, write_enabled, function, relation);
        for (int mode = 0; mode < 3; mode++)
            visibility_case(m, format, mode);
        blend_transition_case(m, format);
        if (format == 3 || format == 4)
            for (int mode = 0; mode < 3; mode++)
                ordered_blend_identity_case(m, format, mode);
        for (unsigned function = 0; function < 8; function++)
            for (int writes = 0; writes <= 1; writes++)
                for (int dir = -1; dir <= 1; dir += 2)
                    for (int split = 0; split <= 1; split++)
                        depth_field_case(m, format, function, writes, dir, split);
        for (int mode = 0; mode < 3; mode++)
            for (int dx = -1; dx <= 1; dx += 2)
                for (int dy = -1; dy <= 1; dy += 2)
                    for (int clip = 0; clip <= 1; clip++)
                        for (int split = 0; split <= 1; split++)
                            color_field_case(m, format, mode, dx, dy, clip, split);
    }
    hidden_fraction_case(m);
    for (int last_pixel = 0; last_pixel <= 1; last_pixel++) {
        line_to_triangle_case(m, DST_BRES_LNTH, last_pixel);
        line_to_triangle_case(m, LEAD_BRES_LNTH, last_pixel);
    }
    legacy_barrier_case(m);
    for (int mode = 0; mode < 9; mode++)
        for (int line = 0; line <= 2; line++)
            scissor_case(m, mode, line);
    for (int mode = 0; mode < 4; mode++) {
        line_dispatch_case(m, mode, DST_BRES_LNTH);
        line_dispatch_case(m, mode, LEAD_BRES_LNTH);
    }
    for (int ymajor = 0; ymajor <= 1; ymajor++) {
        textured_line_case(m, DST_BRES_LNTH, ymajor);
        textured_line_case(m, LEAD_BRES_LNTH, ymajor);
    }
    for (int textured = 0; textured <= 1; textured++)
        for (int dx = -1; dx <= 1; dx += 2)
            for (int dy = -1; dy <= 1; dy += 2)
                for (int slope = 0; slope <= 1; slope++)
                    for (int clip = 0; clip <= 1; clip++)
                        pixie_profile_case(m, textured, dx, dy, slope, clip);
    for (unsigned fixture = 0; fixture < sizeof(ati_driver_triangles) / sizeof(ati_driver_triangles[0]); fixture++)
        for (unsigned fi = 0; fi < sizeof(formats) / sizeof(formats[0]); fi++)
            driver_triangle_case(m, fixture, formats[fi]);
    for (int dx = -1; dx <= 1; dx += 2)
        for (int dy = -1; dy <= 1; dy += 2)
            for (int zero_negative = 0; zero_negative <= 1; zero_negative++)
                for (int split = 0; split <= 1; split++)
                    initial_edge_texture_case(m, dx, dy, zero_negative, split);
    card_close(m);
}

/*
 * With no argument the whole suite runs. Each option selects the checks of
 * one area and any number of them can be given together; an unknown option is
 * an error, so a mistyped command line cannot fall through to the default run.
 */
enum {
    MODE_VQ = 1 << 0, MODE_BILINEAR = 1 << 1, MODE_MODULATE = 1 << 2, MODE_COMPOSITE = 1 << 3,
    MODE_MIP = 1 << 4, MODE_DITHER = 1 << 5, MODE_PALETTE = 1 << 6
};

static void
ragepro_parallel_diffusion_tests(void)
{
    for (int threads = 2; threads <= 4; threads += 2) {
        for (int scenario = 0; scenario < 12; scenario++) {
            mach64_t *m = card_create();
            mach64_3d_t *ctx = m->gt3d;
            mach64_rp_tri_t tri = { 0 };
            mach64_3d_dither_t initial = { 1, 0, { 2, -3, 1 } };
            mach64_3d_dither_t expected;
            uint8_t reference[4096];

            tri.dst.pitch = 64;
            tri.dst.bpp = 2;
            tri.dst.format = 4;
            tri.cntl = DITHER_EN | DITHER_INIT;
            tri.pl[RPP_R].c = 103.2;
            tri.pl[RPP_G].c = 127.8;
            tri.pl[RPP_B].c = 58.9;
            tri.pl[RPP_R].dx = 0.7;
            tri.pl[RPP_G].dy = 0.3;
            tri.pl[RPP_A].c = 255;
            if (scenario == 1) {
                /* Last row and an entire helper lane fail the Z test. */
                tri.z_on = 1;
                tri.z_pitch = 64;
                tri.z_base = 8192;
                tri.z_cntl = 1 << Z_TEST_SHIFT; /* less than */
                tri.pl[RPP_Z].c = 10;
                for (int y = 0; y < 32; y++) {
                    for (int x = 0; x < 64; x++)
                        mach64_3d_vram_write(m, 8192 + 2 * (y * 64 + x),
                                            ((y % threads == 1) || (y == 31)) ? 0 : 65535, 2);
                }
            } else if (scenario == 2) {
                tri.alpha_tst = RP_ALPHA_TEST_EN; /* never */
            } else if (scenario == 3) {
                initial.y = -1;
            } else if (scenario == 4) {
                initial.valid = 0;
            } else if (scenario == 5) {
                initial.y = 5; /* incoming error inside batch */
            } else if (scenario == 6) {
                tri.cntl &= ~DITHER_INIT; /* cross-row carry: serial */
            } else if (scenario >= 7) {
                initial.y = 40; /* incoming state past the batch */
                if (scenario == 8)
                    tri.alpha_tst = RP_ALPHA_TEST_EN; /* no writes */
                if (scenario == 9) {
                    tri.alpha_tst = RP_ALPHA_TEST_EN | (1 << RP_ALPHA_TEST_SHIFT) | (128 << RP_REF_ALPHA_SHIFT) |
                                    RP_ALPHA_TST_SRC_ALPHA;
                    tri.pl[RPP_A].c = 255;
                    tri.pl[RPP_A].dy = -16; /* only later rows pass */
                }
                if (scenario >= 10) {
                    initial.y = 5;
                    tri.alpha_tst = RP_ALPHA_TEST_EN | RP_ALPHA_TST_SRC_ALPHA;
                    if (scenario == 11) {
                        tri.alpha_tst |= (1 << RP_ALPHA_TEST_SHIFT) | (128 << RP_REF_ALPHA_SHIFT);
                        tri.pl[RPP_A].c = 208;
                        tri.pl[RPP_A].dy = -16; /* first write on incoming Y */
                    }
                }
            }
            ctx->regs[SCALE_3D_CNTL >> 2] = tri.cntl;
            ctx->threads = threads;
            ctx->dither = initial;
            ctx->rp_nspans = 32;
            for (int y = 0; y < 32; y++)
                ctx->rp_spans[y] = (mach64_rp_span_t) { y, 0, 63 };
            CHECK(mach64_rp_spans_shared(ctx, &tri, 2048) == (scenario != 6));
            mach64_rp_draw_spans(ctx, &tri, 0, 1, &ctx->dither, &ctx->agp);
            memcpy(reference, m->svga.vram, sizeof(reference));
            expected = ctx->dither;
            memset(m->svga.vram, 0, sizeof(reference));
            ctx->dither = initial;
            mach64_rp_draw_batch(ctx, &tri, 2048);
            CHECK(memcmp(reference, m->svga.vram, sizeof(reference)) == 0);
            CHECK(ctx->dither.valid == expected.valid);
            CHECK(ctx->dither.y == expected.y);
            CHECK(memcmp(ctx->dither.err, expected.err, sizeof(expected.err)) == 0);
            card_close(m);
        }
    }
}

int
main(int argc, char **argv)
{
    static const struct {
        const char *option;
        unsigned    bit;
        const char *name;
    } modes[] = {
        { "--vq", MODE_VQ, "VQ" },
        { "--bilinear", MODE_BILINEAR, "bilinear" },
        { "--modulate", MODE_MODULATE, "modulate and formats" },
        { "--composite", MODE_COMPOSITE, "composite" },
        { "--mip", MODE_MIP, "mip" },
        { "--dither", MODE_DITHER, "dither" },
        { "--palette", MODE_PALETTE, "palette" },
    };
    unsigned selected = 0;

    for (int a = 1; a < argc; a++) {
        unsigned bit = 0;

        for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
            if (!strcmp(argv[a], modes[i].option))
                bit = modes[i].bit;
        if (!bit) {
            fprintf(stderr, "unknown option %s; options:", argv[a]);
            for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
                fprintf(stderr, " %s", modes[i].option);
            fprintf(stderr, "\n");
            return 2;
        }
        selected |= bit;
    }
    if (selected) {
        if (selected & MODE_PALETTE) {
            palette_tests();
            ragepro_palette_upload_tests();
            ragepro_scaler_offset_acc_tests();
            ragepro_ci8_tiling_tests();
        }
        if (selected & MODE_VQ) {
            ragepro_vq_tests();
            ragepro_vq_real_card_tests();
            ragepro_vq_triangle_tests();
        }
        if (selected & MODE_MIP) {
            ragepro_mip_tests();
            ragepro_mip_chain_tests();
        }
        if (selected & MODE_DITHER) {
            ragepro_parallel_diffusion_tests();
            ragepro_dither_tests();
            dither_bayer_tests();
            ragepro_blend_tests();
        }
        if (selected & MODE_COMPOSITE)
            composite_tests();
        if (selected & MODE_BILINEAR)
            ragepro_bilinear_tests();
        if (selected & MODE_MODULATE) {
            ragepro_modulate_tests();
            ragepro_filter_format_tests();
            ragepro_filter_cell_tests();
            ragepro_filter_edge_triangle_tests();
        }
        for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
            if (selected & modes[i].bit)
                printf("Mach64 %s: checks run\n", modes[i].name);
        printf("Mach64 selected checks: %d failed\n", failures);
        return failures ? 1 : 0;
    }
    /* Both generations use this register-driven draw engine and scaler. */
    for (card_type = MACH64_GT; card_type <= MACH64_GTB; card_type++) {
        field_tests();
        data_path_tests();
        trapezoid_tests();
        mipmap_tests();
        yuv_tests();
        palette_tests();
        fifo_identity_tests();
        control_tests();
        texel_path_tests();
        scaler_math_tests();
        scaler_tests();
        xl_scaler_alias_tests();
        xl_idct_reconstruction_test();
        draw_tests();
    }
    /* The Rage Pro's own paths are on the GT-B core. */
    card_type = MACH64_GTB;
    ragepro_parallel_diffusion_tests();
    ragepro_palette_upload_tests();
    ragepro_scaler_offset_acc_tests();
    ragepro_ci8_tiling_tests();
    ragepro_vq_tests();
    ragepro_vq_real_card_tests();
    ragepro_vq_triangle_tests();
    ragepro_dither_tests();
    dither_bayer_tests();
    ragepro_blend_tests();
    ragepro_mip_tests();
    ragepro_mip_chain_tests();
    composite_tests();
    ragepro_bilinear_tests();
    ragepro_modulate_tests();
    ragepro_filter_format_tests();
    ragepro_filter_cell_tests();
    ragepro_filter_edge_triangle_tests();

    if (failures) {
        fprintf(stderr, "Mach64 3D engine: %d checks failed\n", failures);
        return 1;
    }
    printf("Mach64 3D engine: %u draw cases, all checks passed\n", cases);
    return 0;
}
