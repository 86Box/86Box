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

/* The card here has one render thread, so the helpers are never made. */
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
    (void) param;
    (void) name;
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
    return NULL;
}

void
thread_set_event(event_t *arg)
{
    (void) arg;
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
    (void) arg;
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
    mach64->type      = MACH64_GTB;
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

static void
scaler_tests(void)
{
    mach64_t *mach64 = card_create();

    failures += run_rgb_scaler_test(mach64);
    failures += run_yuyv_scaler_test(mach64);
    failures += run_apple_yuv_test(mach64);
    failures += run_scaler_color_compare_test(mach64);
    failures += run_scaler_visibility_test(mach64);
    card_close(mach64);
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
    m->sc_left_right = ((uint32_t) left & 0x1fffu) | ((uint32_t) right & 0x1fffu) << 16;
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

/* Apply delayed source/polygon state at the FIFO barrier. Classification must
 * use this state, not the stale state preceding a 2D/3D transition. */
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
    /* Model a shared-register fallback notification, not a running FIFO. */
    case_expect("shared write falls through", mach64_3d_write(m, 0x2d8, m->dp_src, FIFO_WRITE_DWORD), 0);
    for (unsigned i = 0; i < 16; i++)
        case_expect("ordinary line remains legacy", mach64_3d_write(m, DST_BRES_LNTH, LINE_DIS | (i << 16), FIFO_WRITE_DWORD), 0);
    case_expect("no extra waits for ordinary line state", fifo_waits, before);
    write_reg(m, Z_CNTL, Z_LEQUAL_WRITE);
    case_expect("existing 2D-to-3D barrier retained", fifo_waits, before + 1);
    write_reg(m, Z_CNTL, Z_LEQUAL_WRITE);
    case_expect("clean 3D state does not wait again", fifo_waits, before + 1);
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
                    int lower     = channels[channel] >> 3;
                    /* Accept either neighboring quantization code; do not
                     * bless the current substituted Bayer table as hardware. */
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

static void
draw_tests(void)
{
    static const int formats[] = { 3, 4, 6 };
    mach64_t        *m         = card_create();
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

int
main(void)
{
    field_tests();
    data_path_tests();
    trapezoid_tests();
    mipmap_tests();
    yuv_tests();
    palette_tests();
    control_tests();
    texel_path_tests();
    scaler_math_tests();
    scaler_tests();
    draw_tests();

    if (failures) {
        fprintf(stderr, "Mach64 3D engine: %d checks failed\n", failures);
        return 1;
    }
    printf("Mach64 3D engine: %u draw cases, all checks passed\n", cases);
    return 0;
}
