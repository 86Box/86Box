/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI 3D Rage II+ (Mach64 GT-B) 3D engine and front-end scaler.
 *
 *          The draw engine of the Rage II+ adds a 3D pipeline to the Mach64
 *          one: Gouraud shaded, texture mapped, Z buffered and alpha blended
 *          trapezoids and lines, and a front-end scaler that stretches and
 *          color converts video into the frame buffer. Its registers sit in
 *          the GUI register block next to the 2D ones; the 2D engine keeps
 *          those it shares with them (vid_ati_mach64_accel.c).
 *
 *          Register layout and fixed-point formats are those of the
 *          "mach64 Register Reference Guide - ATI-264VT and 3D RAGE",
 *          RRG-G02700 (the VT/RAGE RRG below), chapter 6.
 *
 * Authors: Avastrap2, <https://github.com/Avastrap2>
 *
 *          Copyright 2026 Avastrap2.
 */

#include "vid_ati_mach64.h"

/* GUI registers, byte offsets in the register block. */
#define DST_BRES_LNTH  0x120 /* also LEAD_BRES_LNTH */
#define DST_Y_X_ALIAS  0x134
#define TRAIL_BRES_ERR 0x138
#define TRAIL_BRES_INC 0x13c
#define TRAIL_BRES_DEC 0x140
#define LEAD_BRES_LNTH 0x144
#define Z_OFF_PITCH    0x148
#define Z_CNTL         0x14c
#define TEX_0_OFF      0x1c0 /* TEX_0_OFF..TEX_10_OFF, the mip maps */
#define TEX_10_OFF     0x1e8
#define SCALE_Y_OFF    0x1c0 /* TEX_0_OFF */
#define SCALE_WIDTH    0x1dc /* TEX_7_OFF */
#define SCALE_HEIGHT   0x1e0 /* TEX_8_OFF */
#define SCALE_Y_PITCH  0x1ec
#define SCALE_X_INC    0x1f0
#define SCALE_Y_INC    0x1f4
#define SCALE_VACC     0x1f8
#define SCALE_3D_CNTL  0x1fc
#define FIFO_STAT      0x310
#define GUI_STAT       0x338
#define S_X_INC2       0x340
#define S_Y_INC2       0x344
#define S_XY_INC2      0x348
#define S_XINC_START   0x34c
#define S_Y_INC        0x350 /* also SCALE_Y_PITCH */
#define S_START        0x354
#define T_X_INC2       0x358
#define T_Y_INC2       0x35c
#define T_XY_INC2      0x360
#define T_XINC_START   0x364
#define T_Y_INC        0x368
#define T_START        0x36c
#define TEX_SIZE_PITCH 0x370
#define TEX_PALETTE    0x37c
#define RED_X_INC      0x3c0 /* also SCALE_X_INC */
#define RED_Y_INC      0x3c4
#define RED_START      0x3c8 /* also SCALE_HACC */
#define GREEN_X_INC    0x3cc /* also SCALE_Y_INC */
#define GREEN_Y_INC    0x3d0
#define GREEN_START    0x3d4
#define BLUE_X_INC     0x3d8 /* also SCALE_XUV_INC */
#define BLUE_Y_INC     0x3dc
#define BLUE_START     0x3e0 /* also SCALE_UV_HACC */
#define Z_X_INC        0x3e4
#define Z_Y_INC        0x3e8
#define Z_START        0x3ec
#define ALPHA_X_INC    0x3f0
#define ALPHA_Y_INC    0x3f4
#define ALPHA_START    0x3f8
#define SCALE_HACC     RED_START
#define SCALE_XUV_INC  BLUE_X_INC
#define SCALE_UV_HACC  BLUE_START

/* DST_BRES_LNTH */
#define DRAW_TRAP (1u << 15)
#define LINE_DIS  (1u << 31) /* DST_BRES_LNTH_LINE_DIS */

/* DST_CNTL, GT-B additions */
#define TRAIL_X_DIR     (1u << 13)
#define TRAP_FILL_DIR   (1u << 14)
#define TRAIL_BRES_SIGN (1u << 15)
#define BRES_SIGN_AUTO  (1u << 16)

/* Z_CNTL */
#define Z_EN         (1u << 0)
#define Z_TEST_SHIFT 4
#define Z_MASK       (1u << 8)

/* SCALE_3D_CNTL */
#define SCALE_PIX_EXPAND   (1u << 0)
#define SCALE_DITHER       (1u << 1)
#define DITHER_EN          (1u << 2)
#define DITHER_INIT        (1u << 3)
#define ROUND_EN           (1u << 4)
#define SCALE_3D_FCN_SHIFT 6
#define SCALE_PIX_REP      (1u << 8)
#define NEAREST_TEX_VIS    (1u << 9)
#define APPLE_YUV_MODE     (1u << 10)
#define ALPHA_FOG_SHIFT    11
#define COLOR_OVERRIDE     (1u << 13)
#define RED_DITHER_MAX     (1u << 14)
#define ALPHA_BLEND_SRC    16
#define ALPHA_BLEND_DST    19
#define TEX_LIGHT_FCN      22
#define MIP_MAP_DISABLE    (1u << 24)
#define BILINEAR_TEX_EN    (1u << 25)
#define TEX_BLEND_FCN      26
#define TEX_AMASK_AEN      (1u << 28)
#define TEX_AMASK_MODE     (1u << 29)
#define TEX_MAP_AEN        (1u << 30)
#define SRC_3D_SEL         (1u << 31)

/* SCALE_3D_FCN */
#define FCN_SCALE   1
#define FCN_TEXTURE 2
#define FCN_SHADE   3

/* ALPHA_FOG_EN */
#define ALPHA_FOG_BLEND 1
#define ALPHA_FOG_FOG   2

/* DP_PIX_WIDTH, the bank and nibble of a 4-bit texel */
#define CI4_RGB_INDEX_SHIFT 20
#define CI4_RGB_LOW_NIBBLE  (1u << 26)
#define CI4_RGB_HIGH_NIBBLE (1u << 27)

/* The GT-B's command FIFO has 48 entries. */
#define GT_FIFO_DEPTH 48

/* S/T trajectory registers that hold the hidden accumulator bits. */
#define TEX_HIDDEN_S_XINC  (1u << 0)
#define TEX_HIDDEN_S_YINC  (1u << 1)
#define TEX_HIDDEN_S_START (1u << 2)
#define TEX_HIDDEN_T_XINC  (1u << 3)
#define TEX_HIDDEN_T_YINC  (1u << 4)
#define TEX_HIDDEN_T_START (1u << 5)
#define TEX_HIDDEN_ALL     0x3f

typedef struct rgba_t {
    int r;
    int g;
    int b;
    int a;
} rgba_t;

struct mach64_3d_t {
    mach64_t *mach64;
    uint32_t  regs[256];            /* the register block, as dwords */
    uint32_t  texture_palette[256]; /* CI8 texels, 0x00RRGGBB */

    /* The trailing edge is a live trajectory register, like DST_Y_X. */
    int trail_x;
    int trail_valid;

    /* A write went to the draw engine FIFO since the last 3D one. */
    int fifo_pending;

    /* TEX_HIDDEN_*: the S/T registers whose value is the live accumulator,
       wider than the register, until the guest writes them again. */
    uint8_t tex_hidden;

    /* X error diffusion of the current scan line. */
    int dither_valid;
    int dither_y;
    int dither_err[3];
};

static int
mach64_3d_clamp8(int v)
{
    return (v < 0) ? 0 : ((v > 255) ? 255 : v);
}

static int
mach64_3d_sign_extend(uint32_t v, unsigned bits)
{
    uint32_t sign = 1u << (bits - 1);

    v &= (1u << bits) - 1;
    return (int) ((v ^ sign) - sign);
}

/*
 * The trajectory and interpolator registers use only some of their 32 bits
 * (VT/RAGE RRG chapter 6). Values are kept in the raw accumulator scale the
 * engine uses, with the reserved bits dropped and the sign taken from the
 * field's top bit.
 */
static int32_t
mach64_3d_signed_field_decode(uint32_t raw, unsigned lsb, unsigned width)
{
    uint32_t mask  = (UINT32_C(1) << width) - 1;
    uint32_t sign  = UINT32_C(1) << (width - 1);
    uint32_t field = (raw >> lsb) & mask;
    int32_t  value = (int32_t) ((field ^ sign) - sign);

    return (int32_t) ((int64_t) value * (INT64_C(1) << lsb));
}

/* S.10.16: S/T X_INC2, Y_INC2 and XY_INC2. */
static int32_t
mach64_3d_s10_16_decode(uint32_t raw)
{
    return mach64_3d_signed_field_decode(raw, 0, 27);
}

static uint32_t
mach64_3d_s10_16_encode(int64_t value)
{
    return (uint32_t) value & 0x07ffffff;
}

/* S.11.16: S/T XINC_START and Y_INC. */
static int32_t
mach64_3d_s11_16_decode(uint32_t raw)
{
    return mach64_3d_signed_field_decode(raw, 0, 28);
}

static uint32_t
mach64_3d_s11_16_encode(int64_t value)
{
    return (uint32_t) value & 0x0fffffff;
}

/* S/T START: unsigned 10.11 in bits 25:5, in the 16-bit fraction scale. */
static int32_t
mach64_3d_u10_11_decode(uint32_t raw)
{
    return (int32_t) (raw & 0x03ffffe0);
}

static uint32_t
mach64_3d_u10_11_encode(int64_t value)
{
    return (uint32_t) value & 0x03ffffe0;
}

/* Color and alpha: S.8.12 in bits 24:4, so 16 fraction bits as stored. */
static int32_t
mach64_3d_s8_12_decode(uint32_t raw)
{
    return mach64_3d_signed_field_decode(raw, 4, 21);
}

static uint32_t
mach64_3d_s8_12_encode(int64_t value)
{
    return (uint32_t) value & 0x01fffff0;
}

/*
 * An interpolated color goes through the register's own field before it is
 * clamped: ATI's set-up can carry an interpolator outside the field beyond
 * the edge of a thin triangle and back to a valid color at the next sample,
 * which a clamp of the wide sum would lose. Only the sign and the eight
 * integer bits matter here.
 */
static int
mach64_3d_s8_12_color(int64_t value)
{
    uint32_t integer = ((uint32_t) value >> 16) & 0x1ff;

    return (integer & 0x100) ? 0 : (int) integer;
}

/* Z: S.16.12 in bits 28:0. */
static int32_t
mach64_3d_s16_12_decode(uint32_t raw)
{
    return mach64_3d_signed_field_decode(raw, 0, 29);
}

static uint32_t
mach64_3d_s16_12_encode(int64_t value)
{
    return (uint32_t) value & 0x1fffffff;
}

/* Likewise for Z: the depth compared is the field's, not the wide sum's. */
static uint16_t
mach64_3d_s16_12_depth(int64_t value)
{
    uint32_t integer = ((uint32_t) value >> 12) & 0x1ffff;

    return (integer & 0x10000) ? 0 : (uint16_t) integer;
}

/*
 * The video memory accesses of the engine, little endian and wrapping
 * like the frame buffer, marking each changed page once per pixel.
 */
static uint8_t
mach64_3d_vram_read8(mach64_t *mach64, uint32_t addr)
{
    return mach64->svga.vram[addr & mach64->vram_mask];
}

static uint16_t
mach64_3d_vram_read16(mach64_t *mach64, uint32_t addr)
{
    const uint8_t *vram = mach64->svga.vram;
    uint32_t       mask = mach64->vram_mask;

    return vram[addr & mask] | (vram[(addr + 1) & mask] << 8);
}

static uint32_t
mach64_3d_vram_read32(mach64_t *mach64, uint32_t addr)
{
    const uint8_t *vram = mach64->svga.vram;
    uint32_t       mask = mach64->vram_mask;

    return vram[addr & mask] | (vram[(addr + 1) & mask] << 8) | (vram[(addr + 2) & mask] << 16) | ((uint32_t) vram[(addr + 3) & mask] << 24);
}

static uint32_t
mach64_3d_vram_read(mach64_t *mach64, uint32_t addr, int bpp)
{
    switch (bpp) {
        case 1:
            return mach64_3d_vram_read8(mach64, addr);
        case 2:
            return mach64_3d_vram_read16(mach64, addr);
        case 4:
            return mach64_3d_vram_read32(mach64, addr);
        default:
            return 0;
    }
}

static void
mach64_3d_vram_changed(mach64_t *mach64, uint32_t addr, unsigned bytes)
{
    uint32_t first = addr & mach64->vram_mask;
    uint32_t last  = (addr + bytes - 1) & mach64->vram_mask;
    int      stamp = mach64->svga.monitor->mon_changeframecount;

    mach64->svga.changedvram[first >> 12] = stamp;
    if ((first >> 12) != (last >> 12))
        mach64->svga.changedvram[last >> 12] = stamp;
}

static void
mach64_3d_vram_write(mach64_t *mach64, uint32_t addr, uint32_t val, int bpp)
{
    uint32_t mask = mach64->vram_mask;
    uint8_t *vram = mach64->svga.vram;

    for (int i = 0; i < bpp; i++)
        vram[(addr + i) & mask] = val >> (i * 8);
    mach64_3d_vram_changed(mach64, addr, bpp);
}

/* DP_DST_PIX_WIDTH is the frame buffer's pixel type, DP_SCALE_PIX_WIDTH
   that of the texture or scaler source. */
static int
mach64_3d_dst_format(const mach64_t *mach64)
{
    return mach64->dp_pix_width & 15;
}

static int
mach64_3d_src_format(const mach64_t *mach64)
{
    return (mach64->dp_pix_width >> 28) & 15;
}

static int
mach64_3d_bytes_per_pixel(int format)
{
    switch (format) {
        case 2: /* 8 bpp, CI8 as a source */
        case 7: /* RGB 332 */
        case 8: /* Y8 */
            return 1;
        case 3:  /* ARGB 1555 */
        case 4:  /* RGB 565 */
        case 15: /* ARGB 4444 */
            return 2;
        case 6:  /* ARGB 8888 */
        case 11: /* YUV 422, read in pairs */
        case 14: /* AYUV 8888 */
            return 4;
        default:
            return 0;
    }
}

/*
 * SCALE_PIX_EXPAND selects how a component narrower than 8 bits enters the
 * 24-bit pipeline: clear, zero extended; set, "dynamic range" corrected,
 * which repeats the component's bits into the low ones.
 */
static uint8_t
mach64_3d_expand_component(unsigned value, unsigned bits, int dynamic_range)
{
    unsigned result;
    unsigned remaining;

    if (!bits || (bits >= 8))
        return value;

    value &= (1u << bits) - 1;
    result = value << (8 - bits);
    if (!dynamic_range)
        return result;

    remaining = 8 - bits;
    while (remaining) {
        unsigned copy = (bits < remaining) ? bits : remaining;

        result |= (value >> (bits - copy)) << (remaining - copy);
        remaining -= copy;
    }
    return result;
}

/*
 * YUV. APPLE_YUV_MODE makes U and V two's complement around zero instead of
 * unsigned around 128.
 */
typedef struct mach64_yuv_rgb_t {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} mach64_yuv_rgb_t;

static int
mach64_yuv_chroma(uint8_t raw, int signed_uv)
{
    return signed_uv ? (int) (int8_t) raw : ((int) raw - 128);
}

static mach64_yuv_rgb_t
mach64_yuv_centered_to_rgb(int y, int u, int v)
{
    mach64_yuv_rgb_t rgb;

    rgb.r = mach64_3d_clamp8(y + ((359 * v) >> 8));
    rgb.g = mach64_3d_clamp8(y - ((88 * u + 183 * v) >> 8));
    rgb.b = mach64_3d_clamp8(y + ((454 * u) >> 8));
    return rgb;
}

static mach64_yuv_rgb_t
mach64_yuv_to_rgb(uint8_t y, uint8_t u, uint8_t v, int signed_uv)
{
    return mach64_yuv_centered_to_rgb(y, mach64_yuv_chroma(u, signed_uv), mach64_yuv_chroma(v, signed_uv));
}

/* YUV 422 pixel pairs are Y0, U, Y1, V from the low byte up. */
static uint8_t
mach64_yuyv_y(uint32_t packed, unsigned pixel)
{
    return packed >> ((pixel & 1) ? 16 : 0);
}

static uint8_t
mach64_yuyv_u(uint32_t packed)
{
    return packed >> 8;
}

static uint8_t
mach64_yuyv_v(uint32_t packed)
{
    return packed >> 24;
}

static rgba_t
mach64_3d_yuv_rgba(int y, int u, int v, int a)
{
    mach64_yuv_rgb_t rgb = mach64_yuv_centered_to_rgb(y, u, v);
    rgba_t           c   = { rgb.r, rgb.g, rgb.b, mach64_3d_clamp8(a) };

    return c;
}

/*
 * Texture palette. CI4 texels use the CI8 path: DP_PIX_WIDTH 23:20 picks a
 * bank of 16 entries, and bit 26 or 27 whether a byte gives its low or high
 * nibble. With neither, or the undocumented both, the byte is a CI8 index.
 */
static uint8_t
mach64_3d_texture_palette_texel_index(uint32_t dp_pix_width, uint8_t texel)
{
    uint32_t ci4  = dp_pix_width & (CI4_RGB_LOW_NIBBLE | CI4_RGB_HIGH_NIBBLE);
    uint8_t  bank = (dp_pix_width >> CI4_RGB_INDEX_SHIFT) & 0x0f;

    if ((ci4 != CI4_RGB_LOW_NIBBLE) && (ci4 != CI4_RGB_HIGH_NIBBLE))
        return texel;
    return (bank << 4) | ((ci4 == CI4_RGB_HIGH_NIBBLE) ? (texel >> 4) : (texel & 0x0f));
}

/* TEX_PALETTE takes one entry at a time, the index in bits 31:24. */
static void
mach64_3d_texture_palette_store(uint32_t *palette, uint32_t value)
{
    palette[value >> 24] = value & 0x00ffffff;
}

/*
 * The color comparator (CLR_CMP_CNTL). CLR_CMP_FCN 1 is "always", 4 "not
 * equal" and 5 "equal"; a true compare keeps the destination, so the write
 * is inhibited. CLR_CMP_SRC 0 compares the destination, 2 the texel or the
 * scaler source; a pseudo-color source compares its index.
 */
static int
mach64_3d_color_compare_inhibits(uint32_t cntl, uint32_t key, uint32_t mask, uint32_t selected)
{
    uint32_t reference = key & mask;

    selected &= mask;
    switch (cntl & 7) {
        case 1:
            return 1;
        case 4:
            return selected != reference;
        case 5:
            return selected == reference;
        default:
            return 0;
    }
}

static int
mach64_3d_destination_compare_enabled(uint32_t cntl)
{
    unsigned fcn = cntl & 7;

    return (((cntl >> 24) & 3) == 0) && ((fcn == 1) || (fcn == 4) || (fcn == 5));
}

static int
mach64_3d_destination_compare_inhibits(uint32_t cntl, uint32_t key, uint32_t mask,
                                       uint32_t destination, uint32_t pixel_mask)
{
    if (!mach64_3d_destination_compare_enabled(cntl))
        return 0;
    return mach64_3d_color_compare_inhibits(cntl, key & pixel_mask, mask & pixel_mask, destination & pixel_mask);
}

static int
mach64_3d_texel_key_compare(uint32_t cntl, uint32_t key, uint32_t mask, uint32_t selected)
{
    if (((cntl >> 24) & 3) != 2)
        return 0;
    return mach64_3d_color_compare_inhibits(cntl, key, mask, selected);
}

static int
mach64_3d_texel_key_match(uint32_t cntl, uint32_t key, uint32_t mask, uint8_t red, uint8_t green, uint8_t blue)
{
    return mach64_3d_texel_key_compare(cntl, key, mask, (red << 16) | (green << 8) | blue);
}

static int
mach64_3d_texel_key_match_index(uint32_t cntl, uint32_t key, uint32_t mask, uint8_t index)
{
    return mach64_3d_texel_key_compare(cntl, key, mask, index);
}

/* NEAREST_TEX_VIS: only the nearest of the filtered texels can hide a
   pixel; otherwise any of them does. */
static int
mach64_3d_texel_visibility_inhibits(int nearest_only, int nearest_inhibits, int any_inhibits)
{
    return nearest_only ? nearest_inhibits : any_inhibits;
}

/*
 * With DP_MONO_SRC 0, always one, DP_FRGD_SRC picks the color entering the
 * data path. ATI's alpha-mask pass switches it from DP_BKGD_CLR to the 3D
 * data, so both are taken here; the others are left to the 3D color.
 */
static uint32_t
mach64_3d_dp_select_source(uint32_t dp_src, uint32_t bkgd_clr, uint32_t frgd_clr, uint32_t color_3d)
{
    if (((dp_src >> 16) & 3) != 0)
        return color_3d;

    switch ((dp_src >> 8) & 7) {
        case SRC_BG:
            return bkgd_clr;
        case SRC_FG:
            return frgd_clr;
        default:
            return color_3d;
    }
}

/* RED_DITHER_MAX keeps a dithered RGB 332 red below 7, leaving palette
   entries 224-255 alone. */
static unsigned
mach64_3d_rgb8_red_code(unsigned red, int dithering, int red_dither_max)
{
    red &= 7;
    if (dithering && red_dither_max && (red > 6))
        red = 6;
    return red;
}

/* TEX_BLEND_FCN 3 with alpha blending takes alpha from the mip map
   distance, for ATI's two-pass trilinear filter. */
static int
mach64_3d_uses_lod_alpha(unsigned alpha_fog, unsigned tex_blend_fcn)
{
    return ((alpha_fog & 3) == ALPHA_FOG_BLEND) && ((tex_blend_fcn & 3) == 3);
}

static rgba_t
mach64_3d_yuv_to_rgba(mach64_3d_t *ctx, int y, int u, int v, int a)
{
    int              signed_uv = !!(ctx->regs[SCALE_3D_CNTL >> 2] & APPLE_YUV_MODE);
    mach64_yuv_rgb_t rgb       = mach64_yuv_to_rgb(y, u, v, signed_uv);
    rgba_t           c         = { rgb.r, rgb.g, rgb.b, mach64_3d_clamp8(a) };

    return c;
}

/* Unpacks a pixel to 8 bits a component. A destination pixel is always
   range corrected, a texel or scaler source one per SCALE_PIX_EXPAND. */
static rgba_t
mach64_3d_unpack(mach64_3d_t *ctx, int format, uint32_t raw, int source)
{
    int      dynamic = !source || (ctx->regs[SCALE_3D_CNTL >> 2] & SCALE_PIX_EXPAND);
    rgba_t   c       = { 0, 0, 0, 255 };
    uint32_t p;

    switch (format) {
        case 2:
            if (source) {
                p   = ctx->texture_palette[mach64_3d_texture_palette_texel_index(ctx->mach64->dp_pix_width, raw)];
                c.r = (p >> 16) & 0xff;
                c.g = (p >> 8) & 0xff;
                c.b = p & 0xff;
            } else {
                p   = ctx->mach64->svga.pallook[raw & 0xff];
                c.r = getcolr(p);
                c.g = getcolg(p);
                c.b = getcolb(p);
            }
            break;
        case 3:
            c.a = (raw & 0x8000) ? 255 : 0;
            c.r = mach64_3d_expand_component((raw >> 10) & 31, 5, dynamic);
            c.g = mach64_3d_expand_component((raw >> 5) & 31, 5, dynamic);
            c.b = mach64_3d_expand_component(raw & 31, 5, dynamic);
            break;
        case 4:
            c.r = mach64_3d_expand_component((raw >> 11) & 31, 5, dynamic);
            c.g = mach64_3d_expand_component((raw >> 5) & 63, 6, dynamic);
            c.b = mach64_3d_expand_component(raw & 31, 5, dynamic);
            break;
        case 6:
            c.a = (raw >> 24) & 0xff;
            c.r = (raw >> 16) & 0xff;
            c.g = (raw >> 8) & 0xff;
            c.b = raw & 0xff;
            break;
        case 7:
            c.r = mach64_3d_expand_component((raw >> 5) & 7, 3, dynamic);
            c.g = mach64_3d_expand_component((raw >> 2) & 7, 3, dynamic);
            c.b = mach64_3d_expand_component(raw & 3, 2, dynamic);
            break;
        case 8:
            c.r = c.g = c.b = raw & 0xff;
            break;
        case 14:
            return mach64_3d_yuv_to_rgba(ctx, (raw >> 16) & 0xff, (raw >> 8) & 0xff, raw & 0xff, (raw >> 24) & 0xff);
        case 15:
            c.a = ((raw >> 12) & 15) * 17;
            c.r = mach64_3d_expand_component((raw >> 8) & 15, 4, dynamic);
            c.g = mach64_3d_expand_component((raw >> 4) & 15, 4, dynamic);
            c.b = mach64_3d_expand_component(raw & 15, 4, dynamic);
            break;
        default:
            break;
    }
    return c;
}

/*
 * The reduction of the 24-bit pipeline to the frame buffer's pixel type,
 * SCALE_3D_CNTL 4:1: DITHER_EN dithers, by X error diffusion or, with
 * SCALE_DITHER, a fixed two-dimensional table; DITHER_INIT clears the error
 * at each scan line; ROUND_EN rounds when not dithering. The register guide
 * does not give the table, so a 4x4 Bayer matrix stands in for it.
 */
static int
mach64_3d_quantize_ordered(int v, int bits, int x, int y)
{
    static const uint8_t bayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    int                  drop       = 8 - bits;
    int                  max        = (1 << bits) - 1;
    int                  span       = (1 << drop) - 1;
    int                  add        = (bayer4[((y & 3) << 2) | (x & 3)] * span + 7) / 15;
    int                  q          = (mach64_3d_clamp8(v) + add) >> drop;

    return (q > max) ? max : q;
}

static int
mach64_3d_quantize_round(int v, int bits)
{
    int drop = 8 - bits;
    int max  = (1 << bits) - 1;
    int q    = (mach64_3d_clamp8(v) + (1 << (drop - 1))) >> drop;

    return (q > max) ? max : q;
}

/* The quantization error carries into the next pixel in X. */
static int
mach64_3d_quantize_diffuse(int v, int bits, int *err)
{
    int max = (1 << bits) - 1;
    int a   = mach64_3d_clamp8(v + *err);
    int q   = a >> (8 - bits);

    if (q > max)
        q = max;
    *err = a - (q * 255 + max / 2) / max;
    return q;
}

static int
mach64_3d_quantize(mach64_3d_t *ctx, uint32_t cntl, int v, int bits, int channel, int x, int y)
{
    if (cntl & DITHER_EN) {
        if (cntl & SCALE_DITHER)
            return mach64_3d_quantize_ordered(v, bits, x, y);
        return mach64_3d_quantize_diffuse(v, bits, &ctx->dither_err[channel]);
    }
    if (cntl & ROUND_EN)
        return mach64_3d_quantize_round(v, bits);
    return v >> (8 - bits);
}

static uint32_t
mach64_3d_pack(mach64_3d_t *ctx, int format, rgba_t c, int x, int y)
{
    uint32_t cntl = ctx->regs[SCALE_3D_CNTL >> 2];
    int      r;
    int      g;
    int      b;

    c.r = mach64_3d_clamp8(c.r);
    c.g = mach64_3d_clamp8(c.g);
    c.b = mach64_3d_clamp8(c.b);
    c.a = mach64_3d_clamp8(c.a);

    if (!(cntl & DITHER_EN))
        ctx->dither_valid = 0;
    else if (!(cntl & SCALE_DITHER)) {
        if (!ctx->dither_valid) {
            ctx->dither_valid  = 1;
            ctx->dither_y      = y;
            ctx->dither_err[0] = ctx->dither_err[1] = ctx->dither_err[2] = 0;
        } else if (y != ctx->dither_y) {
            if (cntl & DITHER_INIT)
                ctx->dither_err[0] = ctx->dither_err[1] = ctx->dither_err[2] = 0;
            ctx->dither_y = y;
        }
    }

    switch (format) {
        case 3:
            r = mach64_3d_quantize(ctx, cntl, c.r, 5, 0, x, y);
            g = mach64_3d_quantize(ctx, cntl, c.g, 5, 1, x, y);
            b = mach64_3d_quantize(ctx, cntl, c.b, 5, 2, x, y);
            return ((c.a >= 128) ? 0x8000 : 0) | (r << 10) | (g << 5) | b;
        case 4:
            r = mach64_3d_quantize(ctx, cntl, c.r, 5, 0, x, y);
            g = mach64_3d_quantize(ctx, cntl, c.g, 6, 1, x, y);
            b = mach64_3d_quantize(ctx, cntl, c.b, 5, 2, x, y);
            return (r << 11) | (g << 5) | b;
        case 6:
            return ((uint32_t) c.a << 24) | (c.r << 16) | (c.g << 8) | c.b;
        case 7:
            r = mach64_3d_quantize(ctx, cntl, c.r, 3, 0, x, y);
            g = mach64_3d_quantize(ctx, cntl, c.g, 3, 1, x, y);
            b = mach64_3d_quantize(ctx, cntl, c.b, 2, 2, x, y);
            r = mach64_3d_rgb8_red_code(r, !!(cntl & DITHER_EN), !!(cntl & RED_DITHER_MAX));
            return (r << 5) | (g << 2) | b;
        case 8:
            return (c.r * 77 + c.g * 150 + c.b * 29) >> 8;
        case 15:
            r = mach64_3d_quantize(ctx, cntl, c.r, 4, 0, x, y);
            g = mach64_3d_quantize(ctx, cntl, c.g, 4, 1, x, y);
            b = mach64_3d_quantize(ctx, cntl, c.b, 4, 2, x, y);
            return ((c.a >> 4) << 12) | (r << 8) | (g << 4) | b;
        default:
            return 0;
    }
}

static rgba_t
mach64_3d_read_dst(mach64_3d_t *ctx, uint32_t addr, int format)
{
    int    bpp   = mach64_3d_bytes_per_pixel(format);
    rgba_t black = { 0, 0, 0, 255 };

    if (!bpp)
        return black;
    return mach64_3d_unpack(ctx, format, mach64_3d_vram_read(ctx->mach64, addr, bpp), 0);
}

/* WRITE_MASK picks the bits written; the whole pixel needs no read. */
static void
mach64_3d_write_dst_raw(mach64_3d_t *ctx, uint32_t addr, int format, uint32_t raw)
{
    mach64_t *mach64     = ctx->mach64;
    int       bpp        = mach64_3d_bytes_per_pixel(format);
    uint32_t  pixel_mask = (bpp == 4) ? 0xffffffff : ((1u << (bpp * 8)) - 1);
    uint32_t  write_mask = mach64->write_mask & pixel_mask;

    if (!bpp)
        return;
    if (write_mask != pixel_mask)
        raw = (mach64_3d_vram_read(mach64, addr, bpp) & ~write_mask) | (raw & write_mask);
    mach64_3d_vram_write(mach64, addr, raw, bpp);
}

static void
mach64_3d_write_dst(mach64_3d_t *ctx, uint32_t addr, int format, rgba_t c)
{
    mach64_t *mach64 = ctx->mach64;
    uint32_t  cntl   = ctx->regs[SCALE_3D_CNTL >> 2];
    int       bpp    = mach64_3d_bytes_per_pixel(format);
    uint32_t  base   = (mach64->dst_off_pitch & 0xfffff) << 3;
    int       pitch  = ((mach64->dst_off_pitch >> 22) & 0x3ff) << 3;
    int       x      = 0;
    int       y      = 0;
    uint32_t  raw;

    /* Dithering is by the pixel's place on the destination. */
    if (bpp && pitch && (addr >= base)) {
        uint32_t pixel = (addr - base) / bpp;

        x = pixel % pitch;
        y = pixel / pitch;
    }

    if (format == 2) {
        raw = ((mach64_3d_clamp8(c.r) >> 5) << 5) | ((mach64_3d_clamp8(c.g) >> 5) << 2) | (mach64_3d_clamp8(c.b) >> 6);
        mach64_3d_vram_write(mach64, addr, raw, 1);
        return;
    }

    raw = mach64_3d_pack(ctx, format, c, x, y);

    /*
     * A blended 15/16-bpp pixel through the dither table: a component equal
     * to the destination's keeps the destination's bits. The table would
     * otherwise raise an already quantized component even where the blend
     * added nothing, as with a black additive texel.
     */
    if (((format == 3) || (format == 4)) && (((cntl >> ALPHA_FOG_SHIFT) & 3) == ALPHA_FOG_BLEND) &&
        ((cntl & (DITHER_EN | SCALE_DITHER)) == (DITHER_EN | SCALE_DITHER))) {
        uint32_t old  = mach64_3d_vram_read(mach64, addr, bpp);
        rgba_t   dst  = mach64_3d_unpack(ctx, format, old, 0);
        uint32_t keep = 0;

        if (c.r == dst.r)
            keep |= (format == 3) ? 0x7c00 : 0xf800;
        if (c.g == dst.g)
            keep |= (format == 3) ? 0x03e0 : 0x07e0;
        if (c.b == dst.b)
            keep |= 0x001f;
        raw = (raw & ~keep) | (old & keep);
    }
    mach64_3d_write_dst_raw(ctx, addr, format, raw);
}

/* Alpha and fog weights run from 0 to 255 inclusive. */
static rgba_t
mach64_3d_lerp(rgba_t a, rgba_t b, int t)
{
    rgba_t r;

    t   = mach64_3d_clamp8(t);
    r.r = (a.r * (255 - t) + b.r * t + 127) / 255;
    r.g = (a.g * (255 - t) + b.g * t + 127) / 255;
    r.b = (a.b * (255 - t) + b.b * t + 127) / 255;
    r.a = (a.a * (255 - t) + b.a * t + 127) / 255;
    return r;
}

/* A texture coordinate's fraction is binary: 128 is one half, and 255 is
   255/256 of the way to the next texel, not the next texel. */
static rgba_t
mach64_3d_lerp_texel(rgba_t a, rgba_t b, int fraction)
{
    rgba_t r;

    r.r = (a.r * (256 - fraction) + b.r * fraction + 128) >> 8;
    r.g = (a.g * (256 - fraction) + b.g * fraction + 128) >> 8;
    r.b = (a.b * (256 - fraction) + b.b * fraction + 128) >> 8;
    r.a = (a.a * (256 - fraction) + b.a * fraction + 128) >> 8;
    return r;
}

static int
mach64_3d_component(rgba_t c, int channel)
{
    switch (channel) {
        case 0:
            return c.r;
        case 1:
            return c.g;
        case 2:
            return c.b;
        default:
            return c.a;
    }
}

/* ALPHA_BLEND_SRC and _DST: zero, one, the other side's color, one minus
   it, source alpha and one minus source alpha. */
static int
mach64_3d_blend_factor(int code, rgba_t src, rgba_t dst, int channel, int is_source)
{
    int other = mach64_3d_component(is_source ? dst : src, channel);

    switch (code) {
        case 0:
            return 0;
        case 1:
            return 255;
        case 2:
            return other;
        case 3:
            return 255 - other;
        case 4:
            return src.a;
        case 5:
            return 255 - src.a;
        default:
            return 255;
    }
}

static rgba_t
mach64_3d_blend(uint32_t cntl, rgba_t src, rgba_t dst)
{
    int    src_code = (cntl >> ALPHA_BLEND_SRC) & 7;
    int    dst_code = (cntl >> ALPHA_BLEND_DST) & 7;
    int    out[4];
    rgba_t o;

    for (int ch = 0; ch < 4; ch++)
        out[ch] = mach64_3d_clamp8((mach64_3d_component(src, ch) * mach64_3d_blend_factor(src_code, src, dst, ch, 1) +
                                    mach64_3d_component(dst, ch) * mach64_3d_blend_factor(dst_code, src, dst, ch, 0) + 127) / 255);
    o.r = out[0];
    o.g = out[1];
    o.b = out[2];
    o.a = out[3];
    return o;
}

/* Z_TEST: never, <, <=, ==, >=, >, != and always. */
static int
mach64_3d_z_test(uint16_t src, uint16_t dst, int test)
{
    switch (test & 7) {
        case 0:
            return 0;
        case 1:
            return src < dst;
        case 2:
            return src <= dst;
        case 3:
            return src == dst;
        case 4:
            return src >= dst;
        case 5:
            return src > dst;
        case 6:
            return src != dst;
        default:
            return 1;
    }
}

/* TEX_LIGHT_FCN: 0 replaces with the texel, 1 modulates by the shading,
   2 blends towards the texel by its alpha. COLOR_OVERRIDE keeps the shading
   with the texel's alpha. */
static rgba_t
mach64_3d_texture_light(uint32_t cntl, rgba_t texel, rgba_t shade)
{
    if (cntl & COLOR_OVERRIDE) {
        int a = texel.a;

        texel   = shade;
        texel.a = a;
        return texel;
    }

    switch ((cntl >> TEX_LIGHT_FCN) & 3) {
        case 1:
            texel.r = texel.r * shade.r / 255;
            texel.g = texel.g * shade.g / 255;
            texel.b = texel.b * shade.b / 255;
            break;
        case 2:
            {
                rgba_t lit = mach64_3d_lerp(shade, texel, texel.a);

                lit.a = shade.a;
                texel = lit;
                break;
            }
        default:
            break;
    }
    return texel;
}

/*
 * The texel's alpha. Without TEX_MAP_AEN it is the interpolated alpha.
 * TEX_AMASK_AEN makes the texel's low alpha bit a mask: with TEX_AMASK_MODE
 * clear a 0 drops the pixel, set a 1 makes it opaque. Returns 0 to drop.
 */
static int
mach64_3d_texture_alpha(uint32_t cntl, rgba_t *texel, int alpha)
{
    if (mach64_3d_uses_lod_alpha((cntl >> ALPHA_FOG_SHIFT) & 3, (cntl >> TEX_BLEND_FCN) & 3))
        return 1;
    if (!(cntl & TEX_MAP_AEN)) {
        texel->a = alpha;
        return 1;
    }
    if (cntl & TEX_AMASK_AEN) {
        int mask = texel->a & 1;

        if (!(cntl & TEX_AMASK_MODE)) {
            if (!mask)
                return 0;
            texel->a = alpha;
        } else
            texel->a = mask ? 255 : alpha;
    }
    return 1;
}

/*
 * Mip maps. ATI's drivers repeat the smallest map's offset in the TEX_n_OFF
 * of every smaller level, so the chain of distinct offsets down from the
 * largest map ends at the smallest one there is; a level past it would read
 * that map with a smaller pitch.
 */
static int
mach64_3d_mip_lowest_populated_level(const uint32_t offsets[11], int largest_level)
{
    int level;

    if (largest_level < 0)
        largest_level = 0;
    if (largest_level > 10)
        largest_level = 10;

    level = largest_level;
    while ((level > 0) && (offsets[level - 1] != offsets[level]))
        level--;
    return level;
}

typedef struct mach64_3d_mip_lod_t {
    int minifying;
    int floor_lod;
    int nearest_lod;
    int fraction;
} mach64_3d_mip_lod_t;

static uint64_t
mach64_3d_abs64(int64_t value)
{
    return (value < 0) ? (uint64_t) -value : (uint64_t) value;
}

/*
 * The level comes from the S/T derivatives: coord_shift is the shift that
 * gives a texel of the largest map. fraction is the place between the two
 * levels on either side, and nearest_lod changes at the square root of two
 * between them, 106/256 of the way. max_lod counts the levels there are
 * below the largest map, which with a repeated offset is fewer than its
 * size suggests.
 */
static mach64_3d_mip_lod_t
mach64_3d_mip_lod(int64_t dsdx, int64_t dtdx, int64_t dsdy, int64_t dtdy, int coord_shift, int max_lod)
{
    mach64_3d_mip_lod_t result = { 0, 0, 0, 0 };
    uint64_t            rho    = mach64_3d_abs64(dsdx);
    uint64_t            scale;

    if (mach64_3d_abs64(dtdx) > rho)
        rho = mach64_3d_abs64(dtdx);
    if (mach64_3d_abs64(dsdy) > rho)
        rho = mach64_3d_abs64(dsdy);
    if (mach64_3d_abs64(dtdy) > rho)
        rho = mach64_3d_abs64(dtdy);

    if (coord_shift < 0)
        coord_shift = 0;
    if (coord_shift > 62)
        coord_shift = 62;
    if (max_lod < 0)
        max_lod = 0;

    scale = UINT64_C(1) << coord_shift;
    if (rho <= scale)
        return result;

    result.minifying = 1;
    while ((result.floor_lod < max_lod) && (scale <= (UINT64_MAX / 2)) && (rho >= (scale * 2))) {
        scale *= 2;
        result.floor_lod++;
    }

    if ((result.floor_lod < max_lod) && (rho > scale)) {
        result.fraction = ((rho - scale) * 255 + scale / 2) / scale;
        if (result.fraction > 255)
            result.fraction = 255;
    }

    result.nearest_lod = result.floor_lod;
    if ((result.fraction >= 106) && (result.nearest_lod < max_lod))
        result.nearest_lod++;
    return result;
}

typedef enum mach64_3d_texture_filter_t {
    MACH64_3D_TEXTURE_FILTER_NONE = 0,
    MACH64_3D_TEXTURE_FILTER_NEAREST,
    MACH64_3D_TEXTURE_FILTER_BILINEAR
} mach64_3d_texture_filter_t;

/*
 * BILINEAR_TEX_EN filters a magnified texture, TEX_BLEND_FCN a minified one.
 * TEX_BLEND_FCN 2 or 3 without BILINEAR_TEX_EN draws nothing while
 * magnifying, for ATI's multipass filters, rather than falling back to the
 * nearest texel.
 */
static mach64_3d_texture_filter_t
mach64_3d_texture_filter(int minifying, unsigned tex_blend_fcn, int bilinear_tex_en)
{
    tex_blend_fcn &= 3;

    if (!minifying) {
        if (!bilinear_tex_en && ((tex_blend_fcn == 2) || (tex_blend_fcn == 3)))
            return MACH64_3D_TEXTURE_FILTER_NONE;
        return bilinear_tex_en ? MACH64_3D_TEXTURE_FILTER_BILINEAR : MACH64_3D_TEXTURE_FILTER_NEAREST;
    }
    return ((tex_blend_fcn == 2) || (tex_blend_fcn == 3)) ? MACH64_3D_TEXTURE_FILTER_BILINEAR : MACH64_3D_TEXTURE_FILTER_NEAREST;
}

/* A texture map: the largest, or the one a level selects. */
typedef struct mach64_3d_texture_t {
    int      format;
    int      bpp;
    int      pitch_log2;
    int      size_log2;
    int      height_log2;
    int      max_lod;
    int      width;
    int      width_mask;
    int      height_mask;
    int      bilinear;
    int      coord_shift;
    uint32_t base;
} mach64_3d_texture_t;

typedef struct mach64_3d_texel_t {
    rgba_t color;
    int    nearest_inhibits;
    int    any_inhibits;
} mach64_3d_texel_t;

/*
 * S and T are normalized: the whole map spans 1024 coordinate units, whatever
 * its size. START's 10.11 field sits in bits 25:5, a 16-bit fraction as
 * stored, so a texel of a map of 2^size_log2 is 1 << (26 - size_log2). The
 * Windows 95 HAL's cube program START 0 and 04000000h at the edges of a
 * 256x256 map (TEX_SIZE_PITCH 888h).
 */
static void
mach64_3d_texture_select(mach64_3d_t *ctx, mach64_3d_texture_t *map, int pitch_log2, int size_log2, int height_log2)
{
    map->pitch_log2  = pitch_log2;
    map->size_log2   = size_log2;
    map->height_log2 = height_log2;
    map->width       = 1 << pitch_log2;
    map->width_mask  = map->width - 1;
    map->height_mask = (1 << height_log2) - 1;
    map->coord_shift = 26 - size_log2;
    map->base        = ctx->regs[(TEX_0_OFF + (size_log2 << 2)) >> 2];
}

/* TEX_SIZE_PITCH holds through a draw: the largest map is set up once. */
static void
mach64_3d_texture_init(mach64_3d_t *ctx, int format, int bilinear, mach64_3d_texture_t *map)
{
    uint32_t tex_size_pitch = ctx->regs[TEX_SIZE_PITCH >> 2];
    int      pitch_log2     = tex_size_pitch & 15;
    int      size_log2      = (tex_size_pitch >> 4) & 15;
    int      height_log2    = (tex_size_pitch >> 8) & 15;
    uint32_t offsets[11];

    if (pitch_log2 > 10)
        pitch_log2 = 10;
    if (size_log2 > 10)
        size_log2 = 10;
    if (height_log2 > 10)
        height_log2 = 10;
    if (!height_log2)
        height_log2 = size_log2;
    for (int level = 0; level <= 10; level++)
        offsets[level] = ctx->regs[(TEX_0_OFF + (level << 2)) >> 2];

    map->format   = format;
    map->bpp      = mach64_3d_bytes_per_pixel(format);
    map->max_lod  = size_log2 - mach64_3d_mip_lowest_populated_level(offsets, size_log2);
    map->bilinear = bilinear;
    mach64_3d_texture_select(ctx, map, pitch_log2, size_log2, height_log2);
}

static void
mach64_3d_texture_level(mach64_3d_t *ctx, const mach64_3d_texture_t *largest, int lod, int bilinear, mach64_3d_texture_t *map)
{
    int pitch_log2;
    int size_log2;
    int height_log2;

    if (lod < 0)
        lod = 0;
    if (lod > largest->max_lod)
        lod = largest->max_lod;
    pitch_log2  = largest->pitch_log2 - lod;
    size_log2   = largest->size_log2 - lod;
    height_log2 = largest->height_log2 - lod;

    *map          = *largest;
    map->bilinear = bilinear;
    mach64_3d_texture_select(ctx, map, (pitch_log2 < 0) ? 0 : pitch_log2, (size_log2 < 0) ? 0 : size_log2,
                             (height_log2 < 0) ? 0 : height_log2);
}

static mach64_3d_texel_t
mach64_3d_texel_at(mach64_3d_t *ctx, const mach64_3d_texture_t *map, int u, int v)
{
    mach64_t         *mach64 = ctx->mach64;
    mach64_3d_texel_t texel  = { 0 };
    uint32_t          raw;
    int               inhibits;

    texel.color.a = 255;
    if (map->format == 11) {
        raw         = mach64_3d_vram_read32(mach64, map->base + (v * map->width + (u & ~1)) * 2);
        texel.color = mach64_3d_yuv_to_rgba(ctx, mach64_yuyv_y(raw, u), mach64_yuyv_u(raw), mach64_yuyv_v(raw), 255);
        inhibits    = mach64_3d_texel_key_match(mach64->clr_cmp_cntl, mach64->clr_cmp_clr, mach64->clr_cmp_mask,
                                                texel.color.r, texel.color.g, texel.color.b);
    } else {
        raw         = mach64_3d_vram_read(mach64, map->base + (v * map->width + u) * map->bpp, map->bpp);
        texel.color = mach64_3d_unpack(ctx, map->format, raw, 1);
        if (map->format == 2)
            inhibits = mach64_3d_texel_key_match_index(mach64->clr_cmp_cntl, mach64->clr_cmp_clr, mach64->clr_cmp_mask,
                                                       mach64_3d_texture_palette_texel_index(mach64->dp_pix_width, raw));
        else
            inhibits = mach64_3d_texel_key_match(mach64->clr_cmp_cntl, mach64->clr_cmp_clr, mach64->clr_cmp_mask,
                                                 texel.color.r, texel.color.g, texel.color.b);
    }
    texel.nearest_inhibits = texel.any_inhibits = inhibits;
    return texel;
}

/*
 * One sample at S/T. The 2x2 filter weights texel centers, half a texel past
 * each integer coordinate: the HAL passes Direct3D's coordinates without a
 * half-texel bias, so T 0 of a wrapping map blends its last and first rows
 * equally, which the rim of Final Reality's neon entrance relies on.
 * Without the filter the sample is the texel holding the coordinate.
 */
static mach64_3d_texel_t
mach64_3d_sample_map(mach64_3d_t *ctx, const mach64_3d_texture_t *map, int64_t s, int64_t t)
{
    int               shift = map->coord_shift;
    int               u;
    int               v;
    int               u1;
    int               v1;
    int               fu;
    int               fv;
    mach64_3d_texel_t c00;
    mach64_3d_texel_t c10;
    mach64_3d_texel_t c01;
    mach64_3d_texel_t c11;
    mach64_3d_texel_t result;

    if (map->bilinear) {
        int64_t half = (int64_t) 1 << (shift - 1);

        s -= half;
        t -= half;
    }
    u   = (int) (s >> shift) & map->width_mask;
    v   = (int) (t >> shift) & map->height_mask;
    c00 = mach64_3d_texel_at(ctx, map, u, v);
    if (!map->bilinear)
        return c00;

    u1  = (u + 1) & map->width_mask;
    v1  = (v + 1) & map->height_mask;
    fu  = (int) (s >> (shift - 8)) & 255;
    fv  = (int) (t >> (shift - 8)) & 255;
    c10 = mach64_3d_texel_at(ctx, map, u1, v);
    c01 = mach64_3d_texel_at(ctx, map, u, v1);
    c11 = mach64_3d_texel_at(ctx, map, u1, v1);

    result.color        = mach64_3d_lerp_texel(mach64_3d_lerp_texel(c00.color, c10.color, fu),
                                               mach64_3d_lerp_texel(c01.color, c11.color, fu), fv);
    result.any_inhibits = c00.any_inhibits || c10.any_inhibits || c01.any_inhibits || c11.any_inhibits;
    if (fv >= 128)
        result.nearest_inhibits = (fu >= 128) ? c11.nearest_inhibits : c01.nearest_inhibits;
    else
        result.nearest_inhibits = (fu >= 128) ? c10.nearest_inhibits : c00.nearest_inhibits;
    return result;
}

/*
 * The texel for a pixel, by the filter and mip map mode. *draw is cleared
 * when the filter draws nothing, *key_inhibit set when the source color key
 * hides the pixel.
 */
static rgba_t
mach64_3d_sample_texture(mach64_3d_t *ctx, const mach64_3d_texture_t *largest, uint32_t cntl, int64_t s, int64_t t,
                         int64_t dsdx, int64_t dtdx, int64_t dsdy, int64_t dtdy, int *draw, int *key_inhibit)
{
    int                        blend   = (cntl >> TEX_BLEND_FCN) & 3;
    int                        nearest = !!(cntl & NEAREST_TEX_VIS);
    mach64_3d_mip_lod_t        lod     = mach64_3d_mip_lod(dsdx, dtdx, dsdy, dtdy, largest->coord_shift, largest->max_lod);
    mach64_3d_texture_filter_t filter  = mach64_3d_texture_filter(lod.minifying, blend, !!(cntl & BILINEAR_TEX_EN));
    mach64_3d_texture_t        map;
    mach64_3d_texel_t          texel;
    rgba_t                     none = { 0, 0, 0, 0 };

    *draw        = 1;
    *key_inhibit = 0;
    if (filter == MACH64_3D_TEXTURE_FILTER_NONE) {
        *draw = 0;
        return none;
    }

    if (!lod.minifying || (cntl & MIP_MAP_DISABLE)) {
        /* One map: the nearest texel, or the 2x2 filter of TEX_BLEND_FCN 2
           and 3. */
        map          = *largest;
        map.bilinear = (filter == MACH64_3D_TEXTURE_FILTER_BILINEAR);
        texel        = mach64_3d_sample_map(ctx, &map, s, t);
    } else if ((blend == 1) && (lod.floor_lod < largest->max_lod)) {
        /* Nearest texels of the two levels, blended between them. */
        mach64_3d_texture_t next;
        mach64_3d_texel_t   a;
        mach64_3d_texel_t   b;

        mach64_3d_texture_level(ctx, largest, lod.floor_lod, 0, &map);
        mach64_3d_texture_level(ctx, largest, lod.floor_lod + 1, 0, &next);
        a                      = mach64_3d_sample_map(ctx, &map, s, t);
        b                      = mach64_3d_sample_map(ctx, &next, s, t);
        texel.color            = mach64_3d_lerp(a.color, b.color, lod.fraction);
        texel.any_inhibits     = a.any_inhibits || b.any_inhibits;
        texel.nearest_inhibits = (lod.nearest_lod == lod.floor_lod) ? a.nearest_inhibits : b.nearest_inhibits;
    } else if (blend == 2) {
        /* The 2x2 filter in the nearest level. */
        mach64_3d_texture_level(ctx, largest, lod.nearest_lod, 1, &map);
        texel = mach64_3d_sample_map(ctx, &map, s, t);
    } else if (blend == 3) {
        /* One pass of the two-pass trilinear filter: the 2x2 filter in the
           other level, its distance as alpha. */
        int other = (lod.nearest_lod == lod.floor_lod) ? (lod.floor_lod + 1) : lod.floor_lod;

        if (other > largest->max_lod)
            other = largest->max_lod;
        mach64_3d_texture_level(ctx, largest, other, 1, &map);
        texel         = mach64_3d_sample_map(ctx, &map, s, t);
        texel.color.a = (lod.nearest_lod == lod.floor_lod) ? lod.fraction : (255 - lod.fraction);
    } else {
        mach64_3d_texture_level(ctx, largest, lod.nearest_lod, 0, &map);
        texel = mach64_3d_sample_map(ctx, &map, s, t);
    }

    *key_inhibit = mach64_3d_texel_visibility_inhibits(nearest, texel.nearest_inhibits, texel.any_inhibits);
    return texel.color;
}

/*
 * S/T START has 11 fraction bits and the S/T increments 16, so a trapezoid
 * leaves the trajectory five bits finer than the registers show. ATI's HAL
 * draws the second trapezoid of a triangle on that live state without
 * writing S/T again; a register the guest writes is back to its field.
 */
static uint8_t
mach64_3d_tex_hidden_bit(uint32_t reg)
{
    switch (reg) {
        case S_XINC_START:
            return TEX_HIDDEN_S_XINC;
        case S_Y_INC:
            return TEX_HIDDEN_S_YINC;
        case S_START:
            return TEX_HIDDEN_S_START;
        case T_XINC_START:
            return TEX_HIDDEN_T_XINC;
        case T_Y_INC:
            return TEX_HIDDEN_T_YINC;
        case T_START:
            return TEX_HIDDEN_T_START;
        default:
            return 0;
    }
}

/* The value the guest reads back: the register's field, never the hidden
   precision or the reserved bits. */
static uint32_t
mach64_3d_visible_reg(uint32_t reg, uint32_t raw)
{
    switch (reg) {
        case S_X_INC2:
        case S_Y_INC2:
        case S_XY_INC2:
        case T_X_INC2:
        case T_Y_INC2:
        case T_XY_INC2:
            return mach64_3d_s10_16_encode((int32_t) raw);
        case S_XINC_START:
        case S_Y_INC:
        case T_XINC_START:
        case T_Y_INC:
            return mach64_3d_s11_16_encode((int32_t) raw);
        case S_START:
        case T_START:
            return mach64_3d_u10_11_encode(raw);
        case RED_X_INC:
        case RED_Y_INC:
        case RED_START:
        case GREEN_X_INC:
        case GREEN_Y_INC:
        case GREEN_START:
        case BLUE_X_INC:
        case BLUE_Y_INC:
        case BLUE_START:
        case ALPHA_X_INC:
        case ALPHA_Y_INC:
        case ALPHA_START:
            return mach64_3d_s8_12_encode((int32_t) raw);
        case Z_X_INC:
        case Z_Y_INC:
        case Z_START:
            return mach64_3d_s16_12_encode((int32_t) raw);
        default:
            return raw;
    }
}

static int64_t
mach64_3d_tex_decode(mach64_3d_t *ctx, uint32_t reg)
{
    uint32_t raw = ctx->regs[reg >> 2];

    if (ctx->tex_hidden & mach64_3d_tex_hidden_bit(reg))
        return (int32_t) raw;

    switch (reg) {
        case S_XINC_START:
        case S_Y_INC:
        case T_XINC_START:
        case T_Y_INC:
            return mach64_3d_s11_16_decode(raw);
        case S_START:
        case T_START:
            return mach64_3d_u10_11_decode(raw);
        default:
            return (int32_t) raw;
    }
}

/*
 * The interpolators of a draw: color, alpha and Z by their X and Y
 * increments, S and T also by their second-order ones. An X increment is for
 * a step in DST_X_DIR and a Y increment for one in DST_Y_DIR, so a step adds
 * them whatever the direction.
 */
typedef struct mach64_3d_interp_t {
    int64_t r, g, b, a, z, s, t;
    int64_t rx, gx, bx, ax, zx;
    int64_t ry, gy, by, ay, zy;
    int64_t sxi, syi, sx2, sy2, sxy2;
    int64_t txi, tyi, tx2, ty2, txy2;
} mach64_3d_interp_t;

static void
mach64_3d_interp_load(mach64_3d_t *ctx, mach64_3d_interp_t *p)
{
    const uint32_t *regs = ctx->regs;

    p->r    = mach64_3d_s8_12_decode(regs[RED_START >> 2]);
    p->rx   = mach64_3d_s8_12_decode(regs[RED_X_INC >> 2]);
    p->ry   = mach64_3d_s8_12_decode(regs[RED_Y_INC >> 2]);
    p->g    = mach64_3d_s8_12_decode(regs[GREEN_START >> 2]);
    p->gx   = mach64_3d_s8_12_decode(regs[GREEN_X_INC >> 2]);
    p->gy   = mach64_3d_s8_12_decode(regs[GREEN_Y_INC >> 2]);
    p->b    = mach64_3d_s8_12_decode(regs[BLUE_START >> 2]);
    p->bx   = mach64_3d_s8_12_decode(regs[BLUE_X_INC >> 2]);
    p->by   = mach64_3d_s8_12_decode(regs[BLUE_Y_INC >> 2]);
    p->a    = mach64_3d_s8_12_decode(regs[ALPHA_START >> 2]);
    p->ax   = mach64_3d_s8_12_decode(regs[ALPHA_X_INC >> 2]);
    p->ay   = mach64_3d_s8_12_decode(regs[ALPHA_Y_INC >> 2]);
    p->z    = mach64_3d_s16_12_decode(regs[Z_START >> 2]);
    p->zx   = mach64_3d_s16_12_decode(regs[Z_X_INC >> 2]);
    p->zy   = mach64_3d_s16_12_decode(regs[Z_Y_INC >> 2]);
    p->s    = mach64_3d_tex_decode(ctx, S_START);
    p->sxi  = mach64_3d_tex_decode(ctx, S_XINC_START);
    p->syi  = mach64_3d_tex_decode(ctx, S_Y_INC);
    p->sx2  = mach64_3d_s10_16_decode(regs[S_X_INC2 >> 2]);
    p->sy2  = mach64_3d_s10_16_decode(regs[S_Y_INC2 >> 2]);
    p->sxy2 = mach64_3d_s10_16_decode(regs[S_XY_INC2 >> 2]);
    p->t    = mach64_3d_tex_decode(ctx, T_START);
    p->txi  = mach64_3d_tex_decode(ctx, T_XINC_START);
    p->tyi  = mach64_3d_tex_decode(ctx, T_Y_INC);
    p->tx2  = mach64_3d_s10_16_decode(regs[T_X_INC2 >> 2]);
    p->ty2  = mach64_3d_s10_16_decode(regs[T_Y_INC2 >> 2]);
    p->txy2 = mach64_3d_s10_16_decode(regs[T_XY_INC2 >> 2]);
}

static void
mach64_3d_interp_step_x(mach64_3d_interp_t *p)
{
    p->r += p->rx;
    p->g += p->gx;
    p->b += p->bx;
    p->a += p->ax;
    p->z += p->zx;
    p->s += p->sxi;
    p->sxi += p->sx2;
    p->syi += p->sxy2;
    p->t += p->txi;
    p->txi += p->tx2;
    p->tyi += p->txy2;
}

static void
mach64_3d_interp_step_y(mach64_3d_interp_t *p)
{
    p->r += p->ry;
    p->g += p->gy;
    p->b += p->by;
    p->a += p->ay;
    p->z += p->zy;
    p->s += p->syi;
    p->syi += p->sy2;
    p->sxi += p->sxy2;
    p->t += p->tyi;
    p->tyi += p->ty2;
    p->txi += p->txy2;
}

/* The value of a second-order interpolator after n steps, and the step
   that leaves it in the given direction. */
static int64_t
mach64_3d_quad_at(int64_t start, int64_t inc, int64_t inc2, int n)
{
    return start + (int64_t) n * inc + ((int64_t) n * (n - 1) / 2) * inc2;
}

static int64_t
mach64_3d_quad_step(int64_t inc, int64_t inc2, int n, int dir)
{
    return (dir > 0) ? (inc + (int64_t) n * inc2) : (-inc - (int64_t) (n - 1) * inc2);
}

/*
 * One scan line of an edge's Bresenham walker: the axial Y step adds INC,
 * then each X step adds DEC until the error turns. A shallow edge moves X
 * by more than a pixel between scan lines. zero_negative breaks the tie of
 * a zero error. The DEC of a valid edge is negative; a bad one cannot hang
 * the walker.
 */
static int
mach64_3d_edge_step(int *x, int *err, int inc, int dec, int dir, int zero_negative)
{
    int old   = *x;
    int steps = 0;

    *err += inc;
    while (((*err > 0) || ((*err == 0) && !zero_negative)) && (steps < 8192)) {
        *x += dir;
        *err += dec;
        steps++;
        if ((dec >= 0) && ((*err > 0) || ((*err == 0) && !zero_negative)))
            break;
    }
    return *x - old;
}

/*
 * The pixels of a scan line between the edges. They follow the edges' roles,
 * not left and right: the leading edge owns its boundary pixel and the
 * trailing one does not, so a left to right fill (TRAP_FILL_DIR) is
 * [lead, trail) and a right to left one (trail, lead]. Edges that cross draw
 * nothing, and a span with lead == trail is empty: ATI's HAL gives a shared
 * edge that only one side may draw the same lead and trail, and drawing it
 * shows the edges bright in additive scenes.
 */
static int
mach64_3d_trapezoid_clip_span(int lead, int trail, int fill_left_to_right, int sc_left, int sc_right, int *first, int *end)
{
    int lo;
    int hi;

    if ((sc_left > sc_right) || (lead == trail))
        return 0;

    if (fill_left_to_right) {
        if (lead > trail)
            return 0;
        lo = lead;
        hi = trail;
    } else {
        if (trail > lead)
            return 0;
        lo = trail + 1;
        hi = lead + 1;
    }

    if (lo < sc_left)
        lo = sc_left;
    if (hi > (sc_right + 1))
        hi = sc_right + 1;
    if (lo >= hi)
        return 0;

    *first = lo;
    *end   = hi;
    return 1;
}

/* The destination of a draw, with the scissors cut to it. */
typedef struct mach64_3d_dst_t {
    uint32_t base;
    int      pitch;
    int      format;
    int      bpp;
    int      left;
    int      right;
    int      top;
    int      bottom;
} mach64_3d_dst_t;

/*
 * The scissors are signed and inclusive, and an inverted rectangle draws
 * nothing. X is further cut to the pitch: past it a draw would only run
 * into the next rows, and a default scissor of 8191 pixels would turn a bad
 * draw into millions of host operations. Rows past the end of video memory
 * wrap to its start.
 */
static int
mach64_3d_dst_init(mach64_t *mach64, mach64_3d_dst_t *dst)
{
    dst->format = mach64_3d_dst_format(mach64);
    dst->bpp    = mach64_3d_bytes_per_pixel(dst->format);
    dst->base   = (mach64->dst_off_pitch & 0xfffff) << 3;
    dst->pitch  = ((mach64->dst_off_pitch >> 22) & 0x3ff) << 3;
    dst->left   = mach64_3d_sign_extend(mach64->sc_left_right, 13);
    dst->right  = mach64_3d_sign_extend(mach64->sc_left_right >> 16, 13);
    dst->top    = mach64_3d_sign_extend(mach64->sc_top_bottom, 15);
    dst->bottom = mach64_3d_sign_extend(mach64->sc_top_bottom >> 16, 15);
    if (!dst->pitch)
        return 0;

    if (dst->left < 0)
        dst->left = 0;
    if (dst->top < 0)
        dst->top = 0;
    if (dst->right >= dst->pitch)
        dst->right = dst->pitch - 1;
    return 1;
}

/* Tells the engine timing model the pixels and scan lines a draw walked and
   what each pixel takes from memory. */
static void
mach64_3d_report_work(mach64_3d_t *ctx, uint32_t pixels, uint32_t rows)
{
    mach64_t        *mach64 = ctx->mach64;
    uint32_t         cntl   = ctx->regs[SCALE_3D_CNTL >> 2];
    uint32_t         z_cntl = ctx->regs[Z_CNTL >> 2];
    int              fcn    = (cntl >> SCALE_3D_FCN_SHIFT) & 3;
    int              z_test = (z_cntl >> Z_TEST_SHIFT) & 7;
    int              blend  = (cntl >> TEX_BLEND_FCN) & 3;
    int              z_on   = (z_cntl & Z_EN) && (((ctx->regs[Z_OFF_PITCH >> 2] >> 22) & 0x3ff) != 0);
    mach64_3d_work_t work   = { 0 };

    work.pixels   = pixels;
    work.rows     = rows;
    work.dst_bits = mach64_3d_bytes_per_pixel(mach64_3d_dst_format(mach64)) * 8;
    if (fcn == FCN_TEXTURE) {
        work.tex_bits = mach64_3d_bytes_per_pixel(mach64_3d_src_format(mach64)) * 8;
        if (cntl & MIP_MAP_DISABLE)
            work.texels = ((cntl & BILINEAR_TEX_EN) || (blend >= 2)) ? 4 : 1;
        else
            work.texels = (blend >= 2) ? 4 : ((blend == 1) ? 2 : ((cntl & BILINEAR_TEX_EN) ? 4 : 1));
    }
    work.z_read   = z_on && (z_test != 0) && (z_test != 7);
    work.z_write  = z_on && !!(z_cntl & Z_MASK);
    work.dst_read = (((cntl >> ALPHA_FOG_SHIFT) & 3) == ALPHA_FOG_BLEND) || mach64_3d_destination_compare_enabled(mach64->clr_cmp_cntl);
    mach64_timing_3d(mach64, &work);
}

/* What every pixel of a draw shares. */
typedef struct mach64_3d_pixel_t {
    uint32_t            cntl;
    uint32_t            z_cntl;
    int                 textured;
    int                 alpha_fog;
    int                 z_enabled;
    int                 z_write;
    int                 dst_compare;
    uint32_t            dst_compare_mask;
    rgba_t              fog;
    mach64_3d_texture_t texture;
} mach64_3d_pixel_t;

static void
mach64_3d_pixel_init(mach64_3d_t *ctx, const mach64_3d_dst_t *dst, int z_pitch, mach64_3d_pixel_t *px)
{
    mach64_t *mach64 = ctx->mach64;

    px->cntl             = ctx->regs[SCALE_3D_CNTL >> 2];
    px->z_cntl           = ctx->regs[Z_CNTL >> 2];
    px->textured         = (((px->cntl >> SCALE_3D_FCN_SHIFT) & 3) == FCN_TEXTURE);
    px->alpha_fog        = (px->cntl >> ALPHA_FOG_SHIFT) & 3;
    px->z_enabled        = (px->z_cntl & Z_EN) && z_pitch;
    px->z_write          = px->z_enabled && (px->z_cntl & Z_MASK);
    px->dst_compare      = mach64_3d_destination_compare_enabled(mach64->clr_cmp_cntl);
    px->dst_compare_mask = (dst->bpp == 1) ? 0xff : ((dst->bpp == 2) ? 0xffff : 0xffffffff);
    px->fog.r            = (mach64->dp_frgd_clr >> 16) & 0xff;
    px->fog.g            = (mach64->dp_frgd_clr >> 8) & 0xff;
    px->fog.b            = mach64->dp_frgd_clr & 0xff;
    px->fog.a            = 255;
    if (px->textured)
        mach64_3d_texture_init(ctx, mach64_3d_src_format(mach64), !!(px->cntl & BILINEAR_TEX_EN), &px->texture);
}

/*
 * The source of a pixel: the shading, or the texel lit by it, then fog
 * (ALPHA_FOG_EN 2) blends towards DP_FRGD_CLR by the shading's alpha.
 * Returns 0 when the texture draws nothing there.
 */
static int
mach64_3d_pixel_source(mach64_3d_t *ctx, const mach64_3d_pixel_t *px, rgba_t shade, int64_t s, int64_t t, int64_t dsdx,
                       int64_t dtdx, int64_t dsdy, int64_t dtdy, rgba_t *src)
{
    *src = shade;
    if (px->textured) {
        int    draw;
        int    key_inhibit;
        rgba_t texel = mach64_3d_sample_texture(ctx, &px->texture, px->cntl, s, t, dsdx, dtdx, dsdy, dtdy, &draw, &key_inhibit);

        if (!draw || key_inhibit || !mach64_3d_texture_alpha(px->cntl, &texel, shade.a))
            return 0;
        *src = mach64_3d_texture_light(px->cntl, texel, shade);
    }
    if (px->alpha_fog == ALPHA_FOG_FOG)
        *src = mach64_3d_lerp(px->fog, *src, shade.a);
    return 1;
}

/*
 * A trapezoid (DST_BRES_LNTH with DRAW_TRAP): the leading edge is DST_Y_X's
 * walker with the 2D engine's error terms, the trailing edge its own one
 * from TRAIL_BRES_*. A triangle is two trapezoids sharing the leading edge.
 */
static void
mach64_3d_draw_trapezoid(mach64_3d_t *ctx, uint32_t cmd)
{
    mach64_t          *mach64 = ctx->mach64;
    uint32_t           cntl   = ctx->regs[SCALE_3D_CNTL >> 2];
    int                fcn    = (cntl >> SCALE_3D_FCN_SHIFT) & 3;
    int                len    = cmd & 0x7fff;
    int                lead;
    int                trail;
    int                y;
    int                lead_err;
    int                trail_err;
    int                lead_inc   = (int32_t) mach64->dst_bres_inc;
    int                lead_dec   = (int32_t) mach64->dst_bres_dec;
    int                trail_inc  = (int32_t) ctx->regs[TRAIL_BRES_INC >> 2];
    int                trail_dec  = (int32_t) ctx->regs[TRAIL_BRES_DEC >> 2];
    int                y_dir      = (mach64->dst_cntl & DST_Y_DIR) ? 1 : -1;
    int                lead_dir   = (mach64->dst_cntl & DST_X_DIR) ? 1 : -1;
    int                trail_dir  = (mach64->dst_cntl & TRAIL_X_DIR) ? 1 : -1;
    int                fill_l2r   = !!(mach64->dst_cntl & TRAP_FILL_DIR);
    int                lead_zneg  = !!(mach64->dst_cntl & DST_BRES_SIGN);
    int                trail_zneg = !!(mach64->dst_cntl & TRAIL_BRES_SIGN);
    uint32_t           z_off_pitch;
    uint32_t           z_base;
    int                z_pitch;
    int                dp_constant;
    int                initial_steps;
    uint32_t           walked = 0;
    mach64_3d_dst_t    dst;
    mach64_3d_pixel_t  px;
    mach64_3d_interp_t p;

    if (!len || !fcn || !mach64_3d_bytes_per_pixel(mach64_3d_dst_format(mach64)))
        return;
    if ((fcn == FCN_TEXTURE) && !mach64_3d_bytes_per_pixel(mach64_3d_src_format(mach64)))
        return;

    lead = mach64_3d_sign_extend(mach64->dst_y_x >> 16, 13);
    y    = mach64_3d_sign_extend(mach64->dst_y_x, 15);

    /* TRAIL_X loads with bit 31 of the command, else it is where the last
       trapezoid left it (VT/RAGE RRG 4-46). */
    if ((cmd & LINE_DIS) || !ctx->trail_valid) {
        ctx->trail_x     = mach64_3d_sign_extend(cmd >> 16, 13);
        ctx->trail_valid = 1;
    }
    trail     = ctx->trail_x;
    lead_err  = (int32_t) mach64->dst_bres_err;
    trail_err = (int32_t) ctx->regs[TRAIL_BRES_ERR >> 2];

    if (!mach64_3d_dst_init(mach64, &dst))
        return;
    z_off_pitch = ctx->regs[Z_OFF_PITCH >> 2];
    z_base      = (z_off_pitch & 0xfffff) << 3;
    z_pitch     = ((z_off_pitch >> 22) & 0x3ff) << 3;
    mach64_3d_pixel_init(ctx, &dst, z_pitch, &px);
    mach64_3d_interp_load(ctx, &p);

    /* DP_MONO_SRC always one with DP_FRGD_SRC a constant color: the data
       path takes that color instead of the 3D one. */
    dp_constant = (((mach64->dp_src >> 16) & 3) == MONO_SRC_1) && ((((mach64->dp_src >> 8) & 7) == SRC_BG) || (((mach64->dp_src >> 8) & 7) == SRC_FG));

    /*
     * ATI's HAL leaves a positive starting error for the walkers to take up
     * (ATI3DCIF 4.03.2510 takes these X steps in software for older chips):
     * take them before the first span, without INC or a Y step. START may
     * be a point outside the triangle; the field wraps on the way to the
     * edge rather than clamping.
     */
    initial_steps = (lead_dec < 0) ? mach64_3d_edge_step(&lead, &lead_err, 0, lead_dec, lead_dir, lead_zneg) : 0;
    if (initial_steps < 0)
        initial_steps = -initial_steps;
    if (trail_dec < 0)
        mach64_3d_edge_step(&trail, &trail_err, 0, trail_dec, trail_dir, trail_zneg);
    if (initial_steps) {
        p.r = mach64_3d_s8_12_decode((uint32_t) (p.r + p.rx * initial_steps));
        p.g = mach64_3d_s8_12_decode((uint32_t) (p.g + p.gx * initial_steps));
        p.b = mach64_3d_s8_12_decode((uint32_t) (p.b + p.bx * initial_steps));
        p.a = mach64_3d_s8_12_decode((uint32_t) (p.a + p.ax * initial_steps));
        p.z = mach64_3d_s16_12_decode((uint32_t) (p.z + p.zx * initial_steps));
        p.s = mach64_3d_quad_at(p.s, p.sxi, p.sx2, initial_steps);
        p.sxi += p.sx2 * initial_steps;
        p.syi += p.sxy2 * initial_steps;
        p.t = mach64_3d_quad_at(p.t, p.txi, p.tx2, initial_steps);
        p.txi += p.tx2 * initial_steps;
        p.tyi += p.txy2 * initial_steps;
    }

    for (int row = 0; row < len; row++) {
        int first;
        int end;
        int lead_steps;

        if ((y >= dst.top) && (y <= dst.bottom) && mach64_3d_trapezoid_clip_span(lead, trail, fill_l2r, dst.left, dst.right, &first, &end)) {
            /* The interpolators at the first pixel, which is n steps along
               DST_X_DIR from the leading edge. */
            int      n      = (first - lead) * lead_dir;
            int64_t  r      = p.r + p.rx * n;
            int64_t  g      = p.g + p.gx * n;
            int64_t  b      = p.b + p.bx * n;
            int64_t  a      = p.a + p.ax * n;
            int64_t  z      = p.z + p.zx * n;
            int64_t  s      = 0;
            int64_t  t      = 0;
            int64_t  dsdx   = 0;
            int64_t  dtdx   = 0;
            int64_t  dsdy   = 0;
            int64_t  dtdy   = 0;
            uint32_t addr   = dst.base + (y * dst.pitch + first) * dst.bpp;
            uint32_t z_addr = px.z_enabled ? (z_base + (y * z_pitch + first) * 2) : 0;

            walked += end - first;
            if (px.textured) {
                s    = mach64_3d_quad_at(p.s, p.sxi, p.sx2, n);
                t    = mach64_3d_quad_at(p.t, p.txi, p.tx2, n);
                dsdx = mach64_3d_quad_step(p.sxi, p.sx2, n, lead_dir);
                dtdx = mach64_3d_quad_step(p.txi, p.tx2, n, lead_dir);
                dsdy = p.syi + n * p.sxy2;
                dtdy = p.tyi + n * p.txy2;
            }

            for (int x = first; x < end; x++) {
                rgba_t   shade = { mach64_3d_s8_12_color(r), mach64_3d_s8_12_color(g), mach64_3d_s8_12_color(b),
                                   mach64_3d_s8_12_color(a) };
                rgba_t   src;
                uint16_t depth = 0;
                int      draw  = mach64_3d_pixel_source(ctx, &px, shade, s, t, dsdx, dtdx, dsdy, dtdy, &src);

                if (draw && px.z_enabled) {
                    depth = mach64_3d_s16_12_depth(z);
                    draw  = mach64_3d_z_test(depth, mach64_3d_vram_read16(mach64, z_addr), (px.z_cntl >> Z_TEST_SHIFT) & 7);
                }
                if (draw) {
                    uint32_t raw        = 0;
                    int      raw_source = 0;

                    if (px.alpha_fog == ALPHA_FOG_BLEND)
                        src = mach64_3d_blend(px.cntl, src, mach64_3d_read_dst(ctx, addr, dst.format));
                    if (dp_constant) {
                        raw        = mach64_3d_dp_select_source(mach64->dp_src, mach64->dp_bkgd_clr, mach64->dp_frgd_clr,
                                                                mach64_3d_pack(ctx, dst.format, src, x, y));
                        src        = mach64_3d_unpack(ctx, dst.format, raw, 0);
                        raw_source = 1;
                    }
                    if (px.dst_compare &&
                        mach64_3d_destination_compare_inhibits(mach64->clr_cmp_cntl, mach64->clr_cmp_clr, mach64->clr_cmp_mask,
                                                               mach64_3d_vram_read(mach64, addr, dst.bpp), px.dst_compare_mask))
                        draw = 0;
                    if (draw) {
                        if (raw_source)
                            mach64_3d_write_dst_raw(ctx, addr, dst.format, raw);
                        else
                            mach64_3d_write_dst(ctx, addr, dst.format, src);
                        if (px.z_write)
                            mach64_3d_vram_write(mach64, z_addr, depth, 2);
                    }
                }

                addr += dst.bpp;
                if (px.z_enabled)
                    z_addr += 2;
                r += p.rx * lead_dir;
                g += p.gx * lead_dir;
                b += p.bx * lead_dir;
                a += p.ax * lead_dir;
                z += p.zx * lead_dir;
                if (px.textured) {
                    s += dsdx;
                    t += dtdx;
                    dsdx += p.sx2;
                    dtdx += p.tx2;
                    dsdy += p.sxy2 * lead_dir;
                    dtdy += p.txy2 * lead_dir;
                }
            }
        }

        /* Down a scan line: one Y step of the interpolators, then an X
           step for each X step of the leading edge. */
        lead_steps = mach64_3d_edge_step(&lead, &lead_err, lead_inc, lead_dec, lead_dir, lead_zneg);
        if (lead_steps < 0)
            lead_steps = -lead_steps;
        mach64_3d_edge_step(&trail, &trail_err, trail_inc, trail_dec, trail_dir, trail_zneg);
        mach64_3d_interp_step_y(&p);
        for (int step = 0; step < lead_steps; step++)
            mach64_3d_interp_step_x(&p);
        y += y_dir;
    }

    /*
     * The edges and interpolators are live registers, like a line's DST_Y_X:
     * the HAL draws a triangle's second trapezoid with a new TRAIL_X and
     * DST_Y_X left where the first one ended.
     */
    mach64->dst_y_x                = ((lead & 0x1fff) << 16) | (y & 0x7fff);
    mach64->dst_bres_err           = lead_err;
    ctx->trail_x                   = trail;
    ctx->regs[TRAIL_BRES_ERR >> 2] = trail_err;
    ctx->regs[RED_START >> 2]      = mach64_3d_s8_12_encode(p.r);
    ctx->regs[GREEN_START >> 2]    = mach64_3d_s8_12_encode(p.g);
    ctx->regs[BLUE_START >> 2]     = mach64_3d_s8_12_encode(p.b);
    ctx->regs[ALPHA_START >> 2]    = mach64_3d_s8_12_encode(p.a);
    ctx->regs[Z_START >> 2]        = mach64_3d_s16_12_encode(p.z);
    ctx->regs[S_START >> 2]        = p.s;
    ctx->regs[S_Y_INC >> 2]        = p.syi;
    ctx->regs[S_XINC_START >> 2]   = p.sxi;
    ctx->regs[T_START >> 2]        = p.t;
    ctx->regs[T_Y_INC >> 2]        = p.tyi;
    ctx->regs[T_XINC_START >> 2]   = p.txi;
    ctx->tex_hidden                = TEX_HIDDEN_ALL;

    if (mach64->timing)
        mach64_3d_report_work(ctx, walked, len);
}

/*
 * 3D lines. DP_SRC's foreground source 5 is the scaler or 3D data: an
 * ordinary Bresenham line, colored by the interpolators (SCALE_3D_FCN 3) or
 * the texture (2). The Windows 95 HAL draws wireframe edges and points so.
 * Lines that are not shading or texturing, polygon outlines and the scaler
 * stay with the 2D engine.
 */
static int
mach64_3d_is_line(mach64_3d_t *ctx, uint32_t cmd)
{
    mach64_t *mach64 = ctx->mach64;
    unsigned  fcn    = (ctx->regs[SCALE_3D_CNTL >> 2] >> SCALE_3D_FCN_SHIFT) & 3;

    if ((cmd & DRAW_TRAP) || (mach64->dst_cntl & DST_POLYGON_EN))
        return 0;
    return ((fcn == FCN_TEXTURE) || (fcn == FCN_SHADE)) && (((mach64->dp_src >> 16) & 3) == MONO_SRC_1) && (((mach64->dp_src >> 8) & 7) == SRC_3D);
}

/*
 * The tie of a zero error: DST_BRES_SIGN, or with BRES_SIGN_AUTO positive
 * for an X-major line going up the screen (DST_Y_DIR 0) or a Y-major one
 * going left (DST_X_DIR 0), negative the other way. Connected line strips
 * join on it.
 */
static int
mach64_3d_line_zero_negative(const mach64_t *mach64, int y_major)
{
    if (!(mach64->dst_cntl & BRES_SIGN_AUTO))
        return !!(mach64->dst_cntl & DST_BRES_SIGN);
    if (y_major)
        return !!(mach64->dst_cntl & DST_X_DIR);
    return !!(mach64->dst_cntl & DST_Y_DIR);
}

static void
mach64_3d_draw_line(mach64_3d_t *ctx, uint32_t cmd)
{
    mach64_t          *mach64  = ctx->mach64;
    int                len     = cmd & 0x7fff;
    int                x       = mach64_3d_sign_extend(mach64->dst_y_x >> 16, 13);
    int                y       = mach64_3d_sign_extend(mach64->dst_y_x, 15);
    int                x_dir   = (mach64->dst_cntl & DST_X_DIR) ? 1 : -1;
    int                y_dir   = (mach64->dst_cntl & DST_Y_DIR) ? 1 : -1;
    int                y_major = !!(mach64->dst_cntl & DST_Y_MAJOR);
    int                zneg    = mach64_3d_line_zero_negative(mach64, y_major);
    int                err     = mach64_3d_sign_extend(mach64->dst_bres_err, 18);
    int                inc     = mach64_3d_sign_extend(mach64->dst_bres_inc, 18);
    int                dec     = mach64_3d_sign_extend(mach64->dst_bres_dec, 18);
    uint32_t           z_off_pitch;
    uint32_t           z_base;
    int                z_pitch;
    mach64_3d_dst_t    dst;
    mach64_3d_pixel_t  px;
    mach64_3d_interp_t p;

    /* LINE_DIS loads the line without drawing it. */
    if (cmd & LINE_DIS)
        return;

    /* Without a pixel type the line draws nothing, as for a trapezoid. */
    if (!len || !mach64_3d_bytes_per_pixel(mach64_3d_dst_format(mach64)))
        return;
    if ((((ctx->regs[SCALE_3D_CNTL >> 2] >> SCALE_3D_FCN_SHIFT) & 3) == FCN_TEXTURE) && !mach64_3d_bytes_per_pixel(mach64_3d_src_format(mach64)))
        return;
    if (!mach64_3d_dst_init(mach64, &dst))
        return;

    z_off_pitch = ctx->regs[Z_OFF_PITCH >> 2];
    z_base      = (z_off_pitch & 0xfffff) << 3;
    z_pitch     = ((z_off_pitch >> 22) & 0x3ff) << 3;
    mach64_3d_pixel_init(ctx, &dst, z_pitch, &px);
    mach64_3d_interp_load(ctx, &p);

    for (int i = 0; i < len; i++) {
        /* DST_LAST_PEL draws the last pixel. */
        int draw = ((i < (len - 1)) || (mach64->dst_cntl & DST_LAST_PEL)) && (x >= dst.left) && (x <= dst.right) && (y >= dst.top) && (y <= dst.bottom);

        if (draw) {
            uint32_t addr   = dst.base + (y * dst.pitch + x) * dst.bpp;
            uint32_t z_addr = 0;
            uint16_t depth  = 0;
            rgba_t   shade  = { mach64_3d_s8_12_color(p.r), mach64_3d_s8_12_color(p.g), mach64_3d_s8_12_color(p.b),
                                mach64_3d_s8_12_color(p.a) };
            rgba_t   src;

            draw = mach64_3d_pixel_source(ctx, &px, shade, p.s, p.t, p.sxi, p.txi, p.syi, p.tyi, &src);
            if (draw && px.z_enabled) {
                if ((x < 0) || (x >= z_pitch) || (y < 0))
                    draw = 0;
                else {
                    z_addr = z_base + (y * z_pitch + x) * 2;
                    depth  = mach64_3d_s16_12_depth(p.z);
                    draw   = mach64_3d_z_test(depth, mach64_3d_vram_read16(mach64, z_addr), (px.z_cntl >> Z_TEST_SHIFT) & 7);
                }
            }
            if (draw) {
                if (px.alpha_fog == ALPHA_FOG_BLEND)
                    src = mach64_3d_blend(px.cntl, src, mach64_3d_read_dst(ctx, addr, dst.format));
                if (px.dst_compare &&
                    mach64_3d_destination_compare_inhibits(mach64->clr_cmp_cntl, mach64->clr_cmp_clr, mach64->clr_cmp_mask,
                                                           mach64_3d_vram_read(mach64, addr, dst.bpp), px.dst_compare_mask))
                    draw = 0;
            }
            if (draw) {
                mach64_3d_write_dst(ctx, addr, dst.format, src);
                if (px.z_write)
                    mach64_3d_vram_write(mach64, z_addr, depth, 2);
            }
        }

        if (i == (len - 1))
            break;

        /* The HAL programs the Bresenham terms in accumulator form: INC on
           every major step, DEC as well when the minor axis moves. */
        if (y_major) {
            y += y_dir;
            mach64_3d_interp_step_y(&p);
        } else {
            x += x_dir;
            mach64_3d_interp_step_x(&p);
        }
        err += inc;
        if ((err > 0) || ((err == 0) && !zneg)) {
            err += dec;
            if (y_major) {
                x += x_dir;
                mach64_3d_interp_step_x(&p);
            } else {
                y += y_dir;
                mach64_3d_interp_step_y(&p);
            }
        }
    }

    /* DST_X/Y end on the last pixel of the line, drawn or not: Windows
       chains connected lines from there. */
    mach64->dst_y_x = ((x & 0x1fff) << 16) | (y & 0x7fff);

    if (mach64->timing)
        mach64_3d_report_work(ctx, len, 0);
}

/*
 * The front-end scaler (SCALE_3D_FCN 1). The X and Y accumulators keep a
 * 16-bit fraction as stored, the low four bits reserved. ATI's later Rage
 * Pro and derivatives guide describes the Rage II/II+ scaler's filter as 2
 * taps with 4-bit coefficients, where the 1996 register guide says 5 bits;
 * the later book is taken here, the top four fraction bits.
 */
static int
mach64_scaler_accum_pixel(int32_t accumulator)
{
    int pixel = accumulator / 0x10000;

    if ((accumulator < 0) && (accumulator % 0x10000))
        pixel--;
    return pixel;
}

static unsigned
mach64_scaler_accum_fraction4(int32_t accumulator)
{
    return ((uint32_t) accumulator >> 12) & 15;
}

static uint8_t
mach64_scaler_lerp4(uint8_t first, uint8_t second, unsigned coefficient)
{
    coefficient &= 15;
    return (first * (16 - coefficient) + second * coefficient + 8) >> 4;
}

/* Signed U and V (APPLE_YUV_MODE) are interpolated about zero. */
static int
mach64_scaler_lerp4_signed(int first, int second, unsigned coefficient)
{
    int value;

    coefficient &= 15;
    value = first * (int) (16 - coefficient) + second * (int) coefficient;
    if (value >= 0)
        return (value + 8) / 16;
    return -((-value + 8) / 16);
}

/* DP_MIX's functions; 17h averages source and destination. */
static uint32_t
mach64_scaler_mix(uint32_t src, uint32_t dst, unsigned function)
{
    switch (function & 0x1f) {
        case 0x00:
            return ~dst;
        case 0x01:
            return 0;
        case 0x02:
            return 0xffffffff;
        case 0x03:
            return dst;
        case 0x04:
            return ~src;
        case 0x05:
            return src ^ dst;
        case 0x06:
            return ~(src ^ dst);
        case 0x07:
            return src;
        case 0x08:
            return ~(src & dst);
        case 0x09:
            return ~src | dst;
        case 0x0a:
            return src | ~dst;
        case 0x0b:
            return src | dst;
        case 0x0c:
            return src & dst;
        case 0x0d:
            return src & ~dst;
        case 0x0e:
            return ~src & dst;
        case 0x0f:
            return ~(src | dst);
        case 0x17:
            return (src + dst) >> 1;
        default:
            return src;
    }
}

/* YUV 422 takes 2 bytes a pixel in the scaler, read in 4-byte pairs. */
static int
mach64_scaler_source_bpp(int format)
{
    return (format == 11) ? 2 : mach64_3d_bytes_per_pixel(format);
}

static int
mach64_scaler_destination_supported(int format)
{
    switch (format) {
        case 2:
        case 3:
        case 4:
        case 6:
        case 7:
        case 8:
        case 15:
            return 1;
        default:
            return 0;
    }
}

static rgba_t
mach64_scaler_read_source(mach64_3d_t *ctx, int format, uint32_t base, uint32_t pitch, int x, int y)
{
    int    bpp   = mach64_scaler_source_bpp(format);
    rgba_t black = { 0, 0, 0, 255 };

    if ((bpp != 1) && (bpp != 2) && (bpp != 4))
        return black;
    return mach64_3d_unpack(ctx, format, mach64_3d_vram_read(ctx->mach64, base + (y * pitch + x) * bpp, bpp), 1);
}

static uint32_t
mach64_scaler_yuyv_pair(mach64_t *mach64, uint32_t base, uint32_t pitch, int pair_x, int y)
{
    return mach64_3d_vram_read32(mach64, base + (y * pitch + pair_x * 2) * 2);
}

static uint8_t
mach64_scaler_yuyv_luma(mach64_t *mach64, uint32_t base, uint32_t pitch, int x, int y)
{
    return mach64_yuyv_y(mach64_scaler_yuyv_pair(mach64, base, pitch, x >> 1, y), x);
}

static void
mach64_scaler_yuyv_chroma(mach64_t *mach64, uint32_t base, uint32_t pitch, int pair_x, int y, int signed_uv, int *u, int *v)
{
    uint32_t raw = mach64_scaler_yuyv_pair(mach64, base, pitch, pair_x, y);

    *u = mach64_yuv_chroma(mach64_yuyv_u(raw), signed_uv);
    *v = mach64_yuv_chroma(mach64_yuyv_v(raw), signed_uv);
}

/*
 * A YUV 422 sample: Y by the X accumulator, U and V by their own one
 * (SCALE_XUV_INC, SCALE_UV_HACC), each filtered over two lines and two
 * samples unless SCALE_PIX_REP replicates the nearest.
 */
static rgba_t
mach64_scaler_sample_yuyv(mach64_3d_t *ctx, uint32_t base, uint32_t pitch, int x, int x_next, int uv_x, int uv_x_next, int y,
                          int y_next, unsigned h_fraction, unsigned uv_fraction, unsigned v_fraction, int replicate)
{
    mach64_t *mach64    = ctx->mach64;
    int       signed_uv = !!(ctx->regs[SCALE_3D_CNTL >> 2] & APPLE_YUV_MODE);
    int       u00;
    int       v00;
    int       luma;
    int       u;
    int       v;

    mach64_scaler_yuyv_chroma(mach64, base, pitch, uv_x, y, signed_uv, &u00, &v00);
    if (replicate) {
        luma = mach64_scaler_yuyv_luma(mach64, base, pitch, x, y);
        u    = u00;
        v    = v00;
    } else {
        int y00 = mach64_scaler_yuyv_luma(mach64, base, pitch, x, y);
        int y01 = mach64_scaler_yuyv_luma(mach64, base, pitch, x_next, y);
        int y10 = mach64_scaler_yuyv_luma(mach64, base, pitch, x, y_next);
        int y11 = mach64_scaler_yuyv_luma(mach64, base, pitch, x_next, y_next);
        int u01;
        int v01;
        int u10;
        int v10;
        int u11;
        int v11;

        mach64_scaler_yuyv_chroma(mach64, base, pitch, uv_x_next, y, signed_uv, &u01, &v01);
        mach64_scaler_yuyv_chroma(mach64, base, pitch, uv_x, y_next, signed_uv, &u10, &v10);
        mach64_scaler_yuyv_chroma(mach64, base, pitch, uv_x_next, y_next, signed_uv, &u11, &v11);
        luma = mach64_scaler_lerp4(mach64_scaler_lerp4(y00, y10, v_fraction), mach64_scaler_lerp4(y01, y11, v_fraction), h_fraction);
        u    = mach64_scaler_lerp4_signed(mach64_scaler_lerp4_signed(u00, u10, v_fraction),
                                          mach64_scaler_lerp4_signed(u01, u11, v_fraction), uv_fraction);
        v    = mach64_scaler_lerp4_signed(mach64_scaler_lerp4_signed(v00, v10, v_fraction),
                                          mach64_scaler_lerp4_signed(v01, v11, v_fraction), uv_fraction);
    }
    return mach64_3d_yuv_rgba(luma, u, v, 255);
}

/* The source color key (CLR_CMP_SRC 2) on one source pixel. */
static int
mach64_scaler_source_key_inhibits(mach64_3d_t *ctx, int format, uint32_t base, uint32_t pitch, int x, int y)
{
    mach64_t *mach64 = ctx->mach64;
    rgba_t    color;

    if (((mach64->clr_cmp_cntl >> 24) & 3) != 2)
        return 0;

    if (format == 2)
        return mach64_3d_texel_key_match_index(mach64->clr_cmp_cntl, mach64->clr_cmp_clr, mach64->clr_cmp_mask,
                                               mach64_3d_texture_palette_texel_index(mach64->dp_pix_width,
                                                                                     mach64_3d_vram_read8(mach64, base + y * pitch + x)));

    if (format == 11) {
        uint32_t raw       = mach64_scaler_yuyv_pair(mach64, base, pitch, x >> 1, y);
        int      signed_uv = !!(ctx->regs[SCALE_3D_CNTL >> 2] & APPLE_YUV_MODE);

        color = mach64_3d_yuv_rgba(mach64_yuyv_y(raw, x), mach64_yuv_chroma(mach64_yuyv_u(raw), signed_uv),
                                   mach64_yuv_chroma(mach64_yuyv_v(raw), signed_uv), 255);
    } else
        color = mach64_scaler_read_source(ctx, format, base, pitch, x, y);

    return mach64_3d_texel_key_match(mach64->clr_cmp_cntl, mach64->clr_cmp_clr, mach64->clr_cmp_mask, mach64_3d_clamp8(color.r),
                                     mach64_3d_clamp8(color.g), mach64_3d_clamp8(color.b));
}

/* As for a texture: any of the filtered pixels, or with NEAREST_TEX_VIS
   only the nearest one, can hide a destination pixel. */
static int
mach64_scaler_source_inhibits(mach64_3d_t *ctx, int format, uint32_t base, uint32_t pitch, int x, int x_next, int y, int y_next,
                              unsigned h_fraction, unsigned v_fraction, int replicate)
{
    int p00;
    int p10;
    int p01;
    int p11;

    if (((ctx->mach64->clr_cmp_cntl >> 24) & 3) != 2)
        return 0;

    p00 = mach64_scaler_source_key_inhibits(ctx, format, base, pitch, x, y);
    if (replicate)
        return p00;
    p10 = mach64_scaler_source_key_inhibits(ctx, format, base, pitch, x_next, y);
    p01 = mach64_scaler_source_key_inhibits(ctx, format, base, pitch, x, y_next);
    p11 = mach64_scaler_source_key_inhibits(ctx, format, base, pitch, x_next, y_next);

    if (ctx->regs[SCALE_3D_CNTL >> 2] & NEAREST_TEX_VIS) {
        if (v_fraction >= 8)
            return (h_fraction >= 8) ? p11 : p01;
        return (h_fraction >= 8) ? p10 : p00;
    }
    return p00 || p10 || p01 || p11;
}

static rgba_t
mach64_scaler_lerp_rgba(rgba_t first, rgba_t second, unsigned coefficient)
{
    rgba_t result;

    result.r = mach64_scaler_lerp4(mach64_3d_clamp8(first.r), mach64_3d_clamp8(second.r), coefficient);
    result.g = mach64_scaler_lerp4(mach64_3d_clamp8(first.g), mach64_3d_clamp8(second.g), coefficient);
    result.b = mach64_scaler_lerp4(mach64_3d_clamp8(first.b), mach64_3d_clamp8(second.b), coefficient);
    result.a = mach64_scaler_lerp4(mach64_3d_clamp8(first.a), mach64_3d_clamp8(second.a), coefficient);
    return result;
}

/*
 * One destination pixel: blended when ALPHA_FOG_EN says so, compared,
 * mixed by DP_MIX's foreground function and written through WRITE_MASK.
 * CLR_CMP_SRC 1 compares the scaled pixel itself; 3 is reserved.
 */
static void
mach64_scaler_write_pixel(mach64_3d_t *ctx, uint32_t addr, int format, int x, int y, rgba_t src, int source_inhibits)
{
    mach64_t *mach64     = ctx->mach64;
    uint32_t  cntl       = ctx->regs[SCALE_3D_CNTL >> 2];
    int       bpp        = mach64_3d_bytes_per_pixel(format);
    uint32_t  pixel_mask = (bpp == 1) ? 0xff : ((bpp == 2) ? 0xffff : 0xffffffff);
    uint32_t  dst        = mach64_3d_vram_read(mach64, addr, bpp);
    uint32_t  packed;
    uint32_t  result;
    int       inhibits;

    if (((cntl >> ALPHA_FOG_SHIFT) & 3) == ALPHA_FOG_BLEND)
        src = mach64_3d_blend(cntl, src, mach64_3d_read_dst(ctx, addr, format));

    if (format == 2)
        packed = ((mach64_3d_clamp8(src.r) >> 5) << 5) | ((mach64_3d_clamp8(src.g) >> 5) << 2) | (mach64_3d_clamp8(src.b) >> 6);
    else
        packed = mach64_3d_pack(ctx, format, src, x, y);
    packed &= pixel_mask;

    switch ((mach64->clr_cmp_cntl >> 24) & 3) {
        case 0:
            inhibits = mach64_3d_color_compare_inhibits(mach64->clr_cmp_cntl, mach64->clr_cmp_clr, mach64->clr_cmp_mask, dst);
            break;
        case 1:
            inhibits = mach64_3d_color_compare_inhibits(mach64->clr_cmp_cntl, mach64->clr_cmp_clr, mach64->clr_cmp_mask, packed);
            break;
        case 2:
            inhibits = source_inhibits;
            break;
        default:
            inhibits = 0;
            break;
    }
    if (inhibits || ((bpp != 1) && (bpp != 2) && (bpp != 4)))
        return;

    result = mach64_scaler_mix(packed, dst, (mach64->dp_mix >> 16) & 0x1f) & pixel_mask;
    result = (result & mach64->write_mask) | (dst & ~mach64->write_mask);
    mach64_3d_vram_write(mach64, addr, result & pixel_mask, bpp);
}

/*
 * A scaled rectangle, started like a 2D one by the destination size: the
 * source is SCALE_Y_OFF, SCALE_WIDTH by SCALE_HEIGHT in SCALE_Y_PITCH, read
 * by the X and Y accumulators from SCALE_HACC and SCALE_VACC; a pixel past
 * the source's edge repeats the edge.
 */
static void
mach64_scaler_draw(mach64_3d_t *ctx)
{
    mach64_t       *mach64     = ctx->mach64;
    const uint32_t *regs       = ctx->regs;
    int             replicate  = !!(regs[SCALE_3D_CNTL >> 2] & SCALE_PIX_REP);
    uint32_t        src_base   = regs[SCALE_Y_OFF >> 2] & ~7;
    int             src_width  = regs[SCALE_WIDTH >> 2] & 0x1fff;
    int             src_height = regs[SCALE_HEIGHT >> 2] & 0x1fff;
    uint32_t        src_pitch  = regs[SCALE_Y_PITCH >> 2] & 0x1fff;
    uint32_t        x_inc      = regs[SCALE_X_INC >> 2] & 0x001ffff0;
    uint32_t        xuv_inc    = regs[SCALE_XUV_INC >> 2] & 0x001ffff0;
    uint32_t        y_inc      = regs[SCALE_Y_INC >> 2] & 0x001ffff0;
    int32_t         h_start    = mach64_3d_sign_extend(regs[SCALE_HACC >> 2], 21);
    int32_t         uv_start   = mach64_3d_sign_extend(regs[SCALE_UV_HACC >> 2], 21);
    int32_t         v_start    = mach64_3d_sign_extend(regs[SCALE_VACC >> 2], 21);
    int             src_format = mach64_3d_src_format(mach64);
    int             dst_format = mach64_3d_dst_format(mach64);
    int             dst_bpp    = mach64_3d_bytes_per_pixel(dst_format);
    int             dst_pitch  = ((mach64->dst_off_pitch >> 22) & 0x3ff) << 3;
    uint32_t        dst_base   = (mach64->dst_off_pitch & 0xfffff) << 3;
    int             dst_x      = mach64_3d_sign_extend(mach64->dst_y_x >> 16, 13);
    int             dst_y      = mach64_3d_sign_extend(mach64->dst_y_x, 15);
    int             dst_width  = (mach64->dst_height_width >> 16) & 0x1fff;
    int             dst_height = mach64->dst_height_width & 0x1fff;
    int             x_dir      = (mach64->dst_cntl & DST_X_DIR) ? 1 : -1;
    int             y_dir      = (mach64->dst_cntl & DST_Y_DIR) ? 1 : -1;
    int             sc_left    = mach64_3d_sign_extend(mach64->sc_left_right, 13);
    int             sc_right   = mach64_3d_sign_extend(mach64->sc_left_right >> 16, 13);
    int             sc_top     = mach64_3d_sign_extend(mach64->sc_top_bottom, 15);
    int             sc_bottom  = mach64_3d_sign_extend(mach64->sc_top_bottom >> 16, 15);
    uint64_t        vram_size  = (uint64_t) mach64->vram_mask + 1;
    uint32_t        vacc_advanced;

    if (src_width && src_height && src_pitch && dst_width && dst_height && mach64_scaler_source_bpp(src_format) && dst_bpp &&
        mach64_scaler_destination_supported(dst_format)) {
        for (int dy = 0; dy < dst_height; dy++) {
            int      y          = dst_y + dy * y_dir;
            int32_t  v_acc      = v_start + (int32_t) (dy * y_inc);
            int      src_y      = mach64_scaler_accum_pixel(v_acc);
            unsigned v_fraction = mach64_scaler_accum_fraction4(v_acc);
            int      src_y_next;

            if (src_y < 0)
                src_y = 0;
            if (src_y >= src_height)
                src_y = src_height - 1;
            src_y_next = (src_y + 1 >= src_height) ? (src_height - 1) : (src_y + 1);

            for (int dx = 0; dx < dst_width; dx++) {
                int      x          = dst_x + dx * x_dir;
                int32_t  h_acc      = h_start + (int32_t) (dx * x_inc);
                int      src_x      = mach64_scaler_accum_pixel(h_acc);
                unsigned h_fraction = mach64_scaler_accum_fraction4(h_acc);
                int      src_x_next;
                int      inhibits;
                int64_t  addr;
                rgba_t   src;

                if ((x < 0) || (y < 0) || (x < sc_left) || (x > sc_right) || (y < sc_top) || (y > sc_bottom))
                    continue;

                if (src_x < 0)
                    src_x = 0;
                if (src_x >= src_width)
                    src_x = src_width - 1;
                src_x_next = (src_x + 1 >= src_width) ? (src_width - 1) : (src_x + 1);

                inhibits = mach64_scaler_source_inhibits(ctx, src_format, src_base, src_pitch, src_x, src_x_next, src_y,
                                                         src_y_next, h_fraction, v_fraction, replicate);

                if (src_format == 11) {
                    int      uv_width    = (src_width + 1) >> 1;
                    int32_t  uv_acc      = uv_start + (int32_t) (dx * xuv_inc);
                    int      uv_x        = mach64_scaler_accum_pixel(uv_acc);
                    unsigned uv_fraction = mach64_scaler_accum_fraction4(uv_acc);
                    int      uv_x_next;

                    if (uv_x < 0)
                        uv_x = 0;
                    if (uv_x >= uv_width)
                        uv_x = uv_width - 1;
                    uv_x_next = (uv_x + 1 >= uv_width) ? (uv_width - 1) : (uv_x + 1);

                    src = mach64_scaler_sample_yuyv(ctx, src_base, src_pitch, src_x, src_x_next, uv_x, uv_x_next, src_y,
                                                    src_y_next, h_fraction, uv_fraction, v_fraction, replicate);
                } else {
                    src = mach64_scaler_read_source(ctx, src_format, src_base, src_pitch, src_x, src_y);
                    if (!replicate) {
                        rgba_t below       = mach64_scaler_read_source(ctx, src_format, src_base, src_pitch, src_x, src_y_next);
                        rgba_t right       = mach64_scaler_read_source(ctx, src_format, src_base, src_pitch, src_x_next, src_y);
                        rgba_t below_right = mach64_scaler_read_source(ctx, src_format, src_base, src_pitch, src_x_next,
                                                                       src_y_next);

                        src = mach64_scaler_lerp_rgba(mach64_scaler_lerp_rgba(src, below, v_fraction),
                                                      mach64_scaler_lerp_rgba(right, below_right, v_fraction), h_fraction);
                    }
                }

                addr = (int64_t) dst_base + ((int64_t) y * dst_pitch + x) * dst_bpp;
                if ((addr < 0) || (((uint64_t) addr + dst_bpp) > vram_size))
                    continue;
                mach64_scaler_write_pixel(ctx, addr, dst_format, x, y, src, inhibits);
            }
        }
    }

    /* SCALE_VACC moves on by the lines drawn. */
    vacc_advanced              = (uint32_t) ((int64_t) v_start + (int64_t) dst_height * y_inc);
    ctx->regs[SCALE_VACC >> 2] = (ctx->regs[SCALE_VACC >> 2] & ~0x001ffff0) | (vacc_advanced & 0x001ffff0);
}

/* A write that completes its register, the last byte of a byte or word
   write or a dword one. */
static int
mach64_3d_write_complete(uint32_t addr, uint32_t type)
{
    if (type == FIFO_WRITE_DWORD)
        return (addr & 3) == 0;
    if (type == FIFO_WRITE_WORD)
        return (addr & 3) == 2;
    return (addr & 3) == 3;
}

static uint32_t
mach64_3d_merge_write(uint32_t old, uint32_t addr, uint32_t val, uint32_t type)
{
    int n = (type == FIFO_WRITE_DWORD) ? 4 : ((type == FIFO_WRITE_WORD) ? 2 : 1);

    for (int i = 0; i < n; i++) {
        unsigned shift = ((addr + i) & 3) * 8;

        old = (old & ~(0xffu << shift)) | (((val >> (i * 8)) & 0xff) << shift);
    }
    return old;
}

/*
 * A scaled rectangle starts on DST_WIDTH, DST_HEIGHT_WIDTH or DST_X_WIDTH as
 * a 2D one does, with the width's top bit clear. The scaler then takes the
 * destination from the write here, ahead of the 2D FIFO, where its source
 * would be no pixel the 2D engine has.
 */
static int
mach64_scaler_start(uint32_t addr, uint32_t val, uint32_t type)
{
    uint32_t reg = addr & ~3;
    unsigned top = (type == FIFO_WRITE_DWORD) ? 24 : ((type == FIFO_WRITE_WORD) ? 8 : 0);

    if ((reg != 0x110) && (reg != 0x118) && (reg != 0x11c))
        return 0;
    return mach64_3d_write_complete(addr, type) && !((val >> top) & 0x80);
}

/* DST_X, DST_Y, DST_Y_X, DST_HEIGHT, DST_WIDTH, DST_HEIGHT_WIDTH and
   DST_X_WIDTH as they set DST_Y_X and DST_HEIGHT_WIDTH. */
static void
mach64_scaler_write_dst(mach64_t *mach64, uint32_t addr, uint32_t val, uint32_t type)
{
    int n = (type == FIFO_WRITE_DWORD) ? 4 : ((type == FIFO_WRITE_WORD) ? 2 : 1);

    for (int i = 0; i < n; i++) {
        uint32_t byte_addr = (addr + i) & 0x3ff;
        uint32_t byte      = (val >> (i * 8)) & 0xff;

        if (((byte_addr >= 0x104) && (byte_addr <= 0x105)) || ((byte_addr >= 0x11c) && (byte_addr <= 0x11d)))
            mach64->dst_y_x = mach64_3d_merge_write(mach64->dst_y_x, byte_addr + 2, byte, FIFO_WRITE_BYTE);
        else if (((byte_addr >= 0x108) && (byte_addr <= 0x109)) || ((byte_addr >= 0x10c) && (byte_addr <= 0x10f)))
            mach64->dst_y_x = mach64_3d_merge_write(mach64->dst_y_x, byte_addr, byte, FIFO_WRITE_BYTE);
        else if ((byte_addr >= 0x110) && (byte_addr <= 0x111))
            mach64->dst_height_width = mach64_3d_merge_write(mach64->dst_height_width, byte_addr + 2, byte, FIFO_WRITE_BYTE);
        else if (((byte_addr >= 0x114) && (byte_addr <= 0x115)) || ((byte_addr >= 0x118) && (byte_addr <= 0x11b)) ||
                 ((byte_addr >= 0x11e) && (byte_addr <= 0x11f)))
            mach64->dst_height_width = mach64_3d_merge_write(mach64->dst_height_width, byte_addr, byte, FIFO_WRITE_BYTE);
    }
}

/* The registers only the 3D engine and the scaler have. */
static int
mach64_3d_register(uint32_t reg)
{
    return ((reg >= TRAIL_BRES_ERR) && (reg <= Z_CNTL)) || ((reg >= TEX_0_OFF) && (reg <= TEX_10_OFF)) ||
           ((reg >= SCALE_Y_PITCH) && (reg <= SCALE_VACC)) || (reg == SCALE_3D_CNTL) ||
           ((reg >= S_X_INC2) && (reg <= TEX_SIZE_PITCH)) || (reg == TEX_PALETTE) || ((reg >= RED_X_INC) && (reg <= ALPHA_START));
}

/* SCALE_Y_PITCH, SCALE_X_INC and SCALE_Y_INC are also at S_Y_INC, RED_X_INC
   and GREEN_X_INC: one register at two addresses. */
static uint32_t
mach64_3d_alias(uint32_t reg)
{
    switch (reg) {
        case SCALE_Y_PITCH:
            return S_Y_INC;
        case S_Y_INC:
            return SCALE_Y_PITCH;
        case SCALE_X_INC:
            return RED_X_INC;
        case RED_X_INC:
            return SCALE_X_INC;
        case SCALE_Y_INC:
            return GREEN_X_INC;
        case GREEN_X_INC:
            return SCALE_Y_INC;
        default:
            return 0;
    }
}

/*
 * The 3D registers are written at once, ahead of the 2D FIFO. Before one of
 * them after a write that went to the FIFO, the FIFO runs out, so a 3D draw
 * sees the 2D state written before it.
 */
static void
mach64_3d_sync(mach64_3d_t *ctx)
{
    if (!ctx->fifo_pending)
        return;
    mach64_wait_fifo_idle(ctx->mach64);
    ctx->fifo_pending = 0;
}

/*
 * GUI_STAT. Its free entry count is of the 48-entry FIFO, and the Windows 95
 * HAL waits for room for more than 32 before its first draw. The scissor
 * bits compare DST_X/DST_Y with the scissors at all times; the HAL reads them
 * while it sets the 3D engine up.
 */
static uint32_t
mach64_3d_gui_stat(mach64_t *mach64)
{
    uint32_t used = mach64->fifo_write_idx - mach64->fifo_read_idx;
    uint32_t ret;
    int      busy;
    int      dst_x;
    int      dst_y;

    if (used && !mach64->blitter_busy)
        mach64_wake_fifo_thread(mach64);
    if (!mach64_timing_status(mach64, &used, &busy))
        busy = used || mach64->blitter_busy;

    ret = ((used >= GT_FIFO_DEPTH) ? 0 : (GT_FIFO_DEPTH - used)) << 16;
    if (busy || mach64->accel.busy)
        ret |= 0x01; /* GUI_ACTIVE */

    dst_x = mach64_3d_sign_extend(mach64->dst_y_x >> 16, 13);
    dst_y = mach64_3d_sign_extend(mach64->dst_y_x, 15);
    if (dst_x < mach64_3d_sign_extend(mach64->sc_left_right, 13))
        ret |= 0x10; /* DSTX_LT_SCISSOR_LEFT */
    if (dst_x > mach64_3d_sign_extend(mach64->sc_left_right >> 16, 13))
        ret |= 0x20; /* DSTX_GT_SCISSOR_RIGHT */
    if (dst_y < mach64_3d_sign_extend(mach64->sc_top_bottom, 15))
        ret |= 0x40; /* DSTY_LT_SCISSOR_TOP */
    if (dst_y > mach64_3d_sign_extend(mach64->sc_top_bottom >> 16, 15))
        ret |= 0x80; /* DSTY_GT_SCISSOR_BOTTOM */
    return ret;
}

/*
 * Reads the GUI register at addr for the GT-B. Returns 0 to leave it to the
 * 2D engine; otherwise *val is the whole dword holding it.
 */
int
mach64_3d_read(mach64_t *mach64, uint32_t addr, uint32_t *val)
{
    mach64_3d_t *ctx = mach64->gt3d;
    uint32_t     reg = addr & 0x3fc;
    uint32_t     used;
    int          busy;

    switch (reg) {
        case GUI_STAT:
            *val = mach64_3d_gui_stat(mach64);
            return 1;
        case FIFO_STAT:
            /* A bit for each of the 16 entries shown in use: of the timed
               FIFO, as the 2D one empties at once. */
            if (!mach64_timing_status(mach64, &used, &busy))
                return 0;
            *val = (used >= 16) ? 0xffff : ((1u << used) - 1);
            return 1;
        case DST_BRES_LNTH:
            /* The 2D line length, unless the last command was a 3D one. */
            if (!(ctx->regs[DST_BRES_LNTH >> 2] & DRAW_TRAP))
                return 0;
            mach64_3d_sync(ctx);
            *val = ctx->regs[DST_BRES_LNTH >> 2];
            return 1;
        case LEAD_BRES_LNTH:
            mach64_3d_sync(ctx);
            *val = ctx->regs[DST_BRES_LNTH >> 2];
            return 1;
        default:
            if (!mach64_3d_register(reg))
                return 0;
            mach64_3d_sync(ctx);
            *val = mach64_3d_visible_reg(reg, ctx->regs[reg >> 2]);
            return 1;
    }
}

/*
 * A write to DST_BRES_LNTH or its 3D address LEAD_BRES_LNTH. Byte and word
 * writes land in the 3D copy as they come, as a driver may write the low
 * length bits before DRAW_TRAP. Returns 0 for a 2D line, which goes on to
 * the 2D engine.
 */
static int
mach64_3d_write_length(mach64_3d_t *ctx, uint32_t addr, uint32_t val, uint32_t type)
{
    mach64_t *mach64   = ctx->mach64;
    uint32_t  cmd      = mach64_3d_merge_write(ctx->regs[DST_BRES_LNTH >> 2], addr, val, type);
    unsigned  fcn      = (ctx->regs[SCALE_3D_CNTL >> 2] >> SCALE_3D_FCN_SHIFT) & 3;
    int       complete = mach64_3d_write_complete(addr, type);

    if (complete) {
        /* Whether the line is a 3D one depends on DP_SRC and DST_CNTL,
           which may still be in the FIFO. */
        if (!(cmd & (DRAW_TRAP | LINE_DIS)) && ((fcn == FCN_TEXTURE) || (fcn == FCN_SHADE)))
            mach64_3d_sync(ctx);

        /* A line command loads TRAIL_X, LINE_DIS ones included; a trapezoid
           keeps it unless bit 31 is set (VT/RAGE RRG 4-46). */
        if (!(cmd & DRAW_TRAP) || (cmd & LINE_DIS)) {
            ctx->trail_x     = mach64_3d_sign_extend(cmd >> 16, 13);
            ctx->trail_valid = 1;
        }

        if (mach64_3d_is_line(ctx, cmd)) {
            ctx->regs[DST_BRES_LNTH >> 2] = cmd;
            mach64->dst_bres_lnth         = mach64_3d_merge_write(mach64->dst_bres_lnth, addr, val, type);
            mach64_3d_sync(ctx);
            mach64_3d_draw_line(ctx, cmd);
            return 1;
        }
    }

    ctx->regs[DST_BRES_LNTH >> 2] = cmd;
    if (!(cmd & DRAW_TRAP) && ((addr & ~3) != LEAD_BRES_LNTH)) {
        ctx->fifo_pending = 1;
        return 0;
    }
    mach64_3d_sync(ctx);
    if ((cmd & DRAW_TRAP) && complete)
        mach64_3d_draw_trapezoid(ctx, cmd);
    return 1;
}

/*
 * Writes the GUI register at addr for the GT-B. Returns 0 for a register of
 * the 2D engine, which the caller queues.
 */
int
mach64_3d_write(mach64_t *mach64, uint32_t addr, uint32_t val, uint32_t type)
{
    mach64_3d_t *ctx = mach64->gt3d;
    uint32_t     reg;
    uint32_t     alias;

    addr &= 0x3ff;
    reg = addr & ~3;

    /* A scaled rectangle, started from the destination's size. */
    if (mach64_scaler_start(addr, val, type) && (((ctx->regs[SCALE_3D_CNTL >> 2] >> SCALE_3D_FCN_SHIFT) & 3) == FCN_SCALE)) {
        mach64_3d_sync(ctx);
        if (!(ctx->regs[SCALE_3D_CNTL >> 2] & SRC_3D_SEL) && (((mach64->dp_src >> 8) & 7) == SRC_3D)) {
            mach64_scaler_write_dst(mach64, addr, val, type);
            mach64_scaler_draw(ctx);
            return 1;
        }
    }

    switch (reg) {
        case DST_Y_X_ALIAS:
            /* A write-only DST_Y_X; the HAL sets trapezoids up through it. */
            mach64_3d_sync(ctx);
            mach64->dst_y_x = mach64_3d_merge_write(mach64->dst_y_x, addr, val, type);
            return 1;
        case DST_BRES_LNTH:
        case LEAD_BRES_LNTH:
            return mach64_3d_write_length(ctx, addr, val, type);
        case TEX_PALETTE:
            mach64_3d_sync(ctx);
            ctx->regs[reg >> 2] = mach64_3d_merge_write(ctx->regs[reg >> 2], addr, val, type);
            if (mach64_3d_write_complete(addr, type))
                mach64_3d_texture_palette_store(ctx->texture_palette, ctx->regs[reg >> 2]);
            return 1;
        default:
            break;
    }

    if (!mach64_3d_register(reg)) {
        ctx->fifo_pending = 1;
        return 0;
    }

    /* The register keeps only its own field, after every partial write. */
    mach64_3d_sync(ctx);
    ctx->regs[reg >> 2] = mach64_3d_visible_reg(reg, mach64_3d_merge_write(ctx->regs[reg >> 2], addr, val, type));
    ctx->tex_hidden &= ~mach64_3d_tex_hidden_bit(reg);
    alias = mach64_3d_alias(reg);
    if (alias) {
        ctx->regs[alias >> 2] = ctx->regs[reg >> 2];
        ctx->tex_hidden &= ~mach64_3d_tex_hidden_bit(alias);
    }
    return 1;
}

mach64_3d_t *
mach64_3d_init(mach64_t *mach64)
{
    mach64_3d_t *ctx = calloc(1, sizeof(mach64_3d_t));

    ctx->mach64 = mach64;
    return ctx;
}

void
mach64_3d_close(mach64_3d_t *ctx)
{
    free(ctx);
}
