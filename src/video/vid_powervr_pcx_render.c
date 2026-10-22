/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PowerVR PCX1/PCX2 frame renderer.
 *
 *          The hidden-surface (ISP) and shading/texturing (TSP) logic is
 *          adapted from the software models of the "SABRE" and "TEXAS"
 *          chips in Imagination Technologies' PowerVR Series 1 driver
 *          source (Source/simulat3/hwsabren.c, hwsabsim.c and texas.c),
 *          released under the MIT licence:
 *
 *            Copyright (c) 1995-2022 Imagination Technologies
 *
 *            Permission is hereby granted, free of charge, to any person
 *            obtaining a copy of this software and associated documentation
 *            files (the "Software"), to deal in the Software without
 *            restriction, including without limitation the rights to use,
 *            copy, modify, merge, publish, distribute, sublicense, and/or
 *            sell copies of the Software, and to permit persons to whom the
 *            Software is furnished to do so, subject to the following
 *            conditions:
 *
 *            The above copyright notice and this permission notice shall be
 *            included in all copies or substantial portions of the Software.
 *
 *            THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *            EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *            OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *            NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *            HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *            WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *            FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *            OTHER DEALINGS IN THE SOFTWARE.
 *
 *          Changes from the simulator: memory comes from the emulated card
 *          (ISP parameters through the TLB, TSP parameters and textures in
 *          texture memory), 32-bit arithmetic is made explicit for 64-bit
 *          hosts, the texture bank split is the PCX2's (pixel address bit
 *          20, i.e. 2 MB), fog uses the chip's FOG_TABLE, BILINEAR = 0
 *          selects point sampling, the output dither is the one measured on the
 *          card, and the debug/statistics code is gone.
 *
 * Authors: Zvi Rosen and contributors.
 *
 *          Copyright 2026 Zvi Rosen.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vid_powervr_pcx_render.h"

static inline int
clz32(uint32_t v) /* v != 0 */
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_clz(v);
#else
    int n = 0;
    while (!(v & 0x80000000u)) {
        v <<= 1;
        n++;
    }
    return n;
#endif
}

#define NUM_CELLS 32

/* Tile list words. */
#define REG_COMPULSORY_BITS 0x40000000u
#define LINK_LIST_BIT       0x20000000u
/* A link word has bit 29 set and bit 30 clear. A region word always has
   bit 30 set, and bit 29 is then the top bit of its Y position (tiles at
   Y 512 and below, reached at 800x600), not a link. */
#define IS_LINK(w)          (((w) & (LINK_LIST_BIT | REG_COMPULSORY_BITS)) == LINK_LIST_BIT)
#define OBJ_VERY_LAST_PTR   0x80000000u
#define WORDS_PER_PLANE     4

/* TSP control word. */
#define MASK_TEXTURE        0x80000000u
#define MASK_SMOOTH_SHADE   0x40000000u
#define MASK_DISABLE_FOG    0x20000000u
#define MASK_SHADOW_FLAG    0x10000000u
#define MASK_FLAT_HIGHLIGHT 0x04000000u
#define MASK_EXPONENT       0x003C0000u
#define MASK_GLOBAL_TRANS   0x0001E000u
#define MASK_FLIP_UV        0x00001800u
#define MASK_TRANS          0x00000400u
#define SHIFT_EXPONENT      18
#define SHIFT_GLOBAL_TRANS  13
#define SHIFT_FLIP_UV       11
#define MASK_MIP_MAPPED     0x80000000u
#define MASK_8_16_MAPS      0x40000000u
#define MASK_MAP_SIZE       0x30000000u
#define MASK_4444_555       0x08000000u
#define SHIFT_MAP_SIZE      28
#define MASK_PMIP_M         0xFF000000u
#define MASK_PMIP_E         0x00FC0000u
#define SHIFT_PMIP_M        24
#define SHIFT_PMIP_E        18

#define MASKING_PLANE_ADDR 2u /* TSP word address of the masking plane */

#define BIG_BANK 0x100000u /* texture pixel address of the second bank (2 MB) */

/* ISP plane instructions. */
enum {
    forw_visib = 0, forw_invis, forw_perp, test_shad_forw, test_shad_perp,
    rev_visib, rev_invis, rev_replace_if,
    forw_visib_fp, forw_invis_fp, forw_perp_fp, test_shad_forw_fp, test_shad_perp_fp,
    test_shadow_rev, test_light_rev, begin_trans
};

/* Wide (internal) cell commands. */
#define load_i_nop        0
#define load_i            8
#define load_i_further    4
#define load_i_closer     2
#define load_i_invis_forw 1
#define load_u_nop        0
#define load_u            2
#define load_u_closer     3
#define test_shad_nop       0
#define test_shad_closer    2
#define test_shadow_further 1
#define test_light_further  3
#define prev_none      0
#define prev_shad_forw 1
#define prev_shadow    3
#define prev_light     2
#define prev_trans     6
#define prev_def_rep   7

typedef struct {
    int i_load, u_load, test_shad;
    int plane_visib, delayed_clear_u_id, plane_perp, mux_sel;
} cell_control;


typedef struct {
    uint8_t r, g, b, a;
} rgba_t;

typedef struct {
    int32_t m;
    int     e;
} pfloat;

typedef struct {
    const pcxr_frame_t *f;
    uint32_t           *rgb;
    pcxr_result_t      *res;
    int                 prev_intern_out;
    uint8_t             fog_r, fog_g, fog_b;
    int                 fog_on;
} ctx_t;

/* ---- ISP (SABRE) ------------------------------------------------------ */

static inline uint32_t
isp_word(const ctx_t *c, uint32_t i)
{
    return (i < c->f->isp_words) ? c->f->isp[i] : 0;
}

static inline uint32_t
strip_trans_bit(uint32_t w)
{
    /* PCX2 start-of-translucent-pass marker. */
    if ((w & 0x80000000u) && (w & 0x20000000u))
        w &= 0x5FFFFFFFu;
    return w;
}

/* Plane C into the chip's 32-bit fixed point. Out-of-range values saturate:
   a perpendicular (edge) plane with A = B = 0 scales to a C a little above
   1.0, and the m3D treats it as positive (inside). Wrapping it to a large
   negative number instead blacked out whole regions of Moto Racer. */
static int32_t
to_int32(double v)
{
    if (v >= 2147483647.0)
        return 0x7fffffff;
    if (v <= -2147483648.0)
        return (int32_t) 0x80000000u;
    /* A small negative C (between -2 and 0) does not truncate toward 0:
       the m3D turns it into the most negative value, so an edge plane with
       such a C is outside everywhere and its object vanishes. Small
       positive values are unaffected. Measured with Shadows of the Empire
       1.1 at 320x240 and 320x200, where menu triangles with C of -0.25,
       -0.56, -0.57, -0.94, -1.55 and -1.63 are missing on the card while
       those with -2.46 and below, or +0.14 and +0.75, are drawn. */
    if (v < 0.0 && v > -2.0)
        return (int32_t) 0x80000000u;
    return (int32_t) v;
}

static cell_control
wide(int i, int u, int shad, int pvis, int clear_uid, int pperp, int muxsel)
{
    cell_control w = { i, u, shad, pvis, clear_uid, pperp, muxsel };
    return w;
}

static cell_control
expand_instruction(ctx_t *c, int instr, int do_nop, int sec_object, int do_flush)
{
    cell_control w;
    int          prev_intern = prev_none;

    if (do_nop)
        return wide(load_i_nop, load_u_nop, test_shad_nop, 0, 0, 0, 0);
    if (do_flush)
        return wide(load_i_nop, load_u_closer, test_shad_nop, 1, 0, 0, 1);

    switch (instr) {
        default:
        case forw_visib:
            w = wide(load_i_further, load_u_nop, test_shad_nop, 1, 0, 0, 0);
            break;
        case forw_visib_fp:
            w = wide(load_i, sec_object ? load_u : load_u_closer, test_shad_nop, 1, 0, 0, 1);
            break;
        case forw_invis:
            w = wide(load_i_further, load_u_nop, test_shad_nop, 0, 0, 0, 0);
            break;
        case forw_invis_fp:
            w = wide(load_i, sec_object ? load_u : load_u_closer, test_shad_nop, 0, 0, 0, 1);
            break;
        case forw_perp:
            w = wide(load_i_further, load_u_nop, test_shad_nop, 0, 0, 1, 0);
            break;
        case forw_perp_fp:
            w = wide(load_i, sec_object ? load_u : load_u_closer, test_shad_nop, 1, 0, 1, 1);
            break;
        case rev_visib:
            w = wide(load_i_closer, load_u_nop, test_shad_nop, 1, 0, 0, 0);
            break;
        case rev_invis:
            w = wide(load_i_closer, load_u_nop, test_shad_nop, 0, 0, 0, 0);
            break;
        case rev_replace_if:
            w           = wide(load_i_closer, load_u_nop, test_shad_nop, 0, 0, 0, 0);
            prev_intern = prev_def_rep;
            break;
        case test_shad_forw:
            w           = wide(load_i_further, load_u_nop, test_shad_nop, 1, 0, 0, 0);
            prev_intern = prev_shad_forw;
            break;
        case test_shad_forw_fp:
            w           = wide(load_i, sec_object ? load_u : load_u_closer, test_shad_nop, 1, 0, 0, 1);
            prev_intern = prev_shad_forw;
            break;
        case test_shad_perp:
            w           = wide(load_i_further, load_u_nop, test_shad_nop, 0, 0, 1, 0);
            prev_intern = prev_shad_forw;
            break;
        case test_shad_perp_fp:
            w           = wide(load_i, sec_object ? load_u : load_u_closer, test_shad_nop, 0, 0, 1, 1);
            prev_intern = prev_shad_forw;
            break;
        case test_shadow_rev:
            w           = wide(load_i_closer, load_u_nop, test_shad_nop, 0, 0, 0, 0);
            prev_intern = prev_shadow;
            break;
        case test_light_rev:
            w           = wide(load_i_closer, load_u_nop, test_shad_nop, 0, 0, 0, 0);
            prev_intern = prev_light;
            break;
        case begin_trans:
            w           = wide(load_i_nop, load_u_closer, test_shad_nop, 1, 0, 0, 1);
            prev_intern = prev_trans;
            break;
    }

    switch (c->prev_intern_out) {
        case prev_shad_forw:
            w.test_shad = test_shad_closer;
            w.i_load    = load_i;
            w.mux_sel   = 1;
            break;
        case prev_shadow:
            w.test_shad = test_shadow_further;
            w.u_load    = load_u_nop;
            break;
        case prev_light:
            w.test_shad = test_light_further;
            w.u_load    = load_u_nop;
            break;
        case prev_trans:
            w.delayed_clear_u_id = 1;
            w.u_load             = load_u_nop;
            break;
        case prev_def_rep:
            w.i_load = load_i_invis_forw;
            break;
        default:
            break;
    }
    c->prev_intern_out = prev_intern;
    return w;
}

static int
fog_factor(const ctx_t *c, int32_t depth)
{
    int32_t idx = depth >> (c->f->fog_shift & 0x1f);
    int32_t fac;

    if (idx < 0)
        idx = 0;
    fac = idx >> 7;
    if (fac > 8)
        fac = 9;
    return (int) ((c->f->fog_table[idx & 0x7f] & 0xff) >> fac);
}

/* ---- TSP (TEXAS) ------------------------------------------------------ */

static inline uint32_t
tsp_word(const ctx_t *c, uint32_t a)
{
    return c->f->tex[a % c->f->tex_words];
}

static int
to_int16(uint32_t x)
{
    return (int) (int16_t) (x & 0xffff);
}

static void
from555(uint16_t p, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (uint8_t) ((((p >> 10) & 0x1f) << 3) + 4);
    *g = (uint8_t) ((((p >> 5) & 0x1f) << 3) + 4);
    *b = (uint8_t) (((p & 0x1f) << 3) + 4);
}

static pfloat
to_pfloat(int32_t x)
{
    pfloat   t;
    uint32_t ax = (x < 0) ? (uint32_t) 0 - (uint32_t) x : (uint32_t) x;

    /* e = number of significant bits of |x| (0 for 0, 32 for INT32_MIN) */
    t.e = ax ? 32 - clz32(ax) : 0;
    t.m = (int32_t) ((uint32_t) x << ((31 - t.e) & 31));
    if (t.e > 31)
        t.m = x >> 1; /* only reachable for INT32_MIN */
    t.m >>= 16;
    return t;
}

static int32_t
ashift(int32_t x, int shift)
{
    if (shift < 0) {
        if (shift < -31)
            return (x < 0) ? -1 : 0;
        return x >> -shift;
    }
    if (shift > 31)
        return 0;
    return (int32_t) ((uint32_t) x << shift);
}

/* Morton (twiddled) order: bit i of x goes to bit 2i + 1, of y to bit 2i.
   spread8[v] spreads the 8 bits of v to the even bit positions; filled by
   pcxr_prepare() before any rendering. */
static uint16_t spread8[256];

static void
init_spread8(void)
{
    for (uint32_t v = 0; v < 256; v++) {
        uint32_t r = 0;
        for (int i = 0; i < 8; i++)
            r |= ((v >> i) & 1) << (2 * i);
        spread8[v] = (uint16_t) r;
    }
}

static inline uint32_t
twid(uint32_t x, uint32_t y)
{
    return ((uint32_t) spread8[x & 0xff] << 1) | spread8[y & 0xff];
}

static uint32_t
address_calc(int32_t u, int32_t v, uint32_t address, int map_size, int which_map, int colour_depth,
             int mip_mapped, int flip_uv)
{
    static const uint32_t mmc[]      = { 0x5555, 0x1555, 0x0555, 0x0155, 0x0055, 0x0015, 0x0005, 0x0001, 0x0000 };
    static const int32_t  map_mask[] = { 0xff, 0x7f, 0x3f, 0x1f };

    if (map_size && flip_uv) {
        if (((map_mask[map_size] + 1) & u) && (flip_uv & 2))
            u = ~u;
        if (((map_mask[map_size] + 1) & v) && (flip_uv & 1))
            v = ~v;
    }
    if (map_size == 0 && flip_uv) {
        if ((128 & u) && (flip_uv & 2))
            u = ~u;
        if ((128 & v) && (flip_uv & 1))
            v = ~v;
    }
    u &= map_mask[map_size];
    v &= map_mask[map_size];
    if (!mip_mapped)
        return address + (colour_depth ? twid(u, v) : twid(u >> 1, v));
    if ((map_size + which_map) & 1)
        address ^= BIG_BANK;
    if (map_size + which_map > 8)
        return address;
    /* The mip chain starts one pixel in: the 1x1 level takes the second
       pixel of the first word, and every larger level follows it (the
       simulator has no such offset). Measured on the m3D with Ultim@te
       Race, where it makes every mip-mapped pixel match. */
    return address + 1 + mmc[map_size + which_map] + twid(u >> which_map, v >> which_map);
}

static uint16_t
fetch_pixel(const ctx_t *c, uint32_t address)
{
    uint32_t w = c->f->tex[(address >> 1) % c->f->tex_words];
    return (address & 1) ? (uint16_t) (w & 0xffff) : (uint16_t) (w >> 16);
}

static rgba_t
colour_convert(uint16_t raw, int colour_depth, int which_pixel, int translucent)
{
    rgba_t p;

    if (translucent) {
        p.b = raw & 0xf;
        p.g = (raw >> 4) & 0xf;
        p.r = (raw >> 8) & 0xf;
        p.a = (raw >> 12) & 0xf;
        p.r <<= 1;
        p.g <<= 1;
        p.b <<= 1;
        p.r |= p.r >> 4;
        p.g |= p.g >> 4;
        p.b |= p.b >> 4;
    } else if (colour_depth) {
        p.r = (raw >> 10) & 0x1f;
        p.g = (raw >> 5) & 0x1f;
        p.b = raw & 0x1f;
        p.a = 0;
    } else {
        if (which_pixel == 0)
            raw >>= 8;
        p.r = raw & 0xe0;
        p.g = (raw << 3) & 0xe0;
        p.b = (raw << 6) & 0xc0;
        p.a = 0;
        if (p.b & 0x80)
            p.b |= 0x3f;
        if (p.g & 0x80)
            p.g |= 0x1f;
        if (p.r & 0x80)
            p.r |= 0x1f;
        p.b >>= 3;
        p.g >>= 3;
        p.r >>= 3;
    }
    return p;
}

static rgba_t
bilerp(rgba_t t, rgba_t u1, rgba_t v1, rgba_t u1v1, int32_t uf, int32_t vf)
{
    rgba_t ab, cd, o;
    int    r, g, b, a;

    ab.r = (uint8_t) ((t.r << 3) + 4 + (((int) u1.r - t.r) * uf >> 2));
    ab.g = (uint8_t) ((t.g << 3) + 4 + (((int) u1.g - t.g) * uf >> 2));
    ab.b = (uint8_t) ((t.b << 3) + 4 + (((int) u1.b - t.b) * uf >> 2));
    ab.a = (uint8_t) (t.a + (((int) u1.a - t.a) * uf >> 5));
    cd.r = (uint8_t) ((v1.r << 3) + 4 + (((int) u1v1.r - v1.r) * uf >> 2));
    cd.g = (uint8_t) ((v1.g << 3) + 4 + (((int) u1v1.g - v1.g) * uf >> 2));
    cd.b = (uint8_t) ((v1.b << 3) + 4 + (((int) u1v1.b - v1.b) * uf >> 2));
    cd.a = (uint8_t) (v1.a + (((int) u1v1.a - v1.a) * uf >> 5));
    r    = ((int) cd.r - ab.r) * vf;
    g    = ((int) cd.g - ab.g) * vf;
    b    = ((int) cd.b - ab.b) * vf;
    a    = ((int) cd.a - ab.a) * vf;
    o.r  = (uint8_t) ((r >> 5) + ab.r);
    o.g  = (uint8_t) ((g >> 5) + ab.g);
    o.b  = (uint8_t) ((b >> 5) + ab.b);
    o.a  = (uint8_t) ((a >> 5) + ab.a);
    return o;
}

static rgba_t
texture_pixel(const ctx_t *c, int x, int y, int a, int b, int cc, int d, int e, int f, int p, int q, int r,
              int exp, uint32_t address, int map_size, pfloat pmip, int colour_depth, int mip_mapped,
              int col4444, int global_trans, int flip_uv)
{
    rgba_t   tcol, u1, v1, u1v1;
    int32_t  abc, def, pqr, uf, vf, u, v;
    int      shift, power_two;
    pfloat   top, bot;
    int32_t  scale  = (int32_t) (c->f->camera & 0xffff);
    /* BILINEAR[1:0]: 0 bilinear, 1 adaptive bilinear (see below), 3 linear
       mip-mapping (no bilinear filter). */
    int      bilin  = ((c->f->bilinear & 3) != 3);

    abc = (int32_t) ((uint32_t) a * x + (uint32_t) b * y + (uint32_t) cc * scale);
    def = (int32_t) ((uint32_t) d * x + (uint32_t) e * y + (uint32_t) f * scale);
    pqr = (int32_t) ((uint32_t) p * x + (uint32_t) q * y + (uint32_t) r * scale);

    bot = to_pfloat(pqr);
    bot.m >>= 1;
    if (bot.m > 0)
        bot.m = 0x8000000 / bot.m;
    else
        bot.m = 0x4000;
    power_two = (bot.m == 0x4000);
    if (power_two)
        bot.m = 0x2000;

    top = to_pfloat(abc);
    top.e += exp;
    u     = (int32_t) (((int64_t) top.m * bot.m) >> 14);
    shift = power_two ? top.e - (bot.e + 13) : top.e - (bot.e + 14);
    uf    = ashift(u, shift + 5) & 8191;
    u     = ashift(u, shift) & 255;

    top = to_pfloat(def);
    top.e += exp;
    v     = (int32_t) (((int64_t) top.m * bot.m) >> 14);
    shift = power_two ? top.e - (bot.e + 13) : top.e - (bot.e + 14);
    vf    = ashift(v, shift + 5) & 8191;
    v     = ashift(v, shift) & 255;

    /* A texture mirrored in u (flip bit 12) on a mirrored repeat also
       mirrors the bilinear weight: the fraction is inverted while each
       sample's address is mirrored as usual. Measured on the m3D with
       edited frames, where it makes every u-mirrored pixel match. The v
       direction does not follow the same rule and is not yet modelled. */
    if (flip_uv & 2) {
        static const int32_t raw_mask[] = { 0x1f, 0x3f, 0x7f, 0xff };
        int32_t              bit        = (map_size == 3) ? 128 : raw_mask[map_size] + 1;
        if (u & bit)
            uf = ~uf & 8191;
    }

    /* Mip-map level from the squared 1/w. */
    bot.m >>= 6;
    bot.m = bot.m * bot.m;
    if (power_two)
        bot.e--;
    if (bot.m & 0x8000) {
        bot.e = bot.e * 2;
        bot.m >>= 8;
    } else {
        bot.e = bot.e * 2 + 1;
        bot.m >>= 7;
    }
    bot.m = bot.m * pmip.m;
    if (bot.m & 0x8000) {
        bot.e = pmip.e - (bot.e - 2);
        bot.m >>= 8;
    } else {
        bot.e = pmip.e - (bot.e - 1);
        bot.m >>= 7;
    }
    if (bot.e < 0)
        bot.e = 0;
    if (bot.e > 15)
        bot.e = 15;

    map_size = 3 - map_size; /* 0 = 256x256 from here on */

    /* Adaptive bilinear (BILINEAR = 1): the filter is used on the two
       finest mip steps only; further away the chip blends between mip
       levels instead, as in mode 3. Measured on the m3D. */
    if ((c->f->bilinear & 3) == 1 && mip_mapped && bot.e >= 2)
        bilin = 0;

    if (bot.e < 1 || !mip_mapped || colour_depth == 0) {
        tcol = colour_convert(fetch_pixel(c, address_calc(u, v, address, map_size, 0, colour_depth, mip_mapped, flip_uv)),
                              colour_depth, u & 1, col4444);
        if (!bilin) {
            tcol.r = (uint8_t) ((tcol.r << 3) + 4);
            tcol.g = (uint8_t) ((tcol.g << 3) + 4);
            tcol.b = (uint8_t) ((tcol.b << 3) + 4);
        } else {
            uf &= 31;
            vf &= 31;
            u1   = colour_convert(fetch_pixel(c, address_calc((u + 1) & 255, v, address, map_size, 0, colour_depth, mip_mapped, flip_uv)),
                                  colour_depth, (u + 1) & 1, col4444);
            v1   = colour_convert(fetch_pixel(c, address_calc(u, (v + 1) & 255, address, map_size, 0, colour_depth, mip_mapped, flip_uv)),
                                  colour_depth, u & 1, col4444);
            u1v1 = colour_convert(fetch_pixel(c, address_calc((u + 1) & 255, (v + 1) & 255, address, map_size, 0, colour_depth, mip_mapped, flip_uv)),
                                  colour_depth, (u + 1) & 1, col4444);
            tcol = bilerp(tcol, u1, v1, u1v1, uf, vf);
        }
    } else if (map_size + bot.e > 8) {
        tcol   = colour_convert(fetch_pixel(c, address_calc(u, v, address, 0, 8, colour_depth, mip_mapped, flip_uv)),
                                colour_depth, 0, col4444);
        tcol.r = (uint8_t) ((tcol.r << 3) + 4);
        tcol.g = (uint8_t) ((tcol.g << 3) + 4);
        tcol.b = (uint8_t) ((tcol.b << 3) + 4);
    } else {
        int compress = bot.e - 1;
        int binc     = 1 << compress;

        if (!bilin) {
            /* Linear mip-mapping: blend this level with the next smaller. */
            rgba_t hi = colour_convert(fetch_pixel(c, address_calc(u, v, address, map_size, compress, colour_depth, mip_mapped, flip_uv)),
                                       colour_depth, 0, col4444);
            rgba_t lo = colour_convert(fetch_pixel(c, address_calc(u, v, address, map_size, bot.e, colour_depth, mip_mapped, flip_uv)),
                                       colour_depth, 0, col4444);
            int    t  = (bot.m & 0x7f) >> 2;
            tcol.r    = (uint8_t) ((hi.r << 3) + ((t * ((int) lo.r - hi.r)) >> 2) + 4);
            tcol.g    = (uint8_t) ((hi.g << 3) + ((t * ((int) lo.g - hi.g)) >> 2) + 4);
            tcol.b    = (uint8_t) ((hi.b << 3) + ((t * ((int) lo.b - hi.b)) >> 2) + 4);
            tcol.a    = lo.a;
            goto done;
        }
        tcol = colour_convert(fetch_pixel(c, address_calc(u, v, address, map_size, compress, colour_depth, mip_mapped, flip_uv)),
                              colour_depth, u & 1, col4444);
        {
            uf = (uf >> compress) & 31;
            vf = (vf >> compress) & 31;
            u1   = colour_convert(fetch_pixel(c, address_calc((u + binc) & 255, v, address, map_size, compress, colour_depth, mip_mapped, flip_uv)),
                                  colour_depth, u & 1, col4444);
            v1   = colour_convert(fetch_pixel(c, address_calc(u, (v + binc) & 255, address, map_size, compress, colour_depth, mip_mapped, flip_uv)),
                                  colour_depth, u & 1, col4444);
            u1v1 = colour_convert(fetch_pixel(c, address_calc((u + binc) & 255, (v + binc) & 255, address, map_size, compress, colour_depth, mip_mapped, flip_uv)),
                                  colour_depth, u & 1, col4444);
            tcol = bilerp(tcol, u1, v1, u1v1, uf, vf);
        }
    }

done:
    {
        int al = tcol.a + global_trans;
        tcol.a = (uint8_t) ((al > 15) ? 15 : al);
    }
    return tcol;
}

static int
clip255(int v)
{
    return (v > 255) ? 255 : v;
}

static void
texas(ctx_t *c, int x, int y, uint32_t address, int shadow, int fog)
{
    uint32_t w0 = tsp_word(c, address);
    uint32_t w1 = tsp_word(c, address + 1);
    uint32_t inc = address;
    int      red, green, blue;
    int      x_off = 0, y_off = 0;
    uint8_t  br = 0, bg = 0, bb = 0;
    rgba_t   col;
    uint32_t *dst;

    if ((unsigned) x >= PCXR_MAX_W || (unsigned) y >= PCXR_MAX_H)
        return;
    /* TSP address 2 (tag 1) is the PCX2 masking plane: where it is the
       visible surface the pixel is not written at all, so whatever is in
       the frame buffer shows through. The D3D driver uses it as the
       background; measured on the m3D. */
    if (address == MASKING_PLANE_ADDR)
        return;
    dst = &c->rgb[y * PCXR_MAX_W + x];

    if (w0 & MASK_SMOOTH_SHADE) {
        x_off = to_int16(w1 >> 16);
        y_off = to_int16(w1);
    } else {
        uint8_t sr, sg, sb;
        br = w0 & 0xff;
        bg = (w1 >> 24) & 0xff;
        bb = (w1 >> 16) & 0xff;
        from555(w1 & 0xffff, &sr, &sg, &sb);
        if ((w0 & MASK_SHADOW_FLAG) && !shadow) {
            br = (uint8_t) clip255(br + sr);
            bg = (uint8_t) clip255(bg + sg);
            bb = (uint8_t) clip255(bb + sb);
        }
    }

    if (w0 & MASK_TEXTURE) {
        uint32_t w2 = tsp_word(c, address + 2), w3 = tsp_word(c, address + 3);
        uint32_t w4 = tsp_word(c, address + 4), w5 = tsp_word(c, address + 5);
        uint32_t w6 = tsp_word(c, address + 6), w7 = tsp_word(c, address + 7);
        pfloat   pmip;

        pmip.m = (int32_t) ((w2 & MASK_PMIP_M) >> SHIFT_PMIP_M);
        pmip.e = (int) ((w2 & MASK_PMIP_E) >> SHIFT_PMIP_E);
        col    = texture_pixel(c, x, y, to_int16(w5), to_int16(w5 >> 16), to_int16(w4), to_int16(w7),
                               to_int16(w7 >> 16), to_int16(w6), to_int16(w3), to_int16(w3 >> 16), to_int16(w2),
                               (int) ((w0 & MASK_EXPONENT) >> SHIFT_EXPONENT), (w4 >> 16) | (w6 & 0x00ff0000),
                               (int) ((w6 & MASK_MAP_SIZE) >> SHIFT_MAP_SIZE), pmip, (w6 & MASK_8_16_MAPS) != 0,
                               (w6 & MASK_MIP_MAPPED) != 0, (w6 & MASK_4444_555) != 0,
                               (int) ((w0 & MASK_GLOBAL_TRANS) >> SHIFT_GLOBAL_TRANS),
                               (int) ((w0 & MASK_FLIP_UV) >> SHIFT_FLIP_UV));
        if (!(w0 & MASK_SMOOTH_SHADE)) {
            col.r = (uint8_t) ((col.r * br) >> 8);
            col.g = (uint8_t) ((col.g * bg) >> 8);
            col.b = (uint8_t) ((col.b * bb) >> 8);
        }
        inc += 8;
    } else {
        col.r = br;
        col.g = bg;
        col.b = bb;
        col.a = 0;
        inc += 2;
    }

    if (w0 & MASK_SMOOTH_SHADE) {
        int32_t  frac;
        uint8_t  hr, hg, hb;
        uint32_t s0 = tsp_word(c, inc), s1 = tsp_word(c, inc + 1);

        frac = (to_int16(s0) << 2) + to_int16(s1 >> 16) * (y - y_off) + to_int16(s1) * (x - x_off);
        from555(s0 >> 16, &hr, &hg, &hb);
        hr >>= 3;
        hg >>= 3;
        hb >>= 3;
        frac = (frac < 0) ? 0 : (frac > 0x10000) ? 0x10000 : frac;
        frac >>= 8;
        hr = (uint8_t) ((hr * frac) >> 5);
        hg = (uint8_t) ((hg * frac) >> 5);
        hb = (uint8_t) ((hb * frac) >> 5);
        if (frac == 0x100) {
            hr += 4;
            hg += 4;
            hb += 4;
        }
        inc += 2;

        if ((w0 & MASK_SHADOW_FLAG) && !shadow) {
            uint8_t sr, sg, sb;
            s0   = tsp_word(c, inc);
            s1   = tsp_word(c, inc + 1);
            frac = (to_int16(s0) << 2) + to_int16(s1 >> 16) * (y - y_off) + to_int16(s1) * (x - x_off);
            from555(s0 >> 16, &sr, &sg, &sb);
            sr >>= 3;
            sg >>= 3;
            sb >>= 3;
            frac = (frac < 0) ? 0 : (frac > 0x10000) ? 0x10000 : frac;
            frac >>= 8;
            red   = (sr * frac) >> 5;
            green = (sg * frac) >> 5;
            blue  = (sb * frac) >> 5;
            if (frac == 0x100) {
                red += 4;
                green += 4;
                blue += 4;
            }
            hr = (uint8_t) clip255(red + hr);
            hg = (uint8_t) clip255(green + hg);
            hb = (uint8_t) clip255(blue + hb);
            inc += 2;
        }

        if (!(w0 & MASK_TEXTURE)) {
            col.r = hr;
            col.g = hg;
            col.b = hb;
            col.a = 0;
        } else {
            col.r = (uint8_t) ((col.r * hr) >> 8);
            col.g = (uint8_t) ((col.g * hg) >> 8);
            col.b = (uint8_t) ((col.b * hb) >> 8);
        }
    }

    if (w0 & MASK_FLAT_HIGHLIGHT) {
        uint32_t hw = tsp_word(c, inc);
        uint8_t  hr, hg, hb, sr, sg, sb;
        from555(hw >> 16, &hr, &hg, &hb);
        from555(hw & 0xffff, &sr, &sg, &sb);
        hr >>= 3;
        hg >>= 3;
        hb >>= 3;
        if ((w0 & MASK_SHADOW_FLAG) && !shadow) {
            hr = (uint8_t) ((hr + (sr >> 3) > 31) ? 31 : hr + (sr >> 3));
            hg = (uint8_t) ((hg + (sg >> 3) > 31) ? 31 : hg + (sg >> 3));
            hb = (uint8_t) ((hb + (sb >> 3) > 31) ? 31 : hb + (sb >> 3));
        }
        col.r = (uint8_t) clip255(col.r + (hr << 3));
        col.g = (uint8_t) clip255(col.g + (hg << 3));
        col.b = (uint8_t) clip255(col.b + (hb << 3));
    }

    if (c->fog_on && !(w0 & MASK_DISABLE_FOG)) {
        red   = ((int) c->fog_r - col.r) * fog;
        green = ((int) c->fog_g - col.g) * fog;
        blue  = ((int) c->fog_b - col.b) * fog;
        col.r = (uint8_t) ((red >> 8) + col.r);
        col.g = (uint8_t) ((green >> 8) + col.g);
        col.b = (uint8_t) ((blue >> 8) + col.b);
    }

    if (w0 & MASK_TRANS) {
        int      al = (col.a == 15) ? 16 : col.a;
        uint32_t d  = *dst;
        red   = (((int) ((d >> 16) & 0xff) * al) >> 4) + (((16 - al) * col.r) >> 4);
        green = (((int) ((d >> 8) & 0xff) * al) >> 4) + (((16 - al) * col.g) >> 4);
        blue  = (((int) (d & 0xff) * al) >> 4) + (((16 - al) * col.b) >> 4);
        col.r = (uint8_t) red;
        col.g = (uint8_t) green;
        col.b = (uint8_t) blue;
    }

    *dst = PCXR_WRITTEN | ((uint32_t) col.r << 16) | ((uint32_t) col.g << 8) | col.b;
    c->res->pixels_shaded++;
}

/* ---- frame ------------------------------------------------------------ */

static int
x_visible(const ctx_t *c, int x)
{
    uint32_t xc = c->f->xclip;
    if ((xc & (1u << 12)) && x < (int) (xc & 0x3ff) - 1)
        return 0;
    /* Measured on the m3D (MDK, XCLIP 0x12590000): column 600 is written,
       601 is not. The left limit is not used by the drivers or measured. */
    if ((xc & (1u << 28)) && x >= (int) ((xc >> 16) & 0x3ff))
        return 0;
    return 1;
}

/* Plane A and B coefficients as the chip uses them, in units of 2^-39 of
   the depth scale: 15 significant bits, truncated toward zero, as in the
   simulator's PackTo20Bit() but without the 20-bit format's exponent
   limits. With approx_recip() below this reproduces the m3D's own output
   on every test frame (one pixel in 921,600 aside). */
static int64_t
plane_q(float v)
{
    double d = (double) v * 549755813888.0; /* 2^39 */
    double m;
    int    e;

    if (d == 0.0)
        return 0;
    if (d > 4.0e18 || d < -4.0e18) /* far outside any screen; keep the sign */
        return (d > 0) ? ((int64_t) 1 << 61) : -((int64_t) 1 << 61);
    m = frexp(d, &e);
    return (int64_t) ldexp(trunc(ldexp(m, 15)), e - 15);
}

/* The simulator's fast reciprocal (sgl_mat2.h ApproxRecip, SLOW_FDIV
   form): negated exponent and one's-complemented mantissa. Used to bring
   perpendicular planes into range; the exact 1/x leaves edge pixels
   different from the card. */
static float
approx_recip(float v)
{
    union {
        float    f;
        uint32_t u;
    } fl;
    uint32_t exp;

    fl.f = v;
    exp  = fl.u & (0xffu << 23);
    exp  = ((253u << 23) - exp) & (0xffu << 23);
    fl.u &= 0x807fffffu;
    fl.u ^= 0x007fffffu;
    fl.u |= exp;
    return fl.f;
}

/* One tile's decoded planes and object list, built once per tile. */
typedef struct {
    int64_t  a, b;
    int32_t  c;
    uint32_t index;
    int      instr;
} plane_t;

typedef struct {
    uint32_t first, count;
    int      very_last, last_in_region;
    int      simple; /* one forw_visib_fp plane followed only by forw_perp edges */
} object_t;

/* One tile: its place, its objects, and the instruction-decoder state it
   starts with (that state runs on from tile to tile, so it is worked out
   in order first; the tiles can then be drawn in any order). */
typedef struct {
    int      xpos, ypos, xsize, ysize;
    uint32_t first_object, n_objects;
    int      prev_intern_out, sec_obj, first_obj;
    int      plain; /* only forw_visib_fp, forw_perp and begin_trans planes */
} tile_t;

struct pcxr_job {
    const pcxr_frame_t *f;
    plane_t            *planes;
    object_t           *objects;
    tile_t             *tiles;
    uint32_t            n_planes, n_objects, n_tiles;
    uint32_t            cap_planes, cap_objects, cap_tiles;
};

/* The 32 SABRE cells of one span, as arrays. Flags are 0 or 1 and every
   field is 32 bits wide so the loops below vectorise. */
typedef struct {
    int32_t  I_depth[NUM_CELLS], U_depth[NUM_CELLS];
    uint32_t I_id[NUM_CELLS], U_id[NUM_CELLS];
    int32_t  I_visib[NUM_CELLS], I_forward[NUM_CELLS], shad_temp[NUM_CELLS], U_visib[NUM_CELLS], U_shadow[NUM_CELLS];
} cells_t;

/* One plane through the 32 cells. Masks are 0 or -1. */
static void
cells_process(cells_t *restrict s, const cell_control *in, const int32_t *restrict C, uint32_t index)
{
    const int32_t *mux = in->mux_sel ? s->U_depth : C;
    int32_t        gte[NUM_CELLS], lte[NUM_CELLS], ire[NUM_CELLS], ure[NUM_CELLS];
    int            i;

    if (in->delayed_clear_u_id)
        memset(s->U_id, 0, sizeof(s->U_id));

    /* The common case, an edge plane: cells outside it (C < 0) are emptied. */
    if (in->i_load == load_i_further && in->plane_perp && in->u_load == load_u_nop && in->test_shad == test_shad_nop) {
        const int32_t pv = in->plane_visib;
        for (i = 0; i < NUM_CELLS; i++) {
            const int32_t m = C[i] >> 31;
            s->I_visib[i]   = (pv & m) | (s->I_visib[i] & ~m);
            s->I_forward[i] = (1 & m) | (s->I_forward[i] & ~m);
            s->I_depth[i]   = (C[i] & m) | (s->I_depth[i] & ~m);
            s->I_id[i]      = (index & (uint32_t) m) | (s->I_id[i] & ~(uint32_t) m);
        }
        return;
    }

    for (i = 0; i < NUM_CELLS; i++) {
        gte[i] = -(int32_t) (s->I_depth[i] >= mux[i]);
        lte[i] = -(int32_t) (s->I_depth[i] <= mux[i]);
    }

    switch (in->i_load) {
        case load_i:
            for (i = 0; i < NUM_CELLS; i++)
                ire[i] = -1;
            break;
        case load_i_further:
            if (in->plane_perp)
                for (i = 0; i < NUM_CELLS; i++)
                    ire[i] = C[i] >> 31;
            else
                memcpy(ire, gte, sizeof(ire));
            break;
        case load_i_closer:
            for (i = 0; i < NUM_CELLS; i++)
                ire[i] = ~gte[i];
            break;
        case load_i_invis_forw:
            for (i = 0; i < NUM_CELLS; i++)
                ire[i] = -((s->I_visib[i] ^ 1) & s->I_forward[i]);
            break;
        default:
            memset(ire, 0, sizeof(ire));
            break;
    }

    switch (in->u_load) {
        case load_u:
            for (i = 0; i < NUM_CELLS; i++)
                ure[i] = -1;
            break;
        case load_u_closer:
            for (i = 0; i < NUM_CELLS; i++)
                ure[i] = (gte[i] & -s->I_visib[i]) | (s->U_visib[i] - 1);
            break;
        default:
            memset(ure, 0, sizeof(ure));
            break;
    }

    /* Shadow flags (only where U is not reloaded). */
    switch (in->test_shad) {
        case test_shad_closer:
            for (i = 0; i < NUM_CELLS; i++)
                s->shad_temp[i] = (s->shad_temp[i] & ure[i]) | ((~lte[i] & 1) & ~ure[i]);
            break;
        case test_shadow_further:
            for (i = 0; i < NUM_CELLS; i++)
                s->U_shadow[i] = s->U_shadow[i] | (lte[i] & s->shad_temp[i]);
            break;
        case test_light_further:
            for (i = 0; i < NUM_CELLS; i++)
                s->U_shadow[i] = ((lte[i] & s->shad_temp[i]) | (s->U_shadow[i] ^ 1)) ^ 1;
            break;
        default:
            break;
    }

    if (in->u_load != load_u_nop) {
        for (i = 0; i < NUM_CELLS; i++) {
            const int32_t m = ure[i];
            s->U_visib[i]   = (s->I_visib[i] & m) | (s->U_visib[i] & ~m);
            s->U_shadow[i] &= ~m;
            s->shad_temp[i] &= ~m; /* as in the simulator: not carried over on a U load */
            s->U_depth[i] = (s->I_depth[i] & m) | (s->U_depth[i] & ~m);
            s->U_id[i]    = (s->I_id[i] & (uint32_t) m) | (s->U_id[i] & ~(uint32_t) m);
        }
    }
    if (in->i_load != load_i_nop) {
        const int32_t fwd = (in->i_load == load_i || in->i_load == load_i_further);
        const int32_t pv  = in->plane_visib;
        for (i = 0; i < NUM_CELLS; i++) {
            const int32_t m = ire[i];
            s->I_visib[i]   = (pv & m) | (s->I_visib[i] & ~m);
            s->I_forward[i] = (fwd & m) | (s->I_forward[i] & ~m);
            s->I_depth[i]   = (C[i] & m) | (s->I_depth[i] & ~m);
            s->I_id[i]      = (index & (uint32_t) m) | (s->I_id[i] & ~(uint32_t) m);
        }
    }
}

static void
shade_span(ctx_t *c, int xspan, int yline, const cells_t *s)
{
    for (int cl = 0; cl < NUM_CELLS; cl++) {
        if (s->U_id[cl] && x_visible(c, xspan + cl))
            texas(c, xspan + cl, yline, (s->U_id[cl] & 0xfffff) << 1, s->U_shadow[cl], fog_factor(c, s->U_depth[cl]));
    }
}

static int
grow(void **p, uint32_t *cap, uint32_t need, size_t sz)
{
    if (need <= *cap)
        return 0;
    {
        uint32_t n = *cap ? *cap : 256;
        void    *q;
        while (n < need)
            n *= 2;
        q = realloc(*p, n * sz);
        if (!q)
            return -1;
        *p   = q;
        *cap = n;
    }
    return 0;
}

static void
decode_plane(const ctx_t *c, uint32_t pa, plane_t *pl)
{
    union {
        uint32_t u;
        float    f;
    } fa, fb, fc;
    uint32_t w3 = isp_word(c, pa + 3);
    float    max_val;

    fa.u      = isp_word(c, pa);
    fb.u      = isp_word(c, pa + 1);
    fc.u      = isp_word(c, pa + 2);
    pl->instr = (int) (w3 & 0xf);
    pl->index = w3 >> 4;
    if (pl->instr == forw_perp || pl->instr == test_shad_perp || pl->instr == forw_perp_fp || pl->instr == test_shad_perp_fp) {
        /* Perpendicular planes are rescaled into range. */
        max_val = fabsf(fc.f) + 2.0f * 1024.0f * (fabsf(fa.f) + fabsf(fb.f));
        max_val = (max_val != 0.0f) ? approx_recip(max_val) : 1.0f;
    } else
        max_val = 1.0f;
    pl->a = plane_q(fa.f * max_val);
    pl->b = plane_q(fb.f * max_val);
    pl->c = to_int32((double) (2147483648.0f * fc.f * max_val));
}

pcxr_job_t *
pcxr_job_create(void)
{
    return calloc(1, sizeof(pcxr_job_t));
}

void
pcxr_job_destroy(pcxr_job_t *job)
{
    if (!job)
        return;
    free(job->planes);
    free(job->objects);
    free(job->tiles);
    free(job);
}

/* Walk the tile list in order, decoding every tile's planes and noting the
   decoder state each tile starts with. Cheap: no pixel work. */
int
pcxr_prepare(pcxr_job_t *job, const pcxr_frame_t *f, pcxr_result_t *res)
{
    ctx_t    c;
    uint32_t start, cur_word = 0, obj_data;
    int      done_all = 0;
    int      sec_obj = 0, first_obj = 0;
    uint32_t region_count = 0;

    if (!spread8[255])
        init_spread8();
    memset(res, 0, sizeof(*res));
    res->min_x = res->min_y = PCXR_MAX_W;
    res->max_x = res->max_y = -1;
    memset(&c, 0, sizeof(c));
    c.f            = f;
    job->f         = f;
    job->n_planes  = 0;
    job->n_objects = 0;
    job->n_tiles   = 0;

    start = (f->object_offset & ~3u) >> 2;

    while (!done_all) {
        uint32_t xy, safety = 0;
        uint32_t local_start, local_word, obj_off = 1;
        int      last_in_region = 0;
        tile_t  *t;

        if (++region_count > 65536) {
            res->error = 1;
            return -1;
        }

        obj_data = strip_trans_bit(isp_word(&c, start + cur_word));
        while (IS_LINK(obj_data) && safety < 10) {
            start    = obj_data & 0x00ffffff;
            cur_word = 0;
            obj_data = isp_word(&c, start);
            safety++;
        }
        if (safety == 10) {
            res->error = 2;
            return -1;
        }

        xy = isp_word(&c, start + cur_word);
        if (!(xy & REG_COMPULSORY_BITS)) {
            res->error = 3;
            return -1;
        }
        if (grow((void **) &job->tiles, &job->cap_tiles, job->n_tiles + 1, sizeof(tile_t))) {
            res->error = 5;
            return -1;
        }
        t                  = &job->tiles[job->n_tiles++];
        t->xsize           = (int) ((xy & 0x1f) + 1) * NUM_CELLS;
        t->ysize           = (int) (((xy >> 5) & 0x3ff) + 1);
        t->xpos            = (int) ((xy >> 15) & 0x1f) * NUM_CELLS;
        t->ypos            = (int) ((xy >> 20) & 0x3ff);
        t->first_object    = job->n_objects;
        t->prev_intern_out = c.prev_intern_out;
        t->sec_obj         = sec_obj;
        t->first_obj       = first_obj;
        res->tiles++;
        if (t->xpos < res->min_x)
            res->min_x = t->xpos;
        if (t->ypos < res->min_y)
            res->min_y = t->ypos;
        if (t->xpos + t->xsize - 1 > res->max_x)
            res->max_x = t->xpos + t->xsize - 1;
        if (t->ypos + t->ysize - 1 > res->max_y)
            res->max_y = t->ypos + t->ysize - 1;

        /* This tile's objects. */
        local_start = start;
        local_word  = cur_word;
        safety      = 0;
        while (!last_in_region) {
            object_t *ob;
            uint32_t  plane_off;

            obj_data = strip_trans_bit(isp_word(&c, local_start + local_word + obj_off));
            while (IS_LINK(obj_data) && safety < 10) {
                local_start = obj_data & 0x00ffffff;
                local_word  = 0;
                obj_off     = 0;
                obj_data    = isp_word(&c, local_start);
                safety++;
            }
            if (safety == 10) {
                res->error = 2;
                return -1;
            }
            if (grow((void **) &job->objects, &job->cap_objects, job->n_objects + 1, sizeof(object_t))) {
                res->error = 5;
                return -1;
            }
            ob        = &job->objects[job->n_objects++];
            ob->first = job->n_planes;
            ob->count = (obj_data >> 19) & 0x3ff;
            plane_off = obj_data & 0x0007ffff;
            if (grow((void **) &job->planes, &job->cap_planes, job->n_planes + ob->count, sizeof(plane_t))) {
                res->error = 5;
                return -1;
            }
            for (uint32_t pl = 0; pl < ob->count; pl++)
                decode_plane(&c, plane_off + pl * WORDS_PER_PLANE, &job->planes[job->n_planes++]);

            ob->simple = ob->count > 1 && job->planes[ob->first].instr == forw_visib_fp;
            for (uint32_t pl = 1; pl < ob->count && ob->simple; pl++)
                ob->simple = job->planes[ob->first + pl].instr == forw_perp && job->planes[ob->first + pl].index == 0;

            ob->very_last = (obj_data & OBJ_VERY_LAST_PTR) != 0;
            if (ob->very_last)
                last_in_region = 1;
            else {
                uint32_t next = strip_trans_bit(isp_word(&c, local_start + local_word + obj_off + 1));
                if (IS_LINK(next)) {
                    uint32_t s2 = 0;
                    while (IS_LINK(next) && s2 < 10) {
                        next = isp_word(&c, next & 0x00ffffff);
                        s2++;
                    }
                    if (s2 >= 10) {
                        res->error = 2;
                        return -1;
                    }
                }
                last_in_region = (next & REG_COMPULSORY_BITS) != 0;
            }
            ob->last_in_region = last_in_region;
            if (ob->very_last)
                done_all = 1;
            obj_off++;
            if (obj_off > 0x100000) {
                res->error = 4;
                return -1;
            }
        }
        t->n_objects = job->n_objects - t->first_object;
        t->plain     = (t->prev_intern_out == prev_none);
        for (uint32_t p = job->objects[t->first_object].first; p < job->n_planes && t->plain; p++)
            t->plain = job->planes[p].instr == forw_visib_fp || job->planes[p].instr == forw_perp
                       || job->planes[p].instr == begin_trans;

        /* Run the instruction decoder over every span of the tile, without
           pixels, to know the state the next tile starts with. */
        for (int span = 0; span < t->ysize * (t->xsize / NUM_CELLS); span++) {
            int no_object_yet = 1, instr = 0;
            for (uint32_t o = t->first_object; o < job->n_objects; o++) {
                const object_t *ob          = &job->objects[o];
                int             translucent = 0;
                for (uint32_t k = 0; k < ob->count; k++) {
                    instr = job->planes[ob->first + k].instr;
                    if (instr == forw_visib_fp || instr == forw_invis_fp || instr == test_shad_forw_fp) {
                        if (no_object_yet) {
                            sec_obj       = 0;
                            first_obj     = 1;
                            no_object_yet = 0;
                        } else if (first_obj && !sec_obj) {
                            first_obj = 0;
                            sec_obj   = 1;
                        } else
                            sec_obj = 0;
                    }
                    if (!translucent)
                        (void) expand_instruction(&c, instr, 0, sec_obj, 0);
                    if (instr == begin_trans)
                        translucent = 1;
                }
            }
        }

        cur_word = local_word + obj_off;
        start    = local_start;
    }
    return (int) job->n_tiles;
}

/* Draw tiles first, first + step, first + 2 * step, ... Tiles cover
   separate pixels, so several threads can do this at once. */
uint32_t
pcxr_render_tiles(pcxr_job_t *job, uint32_t *rgb, uint32_t first, uint32_t step)
{
    ctx_t         c;
    cells_t       cells;
    int32_t       depth[NUM_CELLS];
    pcxr_result_t res;
    const pcxr_frame_t *f = job->f;

    memset(&c, 0, sizeof(c));
    memset(&res, 0, sizeof(res));
    c.f     = f;
    c.rgb   = rgb;
    c.res   = &res;
    c.fog_r = (f->fog_colour >> 16) & 0xff;
    c.fog_g = (f->fog_colour >> 8) & 0xff;
    c.fog_b = f->fog_colour & 0xff;
    c.fog_on = (f->fog_colour >> 24) & 1; /* FOGCOL bit 24 enables fog (measured) */

    for (uint32_t ti = first; ti < job->n_tiles; ti += step) {
        const tile_t *t        = &job->tiles[ti];
        int           sec_obj   = t->sec_obj;
        int           first_obj = t->first_obj;

        c.prev_intern_out = t->prev_intern_out;

        for (int yline = t->ypos; yline < t->ypos + t->ysize; yline++) {
            for (int xspan = t->xpos; xspan < t->xpos + t->xsize; xspan += NUM_CELLS) {
                int no_object_yet = 1;
                int instr         = 0;

                memset(&cells, 0, sizeof(cells));

                for (uint32_t o = t->first_object; o < t->first_object + t->n_objects; o++) {
                    const object_t *ob          = &job->objects[o];
                    int             translucent = 0;

                    /* A plain convex object with an edge that is outside on
                       every cell of the span adds nothing: its depth plane
                       still runs (it commits the previous object), then the
                       cells are marked empty as the edges would leave them.
                       In a tile of only these instructions nothing reads the
                       depth of an empty cell, so the result is the same. */
                    if (t->plain && ob->simple) {
                        const plane_t *dead = NULL;
                        for (uint32_t k = 1; k < ob->count; k++) {
                            const plane_t *pl  = &job->planes[ob->first + k];
                            int64_t        v0  = pl->a * xspan + pl->b * yline + (int64_t) pl->c * 256;
                            int64_t        v1  = v0 + (NUM_CELLS - 1) * pl->a;
                            const int64_t  lim = -((int64_t) 1 << 39);
                            if (v0 <= -256 && v1 <= -256 && v0 > lim && v1 > lim) {
                                dead = pl;
                                break;
                            }
                        }
                        if (dead) {
                            const plane_t *pl = &job->planes[ob->first];
                            cell_control   wi;
                            int64_t        creg;

                            instr = forw_visib_fp;
                            if (no_object_yet) {
                                sec_obj       = 0;
                                first_obj     = 1;
                                no_object_yet = 0;
                            } else if (first_obj && !sec_obj) {
                                first_obj = 0;
                                sec_obj   = 1;
                            } else
                                sec_obj = 0;
                            wi   = expand_instruction(&c, instr, 0, sec_obj, 0);
                            creg = pl->a * xspan + pl->b * yline + (int64_t) pl->c * 256;
                            for (int cl = 0; cl < NUM_CELLS; cl++) {
                                int64_t v = creg + (int64_t) cl * pl->a;
                                depth[cl] = (int32_t) (uint32_t) ((v + ((v >> 63) & 255)) >> 8);
                            }
                            cells_process(&cells, &wi, depth, pl->index);
                            creg = dead->a * xspan + dead->b * yline + (int64_t) dead->c * 256;
                            for (int cl = 0; cl < NUM_CELLS; cl++) {
                                int64_t v = creg + (int64_t) cl * dead->a;
                                cells.I_depth[cl] = (int32_t) (uint32_t) ((v + ((v >> 63) & 255)) >> 8);
                                cells.I_visib[cl] = 0;
                                cells.I_id[cl]    = 0;
                            }
                            (void) expand_instruction(&c, forw_perp, 0, sec_obj, 0);
                            instr = forw_perp;
                            goto object_done;
                        }
                    }

                    for (uint32_t k = 0; k < ob->count; k++) {
                        const plane_t *pl = &job->planes[ob->first + k];

                        instr = pl->instr;
                        if (instr == forw_visib_fp || instr == forw_invis_fp || instr == test_shad_forw_fp) {
                            if (no_object_yet) {
                                sec_obj       = 0;
                                first_obj     = 1;
                                no_object_yet = 0;
                            } else if (first_obj && !sec_obj) {
                                first_obj = 0;
                                sec_obj   = 1;
                            } else
                                sec_obj = 0;
                        }

                        if (!translucent) {
                            cell_control wi = expand_instruction(&c, instr, 0, sec_obj, 0);
                            /* 40-bit adder: the comparator sees the top 32 bits. */
                            int64_t creg = pl->a * xspan + pl->b * yline + (int64_t) pl->c * 256;
                            /* An edge plane that is inside (>= 0) on all 32
                               cells changes nothing; the values are linear in
                               the cell, so the two ends decide (and must not
                               wrap past 32 bits). */
                            if (wi.i_load == load_i_further && wi.plane_perp && wi.u_load == load_u_nop
                                && wi.test_shad == test_shad_nop && !wi.delayed_clear_u_id) {
                                int64_t end = creg + (NUM_CELLS - 1) * pl->a;
                                if (creg >= 0 && end >= 0 && creg < ((int64_t) 1 << 39) && end < ((int64_t) 1 << 39))
                                    goto next_plane;
                            }
                            if (wi.plane_perp) {
                                /* Edge planes: the inside test sees the sign of
                                   the full 40-bit sum, so a value just below
                                   zero is outside (rounding toward zero would
                                   make it 0, inside). Measured on the m3D. */
                                for (int cl = 0; cl < NUM_CELLS; cl++) {
                                    int64_t v = creg + (int64_t) cl * pl->a;
                                    depth[cl] = (int32_t) (uint32_t) (v >> 8);
                                }
                            } else {
                                for (int cl = 0; cl < NUM_CELLS; cl++) {
                                    int64_t v = creg + (int64_t) cl * pl->a;
                                    /* v / 256, rounding toward zero like the simulator */
                                    depth[cl] = (int32_t) (uint32_t) ((v + ((v >> 63) & 255)) >> 8);
                                }
                            }
                            cells_process(&cells, &wi, depth, pl->index);
                        }
next_plane:
                        if (instr == begin_trans)
                            translucent = 1;
                    }

                    if (translucent)
                        shade_span(&c, xspan, yline, &cells);
object_done:

                    if (ob->last_in_region) {
                        cell_control wi = expand_instruction(&c, instr, 0, 0, 1);
                        memset(depth, 0, sizeof(depth));
                        cells_process(&cells, &wi, depth, 0);
                        shade_span(&c, xspan, yline, &cells);
                    }
                }
            }
        }
    }
    return res.pixels_shaded;
}

void
pcxr_render(const pcxr_frame_t *f, uint32_t *rgb, pcxr_result_t *res)
{
    static pcxr_job_t job; /* single-threaded convenience; reused */

    if (pcxr_prepare(&job, f, res) > 0)
        res->pixels_shaded = pcxr_render_tiles(&job, rgb, 0, 1);
}

/* ---- output ----------------------------------------------------------- */

/* PACKMODE bit 4 dither, measured on the m3D: this offset is added to each
   8-bit channel before it is cut to 5 bits (half of it for the 6-bit green
   of 565), saturating at the top. Matches the card on every pixel in both
   16-bit modes. */
static const uint8_t dither4x4[4][4] = {
    { 7, 3, 6, 2 },
    { 1, 5, 0, 4 },
    { 6, 2, 7, 3 },
    { 0, 4, 1, 5 },
};

static uint32_t
cut(uint32_t c, uint32_t d, int shift, uint32_t max)
{
    c = (c + d) >> shift;
    return (c > max) ? max : c;
}

int
pcxr_pack(uint32_t packmode, uint32_t rgb, int x, int y, uint32_t *out)
{
    uint32_t r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
    uint32_t d = (packmode & 0x10) ? dither4x4[y & 3][x & 3] : 0;

    switch (packmode & 3) {
        case 0: /* 32 bpp */
            *out = rgb & 0xffffff;
            return 4;
        case 1: /* 24 bpp */
            *out = rgb & 0xffffff;
            return 3;
        case 2: /* 16 bpp 565 */
            *out = (cut(r, d, 3, 31) << 11) | (cut(g, d >> 1, 2, 63) << 5) | cut(b, d, 3, 31);
            return 2;
        default: /* 15 bpp 555 */
            *out = (cut(r, d, 3, 31) << 10) | (cut(g, d, 3, 31) << 5) | cut(b, d, 3, 31);
            return 2;
    }
}
