/*
* 86Box    A hypervisor and IBM PC system emulator that specializes in
*          running old operating systems and software designed for IBM
*          PC systems and compatibles from 1981 through fairly recent
*          system designs based on the PCI bus.
*
*          This file is part of the 86Box distribution.
*
*          NV3 3D: the Direct3D 5 textured triangle (class 0x17) and the
*          Z-buffered point (class 0x18).
*
*          State layout, vertex handling, depth conversion, comparisons,
*          write enables and the point blend follow envytools' hardware
*          tests (hwtest/pgraph_class_d3d0.cc, nvhw/pgraph_d3d_nv3.c); the
*          triangle setup and texturing are this file's own.
*
* Authors: Connor Hyde, <mario64crashed@gmail.com>
 *          Marcos Alves, <xeon4d@gmail.com>
*
*          Copyright 2024-2026 Connor Hyde
 *          Copyright 2026 Marcos Alves
*/

#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/plat_unused.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

uint32_t nv3_dma_map_pages(uint32_t inst, uint32_t offset, uint32_t *addr, uint32_t pages, uint32_t *lin);

/* D3D CONTROL0 (method 0x314) fields of class 0x17, the D3D triangle with zeta buffer */
#define NV3_D3D_CONFIG_INTERP(c)      ((c) & 3)          /* 0 ZOH, 1 ZOH_MS, 2 FOH */
#define NV3_D3D_CONFIG_WRAP_U(c)      (((c) >> 4) & 3)   /* 0 cylindrical, 1 wrap, 2 mirror, 3 clamp */
#define NV3_D3D_CONFIG_WRAP_V(c)      (((c) >> 6) & 3)
#define NV3_D3D_CONFIG_ARGB(c)        (((c) >> 8) & 1)   /* 0 X8R8G8B8 (vertex alpha ignored), 1 A8R8G8B8 */
#define NV3_D3D_CONFIG_SRC_COLOR(c)   (((c) >> 10) & 3)  /* 0 normal, 1 colour inverse, 2 alpha inverse, 3 alpha one */
#define NV3_D3D_CONFIG_CULL(c)        (((c) >> 12) & 3)  /* 1 none, 2/3 one winding each */
#define NV3_D3D_CONFIG_ZLINEAR(c)     (((c) >> 15) & 1)  /* 0 screen z, 1 linear (1/m) */
#define NV3_D3D_CONFIG_ZFUNC(c)       (((c) >> 16) & 0xF)
#define NV3_D3D_CONFIG_ZWRITE(c)      (((c) >> 20) & 7)
#define NV3_D3D_CONFIG_CWRITE(c)      (((c) >> 24) & 7)
#define NV3_D3D_CONFIG_ADD(c)         (((c) >> 28) & 1)
#define NV3_D3D_CONFIG_BETA_DST(c)    (((c) >> 29) & 1)
#define NV3_D3D_CONFIG_DST_ZERO(c)    (((c) >> 30) & 1)
#define NV3_D3D_CONFIG_SRC_ZERO(c)    (((c) >> 31) & 1)

/* per-channel primitive state (nv3_prim_state_t, nv3_d3d_state_t) */
#define nv3_d3d (nv3->pgraph.prim.d3d)

/* Comparison and write-enable functions (nvhw nv03_pgraph_d3d_cmp/wren) */
static bool
nv3_d3d_cmp(int func, uint32_t a, uint32_t b)
{
    switch (func) {
        case 1:
            return false;
        case 2:
            return a < b;
        case 3:
            return a == b;
        case 0:
        case 4:
            return a <= b;
        case 5:
            return a > b;
        case 6:
            return a != b;
        case 7:
            return a >= b;
        default:
            return true;
    }
}

static bool
nv3_d3d_wren(int func, bool zeta_test, bool alpha_test)
{
    switch (func) {
        case 1:
            return alpha_test;
        case 2:
            return alpha_test && zeta_test;
        case 3:
            return zeta_test;
        case 4:
            return true;
        default:
            return false;
    }
}

/* Depth as the zeta buffer keeps it: 16 bits, 0 = near (D3D's sz scaled). envytools' convert_z
   holds the 24-bit vertex z inverted, but with the compare functions as NV3DD32.DLL passes them
   (D3DCMP values: LESSEQUAL = 4, a <= b with a the new value) the inverted form lets the farther
   surface win: 3DMark 99 Race drew its sky over the track. Not inverted, it renders right. */
static uint32_t
nv3_d3d_zeta(double z)
{
    if (!(z > 0.0))
        return 0;
    if (z >= 1.0)
        return 0xFFFF;
    return (uint32_t) (z * 65535.0 + 0.5);
}

/* ---- textures ---- */

static void
nv3_d3d_texture_changed(void)
{
    nv3_d3d.tex_mapped = false;
}

static uint16_t
nv3_d3d_read_tex16(UNUSED(nv3_grobj_t grobj), uint32_t byte)
{
    if (!nv3_d3d.tex_mapped) {
        nv3_d3d.tex_target = nv3_dma_map_pages(nv3->pgraph.dma_settings & 0xFFFF, nv3_d3d.tex_offset, nv3_d3d.tex_pages, 64, &nv3_d3d.tex_lin);
        nv3_d3d.tex_mapped = true;
    }

    uint32_t lin  = nv3_d3d.tex_lin + byte;
    uint32_t page = (lin >> 12) - (nv3_d3d.tex_lin >> 12);
    if (page >= 64)
        return 0;
    uint32_t addr = nv3_d3d.tex_pages[page] | (lin & 0xFFE);

    if (nv3_d3d.tex_target == NV3_DMA_TARGET_NODE_VRAM)
        return *(uint16_t *) &nv3->nvbase.svga.vram[addr & nv3->nvbase.svga.vram_mask & ~1];

    uint16_t val = 0;
    dma_bm_read(addr, (uint8_t *) &val, 2, 2);
    return val;
}

/* Textures are stored swizzled (Morton order): bit i of s goes to bit 2i of the texel index and
   bit i of t to bit 2i+1 -- the order the Win9x driver (NV3DD32.DLL) writes them in */
static uint32_t
nv3_d3d_swizzle(uint32_t s, uint32_t t)
{
    uint32_t idx = 0;

    for (int i = 0; i < 11; i++)
        idx |= (((s >> i) & 1) << (2 * i)) | (((t >> i) & 1) << (2 * i + 1));
    return idx;
}

/* A texel as A8R8G8B8 */
static uint32_t
nv3_d3d_texel(nv3_grobj_t grobj, uint32_t level_offset, UNUSED(uint32_t size), int32_t s, int32_t t)
{
    uint32_t fmt = (nv3_d3d.tex_format >> 20) & 0xF;
    uint16_t p   = nv3_d3d_read_tex16(grobj, level_offset + nv3_d3d_swizzle((uint32_t) s, (uint32_t) t) * 2);
    uint32_t a, r, g, b;


    switch (fmt) {
        case 0: /* A1R5G5B5 */
        default:
            a = (p & 0x8000) ? 0xFF : 0;
            r = (p >> 10) & 0x1F;
            g = (p >> 5) & 0x1F;
            b = p & 0x1F;
            r = (r << 3) | (r >> 2);
            g = (g << 3) | (g >> 2);
            b = (b << 3) | (b >> 2);
            if (fmt == 1) /* X1R5G5B5 */
                a = 0xFF;
            break;
        case 2: /* A4R4G4B4 */
            a = ((p >> 12) & 0xF) * 0x11;
            r = ((p >> 8) & 0xF) * 0x11;
            g = ((p >> 4) & 0xF) * 0x11;
            b = (p & 0xF) * 0x11;
            break;
        case 3: /* R5G6B5 */
            a = 0xFF;
            r = (p >> 11) & 0x1F;
            g = (p >> 5) & 0x3F;
            b = p & 0x1F;
            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
            break;
    }
    /* colour key: only the alpha goes to zero; filtering still mixes in the
       key colour, which is why NV3DD32 rewrites keyed texels in its textures (colour key fringe fix) */
    if (((nv3_d3d.tex_format >> 16) & 0xF) && p == (nv3_d3d.tex_format & 0xFFFF))
        a = 0;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static int32_t
nv3_d3d_wrap(int32_t c, int32_t size, int mode)
{
    switch (mode) {
        case 2: /* mirror */
        {
            int32_t period = size * 2;
            c %= period;
            if (c < 0)
                c += period;
            return (c >= size) ? (period - 1 - c) : c;
        }
        case 3: /* clamp */
            return (c < 0) ? 0 : ((c >= size) ? size - 1 : c);
        default: /* wrap; cylindrical (already moved across the seam per triangle) */
            return c & (size - 1);
    }
}

/* Sample at (u, v) in texture space (0..1 across the texture) from mip level `level` */
static uint32_t
nv3_d3d_sample(nv3_grobj_t grobj, float u, float v, int level)
{
    int      max_log = (nv3_d3d.tex_format >> 28) & 0xF;
    int      min_log = (nv3_d3d.tex_format >> 24) & 0xF;
    uint32_t offset  = 0;

    if (max_log > 11)
        max_log = 11;
    if (min_log > max_log)
        min_log = max_log;
    if (level > max_log - min_log)
        level = max_log - min_log;
    for (int l = 0; l < level; l++)
        offset += (1u << (max_log - l)) * (1u << (max_log - l)) * 2;

    int32_t size  = 1 << (max_log - level);
    int     wu    = NV3_D3D_CONFIG_WRAP_U(nv3->pgraph.d3d_config);
    int     wv    = NV3_D3D_CONFIG_WRAP_V(nv3->pgraph.d3d_config);
    float   fu    = u * size;
    float   fv    = v * size;

    /* The kernels are centred on each texel's origin, its upper-left corner (ZOH a box, FOH a
       tent); ZOH_MS moves the origin to the texel centre. NV3DD32 picks ZOH for point-sampled
       textures and, for FOH, takes 0.4375 texel off u and v itself -- so no half-texel shift
       here. */
    if (NV3_D3D_CONFIG_INTERP(nv3->pgraph.d3d_config) != 2) {
        if (NV3_D3D_CONFIG_INTERP(nv3->pgraph.d3d_config) == 0) {
            fu += 0.5f;
            fv += 0.5f;
        }
        int32_t s = nv3_d3d_wrap((int32_t) floorf(fu), size, wu);
        int32_t t = nv3_d3d_wrap((int32_t) floorf(fv), size, wv);
        return nv3_d3d_texel(grobj, offset, size, s, t);
    }

    /* first order hold: bilinear between the four texels around (fu, fv) */
    int32_t  s0 = (int32_t) floorf(fu);
    int32_t  t0 = (int32_t) floorf(fv);
    uint32_t wx = (uint32_t) ((fu - s0) * 256.0f);
    uint32_t wy = (uint32_t) ((fv - t0) * 256.0f);
    uint32_t c[4];
    c[0] = nv3_d3d_texel(grobj, offset, size, nv3_d3d_wrap(s0, size, wu), nv3_d3d_wrap(t0, size, wv));
    c[1] = nv3_d3d_texel(grobj, offset, size, nv3_d3d_wrap(s0 + 1, size, wu), nv3_d3d_wrap(t0, size, wv));
    c[2] = nv3_d3d_texel(grobj, offset, size, nv3_d3d_wrap(s0, size, wu), nv3_d3d_wrap(t0 + 1, size, wv));
    c[3] = nv3_d3d_texel(grobj, offset, size, nv3_d3d_wrap(s0 + 1, size, wu), nv3_d3d_wrap(t0 + 1, size, wv));

    uint32_t res = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t top = ((c[0] >> sh) & 0xFF) * (256 - wx) + ((c[1] >> sh) & 0xFF) * wx;
        uint32_t bot = ((c[2] >> sh) & 0xFF) * (256 - wx) + ((c[3] >> sh) & 0xFF) * wx;
        res |= (((top * (256 - wy) + bot * wy) >> 16) & 0xFF) << sh;
    }
    return res;
}

/* Debug ("dev nv3 tex"): log each mip level of the texture -- offset, size, mean colour, first texels */
int nv3_d3d_tex_dump;

static void
nv3_d3d_dump_texture(nv3_grobj_t grobj)
{
    int      max_log = (nv3_d3d.tex_format >> 28) & 0xF;
    int      min_log = (nv3_d3d.tex_format >> 24) & 0xF;
    uint32_t offset  = 0;

    always_log("nv3: d3d tex %08x format %08x levels %d..%d%c", nv3_d3d.tex_offset, nv3_d3d.tex_format, max_log, min_log, 10);
    for (int l = max_log; l >= min_log && l >= 0 && l <= 11; l--) {
        uint32_t size = 1u << l;
        uint64_t sum[4] = { 0 };
        for (uint32_t t = 0; t < size; t++)
            for (uint32_t s = 0; s < size; s++) {
                uint32_t c = nv3_d3d_texel(grobj, offset, size, s, t);
                for (int i = 0; i < 4; i++)
                    sum[i] += (c >> (24 - 8 * i)) & 0xFF;
            }
        uint32_t n = size * size;
        always_log("nv3:   level %2u offset %06x mean argb %02x %02x %02x %02x | %04x %04x %04x %04x %04x %04x %04x %04x%c", size, offset,
                   (uint32_t) (sum[0] / n), (uint32_t) (sum[1] / n), (uint32_t) (sum[2] / n), (uint32_t) (sum[3] / n),
                   nv3_d3d_read_tex16(grobj, offset), nv3_d3d_read_tex16(grobj, offset + 2), nv3_d3d_read_tex16(grobj, offset + 4),
                   nv3_d3d_read_tex16(grobj, offset + 6), nv3_d3d_read_tex16(grobj, offset + 8), nv3_d3d_read_tex16(grobj, offset + 10),
                   nv3_d3d_read_tex16(grobj, offset + 12), nv3_d3d_read_tex16(grobj, offset + 14), 10);
        offset += n * 2;
    }
}

/* ---- pixel output ---- */

static uint32_t
nv3_d3d_dither_8to5(uint32_t v, int32_t x, int32_t y, bool dither)
{
    static const uint8_t bayer[4][4] = {
        { 0, 8, 2, 10 },
        { 12, 4, 14, 6 },
        { 3, 11, 1, 9 },
        { 15, 7, 13, 5 },
    };

    if (dither) {
        v += bayer[y & 3][x & 3] >> 1;
        if (v > 0xFF)
            v = 0xFF;
    }
    return v >> 3;
}

/* Blend a source colour (A8R8G8B8) with the R5G5B5 pixel per D3D_CONFIG,
   as the point class does it (nvhw nv03_pgraph_zpoint_rop), in 8 bits */
static uint16_t
nv3_d3d_blend(uint32_t config, uint32_t src, uint16_t dst, int32_t x, int32_t y)
{
    bool     dither = (nv3->pgraph.debug_3 >> NV3_PGRAPH_DEBUG_3_DITHER) & 1;
    uint32_t s[3], d[3], o[3];
    uint32_t sa = src >> 24;

    for (int i = 0; i < 3; i++) {
        uint32_t d5 = (dst >> (10 - i * 5)) & 0x1F;
        s[i]        = (src >> (16 - i * 8)) & 0xFF;
        d[i]        = (d5 << 3) | (d5 >> 2);
    }

    if (!NV3_D3D_CONFIG_ADD(config)) {
        for (int i = 0; i < 3; i++) {
            uint32_t beta = NV3_D3D_CONFIG_BETA_DST(config) ? d[i] : sa;
            uint32_t sv   = NV3_D3D_CONFIG_SRC_ZERO(config) ? 0 : s[i];
            uint32_t dv   = NV3_D3D_CONFIG_DST_ZERO(config) ? 0 : d[i];
            o[i]          = (dv * (255 - beta) + sv * beta + 127) / 255;
        }
    } else {
        for (int i = 0; i < 3; i++) {
            o[i] = s[i] + d[i];
            if (o[i] > 0xFF)
                o[i] = 0xFF;
        }
    }

    return (nv3_d3d_dither_8to5(o[0], x, y, dither) << 10) | (nv3_d3d_dither_8to5(o[1], x, y, dither) << 5)
        | nv3_d3d_dither_8to5(o[2], x, y, dither);
}

/* Debug: "dev nv3 d3d N" logs the next N triangles and what they wrote */
uint32_t        nv3_d3d_trace_left;
static uint32_t nv3_d3d_frag_in, nv3_d3d_frag_clip, nv3_d3d_frag_written;

/* One fragment: alpha and Z tests, write enables, blend, write. `test_alpha` is what the alpha
   test (CONTROL1) compares: the combined texture x vertex alpha, before SRCCOLOR. The
   "ALPHA" write conditions check the alpha that reaches the blender (in `color`) for non-zero at
   the blender's 4-bit precision, and a failed alpha test writes neither colour nor zeta (envytools
   hwtest pgraph_rop.cc, class 0x18). NV3DD32 relies on the former: it says colour keys only work
   with a blend mode that does not force the alpha to 1. */
static void
nv3_d3d_fragment(nv3_grobj_t grobj, uint32_t config, uint32_t alpha_ctl, int32_t x, int32_t y, uint32_t color, uint32_t test_alpha, uint32_t zeta)
{
    uint32_t ctx = grobj.grobj_0;

    nv3_d3d_frag_in++;
    if (x < 0 || y < 0 || !nv3_render_clip_pass(x, y, grobj, false)) {
        nv3_d3d_frag_clip++;
        return;
    }

    if (!nv3_d3d_cmp((alpha_ctl >> 8) & 0xF, test_alpha, alpha_ctl & 0xFF))
        return;

    bool     zeta_en  = (ctx >> NV3_PGRAPH_CTX_SWITCH_Z_WRITE) & 1;
    uint32_t zaddr    = (nv3->pgraph.boffset[3] + (uint32_t) y * nv3->pgraph.bpitch[3] + (uint32_t) x * 2) & nv3->nvbase.svga.vram_mask & ~1;
    uint32_t zcur     = zeta_en ? *(uint16_t *) &nv3->nvbase.svga.vram[zaddr] : 0;
    bool     ztest    = !zeta_en || nv3_d3d_cmp(NV3_D3D_CONFIG_ZFUNC(config), zeta, zcur);
    bool     anz      = (color >> 28) != 0;
    if (nv3_d3d_wren(NV3_D3D_CONFIG_CWRITE(config), ztest, anz)) {
        for (uint32_t j = 0; j < 3; j++) { /* surface 3 is the zeta buffer */
            if (!((ctx >> (NV3_PGRAPH_CTX_SWITCH_DST_BUFFER0_ENABLED + j)) & 1))
                continue;
            uint32_t addr = (nv3->pgraph.boffset[j] + (uint32_t) y * nv3->pgraph.bpitch[j] + (uint32_t) x * 2) & nv3->nvbase.svga.vram_mask & ~1;
            uint16_t *p   = (uint16_t *) &nv3->nvbase.svga.vram[addr];
            *p            = (*p & 0x8000) | nv3_d3d_blend(config, color, *p, x, y);
            nv3_d3d_frag_written++;
            nv3->nvbase.svga.changedvram[addr >> 12] = changeframecount;
        }
    }
    if (zeta_en && nv3_d3d_wren(NV3_D3D_CONFIG_ZWRITE(config), ztest, anz)) {
        *(uint16_t *) &nv3->nvbase.svga.vram[zaddr] = zeta;
        nv3->nvbase.svga.changedvram[zaddr >> 12]    = changeframecount;
    }
}

/* ---- triangles ---- */

static uint32_t
nv3_d3d_lerp_color(const nv3_d3d_vertex_t *v[3], const float w[3])
{
    uint32_t res = 0;

    for (int sh = 0; sh < 32; sh += 8) {
        float c = ((v[0]->color >> sh) & 0xFF) * w[0] + ((v[1]->color >> sh) & 0xFF) * w[1] + ((v[2]->color >> sh) & 0xFF) * w[2];
        int   i = (int) (c + 0.5f);
        res |= (uint32_t) (i < 0 ? 0 : (i > 255 ? 255 : i)) << sh;
    }
    return res;
}

static uint32_t
nv3_d3d_mul(uint32_t a, uint32_t b)
{
    uint32_t res = 0;

    for (int sh = 0; sh < 32; sh += 8)
        res |= ((((a >> sh) & 0xFF) * ((b >> sh) & 0xFF) + 127) / 255) << sh;
    return res;
}

/* Cylindrical wrapping (CONTROL0 WRAP_U/V): when the triangle's coordinates cross the
   texture seam, 1.0 is added to the vertices on the low side so it takes the short way round */
static void
nv3_d3d_cylinder(float *c0, float *c1, float *c2)
{
    float *c[3] = { c0, c1, c2 };

    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (*c[j] - *c[i] > 0.5f)
                *c[i] += 1.0f;
}

static void
nv3_d3d_triangle(nv3_grobj_t grobj, const nv3_d3d_vertex_t *a_in, const nv3_d3d_vertex_t *b_in, const nv3_d3d_vertex_t *c_in)
{
    nv3_d3d_vertex_t        va = *a_in, vb = *b_in, vc = *c_in;
    const nv3_d3d_vertex_t *a = &va, *b = &vb, *c = &vc;
    const nv3_d3d_vertex_t *v[3] = { a, b, c };
    uint32_t                config = nv3->pgraph.d3d_config;

    if (NV3_D3D_CONFIG_WRAP_U(config) == 0)
        nv3_d3d_cylinder(&va.u, &vb.u, &vc.u);
    if (NV3_D3D_CONFIG_WRAP_V(config) == 0)
        nv3_d3d_cylinder(&va.v, &vb.v, &vc.v);
    float                   area   = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    double                  darea  = ((double) b->x - a->x) * ((double) c->y - a->y) - ((double) b->y - a->y) * ((double) c->x - a->x);
    int                     cull   = NV3_D3D_CONFIG_CULL(config);

    if (area == 0.0f || isnan(area))
        return;
    /* y points down: area > 0 is clockwise on screen */
    if ((cull == 2 && area < 0.0f) || (cull == 3 && area > 0.0f))
        return;

    float minx = fminf(a->x, fminf(b->x, c->x));
    float maxx = fmaxf(a->x, fmaxf(b->x, c->x));
    float miny = fminf(a->y, fminf(b->y, c->y));
    float maxy = fmaxf(a->y, fmaxf(b->y, c->y));
    int   x0   = (int) fmaxf(ceilf(minx), 0.0f);
    int   x1   = (int) fminf(floorf(maxx), 2047.0f);
    int   y0   = (int) fmaxf(ceilf(miny), 0.0f);
    int   y1   = (int) fminf(floorf(maxy), 2047.0f);

    /* mip level per pixel from the screen-space derivatives of the perspective-correct texture
       coordinates: u*q, v*q and q (q = 1/w) are linear in screen space */
    int   max_log = (nv3_d3d.tex_format >> 28) & 0xF;
    float tsize   = (float) (1 << (max_log > 11 ? 11 : max_log));
    float pq[3], pu[3], pv[3];
    bool  persp   = (a->rhw > 0.0f) && isfinite(a->rhw) && (b->rhw > 0.0f) && isfinite(b->rhw) && (c->rhw > 0.0f) && isfinite(c->rhw);
    for (int i = 0; i < 3; i++) {
        pq[i] = persp ? v[i]->rhw : 1.0f;
        pu[i] = v[i]->u * pq[i];
        pv[i] = v[i]->v * pq[i];
    }
    /* d/dx and d/dy of a linear attribute f over the triangle */
#define NV3_D3D_DX(f) ((((f)[1] - (f)[0]) * (c->y - a->y) - ((f)[2] - (f)[0]) * (b->y - a->y)) / area)
#define NV3_D3D_DY(f) ((((f)[2] - (f)[0]) * (b->x - a->x) - ((f)[1] - (f)[0]) * (c->x - a->x)) / area)
    float dqdx = NV3_D3D_DX(pq), dqdy = NV3_D3D_DY(pq);
    float dudx = NV3_D3D_DX(pu), dudy = NV3_D3D_DY(pu);
    float dvdx = NV3_D3D_DX(pv), dvdy = NV3_D3D_DY(pv);
#undef NV3_D3D_DX
#undef NV3_D3D_DY

    bool     textured  = (nv3_d3d.tex_format >> 28) || ((nv3_d3d.tex_format >> 24) & 0xF);
    if (nv3_d3d_tex_dump && textured && ((nv3_d3d.tex_format >> 28) != ((nv3_d3d.tex_format >> 24) & 0xF))) {
        nv3_d3d_tex_dump = 0;
        nv3_d3d_dump_texture(grobj);
    }
    uint32_t src_color = NV3_D3D_CONFIG_SRC_COLOR(config);
    bool     argb      = NV3_D3D_CONFIG_ARGB(config);
    uint32_t fog_rgb   = nv3_d3d.fog_color & 0xFFFFFF;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float  w[3];
            double wd[3];
            bool   inside = true;

            /* barycentric weights at the pixel (D3D samples at integer coordinates); top-left rule.
               In double: multipass effects draw the same plane tessellated differently and compare
               with Z EQUAL (3DMark 99's filtering tunnel: 286 triangles write Z, 1626 test it), and
               float weights on small triangles were off by up to a 16-bit Z step */
            for (int e = 0; e < 3; e++) {
                const nv3_d3d_vertex_t *p = v[(e + 1) % 3];
                const nv3_d3d_vertex_t *q = v[(e + 2) % 3];
                double ef = ((double) q->x - p->x) * ((double) y - p->y) - ((double) q->y - p->y) * ((double) x - p->x);

                wd[e] = ef / darea;
                w[e]  = (float) wd[e];
                if (wd[e] < 0.0) {
                    inside = false;
                    break;
                }
                if (wd[e] == 0.0) {
                    float dy = (area > 0.0f) ? (q->y - p->y) : (p->y - q->y);
                    float dx = (area > 0.0f) ? (q->x - p->x) : (p->x - q->x);
                    bool  tl = (dy < 0.0f) || (dy == 0.0f && dx > 0.0f);
                    if (!tl) {
                        inside = false;
                        break;
                    }
                }
            }
            if (!inside)
                continue;

            uint32_t color = nv3_d3d_lerp_color(v, w);

            if (textured) {
                float q  = pq[0] * w[0] + pq[1] * w[1] + pq[2] * w[2];
                float uq = pu[0] * w[0] + pu[1] * w[1] + pu[2] * w[2];
                float vq = pv[0] * w[0] + pv[1] * w[1] + pv[2] * w[2];
                float u = 0.0f, tv = 0.0f;
                int   level = 0;
                if (q != 0.0f && isfinite(q)) {
                    u  = uq / q;
                    tv = vq / q;
                    /* texels per pixel along x and y; the larger picks the level (log2 rounded down) */
                    float q2  = q * q;
                    float ux  = (dudx * q - uq * dqdx) / q2, vx = (dvdx * q - vq * dqdx) / q2;
                    float uy  = (dudy * q - uq * dqdy) / q2, vy = (dvdy * q - vq * dqdy) / q2;
                    float rho = fmaxf(ux * ux + vx * vx, uy * uy + vy * vy) * tsize * tsize;
                    if (rho > 1.0f && isfinite(rho))
                        level = (int) (0.5f * log2f(rho));
                }
                uint32_t texel = nv3_d3d_sample(grobj, u, tv, level);
                color = nv3_d3d_mul(texel, argb ? color : (color | 0xFF000000));
            } else if (!argb)
                color |= 0xFF000000;

            /* the source: texture x vertex colour; the alpha is the texture's times the vertex's
               only with COLOR_FORMAT A8R8G8B8 (X8R8G8B8 ignores the vertex alpha). That is what the
               alpha test sees; SRCCOLOR then modifies what goes to the blender. With blending off
               NV3DD32 programs ALPHA_ONE, src x srcalpha, dst x 0:
               3DMark 99's opaque geometry has vertex alpha 0. */
            uint32_t test_alpha = color >> 24;
            switch (src_color) {
                case 1: /* colour inverse */
                    color = ~color;
                    break;
                case 2: /* alpha inverse */
                    color ^= 0xFF000000;
                    break;
                case 3: /* alpha one */
                    color |= 0xFF000000;
                    break;
                default:
                    break;
            }

            /* fog: the vertex fog byte is the amount of fog colour (0 = none). NV3DD32.DLL sends 0 with
               fog off (dxdiag's cube: fog colour 0, bytes 0); in 3DMark 99 the far geometry (z ~ 1,
               w ~ 0.004) carries 0xff and the near geometry and sky 0 -- the reverse of D3D's 255 = no fog */
            float fog = 1.0f - (a->fog * w[0] + b->fog * w[1] + c->fog * w[2]) / 255.0f;
            if (fog < 1.0f) {
                uint32_t res = color & 0xFF000000;
                for (int sh = 0; sh < 24; sh += 8) {
                    float ch = ((color >> sh) & 0xFF) * fog + ((fog_rgb >> sh) & 0xFF) * (1.0f - fog);
                    res |= ((uint32_t) (ch + 0.5f) & 0xFF) << sh;
                }
                color = res;
            }

            uint32_t zeta;
            if (NV3_D3D_CONFIG_ZLINEAR(config)) {
                /* linear zeta: 1/m interpolated in world space (m = F/Z, so 1/m = Z/F, 0 at the
                   eye, 1 at the far plane); m is linear in screen space. NV3DD32 never sets it. */
                double q = a->rhw * wd[0] + b->rhw * wd[1] + c->rhw * wd[2];
                zeta     = (q > 0.0) ? nv3_d3d_zeta(1.0 / q) : 0xFFFF;
            } else
                zeta = nv3_d3d_zeta(a->z * wd[0] + b->z * wd[1] + c->z * wd[2]);

            nv3_d3d_fragment(grobj, config, nv3->pgraph.d3d_alpha, x, y, color, test_alpha, zeta);
        }
    }
}

/* ---- class 0x17: D3D5 textured triangle with zeta buffer ---- */

static float
nv3_d3d_float(uint32_t param)
{
    float f;
    memcpy(&f, &param, 4);
    return f;
}

void
nv3_class_017_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    switch (method_id) {
        case 0x0304:
            nv3_d3d.tex_offset = param;
            nv3_d3d_texture_changed();
            return;
        case 0x0308:
            nv3_d3d.tex_format = param;
            nv3_d3d_texture_changed();
            return;
        case 0x030C:
            nv3_d3d.filter = param;
            return;
        case 0x0310:
            nv3_d3d.fog_color = param;
            return;
        /* CONTROL0/1 live in PGRAPH's CONTROL_OUT and ALPHACNTRL, shared with class 0x18 and
           saved/restored by the resman on channel switches */
        case 0x0314:
            nv3->pgraph.d3d_config = param;
            return;
        case 0x0318:
            nv3->pgraph.d3d_alpha = param & 0xFFF;
            return;
        default:
            break;
    }

    if (method_id >= 0x1000 && method_id < 0x2000) {
        switch ((method_id >> 2) & 7) {
            case 0: /* fog and triangle indices */
                nv3_d3d.fog_tri = param;
                nv3_d3d.cur.fog = param >> 24;
                break;
            case 1:
                nv3_d3d.cur.color = param;
                break;
            case 2:
                nv3_d3d.cur.x = nv3_d3d_float(param);
                break;
            case 3:
                nv3_d3d.cur.y = nv3_d3d_float(param);
                break;
            case 4:
                nv3_d3d.cur.z = nv3_d3d_float(param);
                break;
            case 5:
                nv3_d3d.cur.rhw = nv3_d3d_float(param);
                break;
            case 6:
                nv3_d3d.cur.u = nv3_d3d_float(param);
                break;
            case 7: /* V: the vertex is complete; store it and draw what it asks for */
            {
                nv3_d3d.cur.v = nv3_d3d_float(param);
                nv3_d3d.vtx[nv3_d3d.fog_tri & 0xF] = nv3_d3d.cur;

                for (int t = 0; t < 2; t++) {
                    uint32_t i0 = (nv3_d3d.fog_tri >> (t * 12)) & 0xF;
                    uint32_t i1 = (nv3_d3d.fog_tri >> (t * 12 + 4)) & 0xF;
                    uint32_t i2 = (nv3_d3d.fog_tri >> (t * 12 + 8)) & 0xF;
                    if (i0 != i1 && i1 != i2 && i0 != i2) {
                        nv3_d3d_frag_in = nv3_d3d_frag_clip = nv3_d3d_frag_written = 0;
                        nv3_d3d_triangle(grobj, &nv3_d3d.vtx[i0], &nv3_d3d.vtx[i1], &nv3_d3d.vtx[i2]);
                        if (nv3_d3d_trace_left) {
                            const nv3_d3d_vertex_t *a = &nv3_d3d.vtx[i0], *b = &nv3_d3d.vtx[i1], *c = &nv3_d3d.vtx[i2];
                            nv3_d3d_trace_left--;
                            always_log("nv3: d3d tri (%.1f,%.1f z%.3f w%.3f c%08x) (%.1f,%.1f) (%.1f,%.1f) cfg %08x filt %08x alpha %03x tex %08x/%08x ctx %08x "
                                       "surf %06x/%d %06x/%d %06x/%d z %06x/%d fog %02x %02x %02x/%08x | frags %u clipped %u written %u%c",
                                       a->x, a->y, a->z, a->rhw, a->color, b->x, b->y, c->x, c->y, nv3->pgraph.d3d_config, nv3_d3d.filter, nv3->pgraph.d3d_alpha,
                                       nv3_d3d.tex_offset, nv3_d3d.tex_format, grobj.grobj_0,
                                       nv3->pgraph.boffset[0], nv3->pgraph.bpitch[0], nv3->pgraph.boffset[1], nv3->pgraph.bpitch[1],
                                       nv3->pgraph.boffset[2], nv3->pgraph.bpitch[2], nv3->pgraph.boffset[3], nv3->pgraph.bpitch[3],
                                       a->fog, b->fog, c->fog, nv3_d3d.fog_color, nv3_d3d_frag_in, nv3_d3d_frag_clip, nv3_d3d_frag_written, 10);
                            always_log("nv3: d3d tex dma %04x flags %08x limit %08x target %d page0 %08x lin %08x texels %04x %04x %04x%c",
                                       nv3->pgraph.dma_settings & 0xFFFF, nv3_ramin_read32((nv3->pgraph.dma_settings & 0xFFFF) << 4, nv3),
                                       nv3_ramin_read32(((nv3->pgraph.dma_settings & 0xFFFF) << 4) + 4, nv3), nv3_d3d.tex_target,
                                       nv3_d3d.tex_pages[0], nv3_d3d.tex_lin, nv3_d3d_read_tex16(grobj, 0), nv3_d3d_read_tex16(grobj, 0x100 * 2 + 0x80),
                                       nv3_d3d_read_tex16(grobj, 0x8000), 10);
                        }
                    }
                }
                break;
            }
        }
        return;
    }

    nv_warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
    nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
}

/* ---- class 0x18: point with zeta buffer ---- */

void
nv3_class_018_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    switch (method_id) {
        case 0x0304:
            nv3->pgraph.d3d_config = param;
            return;
        case 0x0308:
            nv3->pgraph.d3d_alpha = param & 0xFFF;
            return;
        case 0x07FC:
            nv3_d3d.zpoint_x = (int16_t) (param & 0xFFFF);
            nv3_d3d.zpoint_y = (int16_t) (param >> 16);
            return;
        default:
            break;
    }

    if (method_id >= 0x0800 && method_id < 0x1000) {
        if (!(method_id & 4))
            nv3_d3d.zpoint_color = param;
        else {
            /* the colour is in the object's colour format; the point steps right */
            nv3_color_expanded_t c   = nv3_render_expand_color(nv3_d3d.zpoint_color, grobj);
            uint32_t             rgb = (((uint32_t) c.a) << 24) | ((uint32_t) (c.r >> 2) << 16) | ((uint32_t) (c.g >> 2) << 8) | (c.b >> 2);

            nv3_d3d_fragment(grobj, nv3->pgraph.d3d_config, nv3->pgraph.d3d_alpha, nv3_d3d.zpoint_x, nv3_d3d.zpoint_y, rgb, rgb >> 24, param >> 16);
            nv3_d3d.zpoint_x++;
        }
        return;
    }

    nv_warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
    nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
}
