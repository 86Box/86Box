/*
* 86Box    A hypervisor and IBM PC system emulator that specializes in
*          running old operating systems and software designed for IBM
*          PC systems and compatibles from 1981 through fairly recent
*          system designs based on the PCI bus.
*
*          This file is part of the 86Box distribution.
*
*          NV3 PGRAPH pixel pipeline (software): colour expansion, clipping,
*          pattern, ROP3/blend, chroma key and dithering onto up to four
*          destination surfaces.
*
*          The pipeline follows envytools' hardware-verified model of NV3
*          PGRAPH (nvhw/pgraph.c, hwtest/pgraph_rop.cc; MIT licence,
*          Marcelina Kościelnicka).
*
* Authors: Connor Hyde, <mario64crashed@gmail.com>
 *          Marcos Alves, <xeon4d@gmail.com>
*
*          Copyright 2024-2026 Connor Hyde
 *          Copyright 2026 Marcos Alves
*/

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/plat.h>
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>
#include <86box/utils/video_stdlib.h>

/* Colour modes (the same numbers as nv3_pgraph_pixel_format for 0-4) */
#define NV3_MODE_RGB5  0
#define NV3_MODE_RGB8  1
#define NV3_MODE_RGB10 2
#define NV3_MODE_Y8    3
#define NV3_MODE_Y16   4

/* Expand a colour in the object's colour format to the engine's internal
   format: 10 bits per channel, 8-bit alpha, plus the raw index bits. */
nv3_color_expanded_t
nv3_render_expand_color(uint32_t color, nv3_grobj_t grobj)
{
    nv3_color_expanded_t c = { 0 };
    uint32_t             format   = grobj.grobj_0 & 0x07;
    bool                 alpha_en = (grobj.grobj_0 >> NV3_PGRAPH_CTX_SWITCH_ALPHA) & 0x01;
    uint32_t             a;

    switch (format) {
        case nv3_pgraph_pixel_format_r5g5b5:
            a              = ((color >> 15) & 1) * 0xFF;
            c.r            = ((color >> 10) & 0x1F) << 5;
            c.g            = ((color >> 5) & 0x1F) << 5;
            c.b            = (color & 0x1F) << 5;
            c.pixel_format = NV3_MODE_RGB5;
            break;
        default:
        case nv3_pgraph_pixel_format_r8g8b8:
            a              = (color >> 24) & 0xFF;
            c.r            = ((color >> 16) & 0xFF) << 2;
            c.g            = ((color >> 8) & 0xFF) << 2;
            c.b            = (color & 0xFF) << 2;
            c.pixel_format = NV3_MODE_RGB8;
            break;
        case nv3_pgraph_pixel_format_r10g10b10:
            a              = ((color >> 30) & 3) * 0x55;
            c.r            = (color >> 20) & 0x3FF;
            c.g            = (color >> 10) & 0x3FF;
            c.b            = color & 0x3FF;
            c.pixel_format = NV3_MODE_RGB10;
            break;
        case nv3_pgraph_pixel_format_y8:
            a   = (color >> 8) & 0xFF;
            c.r = c.g = c.b = (color & 0xFF) << 2;
            c.pixel_format  = NV3_MODE_Y8;
            break;
        case nv3_pgraph_pixel_format_y16:
            a   = (color >> 24) & 0xFF;
            c.r = c.g = c.b = (color & 0xFFFF) >> 6;
            c.pixel_format  = NV3_MODE_Y16;
            break;
    }

    c.a   = alpha_en ? a : 0xFF;
    c.i16 = color & 0xFFFF;
    return c;
}

/* Expand a pixel read from a surface of format fmt (BPIXEL & 3) */
nv3_color_expanded_t
nv3_render_expand_surface(uint32_t fmt, uint32_t pixel)
{
    nv3_color_expanded_t c = { 0 };

    c.i16 = pixel & 0xFFFF;
    c.a   = 0xFF;
    switch (fmt & 3) {
        case bpixel_fmt_8bit:
            c.pixel_format = NV3_MODE_Y8;
            break;
        case bpixel_fmt_y16:
        case bpixel_fmt_16bit:
            c.pixel_format = ((fmt & 3) == bpixel_fmt_y16) ? NV3_MODE_Y16 : NV3_MODE_RGB5;
            c.r            = ((pixel >> 10) & 0x1F) << 5;
            c.g            = ((pixel >> 5) & 0x1F) << 5;
            c.b            = (pixel & 0x1F) << 5;
            break;
        case bpixel_fmt_32bit:
            c.pixel_format = NV3_MODE_RGB10;
            c.r            = (((pixel >> 16) & 0xFF) << 2) | ((pixel >> 28) & 3);
            c.g            = (((pixel >> 8) & 0xFF) << 2) | ((pixel >> 26) & 3);
            c.b            = ((pixel & 0xFF) << 2) | ((pixel >> 24) & 3);
            break;
    }
    return c;
}

/* The chroma key register's format: A1R10G10B10 with the alpha at bit 30 */
uint32_t
nv3_render_to_chroma(nv3_color_expanded_t expanded)
{
    return (!!expanded.a << 30) | (expanded.r << 20) | (expanded.g << 10) | expanded.b;
}

/* Get a colour for a palette index (RGB888 with 0xFF alpha) */
uint32_t
nv3_render_get_palette_index(uint8_t index)
{
    /* The CLUT is the SVGA DAC (the user DAC registers are routed there) */
    return 0xFF000000 | (nv3->nvbase.svga.pallook[index] & 0xFFFFFF);
}

/* Pattern colours are kept as R10G10B10 plus alpha */
void
nv3_render_set_pattern_color(nv3_color_expanded_t pattern_colour, bool use_color1)
{
    nv3->pgraph.pattern_mono_rgb[use_color1] = (pattern_colour.r << 20) | (pattern_colour.g << 10) | pattern_colour.b;
    nv3->pgraph.pattern_mono_a[use_color1]   = pattern_colour.a;
}

/* Monochrome data in the object's mono format: CGA6 (ctx bit 8) has the first
   pixel in each byte's top bit; LE has it in bit 0. Returns pixel n at bit n. */
uint32_t
nv3_render_expand_mono(uint32_t mono, nv3_grobj_t grobj)
{
    if (!((grobj.grobj_0 >> NV3_PGRAPH_CTX_SWITCH_MONO_FORMAT) & 1))
        return mono;

    uint32_t res = 0;
    for (int i = 0; i < 32; i++)
        res |= ((mono >> i) & 1) << (i ^ 7);
    return res;
}

/* Surface format of the object's first destination surface (surface 3 if none) */
uint32_t
nv3_render_surface_format(nv3_grobj_t grobj)
{
    for (int i = 0; i < NV3_PGRAPH_MAX_BUFFERS; i++) {
        if ((grobj.grobj_0 >> (NV3_PGRAPH_CTX_SWITCH_DST_BUFFER0_ENABLED + i)) & 1)
            return nv3->pgraph.bpixel[i] & 0x7;
    }
    return nv3->pgraph.bpixel[3] & 0x7;
}

/* Bytes per pixel for a surface format */
uint32_t
nv3_render_cpp(uint32_t fmt)
{
    switch (fmt & 3) {
        case bpixel_fmt_8bit:
            return 1;
        case bpixel_fmt_32bit:
            return 4;
        default:
            return 2;
    }
}

/* Read/write a surface pixel of cpp bytes */
uint32_t
nv3_render_read_surface(uint32_t buffer, int32_t x, int32_t y, uint32_t cpp)
{
    uint32_t addr = (nv3->pgraph.boffset[buffer] + (uint32_t) y * nv3->pgraph.bpitch[buffer] + (uint32_t) x * cpp)
        & nv3->nvbase.svga.vram_mask;

    switch (cpp) {
        case 1:
            return nv3->nvbase.svga.vram[addr];
        case 2:
            return *(uint16_t *) &nv3->nvbase.svga.vram[addr & ~1];
        default:
            return *(uint32_t *) &nv3->nvbase.svga.vram[addr & ~3];
    }
}

static void
nv3_render_write_surface(uint32_t addr, uint32_t val, uint32_t cpp)
{
    switch (cpp) {
        case 1:
            nv3->nvbase.svga.vram[addr] = val;
            break;
        case 2:
            addr &= ~1;
            *(uint16_t *) &nv3->nvbase.svga.vram[addr] = val;
            break;
        default:
            addr &= ~3;
            *(uint32_t *) &nv3->nvbase.svga.vram[addr] = val;
            break;
    }
    nv3->nvbase.svga.changedvram[addr >> 12] = changeframecount;
}

/* Clipping: the destination canvas always, the user clip (class 0x05) for
   objects with CLIP set, and the clip rectangles. Maxima are exclusive. */
bool
nv3_render_clip_pass(int32_t x, int32_t y, nv3_grobj_t grobj, bool own_clip)
{
    int32_t cmin_x = nv3->pgraph.dst_canvas_min & 0x7FF;
    int32_t cmin_y = (nv3->pgraph.dst_canvas_min >> 16) & 0x3FFF;
    int32_t cmax_x = nv3->pgraph.dst_canvas_max & 0x7FF;
    int32_t cmax_y = (nv3->pgraph.dst_canvas_max >> 16) & 0x3FFF;

    /* A canvas nobody set up does not clip */
    if (cmax_x > cmin_x && cmax_y > cmin_y) {
        if (x < cmin_x || y < cmin_y || x >= cmax_x || y >= cmax_y)
            return false;
    }

    if (!own_clip && ((grobj.grobj_0 >> NV3_PGRAPH_CTX_SWITCH_USER_CLIP) & 1)) {
        if (x < nv3->pgraph.uclip_min[0] || y < nv3->pgraph.uclip_min[1]
            || x >= nv3->pgraph.uclip_max[0] || y >= nv3->pgraph.uclip_max[1])
            return false;
    }

    uint32_t num = nv3->pgraph.cliprect_ctrl & 3;
    if (num) {
        bool covered = false;

        if (num == 3)
            num = 2;
        for (uint32_t i = 0; i < num; i++) {
            if (x >= (int32_t) (nv3->pgraph.cliprect_min[i] & 0xFFFF) && y >= (int32_t) (nv3->pgraph.cliprect_min[i] >> 16)
                && x < (int32_t) (nv3->pgraph.cliprect_max[i] & 0xFFFF) && y < (int32_t) (nv3->pgraph.cliprect_max[i] >> 16))
                covered = true;
        }
        if (covered == !!(nv3->pgraph.cliprect_ctrl & 0x10))
            return false;
    }

    return true;
}

/* The hardware's ordered dither from 10 to 5 bits (nvhw nv01_pgraph_dither_10to5) */
static uint32_t
nv3_render_dither_10to5(uint32_t val, int32_t x, int32_t y, bool isg)
{
    static const uint8_t tab1[4][4] = {
        { 0, 1, 1, 0 },
        { 0, 0, 1, 0 },
        { 0, 0, 1, 1 },
        { 1, 1, 1, 1 },
    };
    uint32_t step = (val >> 2) & 7;
    int      w    = ((x ^ y) >> 1) & 1;
    int      z    = tab1[(y >> 2) & 3][(x >> 2) & 3] ^ isg;
    int      tx   = x & 1;
    int      ty   = y & 1;
    int      d;

    val >>= 5;
    if (step & 1)
        z ^= w;
    switch (step) {
        default:
        case 0:
            d = 0;
            break;
        case 1:
            d = !tx && !ty && z;
            break;
        case 2:
            d = !(tx ^ ty) && (tx ^ z);
            break;
        case 3:
            d = !(tx ^ ty) && (!tx || z);
            break;
        case 4:
            d = !(tx ^ ty);
            break;
        case 5:
            d = !(tx ^ ty) || (tx && !ty && z);
            break;
        case 6:
            d = !(tx ^ ty) || (ty ^ z);
            break;
        case 7:
            d = tx || !ty || z;
            break;
    }
    if (val < 0x1F)
        val += d;
    return val;
}

/* R10G10B10 to (dithered) R5G5B5 */
static uint32_t
nv3_render_downconvert_r5g5b5(int32_t x, int32_t y, uint32_t val)
{
    uint32_t r = nv3_render_dither_10to5(((val >> 22) & 0xFF) << 2, x, y, false);
    uint32_t g = nv3_render_dither_10to5(((val >> 12) & 0xFF) << 2, x, y, true);
    uint32_t b = nv3_render_dither_10to5(((val >> 2) & 0xFF) << 2, x, y, false);

    return (r << 10) | (g << 5) | b;
}

/* A state colour (R10G10B10) in the working mode */
static uint32_t
nv3_render_state_downconvert(int mode, uint32_t val)
{
    if (mode == NV3_MODE_Y8)
        return (val >> 2) & 0xFF;
    if (mode == NV3_MODE_RGB5 || mode == NV3_MODE_Y16)
        return (((val >> 25) & 0x1F) << 10) | (((val >> 15) & 0x1F) << 5) | ((val >> 5) & 0x1F);
    return val & 0x3FFFFFFF;
}

/* Translate an NV3 patch configuration ("operation") and a ROP3 into the
   ROP3 actually applied to (pattern, source, destination). From envytools'
   nv01_pgraph_xlat_rop. */
uint8_t
nv3_render_translate_nvrop(nv3_grobj_t grobj, uint32_t rop)
{
    uint32_t op = (grobj.grobj_0 >> NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG) & 0x1F;
    uint8_t  res = 0;
    int      swizzle[3];

    if (op == 0x17)
        return 0xCC;
    if (op < 8) {
        swizzle[0] = op >> 0 & 1;
        swizzle[1] = op >> 1 & 1;
        swizzle[2] = op >> 2 & 1;
    } else if (op < 0x10) {
        swizzle[0] = (op >> 0 & 1) + 1;
        swizzle[1] = (op >> 1 & 1) + 1;
        swizzle[2] = (op >> 2 & 1) + 1;
    } else if (op == 0x10) {
        swizzle[0] = 0, swizzle[1] = 1, swizzle[2] = 2;
    } else if (op == 0x11) {
        swizzle[0] = 1, swizzle[1] = 0, swizzle[2] = 2;
    } else if (op == 0x12) {
        swizzle[0] = 0, swizzle[1] = 2, swizzle[2] = 1;
    } else if (op == 0x13) {
        swizzle[0] = 2, swizzle[1] = 0, swizzle[2] = 1;
    } else if (op == 0x14) {
        swizzle[0] = 1, swizzle[1] = 2, swizzle[2] = 0;
    } else if (op == 0x15) {
        swizzle[0] = 2, swizzle[1] = 1, swizzle[2] = 0;
    } else
        return 0xCC;

    if (op == 0) {
        if (rop & 0x01)
            res |= 0x11;
        if (rop & 0x16)
            res |= 0x44;
        if (rop & 0x68)
            res |= 0x22;
        if (rop & 0x80)
            res |= 0x88;
    } else if (op == 0xF) {
        if (rop & 0x01)
            res |= 0x03;
        if (rop & 0x16)
            res |= 0x0C;
        if (rop & 0x68)
            res |= 0x30;
        if (rop & 0x80)
            res |= 0xC0;
    } else {
        for (int i = 0; i < 8; i++) {
            int s0 = i >> swizzle[0] & 1;
            int s1 = i >> swizzle[1] & 1;
            int s2 = i >> swizzle[2] & 1;
            int s  = s2 << 2 | s1 << 1 | s0;
            if (rop >> s & 1)
                res |= 1 << i;
        }
    }
    return res;
}

/* ROP3 on bit vectors: bit (p << 2 | s << 1 | d) of the ROP gives the result */
static uint32_t
nv3_render_do_rop(uint8_t rop, uint32_t dst, uint32_t src, uint32_t pat)
{
    uint32_t res = 0;

    for (int i = 0; i < 8; i++) {
        if (rop & (1 << i))
            res |= ((i & 4) ? pat : ~pat) & ((i & 2) ? src : ~src) & ((i & 1) ? dst : ~dst);
    }
    return res;
}

static uint32_t
nv3_render_blend_factor(uint32_t alpha, uint32_t beta)
{
    if (beta == 0xFF)
        return alpha;
    if (alpha == 0xFF)
        return beta;
    alpha >>= 4;
    beta >>= 3;
    return (alpha * beta) >> 1;
}

static uint32_t
nv3_render_do_blend(uint8_t factor, uint32_t dst, uint32_t src)
{
    factor >>= 3;
    if (factor == 0x1F)
        return src;
    if (!factor)
        return dst;

    uint32_t res = 0;
    for (int sh = 0; sh <= 20; sh += 10) {
        uint32_t d = (dst >> sh) & 0x3FF;
        uint32_t s = (src >> sh) & 0x3FF;
        res |= ((((d >> 2) * (0x20 - factor) + (s >> 2) * factor) >> 3) & 0x3FF) << sh;
    }
    return res;
}

/* One pixel through pattern, ROP or blend and the chroma key. pixel is what
   the surface holds; returns what it should hold. (nvhw nv03_pgraph_rop) */
static uint32_t
nv3_render_rop_pixel(int32_t x, int32_t y, uint32_t pixel, nv3_color_expanded_t s, uint32_t ctx, uint32_t fmt)
{
    bool     bd       = (ctx >> 9) & 1;
    uint32_t op       = (ctx >> NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG) & 0x1F;
    bool     blend_en = op > 0x17;
    int      mode;
    uint32_t src, dst, mask;

    fmt &= 3;
    if (fmt == bpixel_fmt_8bit)
        mode = NV3_MODE_Y8;
    else if (fmt == bpixel_fmt_32bit)
        mode = NV3_MODE_RGB10;
    else if (fmt == bpixel_fmt_y16 && s.pixel_format == NV3_MODE_Y16)
        mode = NV3_MODE_Y16;
    else if (s.pixel_format == NV3_MODE_RGB5)
        mode = NV3_MODE_RGB5;
    else
        mode = NV3_MODE_RGB10;

    if (!s.a)
        return pixel;

    switch (mode) {
        case NV3_MODE_Y8:
            mask = 0xFF;
            src  = s.i16 & 0xFF;
            dst  = pixel & mask;
            break;
        case NV3_MODE_Y16:
            mask = 0xFFFF;
            src  = s.i16;
            dst  = pixel & mask;
            break;
        case NV3_MODE_RGB5:
            mask = 0x7FFF;
            src  = ((s.r >> 5) << 10) | ((s.g >> 5) << 5) | (s.b >> 5);
            dst  = pixel & mask;
            break;
        default:
            mask = 0x3FFFFFFF;
            src  = (s.r << 20) | (s.g << 10) | s.b;
            if (fmt == bpixel_fmt_32bit) {
                dst = ((pixel >> 24) & 3) | ((pixel & 0xFF) << 2)
                    | (((pixel >> 26) & 3) << 10) | (((pixel >> 8) & 0xFF) << 12)
                    | (((pixel >> 28) & 3) << 20) | (((pixel >> 16) & 0xFF) << 22);
            } else
                dst = (((pixel >> 10) & 0x1F) << 25) | (((pixel >> 5) & 0x1F) << 15) | ((pixel & 0x1F) << 5);
            break;
    }

    /* Pattern */
    uint32_t bidx;
    switch (nv3->pgraph.pattern_shape & 3) {
        default:
        case NV3_PATTERN_SHAPE_8X8:
            bidx = (x & 7) | ((y & 7) << 3);
            break;
        case NV3_PATTERN_SHAPE_64X1:
            bidx = x & 0x3F;
            break;
        case NV3_PATTERN_SHAPE_1X64:
            bidx = y & 0x3F;
            break;
        case 3:
            bidx = (y & 0x3F) | (x & 0x3C);
            break;
    }
    uint32_t bit = (nv3->pgraph.pattern_mono_bitmap[bidx >> 5] >> (bidx & 0x1F)) & 1;
    uint32_t pat = nv3_render_state_downconvert(mode, nv3->pgraph.pattern_mono_rgb[bit]);
    uint8_t  pa  = nv3->pgraph.pattern_mono_a[bit];

    if (op >= 9 && op < 0x16 && !pa)
        return pixel;

    uint32_t ropres;
    if (!blend_en) {
        uint8_t rop = nv3_render_translate_nvrop((nv3_grobj_t) { .grobj_0 = ctx }, nv3->pgraph.rop);

        /* write-only ROPs leave the pixel alone for "D" */
        if (rop == 0xAA && ((nv3->pgraph.debug_0 >> NV3_PGRAPH_DEBUG_0_WRITE_ONLY_ROPS_2D) & 1) && !((ctx >> NV3_PGRAPH_CTX_SWITCH_PLANE_MASK) & 1))
            return pixel;
        ropres = nv3_render_do_rop(rop, dst, src, pat) & mask;
        if ((ctx >> NV3_PGRAPH_CTX_SWITCH_CHROMA_KEY) & 1) {
            uint32_t chr = nv3_render_state_downconvert(mode, nv3->pgraph.chroma_key);
            if (chr == ropres && ((nv3->pgraph.chroma_key >> 30) & 1))
                return pixel;
        }
    } else {
        uint8_t  beta = (nv3->pgraph.beta_factor >> 23) & 0xFF;
        uint32_t factor;

        if (op == 0x19) {
            if (!beta)
                return pixel;
            factor = nv3_render_blend_factor(s.a, beta);
        } else if (op == 0x1A) {
            if (beta == 0xFF)
                return pixel;
            factor = nv3_render_blend_factor(s.a, 0xFF - beta);
        } else if (op == 0x1D)
            factor = s.a;
        else
            return pixel;

        if (mode == NV3_MODE_RGB5) {
            src = (((src >> 10) & 0x1F) << 25) | (((src >> 5) & 0x1F) << 15) | ((src & 0x1F) << 5);
            dst = (((dst >> 10) & 0x1F) << 25) | (((dst >> 5) & 0x1F) << 15) | ((dst & 0x1F) << 5);
        } else {
            src = (src & ~0x00300C03) | ((src >> 8) & 3) | (((src >> 18) & 3) << 10) | (((src >> 28) & 3) << 20);
        }
        ropres = nv3_render_do_blend(factor, dst, src);
    }

    if (fmt != bpixel_fmt_32bit && (mode == NV3_MODE_RGB10 || blend_en))
        ropres = nv3_render_downconvert_r5g5b5(x, y, ropres);

    switch (fmt) {
        case bpixel_fmt_8bit:
            return ropres & 0xFF;
        case bpixel_fmt_y16:
        case bpixel_fmt_16bit:
            if (mode == NV3_MODE_Y16)
                return ropres & 0xFFFF;
            return (bd << 15) | (ropres & 0x7FFF);
        default:
            return ((uint32_t) bd << 31)
                | (((ropres >> 20) & 3) << 28) | (((ropres >> 10) & 3) << 26) | ((ropres & 3) << 24)
                | (((ropres >> 22) & 0xFF) << 16) | (((ropres >> 12) & 0xFF) << 8) | ((ropres >> 2) & 0xFF);
    }
}

/* Draw one pixel of source colour s at (x, y) on every destination surface
   the object writes. own_clip: the class clips with its own rectangle (GDI,
   SIFM, SIFC), so the user clip does not apply; the canvas still does. */
void
nv3_render_pixel_ex(int32_t x, int32_t y, nv3_color_expanded_t s, nv3_grobj_t grobj, bool own_clip)
{
    uint32_t ctx = grobj.grobj_0;

    if (x < 0 || y < 0)
        return;
    if (!nv3_render_clip_pass(x, y, grobj, own_clip))
        return;

    uint32_t fmt = nv3_render_surface_format(grobj);
    uint32_t cpp = nv3_render_cpp(fmt);

    for (uint32_t j = 0; j < NV3_PGRAPH_MAX_BUFFERS; j++) {
        if (!((ctx >> (NV3_PGRAPH_CTX_SWITCH_DST_BUFFER0_ENABLED + j)) & 1))
            continue;

        uint32_t addr  = (nv3->pgraph.boffset[j] + (uint32_t) y * nv3->pgraph.bpitch[j] + (uint32_t) x * cpp) & nv3->nvbase.svga.vram_mask;
        uint32_t pixel = nv3_render_read_surface(j, x, y, cpp);
        uint32_t res   = nv3_render_rop_pixel(x, y, pixel, s, ctx, fmt);

        if (res != pixel)
            nv3_render_write_surface(addr, res, cpp);
    }
}

void
nv3_render_pixel(int32_t x, int32_t y, nv3_color_expanded_t s, nv3_grobj_t grobj)
{
    nv3_render_pixel_ex(x, y, s, grobj, false);
}

/* Draw a pixel given a colour in the object's own colour format */
void
nv3_render_write_pixel(nv3_coord_16_t position, uint32_t color, nv3_grobj_t grobj)
{
    nv3_render_pixel((int16_t) position.x, (int16_t) position.y, nv3_render_expand_color(color, grobj), grobj);
}
