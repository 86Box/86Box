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
*          The work is split as the RIVA TNT's is (nv4_pgraph_3d.c), so the
*          per-pixel part can run on worker threads or a GPU kernel:
*
*            state    Before a triangle the PGRAPH state it is drawn with
*                     (CONTROL0/1, surfaces, clip, texture and its pages)
*                     is taken into a draw state, immutable once in the
*                     batch; a triangle names the one it was submitted
*                     under.
*            setup    On the emulation thread: culling, cylindrical wrap,
*                     one span per row (the double precision edge test,
*                     top-left rule), attributes turned into plane
*                     equations.
*            shading  Per span, at flush: texture fetch and filter, the
*                     vertex colour, SRCCOLOR, fog, alpha test, zeta,
*                     blend, dither.
*
*          A batch is drawn before anything that must see its pixels: a
*          method of another class (but the state-only ones), a notifier,
*          CPU access to the framebuffer (linear, the VGA window, the
*          real-mode window), the start of vertical blank, a full batch.
*          With the GPU lane a flush only submits; nv3_d3d_sync also waits
*          for the pixels, and vertical blank waits only when the batch in
*          flight draws into the displayed bytes.
*
*          With the "Render threads" option above 1 a flush shades on
*          worker threads, each walking the whole span list in submission
*          order and shading only its rows (y mod n): bit-exact with one
*          thread while every row of the batch is on its own bytes, so a
*          batch is flushed first when a surface's layout would meet
*          another's or a texture would read bytes the batch has written,
*          and a batch whose rows alias is shaded serially. The GPU lane
*          (nv3_render_d3d_gpu.c) takes a batch under the same rules.
*          Floats round the same way on both (the file is built with
*          -ffp-contract=off, float-to-int conversions go through
*          nv3_f2i, the mip level comes from the float's bits).
*          NV3_3D_VERIFY=1 shades every parallel batch twice and logs any
*          byte that differs.
*
* Authors: Connor Hyde, <mario64crashed@gmail.com>
 *          Marcos Alves, <xeon4d@gmail.com>
*
*          Copyright 2024-2026 Connor Hyde
 *          Copyright 2026 Marcos Alves
*/

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <86box/86box.h>
#include <86box/plat_unused.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/agpgart.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>
#include "nv3_render_d3d.h"

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

/* Float to int truncating, out of range (and NaN) giving INT_MIN as x86
   does; the GPU kernel defines it the same way. */
static int32_t
nv3_f2i(float f)
{
    return ((f > -2147483648.0f) && (f < 2147483648.0f)) ? (int32_t) f : INT32_MIN;
}

static bool
nv3_finite(float f)
{
    uint32_t u;

    memcpy(&u, &f, 4);
    return (u & 0x7F800000) != 0x7F800000;
}

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
nv3_d3d_zeta(float z)
{
    if (!(z > 0.0f))
        return 0;
    if (z >= 1.0f)
        return 0xFFFF;
    return (uint32_t) nv3_f2i(z * 65535.0f + 0.5f);
}

/* ---- textures ---- */

static void
nv3_d3d_texture_changed(void)
{
    nv3->pgraph.prim.d3d.tex_mapped = false;
}

/* <len> bytes at byte <byte> of a draw state's texture (within one of its pages): a pointer into
   VRAM or system memory, or <tmp> filled. Bytes past the texture's 64 pages read 0. Worker
   threads and the GPU lane read through here, so system memory is read in place (physical
   addresses, the AGP aperture resolved when the state was made), never through the bus. */
const uint8_t *
nv3_d3d_tex_ptr(const nv3_d3d_dstate_t *s, uint32_t byte, uint32_t len, uint8_t *tmp)
{
    uint32_t lin = s->tex_l0 + byte;
    uint32_t addr;

    if ((byte >= s->tex_size) || ((lin >> 12) >= D3_TEX_PAGES))
        goto zero;
    addr = s->tex_pages[lin >> 12] | (lin & 0xFFF);
    if (s->tex_vram)
        return &nv3->nvbase.svga.vram[addr & nv3->nvbase.svga.vram_mask];
    if ((addr >= 0x100000) && ((uint64_t) addr + len <= (uint64_t) mem_size * 1024))
        return ram + addr;
zero:
    memset(tmp, 0, len);
    return tmp;
}

static uint16_t
nv3_d3d_read_tex16(const nv3_d3d_dstate_t *s, uint32_t byte)
{
    uint8_t        tmp[2];
    const uint8_t *p = nv3_d3d_tex_ptr(s, byte & ~1u, 2, tmp);

    return p[0] | (p[1] << 8);
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
nv3_d3d_texel(const nv3_d3d_dstate_t *st, uint32_t level_offset, int32_t s, int32_t t)
{
    uint32_t fmt = (st->tex_format >> 20) & 0xF;
    uint16_t p   = nv3_d3d_read_tex16(st, level_offset + nv3_d3d_swizzle((uint32_t) s, (uint32_t) t) * 2);
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
    if (((st->tex_format >> 16) & 0xF) && p == (st->tex_format & 0xFFFF))
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

static void
nv3_d3d_tex_logs(uint32_t format, int *max_log, int *min_log)
{
    *max_log = (format >> 28) & 0xF;
    *min_log = (format >> 24) & 0xF;
    if (*max_log > 11)
        *max_log = 11;
    if (*min_log > *max_log)
        *min_log = *max_log;
}

/* Sample at (u, v) in texture space (0..1 across the texture) from mip level `level` */
static uint32_t
nv3_d3d_sample(const nv3_d3d_dstate_t *st, float u, float v, int level)
{
    int      max_log, min_log;
    uint32_t offset = 0;

    nv3_d3d_tex_logs(st->tex_format, &max_log, &min_log);
    if (level > max_log - min_log)
        level = max_log - min_log;
    for (int l = 0; l < level; l++)
        offset += (1u << (max_log - l)) * (1u << (max_log - l)) * 2;

    int32_t size  = 1 << (max_log - level);
    int     wu    = NV3_D3D_CONFIG_WRAP_U(st->config);
    int     wv    = NV3_D3D_CONFIG_WRAP_V(st->config);
    float   fu    = u * (float) size;
    float   fv    = v * (float) size;

    /* The kernels are centred on each texel's origin, its upper-left corner (ZOH a box, FOH a
       tent); ZOH_MS moves the origin to the texel centre. NV3DD32 picks ZOH for point-sampled
       textures and, for FOH, takes 0.4375 texel off u and v itself -- so no half-texel shift
       here. */
    if (NV3_D3D_CONFIG_INTERP(st->config) != 2) {
        if (NV3_D3D_CONFIG_INTERP(st->config) == 0) {
            fu += 0.5f;
            fv += 0.5f;
        }
        int32_t s = nv3_d3d_wrap(nv3_f2i(floorf(fu)), size, wu);
        int32_t t = nv3_d3d_wrap(nv3_f2i(floorf(fv)), size, wv);
        return nv3_d3d_texel(st, offset, s, t);
    }

    /* first order hold: bilinear between the four texels around (fu, fv) */
    int32_t  s0 = nv3_f2i(floorf(fu));
    int32_t  t0 = nv3_f2i(floorf(fv));
    uint32_t wx = (uint32_t) nv3_f2i((fu - (float) s0) * 256.0f);
    uint32_t wy = (uint32_t) nv3_f2i((fv - (float) t0) * 256.0f);
    uint32_t c[4];
    c[0] = nv3_d3d_texel(st, offset, nv3_d3d_wrap(s0, size, wu), nv3_d3d_wrap(t0, size, wv));
    c[1] = nv3_d3d_texel(st, offset, nv3_d3d_wrap(s0 + 1, size, wu), nv3_d3d_wrap(t0, size, wv));
    c[2] = nv3_d3d_texel(st, offset, nv3_d3d_wrap(s0, size, wu), nv3_d3d_wrap(t0 + 1, size, wv));
    c[3] = nv3_d3d_texel(st, offset, nv3_d3d_wrap(s0 + 1, size, wu), nv3_d3d_wrap(t0 + 1, size, wv));

    uint32_t res = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t top = ((c[0] >> sh) & 0xFF) * (256 - wx) + ((c[1] >> sh) & 0xFF) * wx;
        uint32_t bot = ((c[2] >> sh) & 0xFF) * (256 - wx) + ((c[3] >> sh) & 0xFF) * wx;
        res |= (((top * (256 - wy) + bot * wy) >> 16) & 0xFF) << sh;
    }
    return res;
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
nv3_d3d_blend(const nv3_d3d_dstate_t *st, uint32_t src, uint16_t dst, int32_t x, int32_t y)
{
    uint32_t config = st->config;
    bool     dither = st->dither;
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

/* One fragment: clip rectangles, alpha and Z tests, write enables, blend, write. `test_alpha` is
   what the alpha test (CONTROL1) compares: the combined texture x vertex alpha, before SRCCOLOR.
   The "ALPHA" write conditions check the alpha that reaches the blender (in `color`) for non-zero
   at the blender's 4-bit precision, and a failed alpha test writes neither colour nor zeta
   (envytools hwtest pgraph_rop.cc, class 0x18). NV3DD32 relies on the former: it says colour keys
   only work with a blend mode that does not force the alpha to 1. The canvas and user clip are
   the caller's. */
static void
nv3_d3d_fragment(const nv3_d3d_dstate_t *st, int32_t x, int32_t y, uint32_t color, uint32_t test_alpha, uint32_t zeta)
{
    uint8_t *vram = nv3->nvbase.svga.vram;
    uint32_t mask = nv3->nvbase.svga.vram_mask & ~1;

    if (st->ncr) {
        bool covered = false;

        for (int i = 0; i < st->ncr; i++)
            if ((x >= st->cr_x0[i]) && (y >= st->cr_y0[i]) && (x < st->cr_x1[i]) && (y < st->cr_y1[i]))
                covered = true;
        if (covered == (bool) st->cr_inv)
            return;
    }

    if (!nv3_d3d_cmp((st->alpha_ctl >> 8) & 0xF, test_alpha, st->alpha_ctl & 0xFF))
        return;

    uint32_t zaddr = (st->boff[3] + (uint32_t) y * st->bpitch[3] + (uint32_t) x * 2) & mask;
    uint32_t zcur  = st->z_on ? *(uint16_t *) &vram[zaddr] : 0;
    bool     ztest = !st->z_on || nv3_d3d_cmp(NV3_D3D_CONFIG_ZFUNC(st->config), zeta, zcur);
    bool     anz   = (color >> 28) != 0;
    if (nv3_d3d_wren(NV3_D3D_CONFIG_CWRITE(st->config), ztest, anz)) {
        for (uint32_t j = 0; j < 3; j++) { /* surface 3 is the zeta buffer */
            if (!((st->buf_on >> j) & 1))
                continue;
            uint32_t  addr = (st->boff[j] + (uint32_t) y * st->bpitch[j] + (uint32_t) x * 2) & mask;
            uint16_t *p    = (uint16_t *) &vram[addr];
            *p             = (*p & 0x8000) | nv3_d3d_blend(st, color, *p, x, y);
        }
    }
    if (st->z_on && nv3_d3d_wren(NV3_D3D_CONFIG_ZWRITE(st->config), ztest, anz))
        *(uint16_t *) &vram[zaddr] = zeta;
}

/* ---- shading ---- */

static uint32_t
nv3_d3d_mul(uint32_t a, uint32_t b)
{
    uint32_t res = 0;

    for (int sh = 0; sh < 32; sh += 8)
        res |= ((((a >> sh) & 0xFF) * ((b >> sh) & 0xFF) + 127) / 255) << sh;
    return res;
}

static int32_t
nv3_d3d_byte(float c)
{
    int32_t i = nv3_f2i(c + 0.5f);

    return (i < 0) ? 0 : ((i > 255) ? 255 : i);
}

/* The mip level: log2 of the larger of the texels per pixel along x and y, rounded down -- half
   the exponent of their square, read from the float's bits so every host and the GPU agree */
static int
nv3_d3d_level(float rho)
{
    uint32_t u;

    memcpy(&u, &rho, 4);
    return ((int) (u >> 23) - 127) >> 1;
}

static void
nv3_d3d_mark(const nv3_d3d_dstate_t *st, int32_t y, int32_t x0, int32_t x1)
{
    uint32_t vm = nv3->nvbase.svga.vram_mask;

    for (uint32_t j = 0; j < 4; j++) {
        if (!((st->buf_on >> j) & 1))
            continue;
        uint32_t row = st->boff[j] + (uint32_t) y * st->bpitch[j];
        uint32_t a0  = row + (uint32_t) x0 * st->bpp;
        uint32_t a1  = row + (uint32_t) x1 * st->bpp;

        for (uint32_t a = a0 & ~0xFFFu; a <= a1; a += 0x1000)
            nv3->nvbase.svga.changedvram[(a & vm) >> 12] = changeframecount;
    }
}

/* A 2D fill's span: its value stored in every surface it writes, as nv3_render_write_surface
   does */
static void
nv3_d3d_fill_span(const nv3_d3d_dstate_t *st, const nv3_d3d_tri_t *t, const nv3_d3d_span_t *sp)
{
    uint8_t *vram = nv3->nvbase.svga.vram;
    uint32_t vm   = nv3->nvbase.svga.vram_mask;
    uint32_t v;

    memcpy(&v, &t->c[0], 4);
    for (uint32_t j = 0; j < 4; j++) {
        if (!((st->buf_on >> j) & 1))
            continue;
        uint32_t row = st->boff[j] + (uint32_t) sp->y * st->bpitch[j];

        for (int32_t x = sp->x0; x < sp->x1; x++) {
            uint32_t addr = (row + (uint32_t) x * st->fill) & vm;

            if (st->fill == 1)
                vram[addr] = v;
            else if (st->fill == 2)
                *(uint16_t *) &vram[addr & ~1] = v;
            else
                *(uint32_t *) &vram[addr & ~3] = v;
        }
    }
    nv3_d3d_mark(st, sp->y, sp->x0, sp->x1);
}

static void
nv3_d3d_span(const nv3_d3d_batch_t *b, const nv3_d3d_span_t *sp)
{
    const nv3_d3d_tri_t    *t        = &b->tris[sp->tri];
    const nv3_d3d_dstate_t *st       = &b->states[t->state];
    uint32_t                config   = st->config;

    if (st->fill) {
        nv3_d3d_fill_span(st, t, sp);
        return;
    }
    uint32_t                src_color = NV3_D3D_CONFIG_SRC_COLOR(config);
    bool                    argb     = NV3_D3D_CONFIG_ARGB(config);
    int                     max_log, min_log;
    float                   tsize;
    float                   fy       = (float) sp->y - t->y0;

    nv3_d3d_tex_logs(st->tex_format, &max_log, &min_log);
    tsize = (float) (1 << max_log);

    for (int32_t x = sp->x0; x < sp->x1; x++) {
        float fx = (float) x - t->x0;
        float at[D3_AT_COUNT];

        for (int i = 0; i < D3_AT_COUNT; i++)
            at[i] = t->c[i] + t->dx[i] * fx + t->dy[i] * fy;

        uint32_t color = ((uint32_t) nv3_d3d_byte(at[D3_AT_A]) << 24) | ((uint32_t) nv3_d3d_byte(at[D3_AT_R]) << 16)
            | ((uint32_t) nv3_d3d_byte(at[D3_AT_G]) << 8) | (uint32_t) nv3_d3d_byte(at[D3_AT_B]);
        float    q     = at[D3_AT_Q];

        if (st->textured) {
            float uq = at[D3_AT_UQ];
            float vq = at[D3_AT_VQ];
            float u = 0.0f, tv = 0.0f;
            int   level = 0;
            if ((q != 0.0f) && nv3_finite(q)) {
                u  = uq / q;
                tv = vq / q;
                /* texels per pixel along x and y; the larger picks the level */
                float q2  = q * q;
                float ux  = (t->dx[D3_AT_UQ] * q - uq * t->dx[D3_AT_Q]) / q2;
                float vx  = (t->dx[D3_AT_VQ] * q - vq * t->dx[D3_AT_Q]) / q2;
                float uy  = (t->dy[D3_AT_UQ] * q - uq * t->dy[D3_AT_Q]) / q2;
                float vy  = (t->dy[D3_AT_VQ] * q - vq * t->dy[D3_AT_Q]) / q2;
                float rx  = ux * ux + vx * vx;
                float ry  = uy * uy + vy * vy;
                float rho = (rx > ry ? rx : ry) * tsize * tsize;
                if ((rho > 1.0f) && nv3_finite(rho))
                    level = nv3_d3d_level(rho);
            }
            uint32_t texel = nv3_d3d_sample(st, u, tv, level);
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
        float fog = 1.0f - at[D3_AT_FOG] / 255.0f;
        if (fog < 1.0f) {
            uint32_t res = color & 0xFF000000;
            for (int sh = 0; sh < 24; sh += 8) {
                float ch = (float) ((color >> sh) & 0xFF) * fog + (float) ((st->fog_rgb >> sh) & 0xFF) * (1.0f - fog);
                res |= ((uint32_t) nv3_f2i(ch + 0.5f) & 0xFF) << sh;
            }
            color = res;
        }

        uint32_t zeta;
        if (NV3_D3D_CONFIG_ZLINEAR(config)) {
            /* linear zeta: 1/m interpolated in world space (m = F/Z, so 1/m = Z/F, 0 at the
               eye, 1 at the far plane); m is linear in screen space. NV3DD32 never sets it. */
            zeta = (q > 0.0f) ? nv3_d3d_zeta(1.0f / q) : 0xFFFF;
        } else
            zeta = nv3_d3d_zeta(at[D3_AT_Z]);

        nv3_d3d_fragment(st, x, sp->y, color, test_alpha, zeta);
    }
    nv3_d3d_mark(st, sp->y, sp->x0, sp->x1);
}

/* ---- the batch ---- */

/* Do two VRAM byte ranges meet? */
static int
nv3_d3d_meets(uint32_t lo0, uint32_t hi0, uint32_t lo1, uint32_t hi1)
{
    return (lo0 <= hi1) && (lo1 <= hi0);
}

/* Widen lo..hi to the VRAM pages a state's texture reads. */
static void
nv3_d3d_tex_range(const nv3_d3d_dstate_t *s, uint32_t *lo, uint32_t *hi)
{
    if (!s->textured || !s->tex_vram || !s->tex_size)
        return;
    for (uint32_t p = 0; p <= ((s->tex_l0 + s->tex_size - 1) >> 12); p++) {
        uint32_t a = s->tex_pages[p] & nv3->nvbase.svga.vram_mask & ~0xFFFu;

        if (a < *lo)
            *lo = a;
        if (a + 0xFFF > *hi)
            *hi = a + 0xFFF;
    }
}

/* Would a state's texture read VRAM the batch has written? */
static int
nv3_d3d_tex_hazard(const nv3_d3d_batch_t *b, const nv3_d3d_dstate_t *s)
{
    if (!s->textured || !s->tex_vram || !s->tex_size)
        return 0;
    for (uint32_t p = 0; p <= ((s->tex_l0 + s->tex_size - 1) >> 12); p++) {
        uint32_t a = s->tex_pages[p] & nv3->nvbase.svga.vram_mask & ~0xFFFu;

        for (int r = 0; r < b->nregions; r++)
            if ((b->regions[r].wlo <= b->regions[r].whi) && nv3_d3d_meets(a, a + 0xFFF, b->regions[r].wlo, b->regions[r].whi))
                return 1;
    }
    return 0;
}

/* The bytes a state can reach through layout base/pitch: its clip's rows, or at least one row's
   pixels. */
static void
nv3_d3d_reach(const nv3_d3d_dstate_t *s, uint32_t base, uint32_t pitch, uint32_t *lo, uint32_t *hi)
{
    uint32_t rows = (uint32_t) (s->cy1 - s->cy0) * pitch;
    uint32_t w    = (uint32_t) s->cx1 * s->bpp;

    *lo = base + (uint32_t) s->cy0 * pitch;
    *hi = *lo + ((rows > w) ? rows : w) - 1;
}

/* The batch region with layout base/pitch: its index, -1 when a new one can be added, -2 when one
   would meet another layout's bytes or the table is full. */
static int
nv3_d3d_region_find(const nv3_d3d_batch_t *b, uint32_t base, uint32_t pitch, uint32_t lo, uint32_t hi)
{
    int same = -1;

    for (int r = 0; r < b->nregions; r++)
        if ((b->regions[r].base == base) && (b->regions[r].pitch == pitch)) {
            same = r;
            /* the merged region must still keep off the others */
            if (b->regions[r].lo < lo)
                lo = b->regions[r].lo;
            if (b->regions[r].hi > hi)
                hi = b->regions[r].hi;
        }
    for (int r = 0; r < b->nregions; r++)
        if ((r != same) && nv3_d3d_meets(lo, hi, b->regions[r].lo, b->regions[r].hi))
            return -2;
    if (same >= 0)
        return same;
    return (b->nregions < D3_REGIONS) ? -1 : -2;
}

static int
nv3_d3d_region_take(nv3_d3d_batch_t *b, uint32_t base, uint32_t pitch, uint32_t lo, uint32_t hi)
{
    int r = nv3_d3d_region_find(b, base, pitch, lo, hi);

    if (r < 0) {
        r                   = b->nregions++;
        b->regions[r].base  = base;
        b->regions[r].pitch = pitch;
        b->regions[r].lo    = lo;
        b->regions[r].hi    = hi;
        b->regions[r].wlo   = 0xFFFFFFFF;
        b->regions[r].whi   = 0;
    } else {
        if (lo < b->regions[r].lo)
            b->regions[r].lo = lo;
        if (hi > b->regions[r].hi)
            b->regions[r].hi = hi;
    }
    return r;
}

/* The surfaces a state draws into: the enabled colour buffers 0-2, then the zeta buffer (3); a
   fill's surfaces 0-3 */
static int
nv3_d3d_layouts(const nv3_d3d_dstate_t *s, int *idx)
{
    uint32_t on = s->buf_on | (s->z_on ? 8 : 0);
    int      n  = 0;

    for (int j = 0; j < 4; j++)
        if ((on >> j) & 1)
            idx[n++] = j;
    return n;
}

/* Put a draw state into the batch, flushing first when it cannot share the batch with what is
   there: the table is full, or, with worker threads or the GPU, its rows would meet another
   layout's bytes or its texture reads bytes the batch writes. */
static int
nv3_d3d_place_state(nv3_d3d_batch_t *b, nv3_d3d_dstate_t *s)
{
    int      flush = b->nstates >= D3_STATES;
    int      idx[4], n = nv3_d3d_layouts(s, idx);
    uint32_t lo[4] = { 0 }, hi[4] = { 0 };
    uint32_t size  = nv3->nvbase.svga.vram_max;

    for (int i = 0; i < 4; i++)
        s->rg[i] = -1;
    if (b->parallel) {
        for (int i = 0; i < n; i++) {
            nv3_d3d_reach(s, s->boff[idx[i]], s->bpitch[idx[i]], &lo[i], &hi[i]);
            if (nv3_d3d_region_find(b, s->boff[idx[i]], s->bpitch[idx[i]], lo[i], hi[i]) == -2)
                flush = 1;
        }
        if (nv3_d3d_tex_hazard(b, s))
            flush = 1;
    }
    if (flush)
        nv3_d3d_flush();

    if (b->parallel) {
        for (int i = 0; i < n; i++) {
            uint32_t base = s->boff[idx[i]], pitch = s->bpitch[idx[i]];
            int      r;

            /* surfaces of this state meeting each other, rows that share bytes, or rows that
               wrap around the end of VRAM: one thread, in submission order */
            if ((nv3_d3d_region_find(b, base, pitch, lo[i], hi[i]) == -2) || (pitch < (uint32_t) s->cx1 * s->bpp) || (hi[i] >= size)) {
                b->serial = 1;
                continue;
            }
            r          = nv3_d3d_region_take(b, base, pitch, lo[i], hi[i]);
            s->rg[idx[i]] = r;
        }
        nv3_d3d_tex_range(s, &b->tex_lo, &b->tex_hi);
    }
    b->states[b->nstates] = *s;
    return b->nstates++;
}

/* Record the rows y0..y1 (pixels below x1) a triangle or fill may write (or, for zeta, read: the
   GPU lane uploads those too). */
static void
nv3_d3d_written(nv3_d3d_batch_t *b, const nv3_d3d_dstate_t *s, int32_t y0, int32_t y1, int32_t x1)
{
    uint32_t w = (uint32_t) x1 * s->bpp;

    for (int j = 0; j < 4; j++) {
        int r = s->rg[j];

        if (r < 0)
            continue;
        uint32_t           lo = s->boff[j] + (uint32_t) y0 * s->bpitch[j];
        uint32_t           n  = (uint32_t) (y1 + 1 - y0) * s->bpitch[j];
        uint32_t           hi = lo + ((n > w) ? n : w) - 1;
        nv3_d3d_region_t *g  = &b->regions[r];

        if (lo < g->wlo)
            g->wlo = lo;
        if (hi > g->whi)
            g->whi = hi;
    }
}

static void
nv3_d3d_worker(void *priv)
{
    nv3_d3d_worker_t *w = (nv3_d3d_worker_t *) priv;
    nv3_d3d_batch_t  *b = nv3->batch3d;

    for (;;) {
        thread_wait_event(w->wake, -1);
        thread_reset_event(w->wake);
        if (!b->run)
            break;
        for (int i = 0; i < b->nspans; i++)
            if (((unsigned) b->spans[i].y % (unsigned) b->nthreads) == (unsigned) w->id)
                nv3_d3d_span(b, &b->spans[i]);
        thread_set_event(w->done);
    }
}

static void
nv3_d3d_shade_threaded(nv3_d3d_batch_t *b)
{
    for (int i = 0; i < b->nthreads; i++)
        thread_set_event(b->workers[i].wake);
    for (int i = 0; i < b->nthreads; i++)
        thread_wait_event(b->workers[i].done, -1);
    for (int i = 0; i < b->nthreads; i++)
        thread_reset_event(b->workers[i].done);
}

static void
nv3_d3d_shade_serial(nv3_d3d_batch_t *b)
{
    for (int i = 0; i < b->nspans; i++)
        nv3_d3d_span(b, &b->spans[i]);
}

/* Shade on the GPU or the worker threads. 0: neither took the batch, VRAM is untouched. */
static int
nv3_d3d_shade_parallel(nv3_d3d_batch_t *b)
{
    if (b->gpu) {
        if (nv3_gpu_shade(b->gpu, b))
            return 1;
        /* the CPU draws it: the GPU's batches in flight first */
        nv3_gpu_wait(b->gpu);
    }
    if (b->nthreads > 1) {
        nv3_d3d_shade_threaded(b);
        return 1;
    }
    return 0;
}

/* Shade in parallel, keep the result, shade the same batch serially from the same starting VRAM
   and compare. */
static void
nv3_d3d_shade_verify(nv3_d3d_batch_t *b)
{
    uint8_t *vram  = nv3->nvbase.svga.vram;
    uint32_t size  = nv3->nvbase.svga.vram_max;
    uint32_t bad   = 0;
    uint32_t first = 0;

    if (b->gpu)
        nv3_gpu_wait(b->gpu);
    memcpy(b->vsnap, vram, size);
    if (!nv3_d3d_shade_parallel(b)) {
        nv3_d3d_shade_serial(b);
        return;
    }
    if (b->gpu)
        nv3_gpu_wait(b->gpu);
    memcpy(b->vres, vram, size);
    memcpy(vram, b->vsnap, size);
    nv3_d3d_shade_serial(b);
    for (uint32_t a = 0; a < size; a++)
        if (b->vres[a] != vram[a]) {
            if (!bad)
                first = a;
            bad++;
        }
    b->vflushes++;
    if (bad) {
        b->vbad++;
        pclog("nv3 3D verify: flush %u: %u bytes differ, first at %06x (parallel %02x, serial %02x), %d spans\n",
              b->vflushes, bad, first, b->vres[first], vram[first], b->nspans);
    }
    if (!(b->vflushes % 1000))
        pclog("nv3 3D verify: %u flushes, %u differed\n", b->vflushes, b->vbad);
}

static uint64_t
nv3_d3d_now_us(void)
{
    struct timespec ts;

    timespec_get(&ts, TIME_UTC);
    return (uint64_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* What the 3D batch cost on the host, logged when the card closes */
static void
nv3_d3d_stats(void)
{
    static const char *const cause[NV3_SYNC_CLASS] = { "LFB", "VGA", "vblank", "other" };
    nv3_d3d_batch_t         *b   = nv3->batch3d;

    if (!b)
        return;
    pclog("nv3 3D: %d threads%s, %llu flushes, %llu spans, %llu us shading\n", b->nthreads, b->gpu ? " + GPU" : "",
          (unsigned long long) b->st_flushes, (unsigned long long) b->st_spans, (unsigned long long) b->st_us);
    if (b->gpu) {
        uint64_t n, f;

        nv3_gpu_counts(b->gpu, &n, &f);
        pclog("nv3 3D: GPU took %llu batches, left %llu to the CPU\n", (unsigned long long) n, (unsigned long long) f);
        for (int i = 0; i < NV3_SYNC_CAUSES; i++)
            if (b->st_sync_n[i])
                pclog("nv3 3D: waited for the GPU %u times, %llu us, for %s %02x\n", b->st_sync_n[i], (unsigned long long) b->st_sync_us[i],
                      (i < NV3_SYNC_CLASS) ? cause[i] : "class", (i < NV3_SYNC_CLASS) ? 0 : (i - NV3_SYNC_CLASS));
        memset(b->st_sync_us, 0, sizeof(b->st_sync_us));
        memset(b->st_sync_n, 0, sizeof(b->st_sync_n));
    }
    b->st_flushes = b->st_spans = b->st_us = 0;
}

static void
nv3_d3d_empty(nv3_d3d_batch_t *b)
{
    b->nspans   = 0;
    b->ntris    = 0;
    b->nstates  = 0;
    b->nregions = 0;
    b->serial   = 0;
    b->tex_lo   = 0xFFFFFFFF;
    b->tex_hi   = 0;
}

void
nv3_d3d_flush(void)
{
    nv3_d3d_batch_t *b = nv3 ? nv3->batch3d : NULL;
    uint64_t         t0;

    if (!b || !b->nstates)
        return;
    t0 = nv3_d3d_now_us();
    if (b->parallel && !b->serial && (b->nspans > 1)) {
        if (b->verify)
            nv3_d3d_shade_verify(b);
        else if (!nv3_d3d_shade_parallel(b))
            nv3_d3d_shade_serial(b);
    } else {
        /* the CPU draws into VRAM: the GPU's batches in flight first */
        if (b->gpu)
            nv3_gpu_wait(b->gpu);
        nv3_d3d_shade_serial(b);
    }
    b->st_flushes++;
    b->st_spans += b->nspans;
    b->st_us += nv3_d3d_now_us() - t0;
    nv3_d3d_empty(b);
}

static void
nv3_d3d_gpu_wait(nv3_d3d_batch_t *b, uint32_t lo, uint32_t hi, int cause)
{
    uint64_t t0;

    if (!nv3_gpu_pending_overlaps(b->gpu, lo, hi))
        return;
    t0 = nv3_d3d_now_us();
    nv3_gpu_wait(b->gpu);
    b->st_sync_us[cause] += nv3_d3d_now_us() - t0;
    b->st_sync_n[cause]++;
}

/* Draw the batch and, when the GPU has batches in flight, wait for their pixels: for anything
   that reads or writes VRAM itself. */
void
nv3_d3d_sync(int cause)
{
    nv3_d3d_batch_t *b = nv3 ? nv3->batch3d : NULL;

    if (!b)
        return;
    nv3_d3d_flush();
    if (b->gpu)
        nv3_d3d_gpu_wait(b, 0, 0xFFFFFFFF, cause);
}

/* The CPU is about to read or write VRAM bytes lo..hi (a 2D draw): draw the batch first if it
   reaches them (its rows, or textures it samples), and wait for the GPU's batches in flight if
   their rows do. Other bytes need neither. */
void
nv3_d3d_touch(uint32_t lo, uint32_t hi, int cause)
{
    nv3_d3d_batch_t *b   = nv3 ? nv3->batch3d : NULL;
    int              hit = 0;

    if (!b)
        return;
    if (b->nstates) {
        if (!b->parallel || b->serial)
            hit = 1; /* rows are not (all) tracked */
        for (int r = 0; !hit && (r < b->nregions); r++)
            hit = nv3_d3d_meets(lo, hi, b->regions[r].lo, b->regions[r].hi);
        if (!hit && (b->tex_lo <= b->tex_hi))
            hit = nv3_d3d_meets(lo, hi, b->tex_lo, b->tex_hi);
        if (hit)
            nv3_d3d_flush();
    }
    if (b->gpu)
        nv3_d3d_gpu_wait(b, lo, hi, cause);
}

/* The same for a 2D draw into rows y0..y1 (inclusive), pixels below x1, of the colour buffers
   object grobj writes; at most 4 bytes a pixel */
void
nv3_d3d_touch_rect(nv3_grobj_t grobj, int32_t y0, int32_t y1, int32_t x1, int cause)
{
    uint32_t ctx  = grobj.grobj_0;
    uint32_t size = nv3->nvbase.svga.vram_max;

    if (y0 < 0)
        y0 = 0;
    if ((y1 < y0) || (x1 <= 0))
        return;
    for (int j = 0; j < NV3_PGRAPH_MAX_BUFFERS; j++) {
        if (!((ctx >> (NV3_PGRAPH_CTX_SWITCH_DST_BUFFER0_ENABLED + j)) & 1))
            continue;
        uint32_t lo = nv3->pgraph.boffset[j] + (uint32_t) y0 * nv3->pgraph.bpitch[j];
        uint32_t hi = nv3->pgraph.boffset[j] + (uint32_t) y1 * nv3->pgraph.bpitch[j] + (uint32_t) x1 * 4 - 1;

        if (hi >= size) {
            /* wraps around the end of VRAM */
            nv3_d3d_sync(cause);
            return;
        }
        nv3_d3d_touch(lo, hi, cause);
    }
}

/* Vertical blank: the batch is drawn, and waited for only if it draws into the scanned-out bytes
   lo..hi (a back buffer can stay in flight). */
void
nv3_d3d_vblank(uint32_t lo, uint32_t hi)
{
    nv3_d3d_batch_t *b = nv3 ? nv3->batch3d : NULL;

    if (!b)
        return;
    b->vblanks++;
    nv3_d3d_flush();
    if (b->gpu)
        nv3_d3d_gpu_wait(b, lo, hi, NV3_SYNC_VBLANK);
}

/* ---- draw states ---- */

/* The texture's pages, mapped once per texture (offset/format) change as before; system memory
   pages are resolved through the AGP aperture to the RAM behind them */
static void
nv3_d3d_map_texture(void)
{
    nv3_d3d_state_t *d = &nv3->pgraph.prim.d3d;

    if (d->tex_mapped)
        return;
    d->tex_target = nv3_dma_map_pages(nv3->pgraph.dma_settings & 0xFFFF, d->tex_offset, d->tex_pages, D3_TEX_PAGES, &d->tex_lin);
    d->tex_mapped = true;
    for (int i = 0; i < D3_TEX_PAGES; i++) {
        uint32_t ra;

        d->tex_rpages[i] = d->tex_pages[i];
        if ((d->tex_target != NV3_DMA_TARGET_NODE_VRAM) && agpgart_resolve(d->tex_pages[i], &ra))
            d->tex_rpages[i] = ra & ~0xFFFu;
    }
}

/* The canvas (one nobody set up does not clip) and, unless the class clips with its own
   rectangle, the user clip, as nv3_render_clip_pass has them: within 0..2048, maxima exclusive */
static void
nv3_d3d_clip_box(uint32_t ctx, bool own_clip, int32_t *box)
{
    int32_t cmin_x = nv3->pgraph.dst_canvas_min & 0x7FF;
    int32_t cmin_y = (nv3->pgraph.dst_canvas_min >> 16) & 0x3FFF;
    int32_t cmax_x = nv3->pgraph.dst_canvas_max & 0x7FF;
    int32_t cmax_y = (nv3->pgraph.dst_canvas_max >> 16) & 0x3FFF;

    box[0] = 0;
    box[1] = 0;
    box[2] = 2048;
    box[3] = 2048;
    if ((cmax_x > cmin_x) && (cmax_y > cmin_y)) {
        box[0] = cmin_x;
        box[1] = cmin_y;
        box[2] = (cmax_x < box[2]) ? cmax_x : box[2];
        box[3] = (cmax_y < box[3]) ? cmax_y : box[3];
    }
    if (!own_clip && ((ctx >> NV3_PGRAPH_CTX_SWITCH_USER_CLIP) & 1)) {
        if (nv3->pgraph.uclip_min[0] > box[0])
            box[0] = nv3->pgraph.uclip_min[0];
        if (nv3->pgraph.uclip_min[1] > box[1])
            box[1] = nv3->pgraph.uclip_min[1];
        if (nv3->pgraph.uclip_max[0] < box[2])
            box[2] = nv3->pgraph.uclip_max[0];
        if (nv3->pgraph.uclip_max[1] < box[3])
            box[3] = nv3->pgraph.uclip_max[1];
    }
}

/* The draw state of what is drawn now, through the object grobj (its CTX_SWITCH) */
static void
nv3_d3d_make_state(nv3_d3d_dstate_t *s, nv3_grobj_t grobj, bool textures)
{
    uint32_t ctx = grobj.grobj_0;
    int32_t  box[4];

    memset(s, 0, sizeof(*s));
    s->bpp       = 2;
    s->config    = nv3->pgraph.d3d_config;
    s->alpha_ctl = nv3->pgraph.d3d_alpha;
    for (int j = 0; j < 3; j++)
        if ((ctx >> (NV3_PGRAPH_CTX_SWITCH_DST_BUFFER0_ENABLED + j)) & 1)
            s->buf_on |= 1 << j;
    s->z_on = (ctx >> NV3_PGRAPH_CTX_SWITCH_Z_WRITE) & 1;
    for (int j = 0; j < 4; j++) {
        s->boff[j]   = nv3->pgraph.boffset[j];
        s->bpitch[j] = nv3->pgraph.bpitch[j];
    }
    nv3_d3d_clip_box(ctx, false, box);
    s->cx0 = box[0];
    s->cy0 = box[1];
    s->cx1 = box[2];
    s->cy1 = box[3];
    s->ncr = nv3->pgraph.cliprect_ctrl & 3;
    if (s->ncr == 3)
        s->ncr = 2;
    s->cr_inv = !!(nv3->pgraph.cliprect_ctrl & 0x10);
    for (int i = 0; i < s->ncr; i++) {
        s->cr_x0[i] = nv3->pgraph.cliprect_min[i] & 0xFFFF;
        s->cr_y0[i] = nv3->pgraph.cliprect_min[i] >> 16;
        s->cr_x1[i] = nv3->pgraph.cliprect_max[i] & 0xFFFF;
        s->cr_y1[i] = nv3->pgraph.cliprect_max[i] >> 16;
    }
    s->dither  = (nv3->pgraph.debug_3 >> NV3_PGRAPH_DEBUG_3_DITHER) & 1;
    s->fog_rgb = nv3->pgraph.prim.d3d.fog_color & 0xFFFFFF;

    if (textures) {
        nv3_d3d_state_t *d = &nv3->pgraph.prim.d3d;
        int              max_log, min_log;
        uint32_t         total = 0;

        s->tex_format = d->tex_format;
        s->textured   = (d->tex_format >> 28) || ((d->tex_format >> 24) & 0xF);
        nv3_d3d_tex_logs(d->tex_format, &max_log, &min_log);
        for (int l = 0; l <= max_log - min_log; l++)
            total += (1u << (max_log - l)) * (1u << (max_log - l)) * 2;
        nv3_d3d_map_texture();
        s->tex_vram = d->tex_target == NV3_DMA_TARGET_NODE_VRAM;
        s->tex_l0   = d->tex_lin & 0xFFF;
        s->tex_size = (total < D3_TEX_PAGES * 0x1000 - s->tex_l0) ? total : (D3_TEX_PAGES * 0x1000 - s->tex_l0);
        memcpy(s->tex_pages, d->tex_rpages, sizeof(s->tex_pages));
    }
}

/* The batch's draw state for what is drawn now: the last one when nothing changed */
static int
nv3_d3d_state(nv3_d3d_batch_t *b, nv3_d3d_dstate_t *s)
{
    if (b->nstates) {
        const nv3_d3d_dstate_t *o = &b->states[b->nstates - 1];

        if (!memcmp(s, o, offsetof(nv3_d3d_dstate_t, rg)))
            return b->nstates - 1;
    }
    return nv3_d3d_place_state(b, s);
}

/* ---- 2D fills joining the batch ---- */

/* A 2D rectangle fill (rows y0..y1-1, pixels x0..x1-1, already clamped at 0) of colour c through
   object grobj, queued with the 3D work instead of drawn now: when it stores one value in every
   pixel (nv3_render_fill_value: the ROP takes only the source, the result is the same anywhere,
   whatever the pixel held) and either draws into surfaces the batch already draws into (the HUD,
   a clear between passes) or is large while 3D is being drawn (a frame's colour and zeta
   clears). The clip rectangles keep it out (they cut rectangles into pieces). Returns false when
   the caller must draw it. */
bool
nv3_d3d_fill(int32_t x0, int32_t y0, int32_t x1, int32_t y1, nv3_color_expanded_t c, nv3_grobj_t grobj, bool own_clip)
{
    nv3_d3d_batch_t *b   = nv3 ? nv3->batch3d : NULL;
    uint32_t         ctx = grobj.grobj_0;
    nv3_d3d_dstate_t st;
    nv3_d3d_tri_t   *t;
    int32_t          box[4];
    int              idx[4], n, si = -1, joins, big;
    uint32_t         val, cpp, lo, hi;

    /* past 2048 the clip box below would cut what the 2D engine draws */
    if (!b || (nv3->pgraph.cliprect_ctrl & 3) || (x1 > 2048) || (y1 > 2048))
        return false;
    nv3_d3d_clip_box(ctx, own_clip, box);
    if (x0 < box[0])
        x0 = box[0];
    if (y0 < box[1])
        y0 = box[1];
    if (x1 > box[2])
        x1 = box[2];
    if (y1 > box[3])
        y1 = box[3];
    if ((x0 >= x1) || (y0 >= y1))
        return true; /* nothing to draw */
    if (!nv3_render_fill_value(c, grobj, &val))
        return false;
    cpp = nv3_render_cpp(nv3_render_surface_format(grobj));

    memset(&st, 0, sizeof(st));
    st.fill = cpp;
    st.bpp  = cpp;
    for (int j = 0; j < NV3_PGRAPH_MAX_BUFFERS; j++)
        if ((ctx >> (NV3_PGRAPH_CTX_SWITCH_DST_BUFFER0_ENABLED + j)) & 1)
            st.buf_on |= 1 << j;
    if (!st.buf_on)
        return true;
    for (int j = 0; j < 4; j++) {
        st.boff[j]   = nv3->pgraph.boffset[j];
        st.bpitch[j] = nv3->pgraph.bpitch[j];
    }
    st.cx0 = x0;
    st.cy0 = y0;
    st.cx1 = x1;
    st.cy1 = y1;
    n      = nv3_d3d_layouts(&st, idx);

    joins = b->nstates > 0;
    for (int i = 0; joins && b->parallel && (i < n); i++) {
        nv3_d3d_reach(&st, st.boff[idx[i]], st.bpitch[idx[i]], &lo, &hi);
        joins = nv3_d3d_region_find(b, st.boff[idx[i]], st.bpitch[idx[i]], lo, hi) >= 0;
    }
    big = b->had3d && ((b->vblanks - b->last3d) <= 2) && ((int64_t) (x1 - x0) * (y1 - y0) >= 65536);
    if (!joins && !big)
        return false;
    if ((b->ntris >= D3_TRIS) || (b->nspans + (y1 - y0) > D3_SPANS))
        nv3_d3d_flush();

    /* one state serves every fill into the same surfaces; its region grows with each */
    for (int i = 0; i < b->nstates; i++) {
        const nv3_d3d_dstate_t *o = &b->states[i];

        if (o->fill && (o->fill == st.fill) && (o->buf_on == st.buf_on) && !memcmp(o->boff, st.boff, sizeof(st.boff))
            && !memcmp(o->bpitch, st.bpitch, sizeof(st.bpitch))) {
            si = i;
            break;
        }
    }
    if ((si >= 0) && b->parallel) {
        const nv3_d3d_dstate_t *o = &b->states[si];

        for (int i = 0; (si >= 0) && (i < n); i++) {
            nv3_d3d_reach(&st, st.boff[idx[i]], st.bpitch[idx[i]], &lo, &hi);
            /* it would grow into another layout, or that surface is shaded serially: a new state */
            if ((o->rg[idx[i]] < 0) || (nv3_d3d_region_find(b, st.boff[idx[i]], st.bpitch[idx[i]], lo, hi) < 0))
                si = -1;
        }
        for (int i = 0; (si >= 0) && (i < n); i++) {
            nv3_d3d_reach(&st, st.boff[idx[i]], st.bpitch[idx[i]], &lo, &hi);
            nv3_d3d_region_take(b, st.boff[idx[i]], st.bpitch[idx[i]], lo, hi);
        }
    }
    if (si < 0)
        si = nv3_d3d_place_state(b, &st);

    t = &b->tris[b->ntris];
    memset(t, 0, sizeof(*t));
    t->state = si;
    memcpy(&t->c[0], &val, 4);
    for (int32_t y = y0; y < y1; y++) {
        b->spans[b->nspans].y   = (int16_t) y;
        b->spans[b->nspans].x0  = (int16_t) x0;
        b->spans[b->nspans].x1  = (int16_t) x1;
        b->spans[b->nspans].tri = (uint16_t) b->ntris;
        b->nspans++;
    }
    if (b->parallel)
        nv3_d3d_written(b, &b->states[si], y0, y1 - 1, x1);
    b->ntris++;
    return true;
}

/* ---- triangle setup ---- */

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

/* Is pixel (x, y) inside the triangle? Barycentric weights at the pixel (D3D samples at integer
   coordinates); top-left rule. In double: multipass effects draw the same plane tessellated
   differently and compare with Z EQUAL (3DMark 99's filtering tunnel: 286 triangles write Z, 1626
   test it), and float weights on small triangles were off by up to a 16-bit Z step */
static bool
nv3_d3d_inside(const nv3_d3d_vertex_t *const v[3], double darea, float area, int32_t x, int32_t y)
{
    for (int e = 0; e < 3; e++) {
        const nv3_d3d_vertex_t *p  = v[(e + 1) % 3];
        const nv3_d3d_vertex_t *q  = v[(e + 2) % 3];
        double                  ef = ((double) q->x - p->x) * ((double) y - p->y) - ((double) q->y - p->y) * ((double) x - p->x);
        double                  wd = ef / darea;

        if (wd < 0.0)
            return false;
        if (wd == 0.0) {
            float dy = (area > 0.0f) ? (q->y - p->y) : (p->y - q->y);
            float dx = (area > 0.0f) ? (q->x - p->x) : (p->x - q->x);
            if (!((dy < 0.0f) || (dy == 0.0f && dx > 0.0f)))
                return false;
        }
    }
    return true;
}

/* The pixels of row y inside the triangle, within bx0..bx1: from where each edge crosses the
   row, then settled with the pixel test itself. False when none. */
static bool
nv3_d3d_row(const nv3_d3d_vertex_t *const v[3], double darea, float area, int32_t y, int32_t bx0, int32_t bx1, int32_t *plo,
            int32_t *phi)
{
    double  lo = bx0, hi = bx1;
    int32_t l, h;

    for (int e = 0; e < 3; e++) {
        const nv3_d3d_vertex_t *p = v[(e + 1) % 3];
        const nv3_d3d_vertex_t *q = v[(e + 2) % 3];
        double                  A = ((double) q->x - p->x) * ((double) y - p->y);
        double                  B = (double) q->y - p->y;
        double                  s = (darea > 0.0) ? 1.0 : -1.0;

        /* the edge function A - B (x - p.x), times the sign of the area, must not be negative */
        if (B == 0.0) {
            if (s * A < 0.0)
                return false;
            continue;
        }
        double xb = p->x + A / B;
        if (s * B > 0.0) {
            if (xb < hi)
                hi = xb;
        } else if (xb > lo)
            lo = xb;
    }
    if (lo > hi + 2.0)
        return false;
    l = (int32_t) ceil(lo);
    h = (int32_t) floor(hi);
    if (l < bx0)
        l = bx0;
    if (h > bx1)
        h = bx1;
    /* rounding near the edges: find a pixel inside, then the run's ends */
    if (l > h) {
        int32_t a = h - 1, z = l + 1, seed = INT32_MIN;

        if (a < bx0)
            a = bx0;
        if (z > bx1)
            z = bx1;
        for (int32_t x = a; x <= z; x++)
            if (nv3_d3d_inside(v, darea, area, x, y)) {
                seed = x;
                break;
            }
        if (seed == INT32_MIN)
            return false;
        l = h = seed;
    }
    while ((l <= h) && !nv3_d3d_inside(v, darea, area, l, y))
        l++;
    while ((h >= l) && !nv3_d3d_inside(v, darea, area, h, y))
        h--;
    if (l > h)
        return false;
    while ((l > bx0) && nv3_d3d_inside(v, darea, area, l - 1, y))
        l--;
    while ((h < bx1) && nv3_d3d_inside(v, darea, area, h + 1, y))
        h++;
    *plo = l;
    *phi = h;
    return true;
}

/* An attribute's plane: its value at vertex a and its d/dx, d/dy, from its values f[] at the
   three vertices; darea is twice the triangle's signed area */
static void
nv3_d3d_plane(nv3_d3d_tri_t *t, int at, const double f[3], const nv3_d3d_vertex_t *a, const nv3_d3d_vertex_t *b,
              const nv3_d3d_vertex_t *c, double darea)
{
    double df1 = f[1] - f[0];
    double df2 = f[2] - f[0];

    t->c[at]  = (float) f[0];
    t->dx[at] = (float) ((df1 * ((double) c->y - a->y) - df2 * ((double) b->y - a->y)) / darea);
    t->dy[at] = (float) ((df2 * ((double) b->x - a->x) - df1 * ((double) c->x - a->x)) / darea);
}

static void
nv3_d3d_triangle(nv3_grobj_t grobj, const nv3_d3d_vertex_t *a_in, const nv3_d3d_vertex_t *b_in, const nv3_d3d_vertex_t *c_in)
{
    nv3_d3d_batch_t        *bt = nv3->batch3d;
    nv3_d3d_vertex_t        va = *a_in, vb = *b_in, vc = *c_in;
    const nv3_d3d_vertex_t *a = &va, *b = &vb, *c = &vc;
    const nv3_d3d_vertex_t *const v[3] = { a, b, c };
    uint32_t                config = nv3->pgraph.d3d_config;
    nv3_d3d_dstate_t        st;

    if (NV3_D3D_CONFIG_WRAP_U(config) == 0)
        nv3_d3d_cylinder(&va.u, &vb.u, &vc.u);
    if (NV3_D3D_CONFIG_WRAP_V(config) == 0)
        nv3_d3d_cylinder(&va.v, &vb.v, &vc.v);
    float                   area   = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    double                  darea  = ((double) b->x - a->x) * ((double) c->y - a->y) - ((double) b->y - a->y) * ((double) c->x - a->x);
    int                     cull   = NV3_D3D_CONFIG_CULL(config);

    if (area == 0.0f || isnan(area) || (darea == 0.0))
        return;
    /* y points down: area > 0 is clockwise on screen */
    if ((cull == 2 && area < 0.0f) || (cull == 3 && area > 0.0f))
        return;

    nv3_d3d_make_state(&st, grobj, true);

    float   minx = fminf(a->x, fminf(b->x, c->x));
    float   maxx = fmaxf(a->x, fmaxf(b->x, c->x));
    float   miny = fminf(a->y, fminf(b->y, c->y));
    float   maxy = fmaxf(a->y, fmaxf(b->y, c->y));
    int32_t x0   = (int32_t) fmaxf(ceilf(minx), 0.0f);
    int32_t x1   = (int32_t) fminf(floorf(maxx), 2047.0f);
    int32_t y0   = (int32_t) fmaxf(ceilf(miny), 0.0f);
    int32_t y1   = (int32_t) fminf(floorf(maxy), 2047.0f);

    if (x0 < st.cx0)
        x0 = st.cx0;
    if (x1 > st.cx1 - 1)
        x1 = st.cx1 - 1;
    if (y0 < st.cy0)
        y0 = st.cy0;
    if (y1 > st.cy1 - 1)
        y1 = st.cy1 - 1;
    if ((x0 > x1) || (y0 > y1) || !(st.buf_on || st.z_on))
        return;

    /* room for the triangle and a span per row */
    if ((bt->ntris >= D3_TRIS) || (bt->nspans + (y1 - y0 + 1) > D3_SPANS))
        nv3_d3d_flush();

    /* the state first: placing it can flush the batch */
    int            si = nv3_d3d_state(bt, &st);
    nv3_d3d_tri_t *t  = &bt->tris[bt->ntris];
    t->state          = si;
    t->x0             = a->x;
    t->y0            = a->y;

    /* the attributes: depth, q = 1/w (perspective-correct texture coordinates u*q, v*q and q are
       linear in screen space), the vertex colour and fog */
    bool   persp = (a->rhw > 0.0f) && isfinite(a->rhw) && (b->rhw > 0.0f) && isfinite(b->rhw) && (c->rhw > 0.0f) && isfinite(c->rhw);
    double f[3];

    for (int i = 0; i < 3; i++)
        f[i] = v[i]->z;
    nv3_d3d_plane(t, D3_AT_Z, f, a, b, c, darea);
    for (int i = 0; i < 3; i++)
        f[i] = persp ? v[i]->rhw : 1.0;
    nv3_d3d_plane(t, D3_AT_Q, f, a, b, c, darea);
    for (int i = 0; i < 3; i++)
        f[i] = (double) v[i]->u * (persp ? v[i]->rhw : 1.0);
    nv3_d3d_plane(t, D3_AT_UQ, f, a, b, c, darea);
    for (int i = 0; i < 3; i++)
        f[i] = (double) v[i]->v * (persp ? v[i]->rhw : 1.0);
    nv3_d3d_plane(t, D3_AT_VQ, f, a, b, c, darea);
    for (int ch = 0; ch < 4; ch++) {
        for (int i = 0; i < 3; i++)
            f[i] = (v[i]->color >> (24 - 8 * ch)) & 0xFF;
        nv3_d3d_plane(t, D3_AT_A + ch, f, a, b, c, darea);
    }
    for (int i = 0; i < 3; i++)
        f[i] = v[i]->fog;
    nv3_d3d_plane(t, D3_AT_FOG, f, a, b, c, darea);

    /* a span per row */
    const nv3_d3d_dstate_t *s    = &bt->states[t->state];
    int32_t                 ymin = INT32_MAX, ymax = INT32_MIN;

    for (int32_t y = y0; y <= y1; y++) {
        int32_t lo, hi;

        if (!nv3_d3d_row(v, darea, area, y, x0, x1, &lo, &hi))
            continue;
        bt->spans[bt->nspans].y   = (int16_t) y;
        bt->spans[bt->nspans].x0  = (int16_t) lo;
        bt->spans[bt->nspans].x1  = (int16_t) (hi + 1);
        bt->spans[bt->nspans].tri = (uint16_t) bt->ntris;
        bt->nspans++;
        if (y < ymin)
            ymin = y;
        ymax = y;
    }
    if (bt->parallel && (ymin <= ymax))
        nv3_d3d_written(bt, s, ymin, ymax, s->cx1);
    bt->ntris++;
    bt->had3d  = 1;
    bt->last3d = bt->vblanks;

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
            nv3->pgraph.prim.d3d.tex_offset = param;
            nv3_d3d_texture_changed();
            return;
        case 0x0308:
            nv3->pgraph.prim.d3d.tex_format = param;
            nv3_d3d_texture_changed();
            return;
        case 0x030C:
            nv3->pgraph.prim.d3d.filter = param;
            return;
        case 0x0310:
            nv3->pgraph.prim.d3d.fog_color = param;
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
                nv3->pgraph.prim.d3d.fog_tri = param;
                nv3->pgraph.prim.d3d.cur.fog = param >> 24;
                break;
            case 1:
                nv3->pgraph.prim.d3d.cur.color = param;
                break;
            case 2:
                nv3->pgraph.prim.d3d.cur.x = nv3_d3d_float(param);
                break;
            case 3:
                nv3->pgraph.prim.d3d.cur.y = nv3_d3d_float(param);
                break;
            case 4:
                nv3->pgraph.prim.d3d.cur.z = nv3_d3d_float(param);
                break;
            case 5:
                nv3->pgraph.prim.d3d.cur.rhw = nv3_d3d_float(param);
                break;
            case 6:
                nv3->pgraph.prim.d3d.cur.u = nv3_d3d_float(param);
                break;
            case 7: /* V: the vertex is complete; store it and draw what it asks for */
            {
                nv3->pgraph.prim.d3d.cur.v = nv3_d3d_float(param);
                nv3->pgraph.prim.d3d.vtx[nv3->pgraph.prim.d3d.fog_tri & 0xF] = nv3->pgraph.prim.d3d.cur;

                for (int t = 0; t < 2; t++) {
                    uint32_t i0 = (nv3->pgraph.prim.d3d.fog_tri >> (t * 12)) & 0xF;
                    uint32_t i1 = (nv3->pgraph.prim.d3d.fog_tri >> (t * 12 + 4)) & 0xF;
                    uint32_t i2 = (nv3->pgraph.prim.d3d.fog_tri >> (t * 12 + 8)) & 0xF;
                    if (i0 != i1 && i1 != i2 && i0 != i2)
                        nv3_d3d_triangle(grobj, &nv3->pgraph.prim.d3d.vtx[i0], &nv3->pgraph.prim.d3d.vtx[i1], &nv3->pgraph.prim.d3d.vtx[i2]);
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
            nv3->pgraph.prim.d3d.zpoint_x = (int16_t) (param & 0xFFFF);
            nv3->pgraph.prim.d3d.zpoint_y = (int16_t) (param >> 16);
            return;
        default:
            break;
    }

    if (method_id >= 0x0800 && method_id < 0x1000) {
        if (!(method_id & 4))
            nv3->pgraph.prim.d3d.zpoint_color = param;
        else {
            /* the colour is in the object's colour format; the point steps right. Drawn at once,
               after the triangles queued before it. */
            nv3_color_expanded_t c   = nv3_render_expand_color(nv3->pgraph.prim.d3d.zpoint_color, grobj);
            uint32_t             rgb = (((uint32_t) c.a) << 24) | ((uint32_t) (c.r >> 2) << 16) | ((uint32_t) (c.g >> 2) << 8) | (c.b >> 2);
            int32_t              x   = nv3->pgraph.prim.d3d.zpoint_x;
            int32_t              y   = nv3->pgraph.prim.d3d.zpoint_y;
            nv3_d3d_dstate_t     st;

            nv3_d3d_sync(NV3_SYNC_OTHER);
            nv3_d3d_make_state(&st, grobj, false);
            if ((x >= st.cx0) && (y >= st.cy0) && (x < st.cx1) && (y < st.cy1)) {
                nv3_d3d_fragment(&st, x, y, rgb, rgb >> 24, param >> 16);
                nv3_d3d_mark(&st, y, x, x + 1);
            }
            nv3->pgraph.prim.d3d.zpoint_x++;
        }
        return;
    }

    nv_warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
    nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
}

/* ---- the batch's life ---- */

void
nv3_d3d_reset(void)
{
    nv3_d3d_batch_t *b = nv3 ? nv3->batch3d : NULL;

    if (!b)
        return;
    if (b->gpu)
        nv3_gpu_wait(b->gpu);
    nv3_d3d_empty(b);
}

void
nv3_d3d_init(int threads, int gpu)
{
    nv3_d3d_batch_t *b = calloc(1, sizeof(nv3_d3d_batch_t));
    const char      *v = getenv("NV3_3D_VERIFY");

    nv3->batch3d = b;
    nv3_d3d_empty(b);
    if (threads < 1)
        threads = 1;
    if (threads > D3_MAX_WORKERS)
        threads = D3_MAX_WORKERS;
    b->nthreads = threads;
    if (gpu)
        b->gpu = nv3_gpu_init();
    b->parallel = (threads > 1) || b->gpu;
    if (b->parallel && v && atoi(v)) {
        b->verify = 1;
        b->vsnap  = malloc(nv3->nvbase.svga.vram_max);
        b->vres   = malloc(nv3->nvbase.svga.vram_max);
        pclog("nv3 3D: verify mode, every parallel batch is shaded twice\n");
    }
    if (threads > 1) {
        b->run = 1;
        for (int i = 0; i < threads; i++) {
            b->workers[i].id     = i;
            b->workers[i].wake   = thread_create_event();
            b->workers[i].done   = thread_create_event();
            b->workers[i].thread = thread_create(nv3_d3d_worker, &b->workers[i]);
        }
    }
    pclog("nv3 3D: %d render thread%s%s\n", threads, (threads > 1) ? "s" : "", b->gpu ? ", GPU lane on" : "");
}

void
nv3_d3d_close(void)
{
    nv3_d3d_batch_t *b = nv3 ? nv3->batch3d : NULL;

    if (!b)
        return;
    nv3_d3d_flush();
    nv3_d3d_stats();
    if (b->gpu)
        nv3_gpu_close(b->gpu);
    if (b->run) {
        b->run = 0;
        for (int i = 0; i < b->nthreads; i++)
            thread_set_event(b->workers[i].wake);
        for (int i = 0; i < b->nthreads; i++) {
            thread_wait(b->workers[i].thread);
            thread_destroy_event(b->workers[i].wake);
            thread_destroy_event(b->workers[i].done);
        }
    }
    free(b->vsnap);
    free(b->vres);
    free(b);
    nv3->batch3d = NULL;
}
