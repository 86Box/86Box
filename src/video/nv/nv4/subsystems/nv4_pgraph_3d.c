/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PGRAPH Direct3D classes.
 *
 *          The DX5 textured triangle (0x54) and the DX6 multitexture
 *          triangle (0x55): transformed and lit vertices, state, and
 *          DrawPrimitive words naming two triangles by vertex index.
 *
 *          The work is split the way the Rage 128's is, so the per-pixel
 *          part can later run on worker threads or a GPU kernel:
 *
 *            state    Methods set per-channel raw state. Before a triangle
 *                     it is derived into a draw state: resolved surfaces,
 *                     clip, texture levels and modes. Draw states are
 *                     immutable once made; a triangle names the one it was
 *                     submitted under.
 *            setup    On the submitting thread: vertices snapped to the
 *                     chip's 12.4 fixed point, culled, cut into one span
 *                     per row with exact integer edge functions (top-left
 *                     rule), attributes turned into plane equations.
 *            shading  Per span, at flush: texture fetch, texture blend or
 *                     combiners, specular, fog, alpha test, stencil, depth,
 *                     framebuffer blend, dither, write masks.
 *
 *          A batch is flushed before anything that must see its pixels:
 *          another class's method, a notifier, CPU access (linear or the
 *          VGA window) to bytes it draws into or samples, the start of
 *          vertical blank (so a frame is on screen by the next one), a
 *          full batch. With the GPU lane a flush only submits; nv4_3d_sync
 *          also waits for the pixels, for the classes that touch VRAM
 *          themselves, the framebuffer windows and the 2D classes wait
 *          only for batches whose rows they reach (nv4_3d_touch), and
 *          vertical blank waits when the batch draws into the displayed
 *          bytes. Not when the FIFO runs dry: the driver moves PUT every
 *          few triangles, and small batches cost the GPU lane a dispatch
 *          each.
 *
 *          With the "Render threads" option above 1 a flush shades on
 *          worker threads. Every worker walks the whole span list in
 *          submission order and shades only the rows it owns (y mod n),
 *          so no two threads write the same bytes and each pixel sees
 *          the writes in the serial order: the output is bit-exact with
 *          one thread. That holds while every row of the batch maps to
 *          its own bytes, so a batch is flushed first when the render
 *          target's layout changes or a texture would read bytes the
 *          batch has written, and a batch whose surfaces alias rows
 *          (zero pitch, depth rows narrower than color rows, a swizzled
 *          target) is shaded serially. The GPU lane (nv4_pgraph_3d_gpu.c)
 *          takes a batch under the same rules, one workgroup per row,
 *          and one whose rows alias as a single workgroup walking its
 *          spans in submission order (else the CPU). NV4_3D_VERIFY=1
 *          in the environment shades every parallel batch twice, in
 *          parallel and serially, and logs any byte that differs.
 *
 *          Method layouts follow envytools (hwtest pgraph_class_d3d56;
 *          MIT) and the public nv32 class definitions; the per-pixel
 *          arithmetic follows Direct3D's definitions of each operation,
 *          as the chip's own is not documented.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/agpgart.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/nv/vid_nv4.h>

#define GR(a) NV4_GR(nv4, a)

#include "nv4_pgraph_3d.h"

/* ------------------------------------------------------------------ */
/* Small helpers.                                                      */
/* ------------------------------------------------------------------ */
static float
f32(uint32_t v)
{
    union {
        uint32_t u;
        float    f;
    } c = { v };

    return c.f;
}

static int
clamp255(int v)
{
    return (v < 0) ? 0 : ((v > 255) ? 255 : v);
}

/* Float to int truncating, out of range (and NaN) giving INT_MIN as
   x86 does; the GPU kernel defines it the same way. */
static int
nv4_f2i(float f)
{
    return ((f > -2147483648.0f) && (f < 2147483648.0f)) ? (int) f : INT32_MIN;
}

static int
mul8(int a, int b)
{
    int t = a * b + 0x80;

    return (t + (t >> 8)) >> 8;
}

static int
bpp_of(int sfmt)
{
    if (sfmt >= 2 && sfmt <= 6)
        return 2;
    if (sfmt >= 7 && sfmt <= 0xd)
        return 4;
    return (sfmt == 1) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Draw state.                                                         */
/* ------------------------------------------------------------------ */
static void
nv4_3d_tex_setup(nv4_t *nv4, nv4_3d_tex_t *t, uint32_t fmt, uint32_t filt, uint32_t off, int dx6)
{
    static const int bpps[8] = { 0, 1, 2, 2, 2, 2, 4, 4 };
    int              dmasel  = fmt & (dx6 ? 0xf : 0x3);
    uint32_t         lo      = 0;

    memset(t, 0, sizeof(*t));
    t->fmt = (fmt >> 8) & 0xf;
    if ((t->fmt < 1) || (t->fmt > 7))
        return;
    t->bpp    = bpps[t->fmt];
    t->dma    = (dmasel == 2) ? (GR(NV4_PGRAPH_CTX_SWITCH3) >> 16) : (GR(NV4_PGRAPH_CTX_SWITCH3) & 0xffff);
    t->offset = off;
    t->wlog   = (fmt >> 16) & 0xf;
    t->hlog   = (fmt >> 20) & 0xf;
    t->levels = (fmt >> 12) & 0xf;
    if (t->wlog > 11)
        t->wlog = 11;
    if (t->hlog > 11)
        t->hlog = 11;
    if (!t->levels)
        t->levels = 1;
    if (t->levels > 12)
        t->levels = 12;
    for (int l = 0; l < t->levels; l++) {
        int wl = t->wlog - l;
        int hl = t->hlog - l;

        t->level_off[l] = lo;
        lo += (uint32_t) t->bpp << (((wl > 0) ? wl : 0) + ((hl > 0) ? hl : 0));
    }
    t->size     = lo;
    t->wrap_u   = (fmt >> 24) & 7;
    t->wrap_v   = (fmt >> 28) & 7;
    /* ORIGIN_FOH: CORNER (2) puts a texel's origin at its corner, so
       its center is at +0.5 and bilinear filtering steps back half a
       texel; NVIDIA's D3D driver (by default) and OpenGL ICD both use
       CORNER and expect the APIs' texel centers. CENTER (1) samples
       from the texel origin as is. */
    t->center   = ((fmt >> 6) & 3) != 1;
    t->ckey     = !dx6 && ((fmt >> 2) & 3) == 1;
    t->min      = (filt >> 24) & 7;
    t->mag      = (filt >> 28) & 7;
    t->lod_bias = (float) (int8_t) (filt >> 16) / 8.0f;
    t->on       = t->dma != 0;
}

/* Does the DX6 combiner setup read texture <n>? */
static int
nv4_3d_comb_uses(const nv4_3d_state_t *s, int n)
{
    for (int st = 0; st < 2; st++)
        for (int a = 0; a < 4; a++) {
            int sa = (s->comb_a[st] >> (a * 8 + 2)) & ((a == 3) ? 7 : 0x3f);
            int sc = (s->comb_c[st] >> (a * 8 + 2)) & ((a == 3) ? 7 : 0x3f);

            if ((sa == 5 + n) || (sc == 5 + n))
                return 1;
        }
    return 0;
}

/* Do two VRAM byte ranges meet? */
static int
nv4_3d_meets(uint32_t lo0, uint32_t hi0, uint32_t lo1, uint32_t hi1)
{
    return (lo0 <= hi1) && (lo1 <= hi0);
}

/* Would texture <t> read VRAM the batch has written? */
static int
nv4_3d_tex_hazard(nv4_t *nv4, const nv4_3d_batch_t *b, const nv4_3d_tex_t *t)
{
    uint32_t first, last;

    if (!t->on || !b->nregions)
        return 0;
    first = t->offset & ~0xfffu;
    last  = (t->offset + t->size - 1) & ~0xfffu;
    if (((last - first) >> 12) >= 1024)
        return 1; /* too many pages to look at: assume it does */
    for (uint32_t o = first;; o += 0x1000) {
        uint32_t a;
        int      target;

        if ((nv4_dma_translate(nv4, t->dma, o, 0, &a, &target) == NV4_DMA_OK) && !target) {
            a &= ~0xfffu;
            for (int r = 0; r < b->nregions; r++)
                if ((b->regions[r].wlo <= b->regions[r].whi)
                    && nv4_3d_meets(a, a + 0xfff, b->regions[r].wlo, b->regions[r].whi))
                    return 1;
        }
        if (o == last)
            break;
    }
    return 0;
}

/* Widen lo..hi to the VRAM pages texture <t> reads. */
static void
nv4_3d_tex_range(nv4_t *nv4, const nv4_3d_tex_t *t, uint32_t *lo, uint32_t *hi)
{
    uint32_t first, last;

    if (!t->on)
        return;
    first = t->offset & ~0xfffu;
    last  = (t->offset + t->size - 1) & ~0xfffu;
    if (((last - first) >> 12) >= 1024) {
        *lo = 0;
        *hi = 0xffffffff;
        return;
    }
    for (uint32_t o = first;; o += 0x1000) {
        uint32_t a;
        int      target;

        if ((nv4_dma_translate(nv4, t->dma, o, 0, &a, &target) == NV4_DMA_OK) && !target) {
            a &= ~0xfffu;
            if (a < *lo)
                *lo = a;
            if (a + 0xfff > *hi)
                *hi = a + 0xfff;
        }
        if (o == last)
            break;
    }
}

/* The bytes a state can reach through layout base/pitch: its clip's
   rows, or at least one row's pixels. */
static void
nv4_3d_reach(const nv4_3d_state_t *s, uint32_t base, uint32_t pitch, uint32_t *lo, uint32_t *hi)
{
    uint32_t rows = (uint32_t) (s->cy1 - s->cy0) * pitch;
    uint32_t w    = (uint32_t) s->cx1 * s->bpp;

    *lo = base + (uint32_t) s->cy0 * pitch;
    *hi = *lo + ((rows > w) ? rows : w) - 1;
}

/* The batch region with layout base/pitch: its index, -1 when a new one
   can be added, -2 when one would meet another layout's bytes or the
   table is full. */
static int
nv4_3d_region_find(const nv4_3d_batch_t *b, uint32_t base, uint32_t pitch, uint32_t lo, uint32_t hi)
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
        if ((r != same) && nv4_3d_meets(lo, hi, b->regions[r].lo, b->regions[r].hi))
            return -2;
    if (same >= 0)
        return same;
    return (b->nregions < B3D_REGIONS) ? -1 : -2;
}

static int
nv4_3d_region_take(nv4_3d_batch_t *b, uint32_t base, uint32_t pitch, uint32_t lo, uint32_t hi)
{
    int r = nv4_3d_region_find(b, base, pitch, lo, hi);

    if (r < 0) {
        r                  = b->nregions++;
        b->regions[r].base = base;
        b->regions[r].pitch = pitch;
        b->regions[r].lo   = lo;
        b->regions[r].hi   = hi;
        b->regions[r].wlo  = 0xffffffff;
        b->regions[r].whi  = 0;
    } else {
        if (lo < b->regions[r].lo)
            b->regions[r].lo = lo;
        if (hi > b->regions[r].hi)
            b->regions[r].hi = hi;
    }
    return r;
}

/* Put a derived state into the batch, flushing first when it cannot
   share the batch with what is there: the table is full, or, with
   worker threads or the GPU, its rows would meet another layout's bytes
   or its textures read bytes the batch writes. */
static int
nv4_3d_place_state(nv4_t *nv4, nv4_3d_batch_t *b, nv4_3d_state_t *s)
{
    int      flush = b->nstates >= B3D_STATES;
    uint32_t clo = 0, chi = 0, zlo = 0, zhi = 0;

    s->rc = -1;
    s->rz = -1;
    /* a batch is either all swizzled targets or none, so the scene
       around a render to texture keeps its threads or GPU */
    if (b->parallel)
        for (int i = 0; i < b->nstates; i++)
            if (b->states[i].swz != s->swz)
                flush = 1;
    if (b->parallel && s->swz) {
        /* Swizzled rows are not runs of bytes, so the region model does
           not hold: the batch is shaded on one thread, in submission
           order, which also keeps any texture reading these pixels
           right. */
        if (flush)
            nv4_3d_flush(nv4);
        b->serial = 1;
        nv4_3d_tex_range(nv4, &s->tex[0], &b->tex_lo, &b->tex_hi);
        nv4_3d_tex_range(nv4, &s->tex[1], &b->tex_lo, &b->tex_hi);
        b->states[b->nstates] = *s;
        return b->nstates++;
    }
    if (b->parallel) {
        nv4_3d_reach(s, s->col_base, s->col_pitch, &clo, &chi);
        nv4_3d_reach(s, s->z_base, s->z_pitch, &zlo, &zhi);
        if (nv4_3d_region_find(b, s->col_base, s->col_pitch, clo, chi) == -2)
            flush = 1;
        if (s->z_on && (nv4_3d_region_find(b, s->z_base, s->z_pitch, zlo, zhi) == -2))
            flush = 1;
        if (nv4_3d_tex_hazard(nv4, b, &s->tex[0]) || nv4_3d_tex_hazard(nv4, b, &s->tex[1]))
            flush = 1;
    }
    if (flush)
        nv4_3d_flush(nv4);

    if (b->parallel) {
        uint32_t w = (uint32_t) s->cx1 * s->bpp;

        s->rc = nv4_3d_region_take(b, s->col_base, s->col_pitch, clo, chi);
        if (s->z_on) {
            /* depth meeting its own color rows: one thread, below */
            if (nv4_3d_region_find(b, s->z_base, s->z_pitch, zlo, zhi) == -2)
                b->serial = 1;
            else
                s->rz = nv4_3d_region_take(b, s->z_base, s->z_pitch, zlo, zhi);
        }
        /* rows that share bytes must stay on one thread */
        if (!s->col_pitch || (s->col_pitch < w))
            b->serial = 1;
        if (s->z_on) {
            if (!s->z_pitch || (s->z_pitch < w))
                b->serial = 1;
            else if (nv4_3d_meets(s->col_base, s->col_base + (uint32_t) s->cy1 * s->col_pitch,
                                  s->z_base, s->z_base + (uint32_t) s->cy1 * s->z_pitch))
                b->serial = 1;
        }
    }
    if (b->parallel) {
        nv4_3d_tex_range(nv4, &s->tex[0], &b->tex_lo, &b->tex_hi);
        nv4_3d_tex_range(nv4, &s->tex[1], &b->tex_lo, &b->tex_hi);
    }
    b->states[b->nstates] = *s;
    return b->nstates++;
}

/* Record the rows a triangle may write (or, for depth, read: the GPU
   lane uploads those too). */
static void
nv4_3d_region_touch(nv4_3d_region_t *g, uint32_t lo, uint32_t hi)
{
    if (lo < g->wlo)
        g->wlo = lo;
    if (hi > g->whi)
        g->whi = hi;
}

static void
nv4_3d_written(nv4_3d_batch_t *b, const nv4_3d_state_t *s, int32_t y0, int32_t y1)
{
    uint32_t w = (uint32_t) s->cx1 * s->bpp;

    if (s->rc >= 0) {
        uint32_t lo = s->col_base + (uint32_t) y0 * s->col_pitch;
        uint32_t n  = (uint32_t) (y1 + 1 - y0) * s->col_pitch;

        nv4_3d_region_touch(&b->regions[s->rc], lo, lo + ((n > w) ? n : w) - 1);
    }
    if (s->rz >= 0) {
        uint32_t lo = s->z_base + (uint32_t) y0 * s->z_pitch;
        uint32_t n  = (uint32_t) (y1 + 1 - y0) * s->z_pitch;

        nv4_3d_region_touch(&b->regions[s->rz], lo, lo + ((n > w) ? n : w) - 1);
    }
}

static int
nv4_3d_make_state(nv4_t *nv4, nv4_3d_batch_t *b, int cls, const nv4_d3d_t *d)
{
    nv4_3d_state_t *s;
    uint32_t        ctl = d->control[0];
    uint32_t        bl  = d->blend;
    uint32_t        lim;
    int             zfmt;
    nv4_3d_state_t  st;

    s = &st;
    memset(s, 0, sizeof(*s));

    s->dx6     = (cls == 0x55);
    /* Rendering into a texture: the 3D surface's FORMAT type 2 makes
       the color surface swizzled, 2^wl by 2^hl pixels from BSWIZZLE2,
       laid out as textures are. The depth surface is taken to follow
       the same addressing from its own offset (one address generator;
       not checked against the hardware), so its pitch is unused. */
    s->swz     = (GR(NV4_PGRAPH_SURFACE) == 2);
    s->swz_wl  = (GR(NV4_PGRAPH_BSWIZZLE2) >> 16) & 0xf;
    s->swz_hl  = (GR(NV4_PGRAPH_BSWIZZLE2) >> 24) & 0xf;
    s->col_fmt = (GR(NV4_PGRAPH_BPIXEL) >> 8) & 0xf;
    s->bpp     = bpp_of(s->col_fmt);
    s->col_base  = GR(NV4_PGRAPH_BBASE + 2 * 4) + GR(NV4_PGRAPH_BOFFSET + 2 * 4);
    s->col_pitch = GR(NV4_PGRAPH_BPITCH + 2 * 4) & 0xffff;
    lim          = GR(NV4_PGRAPH_BLIMIT + 2 * 4);
    if ((lim & 0x80000000) || !s->bpp) {
        b->st_nosurf++;
        return -1;
    }
    s->col_lim  = GR(NV4_PGRAPH_BBASE + 2 * 4) + (lim & 0x7fffffff);
    s->z_base   = GR(NV4_PGRAPH_BBASE + 3 * 4) + GR(NV4_PGRAPH_BOFFSET + 3 * 4);
    s->z_pitch  = GR(NV4_PGRAPH_BPITCH + 3 * 4) & 0xffff;
    lim         = GR(NV4_PGRAPH_BLIMIT + 3 * 4);
    s->z_on     = !(lim & 0x80000000) && (s->z_pitch || s->swz);
    s->z_lim    = GR(NV4_PGRAPH_BBASE + 3 * 4) + (lim & 0x7fffffff);

    s->cx0 = (int16_t) GR(NV4_PGRAPH_UCLIP_MIN);
    s->cy0 = (int16_t) GR(NV4_PGRAPH_UCLIP_MIN + 4);
    s->cx1 = (int32_t) (GR(NV4_PGRAPH_UCLIP_MAX) << 14) >> 14;
    s->cy1 = (int32_t) (GR(NV4_PGRAPH_UCLIP_MAX + 4) << 14) >> 14;
    if (s->cx0 < 0)
        s->cx0 = 0;
    if (s->cy0 < 0)
        s->cy0 = 0;
    if (s->swz) {
        if (s->cx1 > (1 << s->swz_wl))
            s->cx1 = 1 << s->swz_wl;
        if (s->cy1 > (1 << s->swz_hl))
            s->cy1 = 1 << s->swz_hl;
    } else if (s->col_pitch && (s->cx1 > (int32_t) (s->col_pitch / s->bpp)))
        s->cx1 = s->col_pitch / s->bpp;
    if (s->cx1 > 4096)
        s->cx1 = 4096;
    if (s->cy1 > 4096)
        s->cy1 = 4096;

    s->origin = ((ctl >> 13) & 1) ? 0.5f : 0.0f;
    if (s->dx6) {
        nv4_3d_tex_setup(nv4, &s->tex[0], d->format[0], d->filter[0], d->offset[0], 1);
        nv4_3d_tex_setup(nv4, &s->tex[1], d->format[1], d->filter[1], d->offset[1], 1);
        s->comb_a[0] = d->comb_alpha[0];
        s->comb_a[1] = d->comb_alpha[1];
        s->comb_c[0] = d->comb_color[0];
        s->comb_c[1] = d->comb_color[1];
        s->factor    = d->comb_factor;
        if (!nv4_3d_comb_uses(s, 0))
            s->tex[0].on = 0;
        if (!nv4_3d_comb_uses(s, 1))
            s->tex[1].on = 0;
    } else {
        nv4_3d_tex_setup(nv4, &s->tex[0], d->format[0], d->filter[0], d->offset[0], 0);
        s->texblend = bl & 0xf;
    }
    s->color_key = d->color_key;

    s->flat      = ((bl >> 6) & 3) == 1;
    s->persp     = (bl >> 8) & 0xf;
    s->spec      = (bl >> 12) & 0xf;
    s->fog       = (bl >> 16) & 0xf;
    s->ablend    = (bl >> 20) & 0xf;
    s->sblend    = (bl >> 24) & 0xf;
    s->dblend    = (bl >> 28) & 0xf;
    s->fog_color = d->fog_color;

    s->atest  = (ctl >> 12) & 1;
    s->afunc  = (ctl >> 8) & 0xf;
    s->aref   = ctl & 0xff;
    s->zen    = ((ctl >> 14) & 3) && s->z_on;
    s->zfunc  = (ctl >> 16) & 0xf;
    zfmt      = (ctl >> 30) & 3;
    s->zfloat = (zfmt == 2);
    s->cull   = (ctl >> 20) & 3;
    s->dither = (ctl >> 22) & 1;
    if (s->dx6) {
        s->zwrite = ((ctl >> 24) & 1) && s->z_on;
        s->swrite = (ctl >> 25) & 1;
        s->wr_a   = (ctl >> 26) & 1;
        s->wr_r   = (ctl >> 27) & 1;
        s->wr_g   = (ctl >> 28) & 1;
        s->wr_b   = (ctl >> 29) & 1;
        s->sten      = (d->control[1] & 0xf) && s->z_on && (s->bpp == 4);
        s->sfunc     = (d->control[1] >> 4) & 0xf;
        s->sref      = (d->control[1] >> 8) & 0xff;
        s->smask_r   = (d->control[1] >> 16) & 0xff;
        s->smask_w   = (d->control[1] >> 24) & 0xff;
        s->sop_fail  = d->control[2] & 0xf;
        s->sop_zfail = (d->control[2] >> 4) & 0xf;
        s->sop_zpass = (d->control[2] >> 8) & 0xf;
    } else {
        s->zwrite = ((ctl >> 24) & 0x3f) && s->z_on;
        s->wr_a = s->wr_r = s->wr_g = s->wr_b = 1;
    }
    return nv4_3d_place_state(nv4, b, s);
}

/* ------------------------------------------------------------------ */
/* Texturing.                                                          */
/* ------------------------------------------------------------------ */
/* Swizzled texel offset: u and v bits interleave, u first, until the
   smaller dimension runs out. */
static uint32_t
nv4_swizzle(uint32_t u, uint32_t v, int wl, int hl)
{
    uint32_t r   = 0;
    int      bit = 0;

    for (int i = 0; (i < wl) || (i < hl); i++) {
        if (i < wl)
            r |= ((u >> i) & 1) << bit++;
        if (i < hl)
            r |= ((v >> i) & 1) << bit++;
    }
    return r;
}

/* Byte offset of pixel x,y in a 3D surface of <pitch>, or in the
   state's swizzled target. */
static uint32_t
nv4_3d_pix(const nv4_3d_state_t *s, uint32_t pitch, int x, int y)
{
    if (s->swz)
        return nv4_swizzle(x, y, s->swz_wl, s->swz_hl) * s->bpp;
    return (uint32_t) y * pitch + (uint32_t) x * s->bpp;
}

typedef struct {
    uint32_t       page;   /* DMA offset of the cached page, ~0: none */
    uint32_t       addr;   /* its address */
    int            target; /* 0 VRAM, else bus */
    const uint8_t *host;   /* a bus page in system memory, NULL: read it
                              through the bus */
} nv4_3d_tcache_t;

static uint32_t
nv4_3d_tex_read(nv4_t *nv4, const nv4_3d_tex_t *t, nv4_3d_tcache_t *tc, uint32_t off, int bytes)
{
    uint32_t a;
    /* the window's page of <off> and the byte in it (the window starts
       part way into its first page) */
    uint32_t in   = 0x1000 - nv4_dma_page_left(nv4, t->dma, off);
    uint32_t page = off - in;

    if (page != tc->page) {
        uint32_t ra;

        if (nv4_dma_translate(nv4, t->dma, off, 0, &tc->addr, &tc->target) != NV4_DMA_OK)
            return 0;
        tc->addr -= in;
        tc->page = page;
        tc->host = NULL;
        if (tc->target) {
            /* system memory, through the AGP aperture if it points
               there: read it in place */
            ra = tc->addr & ~0xfffu;
            if (!agpgart_resolve(ra, &ra))
                ra = tc->addr & ~0xfffu;
            if ((ra >= 0x100000) && ((uint64_t) ra + 0x1000 <= (uint64_t) mem_size * 1024))
                tc->host = ram + ra;
        }
    }
    a = tc->addr + in;
    if (tc->host && (((tc->addr & 0xfff) + in + bytes) <= 0x1000)) {
        const uint8_t *h = tc->host + (tc->addr & 0xfff) + in;

        if (bytes == 1)
            return h[0];
        if (bytes == 2)
            return h[0] | (h[1] << 8);
        return h[0] | (h[1] << 8) | (h[2] << 16) | ((uint32_t) h[3] << 24);
    }
    if (!tc->target) {
        uint8_t *v = nv4->svga.vram;
        uint32_t m = nv4->vram_mask;

        if (bytes == 1)
            return v[a & m];
        if (bytes == 2)
            return v[a & m] | (v[(a + 1) & m] << 8);
        return v[a & m] | (v[(a + 1) & m] << 8) | (v[(a + 2) & m] << 16) | ((uint32_t) v[(a + 3) & m] << 24);
    }
    if (bytes == 1)
        return mem_readb_phys(a);
    if (bytes == 2)
        return mem_readw_phys(a);
    return mem_readl_phys(a);
}

static uint32_t
nv4_texel_argb(uint32_t v, int fmt)
{
    uint32_t r, g, b, a;

    switch (fmt) {
        case 1: /* Y8 / AY8 */
            return (v << 24) | (v * 0x010101);
        case 2: /* A1R5G5B5 */
        case 3: /* X1R5G5B5 */
            r = (v >> 10) & 0x1f;
            g = (v >> 5) & 0x1f;
            b = v & 0x1f;
            a = (fmt == 3 || (v & 0x8000)) ? 0xff : 0;
            return (a << 24) | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
        case 4: /* A4R4G4B4 */
            return ((v >> 12) * 0x11u << 24) | (((v >> 8) & 0xf) * 0x11 << 16) | (((v >> 4) & 0xf) * 0x11 << 8)
                | ((v & 0xf) * 0x11);
        case 5: /* R5G6B5 */
            r = (v >> 11) & 0x1f;
            g = (v >> 5) & 0x3f;
            b = v & 0x1f;
            return 0xff000000 | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
        case 6:
            return v;
        default:
            return v | 0xff000000;
    }
}

static int
nv4_wrap(int i, int n, int mode, int *border)
{
    switch (mode) {
        case 2: /* mirror */
            i &= 2 * n - 1;
            return (i >= n) ? (2 * n - 1 - i) : i;
        case 3: /* clamp */
            return (i < 0) ? 0 : ((i >= n) ? n - 1 : i);
        case 4: /* border */
            if ((i < 0) || (i >= n))
                *border = 1;
            return (i < 0) ? 0 : ((i >= n) ? n - 1 : i);
        default:
            return i & (n - 1);
    }
}

/* One texel of level <l>; returns 0 with *killed set for a color-keyed
   texel. */
static uint32_t
nv4_texel(nv4_t *nv4, const nv4_3d_tex_t *t, nv4_3d_tcache_t *tc, int l, int iu, int iv, uint32_t key, int *killed)
{
    int      wl     = (t->wlog - l > 0) ? t->wlog - l : 0;
    int      hl     = (t->hlog - l > 0) ? t->hlog - l : 0;
    int      border = 0;
    uint32_t raw;

    iu  = nv4_wrap(iu, 1 << wl, t->wrap_u, &border);
    iv  = nv4_wrap(iv, 1 << hl, t->wrap_v, &border);
    if (border)
        return 0;
    raw = nv4_3d_tex_read(nv4, t, tc, t->offset + t->level_off[l] + nv4_swizzle(iu, iv, wl, hl) * t->bpp, t->bpp);
    if (t->ckey && killed && (raw == (key & ((t->bpp == 4) ? 0xffffffff : ((t->bpp == 2) ? 0xffff : 0xff)))))
        *killed = 1;
    return nv4_texel_argb(raw, t->fmt);
}

static uint32_t
nv4_lerp4(uint32_t a, uint32_t b, int f) /* f 0-256 */
{
    uint32_t r = 0;

    for (int s = 0; s < 32; s += 8) {
        int ca = (a >> s) & 0xff;
        int cb = (b >> s) & 0xff;

        r |= (uint32_t) ((ca * (256 - f) + cb * f) >> 8) << s;
    }
    return r;
}

static uint32_t
nv4_sample_level(nv4_t *nv4, const nv4_3d_tex_t *t, nv4_3d_tcache_t *tc, int l, float u, float v, int linear,
                 uint32_t key, int *killed)
{
    int   wl = (t->wlog - l > 0) ? t->wlog - l : 0;
    int   hl = (t->hlog - l > 0) ? t->hlog - l : 0;
    float fu = u * (float) (1 << wl);
    float fv = v * (float) (1 << hl);

    if (!linear)
        return nv4_texel(nv4, t, tc, l, nv4_f2i(floorf(fu)), nv4_f2i(floorf(fv)), key, killed);
    if (t->center) {
        fu -= 0.5f;
        fv -= 0.5f;
    }
    {
        int      iu = nv4_f2i(floorf(fu));
        int      iv = nv4_f2i(floorf(fv));
        int      au = nv4_f2i((fu - (float) iu) * 256.0f);
        int      av = nv4_f2i((fv - (float) iv) * 256.0f);
        uint32_t t00 = nv4_texel(nv4, t, tc, l, iu, iv, key, killed);
        uint32_t t10 = nv4_texel(nv4, t, tc, l, iu + 1, iv, key, NULL);
        uint32_t t01 = nv4_texel(nv4, t, tc, l, iu, iv + 1, key, NULL);
        uint32_t t11 = nv4_texel(nv4, t, tc, l, iu + 1, iv + 1, key, NULL);

        return nv4_lerp4(nv4_lerp4(t00, t10, au), nv4_lerp4(t01, t11, au), av);
    }
}

/* Samples at u,v with level of detail <lod> (log2 texels per pixel). */
static uint32_t
nv4_sample(nv4_t *nv4, const nv4_3d_tex_t *t, nv4_3d_tcache_t *tc, float u, float v, float lod, uint32_t key,
           int *killed)
{
    int filt;

    lod += t->lod_bias;
    if (lod <= 0.0f || t->levels == 1)
        return nv4_sample_level(nv4, t, tc, 0, u, v, (lod <= 0.0f ? t->mag : t->min) != 1 && (lod <= 0.0f ? t->mag : t->min) != 3, key, killed);

    filt = t->min;
    switch (filt) {
        case 3: /* mip nearest */
        case 5: /* linear, mip nearest */
            {
                int l = nv4_f2i(lod + 0.5f);

                if (l >= t->levels)
                    l = t->levels - 1;
                return nv4_sample_level(nv4, t, tc, l, u, v, filt == 5, key, killed);
            }
        case 4: /* mip linear */
        case 6: /* trilinear */
            {
                int l = nv4_f2i(lod);
                int f = nv4_f2i((lod - (float) l) * 256.0f);

                if (l >= t->levels - 1)
                    return nv4_sample_level(nv4, t, tc, t->levels - 1, u, v, filt == 6, key, killed);
                return nv4_lerp4(nv4_sample_level(nv4, t, tc, l, u, v, filt == 6, key, killed),
                                 nv4_sample_level(nv4, t, tc, l + 1, u, v, filt == 6, key, NULL), f);
            }
        default:
            return nv4_sample_level(nv4, t, tc, 0, u, v, filt == 2, key, killed);
    }
}

/* ------------------------------------------------------------------ */
/* Combining.                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
    int r, g, b, a;
} rgba_t;

static rgba_t
rgba(uint32_t c)
{
    rgba_t r = { (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, c >> 24 };

    return r;
}

/* DX5 texture blend, as Direct3D defines it */
static rgba_t
nv4_texblend(int mode, rgba_t t, rgba_t d, int tex_alpha)
{
    rgba_t o = d;

    switch (mode) {
        case 1: /* decal */
        case 7: /* copy */
            o = t;
            break;
        case 2: /* modulate */
            o.r = mul8(t.r, d.r);
            o.g = mul8(t.g, d.g);
            o.b = mul8(t.b, d.b);
            o.a = tex_alpha ? t.a : d.a;
            break;
        case 3: /* decal alpha */
            o.r = mul8(t.r, t.a) + mul8(d.r, 255 - t.a);
            o.g = mul8(t.g, t.a) + mul8(d.g, 255 - t.a);
            o.b = mul8(t.b, t.a) + mul8(d.b, 255 - t.a);
            o.a = d.a;
            break;
        case 4: /* modulate alpha */
            o.r = mul8(t.r, d.r);
            o.g = mul8(t.g, d.g);
            o.b = mul8(t.b, d.b);
            o.a = mul8(t.a, d.a);
            break;
        case 5: /* decal mask */
            if (t.a & 0x80) {
                o.r = t.r;
                o.g = t.g;
                o.b = t.b;
            }
            break;
        case 6: /* modulate mask */
            if (t.a & 0x80) {
                o.r = mul8(t.r, d.r);
                o.g = mul8(t.g, d.g);
                o.b = mul8(t.b, d.b);
            }
            break;
        case 8: /* add */
            o.r = clamp255(t.r + d.r);
            o.g = clamp255(t.g + d.g);
            o.b = clamp255(t.b + d.b);
            break;
        default:
            break;
    }
    return o;
}

/* One DX6 combiner: four arguments, out = op(a0*a1, a2*a3). <alpha>:
   the alpha combiner (one channel). */
static void
nv4_combine(uint32_t ctl, int alpha, const rgba_t *src, int *out)
{
    int prod[2][3];
    int v[4][3];

    for (int a = 0; a < 4; a++) {
        int    sel = (ctl >> (a * 8 + 2)) & ((a == 3) ? 7 : 0x3f);
        int    inv = (ctl >> (a * 8)) & 1;
        int    rep = ((ctl >> (a * 8 + 1)) & 1) || alpha;
        rgba_t s   = (sel >= 1 && sel <= 7) ? src[sel] : src[1];

        v[a][0] = rep ? s.a : s.r;
        v[a][1] = rep ? s.a : s.g;
        v[a][2] = rep ? s.a : s.b;
        if (inv)
            for (int c = 0; c < 3; c++)
                v[a][c] = 255 - v[a][c];
    }
    for (int c = 0; c < 3; c++) {
        int sum;

        prod[0][c] = mul8(v[0][c], v[1][c]);
        prod[1][c] = mul8(v[2][c], v[3][c]);
        sum        = prod[0][c] + prod[1][c];
        switch (ctl >> 29) {
            case 2:
                sum *= 2;
                break;
            case 3:
                sum *= 4;
                break;
            case 4:
                sum -= 128;
                break;
            case 5: /* mux: the input's alpha picks a product */
                sum = (src[4].a & 0x80) ? prod[1][c] : prod[0][c];
                break;
            case 6:
                sum = 255 - sum;
                break;
            case 7:
                sum = (sum - 128) * 2;
                break;
            default:
                break;
        }
        out[c] = clamp255(sum);
    }
}

static int
nv4_cmp(int func, uint32_t a, uint32_t ref)
{
    switch (func) {
        case 1:
            return 0;
        case 2:
            return a < ref;
        case 3:
            return a == ref;
        case 4:
            return a <= ref;
        case 5:
            return a > ref;
        case 6:
            return a != ref;
        case 7:
            return a >= ref;
        default:
            return 1;
    }
}

static int
nv4_blend_factor(int f, int c, int sa, int da, int sc, int dc, int is_alpha)
{
    switch (f) {
        case 1:
            return 0;
        case 3:
            return sc;
        case 4:
            return 255 - sc;
        case 5:
            return sa;
        case 6:
            return 255 - sa;
        case 7:
            return da;
        case 8:
            return 255 - da;
        case 9:
            return dc;
        case 10:
            return 255 - dc;
        case 11:
            return is_alpha ? 255 : ((sa < 255 - da) ? sa : 255 - da);
        default:
            (void) c;
            return 255;
    }
}

static int
nv4_stencil_op(int op, int s, int ref)
{
    switch (op) {
        case 2:
            return 0;
        case 3:
            return ref;
        case 4:
            return (s < 255) ? s + 1 : 255;
        case 5:
            return (s > 0) ? s - 1 : 0;
        case 6:
            return ~s & 0xff;
        case 7:
            return (s + 1) & 0xff;
        case 8:
            return (s - 1) & 0xff;
        default:
            return s;
    }
}

static const int dither4[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 }
};

static uint32_t
nv4_pack_color(const nv4_3d_state_t *s, rgba_t c, int x, int y)
{
    if ((s->bpp == 2) && s->dither) {
        int d = dither4[y & 3][x & 3] >> 1; /* 0-7 */

        c.r = clamp255(c.r + d - 4);
        c.g = clamp255(c.g + ((s->col_fmt == 5) ? (d >> 1) - 2 : d - 4));
        c.b = clamp255(c.b + d - 4);
    }
    switch (s->col_fmt) {
        case 2:
        case 3:
            return ((c.r >> 3) << 10) | ((c.g >> 3) << 5) | (c.b >> 3) | ((s->col_fmt == 3) ? 0x8000 : 0);
        case 4:
            return ((c.r >> 3) << 10) | ((c.g >> 3) << 5) | (c.b >> 3) | ((c.a & 0x80) << 8);
        case 5:
            return ((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3);
        case 9:
        case 0xa:
            return ((uint32_t) (c.a >> 1) << 24) | (c.r << 16) | (c.g << 8) | c.b | ((s->col_fmt == 0xa) ? 0x80000000 : 0);
        case 0xc:
            return ((uint32_t) c.a << 24) | (c.r << 16) | (c.g << 8) | c.b;
        case 0xb:
            return 0xff000000 | (c.r << 16) | (c.g << 8) | c.b;
        default:
            return (c.r << 16) | (c.g << 8) | c.b;
    }
}

static rgba_t
nv4_unpack_color(const nv4_3d_state_t *s, uint32_t v)
{
    rgba_t c;

    switch (s->col_fmt) {
        case 2:
        case 3:
        case 4:
            c.r = ((v >> 10) & 0x1f) * 0x21 >> 2;
            c.g = ((v >> 5) & 0x1f) * 0x21 >> 2;
            c.b = (v & 0x1f) * 0x21 >> 2;
            c.a = (s->col_fmt == 4) ? ((v & 0x8000) ? 255 : 0) : 255;
            break;
        case 5:
            c.r = ((v >> 11) & 0x1f) * 0x21 >> 2;
            c.g = ((v >> 5) & 0x3f) * 0x41 >> 4;
            c.b = (v & 0x1f) * 0x21 >> 2;
            c.a = 255;
            break;
        default:
            c   = rgba(v);
            if (s->col_fmt == 9 || s->col_fmt == 0xa)
                c.a = ((v >> 24) & 0x7f) << 1 | ((v >> 30) & 1);
            else if (s->col_fmt != 0xc)
                c.a = 255;
            break;
    }
    return c;
}

/* ------------------------------------------------------------------ */
/* Shading.                                                            */
/* ------------------------------------------------------------------ */
/* log2 from the float's bits: the exponent plus the mantissa as a
   linear fraction. Unlike log2f, the result is the same on every host
   and in the GPU kernel. */
static float
nv4_log2_bits(float r)
{
    union {
        float    f;
        uint32_t u;
    } c = { r };

    return (float) ((int) (c.u >> 23) - 127) + (float) (c.u & 0x7fffff) * (1.0f / 8388608.0f);
}

static float
nv4_lod(const nv4_3d_tri_t *t, int au, int av, float q, float uq, float vq, int wl, int hl)
{
    float iq   = 1.0f / q;
    float iq2  = iq * iq;
    float dudx = (t->dx[au] * q - uq * t->dx[AT_Q]) * iq2 * (float) (1 << wl);
    float dvdx = (t->dx[av] * q - vq * t->dx[AT_Q]) * iq2 * (float) (1 << hl);
    float dudy = (t->dy[au] * q - uq * t->dy[AT_Q]) * iq2 * (float) (1 << wl);
    float dvdy = (t->dy[av] * q - vq * t->dy[AT_Q]) * iq2 * (float) (1 << hl);
    float rx   = dudx * dudx + dvdx * dvdx;
    float ry   = dudy * dudy + dvdy * dvdy;
    float r    = (rx > ry) ? rx : ry;

    return (r > 0.0f) ? 0.5f * nv4_log2_bits(r) : -16.0f;
}

static void
nv4_3d_span(nv4_t *nv4, const nv4_3d_batch_t *b, const nv4_3d_span_t *sp)
{
    const nv4_3d_tri_t   *t = &b->tris[sp->tri];
    const nv4_3d_state_t *s = &b->states[t->state];
    nv4_3d_tcache_t       tc[2] = { { ~0u, 0, 0 }, { ~0u, 0, 0 } };
    uint8_t              *vram  = nv4->svga.vram;
    uint32_t              vm    = nv4->vram_mask;
    float                 py    = (float) sp->y + s->origin - t->y0;

    for (int x = sp->x0; x < sp->x1; x++) {
        float    px = (float) x + s->origin - t->x0;
        float    at[AT_COUNT];
        rgba_t   d, spec, out, src[8];
        int      killed = 0;
        uint32_t ca     = s->col_base + nv4_3d_pix(s, s->col_pitch, x, sp->y);
        uint32_t za     = s->z_base + nv4_3d_pix(s, s->z_pitch, x, sp->y);
        uint32_t zold   = 0, znew = 0;
        uint32_t depth;
        int      sten   = 0;

        if (ca + s->bpp - 1 > s->col_lim)
            continue;
        if (s->fill) {
            uint32_t fv, kind;

            memcpy(&fv, &t->c[0], 4);
            memcpy(&kind, &t->c[NV4_FILL_KIND], 4);
            if (kind == NV4_FILL_ROP) {
                uint32_t r[7], dv = 0;
                int      k = 0;

                memcpy(r, &t->c[0], sizeof(r));
                if (r[6] & NV4_ROPFILL_PAT) {
                    int pi = ((r[6] & 3) == 1) ? (x & 63) : (((r[6] & 3) == 2) ? (sp->y & 63) : (((sp->y & 7) << 3) | (x & 7)));

                    k = (r[4 + (pi >> 5)] >> (pi & 31)) & 1;
                    if (r[6] & (k ? NV4_ROPFILL_SKIP1 : NV4_ROPFILL_SKIP0))
                        continue;
                }
                for (int i = 0; i < s->bpp; i++)
                    dv |= (uint32_t) vram[(ca + i) & vm] << (i * 8);
                fv = (dv & r[k * 2]) | (~dv & r[k * 2 + 1]);
            }
            for (int i = 0; i < s->bpp; i++)
                vram[(ca + i) & vm] = fv >> (i * 8);
            continue;
        }
        for (int i = 0; i < AT_COUNT; i++)
            at[i] = t->c[i] + t->dx[i] * px + t->dy[i] * py;
        if (at[AT_Q] <= 0.0f)
            at[AT_Q] = 1e-6f;

        /* depth */
        if (s->zfloat) {
            float w = 1.0f - at[AT_Q];

            w     = (w < 0.0f) ? 0.0f : ((w > 1.0f) ? 1.0f : w);
            depth = (uint32_t) (w * ((s->bpp == 2) ? 65535.0f : 16777215.0f));
        } else {
            float z = at[AT_Z];

            z     = (z < 0.0f) ? 0.0f : ((z > 1.0f) ? 1.0f : z);
            depth = (uint32_t) (z * ((s->bpp == 2) ? 65535.0f : 16777215.0f));
        }
        if (s->z_on && (za + s->bpp - 1 <= s->z_lim)) {
            if (s->bpp == 2)
                zold = vram[za & vm] | (vram[(za + 1) & vm] << 8);
            else
                zold = vram[za & vm] | (vram[(za + 1) & vm] << 8) | (vram[(za + 2) & vm] << 16)
                    | ((uint32_t) vram[(za + 3) & vm] << 24);
        }
        sten = (s->bpp == 4) ? (zold >> 24) : 0;

        /* colors */
        d.r    = clamp255(nv4_f2i(at[AT_R]));
        d.g    = clamp255(nv4_f2i(at[AT_G]));
        d.b    = clamp255(nv4_f2i(at[AT_B]));
        d.a    = clamp255(nv4_f2i(at[AT_A]));
        spec.r = clamp255(nv4_f2i(at[AT_SR]));
        spec.g = clamp255(nv4_f2i(at[AT_SG]));
        spec.b = clamp255(nv4_f2i(at[AT_SB]));
        spec.a = clamp255(nv4_f2i(at[AT_FOG]));

        /* textures */
        for (int n = 0; n < 2; n++) {
            const nv4_3d_tex_t *tx = &s->tex[n];
            float               iq;

            src[5 + n] = (rgba_t) { 255, 255, 255, 255 };
            if (!tx->on)
                continue;
            iq = s->persp ? 1.0f / at[AT_Q] : 1.0f;
            {
                int   au  = n ? AT_U1 : AT_U0;
                int   av  = n ? AT_V1 : AT_V0;
                float u   = at[au] * iq;
                float v   = at[av] * iq;
                float lod = nv4_lod(t, au, av, s->persp ? at[AT_Q] : 1.0f, at[au], at[av], tx->wlog, tx->hlog);

                src[5 + n] = rgba(nv4_sample(nv4, tx, &tc[n], u, v, lod, s->color_key, &killed));
            }
        }
        if (killed)
            continue;

        if (s->dx6) {
            int    co[3], ao[3];
            rgba_t input = d;

            src[1] = (rgba_t) { 0, 0, 0, 0 };
            src[2] = rgba(s->factor);
            src[3] = d;
            src[7] = (rgba_t) { 0, 0, 0, 0 };
            for (int st = 0; st < 2; st++) {
                src[4] = input;
                nv4_combine(s->comb_c[st], 0, src, co);
                nv4_combine(s->comb_a[st], 1, src, ao);
                input.r = co[0];
                input.g = co[1];
                input.b = co[2];
                input.a = ao[0];
            }
            out = input;
        } else if (s->tex[0].on)
            out = nv4_texblend(s->texblend, src[5], d, s->tex[0].fmt == 2 || s->tex[0].fmt == 4 || s->tex[0].fmt == 6 || s->tex[0].fmt == 1);
        else
            out = d;

        if (s->spec) {
            out.r = clamp255(out.r + spec.r);
            out.g = clamp255(out.g + spec.g);
            out.b = clamp255(out.b + spec.b);
        }
        if (s->fog) {
            rgba_t fc = rgba(s->fog_color);
            int    f  = spec.a;

            out.r = mul8(out.r, f) + mul8(fc.r, 255 - f);
            out.g = mul8(out.g, f) + mul8(fc.g, 255 - f);
            out.b = mul8(out.b, f) + mul8(fc.b, 255 - f);
        }

        if (s->atest && !nv4_cmp(s->afunc, out.a, s->aref))
            continue;

        /* stencil and depth */
        if (s->sten) {
            int pass = nv4_cmp(s->sfunc, s->sref & s->smask_r, sten & s->smask_r);

            if (!pass) {
                if (s->swrite) {
                    int ns = nv4_stencil_op(s->sop_fail, sten, s->sref);
                    sten   = (sten & ~s->smask_w) | (ns & s->smask_w);
                    zold   = (zold & 0x00ffffff) | ((uint32_t) sten << 24);
                    for (int i = 0; i < 4; i++)
                        vram[(za + i) & vm] = zold >> (i * 8);
                }
                continue;
            }
        }
        {
            uint32_t zcur = (s->bpp == 2) ? zold : (zold & 0xffffff);
            int      zpass = !s->zen || nv4_cmp(s->zfunc, depth, zcur);

            if (s->sten && s->swrite) {
                int ns = nv4_stencil_op(zpass ? s->sop_zpass : s->sop_zfail, sten, s->sref);
                sten   = (sten & ~s->smask_w) | (ns & s->smask_w);
            }
            znew = (s->bpp == 2) ? depth : ((depth & 0xffffff) | ((uint32_t) sten << 24));
            if (!zpass) {
                if (s->sten && s->swrite)
                    for (int i = 0; i < 4; i++)
                        vram[(za + i) & vm] = ((zold & 0xffffff) | ((uint32_t) sten << 24)) >> (i * 8);
                continue;
            }
            if (s->z_on && (s->zwrite || (s->sten && s->swrite)) && (za + s->bpp - 1 <= s->z_lim)) {
                if (!s->zwrite)
                    znew = (s->bpp == 2) ? zold : ((zold & 0xffffff) | ((uint32_t) sten << 24));
                for (int i = 0; i < s->bpp; i++)
                    vram[(za + i) & vm] = znew >> (i * 8);
            }
        }

        /* framebuffer */
        {
            uint32_t old = 0;
            rgba_t   dst = { 0, 0, 0, 0 };
            uint32_t v;

            if (s->ablend || !(s->wr_r && s->wr_g && s->wr_b && s->wr_a)) {
                for (int i = 0; i < s->bpp; i++)
                    old |= (uint32_t) vram[(ca + i) & vm] << (i * 8);
                dst = nv4_unpack_color(s, old);
            }
            if (s->ablend) {
                int sfr = nv4_blend_factor(s->sblend, 0, out.a, dst.a, out.r, dst.r, 0);
                int sfg = nv4_blend_factor(s->sblend, 0, out.a, dst.a, out.g, dst.g, 0);
                int sfb = nv4_blend_factor(s->sblend, 0, out.a, dst.a, out.b, dst.b, 0);
                int sfa = nv4_blend_factor(s->sblend, 0, out.a, dst.a, out.a, dst.a, 1);
                int dfr = nv4_blend_factor(s->dblend, 0, out.a, dst.a, out.r, dst.r, 0);
                int dfg = nv4_blend_factor(s->dblend, 0, out.a, dst.a, out.g, dst.g, 0);
                int dfb = nv4_blend_factor(s->dblend, 0, out.a, dst.a, out.b, dst.b, 0);
                int dfa = nv4_blend_factor(s->dblend, 0, out.a, dst.a, out.a, dst.a, 1);

                out.r = clamp255(mul8(out.r, sfr) + mul8(dst.r, dfr));
                out.g = clamp255(mul8(out.g, sfg) + mul8(dst.g, dfg));
                out.b = clamp255(mul8(out.b, sfb) + mul8(dst.b, dfb));
                out.a = clamp255(mul8(out.a, sfa) + mul8(dst.a, dfa));
            }
            if (!s->wr_r)
                out.r = dst.r;
            if (!s->wr_g)
                out.g = dst.g;
            if (!s->wr_b)
                out.b = dst.b;
            if (!s->wr_a)
                out.a = dst.a;
            v = nv4_pack_color(s, out, x, sp->y);
            for (int i = 0; i < s->bpp; i++)
                vram[(ca + i) & vm] = v >> (i * 8);
        }
    }
    if (s->swz) {
        /* a row's pixels spread over the surface */
        for (int x = sp->x0; x < sp->x1; x++)
            nv4->svga.changedvram[((s->col_base + nv4_3d_pix(s, 0, x, sp->y)) & vm) >> 12]
                = nv4->svga.monitor->mon_changeframecount;
    } else {
        uint32_t row = s->col_base + (uint32_t) sp->y * s->col_pitch;
        uint32_t a0  = (row + sp->x0 * s->bpp) & vm;
        uint32_t a1  = (row + sp->x1 * s->bpp) & vm;

        for (uint32_t a = a0 & ~0xfffu; a <= a1; a += 0x1000)
            nv4->svga.changedvram[(a & vm) >> 12] = nv4->svga.monitor->mon_changeframecount;
    }
}

static void
nv4_3d_worker(void *priv)
{
    nv4_3d_worker_t *w   = (nv4_3d_worker_t *) priv;
    nv4_t           *nv4 = w->nv4;
    nv4_3d_batch_t  *b   = nv4->batch3d;

    for (;;) {
        thread_wait_event(w->wake, -1);
        thread_reset_event(w->wake);
        if (!b->run)
            break;
        for (int i = 0; i < b->nspans; i++)
            if (((unsigned) b->spans[i].y % (unsigned) b->nthreads) == (unsigned) w->id)
                nv4_3d_span(nv4, b, &b->spans[i]);
        thread_set_event(w->done);
    }
}

static void
nv4_3d_shade_threaded(nv4_3d_batch_t *b)
{
    for (int i = 0; i < b->nthreads; i++)
        thread_set_event(b->workers[i].wake);
    for (int i = 0; i < b->nthreads; i++)
        thread_wait_event(b->workers[i].done, -1);
    for (int i = 0; i < b->nthreads; i++)
        thread_reset_event(b->workers[i].done);
}

static void
nv4_3d_shade_serial(nv4_t *nv4, nv4_3d_batch_t *b)
{
    for (int i = 0; i < b->nspans; i++)
        nv4_3d_span(nv4, b, &b->spans[i]);
}

/* Shade on the GPU or the worker threads. 0: neither took the batch,
   VRAM is untouched. */
static int
nv4_3d_shade_parallel(nv4_t *nv4, nv4_3d_batch_t *b)
{
    if (b->gpu) {
        if (nv4_gpu_shade(b->gpu, nv4, b))
            return 1;
        /* the CPU draws it: the GPU's batches in flight first */
        nv4_gpu_wait(b->gpu, nv4);
    }
    /* the threads own rows, which a batch whose rows alias does not
       have; the GPU takes it as one workgroup in order */
    if ((b->nthreads > 1) && !b->serial) {
        nv4_3d_shade_threaded(b);
        return 1;
    }
    return 0;
}

/* Shade in parallel, keep the result, shade the same batch serially
   from the same starting VRAM and compare: the bytes of the batch's
   regions, which are all the parallel paths may write (the GPU copies
   back only those), or all of VRAM every NV4_VERIFY_FULL_EVERY flushes,
   for rows that alias (no regions to go by) and with "full", which also
   catches a write outside them. */
static void
nv4_3d_shade_verify(nv4_t *nv4, nv4_3d_batch_t *b)
{
    uint32_t lo[B3D_REGIONS], hi[B3D_REGIONS];
    int      n     = 0;
    int      full  = b->verify_full || b->serial || !(b->vflushes % NV4_VERIFY_FULL_EVERY);
    uint32_t bad   = 0;
    uint32_t first = 0;

    for (int r = 0; !full && (r < b->nregions); r++) {
        if (b->regions[r].wlo > b->regions[r].whi)
            continue;
        if (b->regions[r].whi >= nv4->vram_size)
            full = 1;
        lo[n]   = b->regions[r].wlo;
        hi[n++] = b->regions[r].whi;
    }
    if (full) {
        lo[0] = 0;
        hi[0] = nv4->vram_size - 1;
        n     = 1;
    }
    for (int i = 0; i < n; i++)
        memcpy(b->vsnap + lo[i], nv4->svga.vram + lo[i], hi[i] - lo[i] + 1);
    if (!nv4_3d_shade_parallel(nv4, b)) {
        nv4_3d_shade_serial(nv4, b);
        return;
    }
    if (b->gpu)
        nv4_gpu_wait(b->gpu, nv4);
    for (int i = 0; i < n; i++) {
        memcpy(b->vres + lo[i], nv4->svga.vram + lo[i], hi[i] - lo[i] + 1);
        memcpy(nv4->svga.vram + lo[i], b->vsnap + lo[i], hi[i] - lo[i] + 1);
    }
    nv4_3d_shade_serial(nv4, b);
    for (int i = 0; i < n; i++) {
        if (!memcmp(b->vres + lo[i], nv4->svga.vram + lo[i], hi[i] - lo[i] + 1))
            continue;
        for (uint32_t a = lo[i]; a <= hi[i]; a++)
            if (b->vres[a] != nv4->svga.vram[a]) {
                if (!bad)
                    first = a;
                bad++;
            }
    }
    b->vflushes++;
    if (full)
        b->vfull++;
    if (bad) {
        b->vbad++;
        pclog("nv4 3D verify: flush %u: %u bytes differ, first at %06x (parallel %02x, serial %02x), %d spans%s\n",
              b->vflushes, bad, first, b->vres[first], nv4->svga.vram[first], b->nspans, b->serial ? ", rows alias" : "");
    }
    if (!(b->vflushes % 1000))
        pclog("nv4 3D verify: %u flushes (%u over all of VRAM), %u differed\n", b->vflushes, b->vfull, b->vbad);
}

/* NV4_3D_RECORD: the batch, the VRAM bytes it can reach (its regions;
   all of VRAM when its rows alias) and its textures' bytes, as they
   are before it is shaded (the format is with NV4_REC_MAGIC). */
static void
nv4_3d_rec_u32(FILE *f, uint32_t v)
{
    fwrite(&v, 4, 1, f);
}

static void
nv4_3d_record(nv4_t *nv4, nv4_3d_batch_t *b)
{
    char  path[600];
    FILE *f;

    if ((b->rec_seen++ % b->rec_every) || (b->rec_done >= b->rec_max))
        return;
    snprintf(path, sizeof(path), "%s/nv4-batch-%06u.bin", b->rec_dir, b->rec_done);
    if (!(f = fopen(path, "wb"))) {
        pclog("nv4 3D: cannot record to %s\n", path);
        b->rec_max = 0;
        return;
    }
    b->rec_done++;
    nv4_3d_rec_u32(f, NV4_REC_MAGIC);
    nv4_3d_rec_u32(f, NV4_REC_VERSION);
    nv4_3d_rec_u32(f, sizeof(nv4_3d_state_t));
    nv4_3d_rec_u32(f, sizeof(nv4_3d_tri_t));
    nv4_3d_rec_u32(f, sizeof(nv4_3d_span_t));
    nv4_3d_rec_u32(f, sizeof(nv4_3d_region_t));
    nv4_3d_rec_u32(f, nv4->vram_size);
    nv4_3d_rec_u32(f, nv4->vram_mask);
    nv4_3d_rec_u32(f, b->nstates);
    nv4_3d_rec_u32(f, b->ntris);
    nv4_3d_rec_u32(f, b->nspans);
    nv4_3d_rec_u32(f, b->nregions);
    nv4_3d_rec_u32(f, b->serial);
    nv4_3d_rec_u32(f, b->parallel);
    nv4_3d_rec_u32(f, b->tex_lo);
    nv4_3d_rec_u32(f, b->tex_hi);
    fwrite(b->states, sizeof(nv4_3d_state_t), b->nstates, f);
    fwrite(b->tris, sizeof(nv4_3d_tri_t), b->ntris, f);
    fwrite(b->spans, sizeof(nv4_3d_span_t), b->nspans, f);
    fwrite(b->regions, sizeof(nv4_3d_region_t), b->nregions, f);

    /* VRAM */
    if (b->serial || !b->parallel) {
        nv4_3d_rec_u32(f, NV4_REC_VRAM);
        nv4_3d_rec_u32(f, 0);
        nv4_3d_rec_u32(f, nv4->vram_size);
        fwrite(nv4->svga.vram, 1, nv4->vram_size, f);
    } else
        for (int r = 0; r < b->nregions; r++) {
            uint32_t lo = b->regions[r].wlo, hi = b->regions[r].whi;

            if ((lo > hi) || (hi >= nv4->vram_size))
                continue;
            nv4_3d_rec_u32(f, NV4_REC_VRAM);
            nv4_3d_rec_u32(f, lo);
            nv4_3d_rec_u32(f, hi - lo + 1);
            fwrite(nv4->svga.vram + lo, 1, hi - lo + 1, f);
        }

    /* textures, a page of their window at a time */
    for (int i = 0; i < b->nstates; i++)
        for (int n = 0; n < 2; n++) {
            const nv4_3d_tex_t *t = &b->states[i].tex[n];

            for (uint32_t done = 0; t->on && (done < t->size);) {
                uint32_t off   = t->offset + done;
                uint32_t left  = nv4_dma_page_left(nv4, t->dma, off);
                uint32_t chunk = left;
                uint32_t addr, ra;
                int      target;
                uint8_t  bytes[0x1000];

                if (!chunk || (chunk > 0x1000))
                    break;
                if (chunk > t->size - done)
                    chunk = t->size - done;
                if (nv4_dma_translate(nv4, t->dma, off, 0, &addr, &target) == NV4_DMA_OK) {
                    if (!target) {
                        for (uint32_t k = 0; k < chunk; k++)
                            bytes[k] = nv4->svga.vram[(addr + k) & nv4->vram_mask];
                    } else {
                        if (agpgart_resolve(addr & ~0xfffu, &ra))
                            addr = ra | (addr & 0xfff);
                        for (uint32_t k = 0; k < chunk; k++)
                            bytes[k] = mem_readb_phys(addr + k);
                    }
                    nv4_3d_rec_u32(f, NV4_REC_TEX);
                    nv4_3d_rec_u32(f, t->dma);
                    nv4_3d_rec_u32(f, off);
                    nv4_3d_rec_u32(f, left);
                    nv4_3d_rec_u32(f, chunk);
                    nv4_3d_rec_u32(f, addr);
                    nv4_3d_rec_u32(f, target);
                    fwrite(bytes, 1, chunk, f);
                }
                done += chunk;
            }
        }
    fclose(f);
    if (b->rec_done == b->rec_max)
        pclog("nv4 3D: recorded %u batches in %s\n", b->rec_done, b->rec_dir);
}

static uint64_t
nv4_3d_now_us(void)
{
    struct timespec ts;

    timespec_get(&ts, TIME_UTC);
    return (uint64_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* For the debug hook: what the flushes cost on the host. */
void
nv4_3d_stats(nv4_t *nv4)
{
    static uint64_t t_last, tsc_last;
    nv4_3d_batch_t *b   = nv4->batch3d;
    uint64_t        now = nv4_3d_now_us();

    if (!b)
        return;
    /* emulation speed: guest time (CPU cycles) over host time; and the
       guest's frames per host second */
    if (t_last && (now > t_last))
        pclog("nv4 3D: speed %.0f%%, %u vertical blanks in %llu ms (%.1f per second)\n",
              100.0 * ((double) (tsc - tsc_last) / cpuclock) / ((double) (now - t_last) / 1e6), nv4->dbg_vblanks,
              (unsigned long long) ((now - t_last) / 1000), nv4->dbg_vblanks * 1e6 / (double) (now - t_last));
    t_last           = now;
    tsc_last         = tsc;
    nv4->dbg_vblanks = 0;
    pclog("nv4 3D: %d threads%s, %llu flushes, %llu spans, %llu us shading, %llu triangles without a colour surface\n",
          b->nthreads, b->gpu ? " + GPU" : "", (unsigned long long) b->st_flushes, (unsigned long long) b->st_spans,
          (unsigned long long) b->st_us, (unsigned long long) b->st_nosurf);
    if (b->gpu) {
        uint64_t n, f;

        nv4_gpu_counts(b->gpu, &n, &f);
        pclog("nv4 3D: GPU took %llu batches, left %llu to the CPU\n", (unsigned long long) n, (unsigned long long) f);
        for (int i = 0; i < NV4_SYNC_CAUSES; i++)
            if (b->st_sync_n[i])
                pclog("nv4 3D: waited for the GPU %u times, %llu us, for %s %02x\n", b->st_sync_n[i],
                      (unsigned long long) b->st_sync_us[i],
                      (i == NV4_SYNC_LFB) ? "LFB" : ((i == NV4_SYNC_VGA) ? "VGA" : ((i == NV4_SYNC_VBLANK) ? "vblank" : "class")),
                      i & 0xff);
        memset(b->st_sync_us, 0, sizeof(b->st_sync_us));
        memset(b->st_sync_n, 0, sizeof(b->st_sync_n));
    }
    if (b->st_serial)
        pclog("nv4 3D: %llu flushes (%llu spans) whose rows alias\n", (unsigned long long) b->st_serial,
              (unsigned long long) b->st_serial_spans);
    b->st_flushes = b->st_spans = b->st_us = b->st_nosurf = b->st_serial = b->st_serial_spans = 0;
}

void
nv4_3d_flush(nv4_t *nv4)
{
    nv4_3d_batch_t *b = nv4->batch3d;
    uint64_t        t0;

    /* 2D pixels waiting to join come first */
    nv4_pgraph_2d_flush_run(nv4);
    if (!b || (!b->nspans && !b->ntris && !b->nstates))
        return;
    t0 = nv4_3d_now_us();
    if (b->rec_max)
        nv4_3d_record(nv4, b);
    if (b->parallel && b->serial) {
        b->st_serial++;
        b->st_serial_spans += b->nspans;
    }
    if (b->parallel && (!b->serial || b->gpu) && (b->nspans > 1)) {
        if (b->verify)
            nv4_3d_shade_verify(nv4, b);
        else if (!nv4_3d_shade_parallel(nv4, b))
            nv4_3d_shade_serial(nv4, b);
    } else {
        /* the CPU draws into VRAM: the GPU's batch in flight first */
        if (b->gpu)
            nv4_gpu_wait(b->gpu, nv4);
        nv4_3d_shade_serial(nv4, b);
    }
    b->st_flushes++;
    b->st_spans += b->nspans;
    b->st_us += nv4_3d_now_us() - t0;
    b->nspans   = 0;
    b->ntris    = 0;
    b->nstates  = 0;
    b->nregions = 0;
    b->serial   = 0;
    b->tex_lo   = 0xffffffff;
    b->tex_hi   = 0;
    for (int c = 0; c < NV4_CHANNELS; c++)
        nv4->d3d[c].state = -1;
}

/* Draw the batch and, when the GPU has one in flight, wait for its
   pixels: for anything that reads or writes VRAM itself. */
static void
nv4_3d_gpu_wait(nv4_t *nv4, int cause)
{
    nv4_3d_batch_t *b  = nv4->batch3d;
    uint64_t        t0 = nv4_3d_now_us();

    if (!nv4_gpu_pending_overlaps(b->gpu, 0, 0xffffffff))
        return;
    nv4_gpu_wait(b->gpu, nv4);
    b->st_sync_us[cause] += nv4_3d_now_us() - t0;
    b->st_sync_n[cause]++;
}

void
nv4_3d_sync(nv4_t *nv4, int cause)
{
    nv4_3d_flush(nv4);
    if (nv4->batch3d && nv4->batch3d->gpu)
        nv4_3d_gpu_wait(nv4, cause);
}

/* Vertical blank: the batch is drawn, and waited for only if it draws
   into the scanned-out bytes lo..hi (a back buffer can stay in
   flight). */
void
nv4_3d_vblank(nv4_t *nv4, uint32_t lo, uint32_t hi)
{
    nv4_3d_flush(nv4);
    if (nv4->batch3d && nv4->batch3d->gpu && nv4_gpu_pending_overlaps(nv4->batch3d->gpu, lo, hi))
        nv4_3d_gpu_wait(nv4, NV4_SYNC_VBLANK);
}

/* ------------------------------------------------------------------ */
/* Setup.                                                              */
/* ------------------------------------------------------------------ */
static int64_t
floordiv(int64_t a, int64_t b)
{
    int64_t q = a / b;

    if ((a % b) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

/* Plane equation of one attribute over the triangle. */
static void
nv4_plane(nv4_3d_tri_t *t, int at, float a0, float a1, float a2, float x10, float y10, float x20, float y20, float inv)
{
    t->c[at]  = a0;
    t->dx[at] = ((a1 - a0) * y20 - (a2 - a0) * y10) * inv;
    t->dy[at] = ((a2 - a0) * x10 - (a1 - a0) * x20) * inv;
}

static void
nv4_3d_triangle(nv4_t *nv4, int chid, int cls, int i0, int i1, int i2)
{
    nv4_3d_batch_t      *b = nv4->batch3d;
    nv4_d3d_t           *d = &nv4->d3d[chid];
    const nv4_3d_state_t *s;
    const nv4_d3d_vtx_t *v[3];
    nv4_3d_tri_t        *t;
    int32_t              X[3], Y[3];
    int64_t              area;
    float                inv, x10, y10, x20, y20;
    int32_t              ymin, ymax;
    int                  off16;
    uint64_t             px = 0;

    if (!b)
        return;
    if (d->state < 0) {
        d->state = nv4_3d_make_state(nv4, b, cls, d);
        if (d->state < 0)
            return;
    }
    s    = &b->states[d->state];
    v[0] = &d->vtx[i0];
    v[1] = &d->vtx[i1];
    v[2] = &d->vtx[i2];

    /* 12.4 fixed point */
    for (int i = 0; i < 3; i++) {
        float fx = v[i]->sx * 16.0f;
        float fy = v[i]->sy * 16.0f;

        if (!(fx > -65536.0f && fx < 65536.0f) || !(fy > -65536.0f && fy < 65536.0f))
            return;
        X[i] = (int32_t) lrintf(fx);
        Y[i] = (int32_t) lrintf(fy);
    }
    area = (int64_t) (X[1] - X[0]) * (Y[2] - Y[0]) - (int64_t) (X[2] - X[0]) * (Y[1] - Y[0]);
    if (!area)
        return;
    /* y grows down: a positive area runs clockwise on screen */
    if ((s->cull == 2) && (area > 0))
        return;
    if ((s->cull == 3) && (area < 0))
        return;

    if ((b->ntris >= B3D_TRIS) || (b->nspans + 4096 > B3D_SPANS)) {
        /* the state must survive the flush */
        nv4_3d_flush(nv4);
        d->state = nv4_3d_make_state(nv4, b, cls, d);
        if (d->state < 0)
            return;
        s = &b->states[d->state];
    }

    t        = &b->tris[b->ntris];
    t->state = d->state;
    t->x0    = X[0] / 16.0f;
    t->y0    = Y[0] / 16.0f;
    x10      = (X[1] - X[0]) / 16.0f;
    y10      = (Y[1] - Y[0]) / 16.0f;
    x20      = (X[2] - X[0]) / 16.0f;
    y20      = (Y[2] - Y[0]) / 16.0f;
    inv      = 1.0f / (x10 * y20 - x20 * y10);

    {
        float a[3][AT_COUNT];

        for (int i = 0; i < 3; i++) {
            const nv4_d3d_vtx_t *vx  = v[i];
            const nv4_d3d_vtx_t *col = s->flat ? v[0] : vx;
            float                q   = s->persp ? vx->rhw : 1.0f;

            a[i][AT_Z]   = vx->sz;
            a[i][AT_Q]   = q;
            a[i][AT_U0]  = vx->tu[0] * q;
            a[i][AT_V0]  = vx->tv[0] * q;
            a[i][AT_U1]  = vx->tu[1] * q;
            a[i][AT_V1]  = vx->tv[1] * q;
            a[i][AT_R]   = (float) ((col->color >> 16) & 0xff);
            a[i][AT_G]   = (float) ((col->color >> 8) & 0xff);
            a[i][AT_B]   = (float) (col->color & 0xff);
            a[i][AT_A]   = (float) (col->color >> 24);
            a[i][AT_SR]  = (float) ((col->specular >> 16) & 0xff);
            a[i][AT_SG]  = (float) ((col->specular >> 8) & 0xff);
            a[i][AT_SB]  = (float) (col->specular & 0xff);
            a[i][AT_FOG] = (float) (vx->specular >> 24);
        }
        for (int k = 0; k < AT_COUNT; k++)
            nv4_plane(t, k, a[0][k], a[1][k], a[2][k], x10, y10, x20, y20, inv);
        /* a half pixel more keeps rounding of colors to the nearest */
        for (int k = AT_R; k <= AT_FOG; k++)
            t->c[k] += 0.5f;
    }

    /* Spans: a pixel is in when its sample point is inside all three
       edges, or on a top or left edge. */
    off16 = (int) (s->origin * 16.0f);
    ymin  = Y[0];
    ymax  = Y[0];
    for (int i = 1; i < 3; i++) {
        if (Y[i] < ymin)
            ymin = Y[i];
        if (Y[i] > ymax)
            ymax = Y[i];
    }
    ymin = (int32_t) floordiv(ymin - off16 + 15, 16);
    ymax = (int32_t) floordiv(ymax - off16 - 1, 16);
    if (ymin < s->cy0)
        ymin = s->cy0;
    if (ymax > s->cy1 - 1)
        ymax = s->cy1 - 1;

    for (int32_t y = ymin; y <= ymax; y++) {
        int64_t sy = (int64_t) y * 16 + off16;
        int64_t lo = INT64_MIN / 4;
        int64_t hi = INT64_MAX / 4;

        for (int e = 0; e < 3; e++) {
            int     a  = e;
            int     c  = (e + 1) % 3;
            int64_t ex = X[c] - X[a];
            int64_t ey = Y[c] - Y[a];
            int64_t A, B;
            int     tl;

            /* orient so that inside is positive for both windings */
            if (area > 0) {
                ex = -ex;
                ey = -ey;
            }
            /* E(sx) = ey*(sx - Xa) - ex*(sy - Ya), inside when > 0; a
               left edge has ey > 0, a top edge ey == 0 and ex < 0 */
            A  = ey;
            B  = -ey * X[a] - ex * (sy - Y[a]);
            tl = (ey > 0) || ((ey == 0) && (ex < 0));
            /* E at sample x*16+off16: A*16*x + (A*off16 + B) */
            {
                int64_t k  = A * 16;
                int64_t c0 = A * off16 + B;

                if (k > 0) {
                    /* k*x + c0 > 0, or >= 0 on a top or left edge */
                    int64_t xm = tl ? floordiv(-c0 + k - 1, k) : floordiv(-c0, k) + 1;

                    if (xm > lo)
                        lo = xm;
                } else if (k < 0) {
                    int64_t xm = tl ? floordiv(c0, -k) : floordiv(c0 - 1, -k);

                    if (xm < hi)
                        hi = xm;
                } else if (tl ? (c0 < 0) : (c0 <= 0)) {
                    lo = 1;
                    hi = 0;
                }
            }
        }
        if (lo < s->cx0)
            lo = s->cx0;
        if (hi > s->cx1 - 1)
            hi = s->cx1 - 1;
        if (lo > hi)
            continue;
        if (b->nspans >= B3D_SPANS)
            break;
        b->spans[b->nspans].y   = (int16_t) y;
        b->spans[b->nspans].x0  = (int16_t) lo;
        b->spans[b->nspans].x1  = (int16_t) (hi + 1);
        b->spans[b->nspans].tri = (uint16_t) b->ntris;
        b->nspans++;
        px += (uint64_t) (hi + 1 - lo);
    }
    /* engine time: the twin pipelines draw two single-textured pixels
       a clock, or work as one on a pixel with two textures (counted
       twice); the color write, a depth read and write (the depth
       buffer has the color's depth), the destination read for blending */
    nv4->eng_px3d += (s->tex[1].on && nv4_3d_comb_uses(s, 1)) ? 2 * px : px;
    nv4->eng_bytes += px * (s->bpp + ((s->z_on && s->zen) ? s->bpp : 0) + ((s->z_on && s->zwrite) ? s->bpp : 0)
                            + (s->ablend ? s->bpp : 0));
    if (b->parallel && (ymin <= ymax))
        nv4_3d_written(b, s, ymin, ymax);
    b->ntris++;
    b->had3d  = 1;
    b->last3d = nv4->vblank_count;
}

/* Would a 2D draw into the rows lo..hi of layout base/pitch join the
   batch: does the batch already draw into that layout, with room? */
int
nv4_3d_joins(nv4_t *nv4, uint32_t base, uint32_t pitch, uint32_t lo, uint32_t hi)
{
    nv4_3d_batch_t *b = nv4->batch3d;

    return b && (b->nspans > 0) && (!b->parallel || (nv4_3d_region_find(b, base, pitch, lo, hi) >= 0));
}

/* The CPU is about to read or write VRAM bytes lo..hi (a 2D draw):
   draw the batch first if it reaches them (its rows, or textures it
   samples), and wait for the GPU's batch in flight if its rows do.
   Other bytes need neither. */
void
nv4_3d_touch(nv4_t *nv4, uint32_t lo, uint32_t hi, int cause)
{
    nv4_3d_batch_t *b = nv4->batch3d;
    int             hit = 0;

    if (!b)
        return;
    if (b->nspans || b->ntris) {
        if (!b->parallel || b->serial)
            hit = 1; /* rows are not (all) tracked */
        for (int r = 0; !hit && (r < b->nregions); r++)
            hit = nv4_3d_meets(lo, hi, b->regions[r].lo, b->regions[r].hi);
        if (!hit && (b->tex_lo <= b->tex_hi))
            hit = nv4_3d_meets(lo, hi, b->tex_lo, b->tex_hi);
        if (hit)
            nv4_3d_flush(nv4);
    }
    if (b->gpu && nv4_gpu_pending_overlaps(b->gpu, lo, hi))
        nv4_3d_gpu_wait(nv4, cause);
}

/* The guest's CPU is about to read or write VRAM bytes lo..hi through a
   framebuffer window: the pending 2D run is drawn (it may land there),
   then as for a 2D draw. While the CPU works on other bytes the batch
   keeps growing and the GPU's batches stay in flight. */
void
nv4_3d_access(nv4_t *nv4, uint32_t lo, uint32_t hi, int cause)
{
    nv4_pgraph_2d_flush_run(nv4);
    nv4_3d_touch(nv4, lo, hi, cause);
}

/* A 2D rectangle fill (the 2D engine's clipping done, rows y0..y1-1,
   pixels x0..x1-1 of the layout base/pitch, bytes past <lim> left
   alone), queued with the 3D work instead of drawn now: when it draws
   into rows the batch already draws into (whatever its size: the HUD),
   or when it is large while 3D is being drawn (a frame's color and
   depth clears). Its triangle carries the <nc> words <c> in c[] and
   <kind> in c[NV4_FILL_KIND]. Returns 0 when the caller must draw it. */
static int
nv4_3d_fill_words(nv4_t *nv4, uint32_t base, uint32_t pitch, uint32_t lim, int bpp, int32_t x0, int32_t y0, int32_t x1,
                  int32_t y1, const uint32_t *c, int nc, uint32_t kind)
{
    nv4_3d_batch_t *b = nv4->batch3d;
    nv4_3d_state_t  st;
    nv4_3d_tri_t   *t;
    uint32_t        lo, hi;
    int             si = -1, joins, big;

    if (!b || (x0 >= x1) || (y0 >= y1) || (x0 < 0) || (y0 < 0) || (x1 > 4096) || (y1 > 4096)
        || ((bpp != 1) && (bpp != 2) && (bpp != 4)))
        return 0;

    memset(&st, 0, sizeof(st));
    st.fill      = 1;
    st.col_base  = base;
    st.col_pitch = pitch;
    st.col_lim   = lim;
    st.bpp       = bpp;
    st.cx0       = x0;
    st.cy0       = y0;
    st.cx1       = x1;
    st.cy1       = y1;
    nv4_3d_reach(&st, base, pitch, &lo, &hi);

    big   = b->had3d && ((nv4->vblank_count - b->last3d) <= 2) && ((int64_t) (x1 - x0) * (y1 - y0) >= 65536);
    joins = (b->nspans > 0) && (!b->parallel || (nv4_3d_region_find(b, base, pitch, lo, hi) >= 0));
    if (!big && !joins)
        return 0;
    if ((b->ntris >= B3D_TRIS) || (b->nspans + (y1 - y0) > B3D_SPANS))
        nv4_3d_flush(nv4);

    /* one state serves every fill into the same surface */
    for (int i = 0; i < b->nstates; i++) {
        const nv4_3d_state_t *o = &b->states[i];

        if (o->fill && (o->col_base == base) && (o->col_pitch == pitch) && (o->col_lim == lim) && (o->bpp == bpp)) {
            si = i;
            break;
        }
    }
    if ((si >= 0) && b->parallel) {
        if (nv4_3d_region_find(b, base, pitch, lo, hi) < 0)
            si = -1; /* it would grow into another layout: a new state flushes */
        else
            nv4_3d_region_take(b, base, pitch, lo, hi);
    }
    if (si < 0)
        si = nv4_3d_place_state(nv4, b, &st);

    t = &b->tris[b->ntris];
    memset(t, 0, sizeof(*t));
    t->state = si;
    memcpy(&t->c[0], c, nc * 4);
    memcpy(&t->c[NV4_FILL_KIND], &kind, 4);
    for (int32_t y = y0; y < y1; y++) {
        b->spans[b->nspans].y   = (int16_t) y;
        b->spans[b->nspans].x0  = (int16_t) x0;
        b->spans[b->nspans].x1  = (int16_t) x1;
        b->spans[b->nspans].tri = (uint16_t) b->ntris;
        b->nspans++;
    }
    if (b->parallel) {
        /* the rows of this rectangle, whatever the state's first one was */
        nv4_3d_state_t tmp = b->states[si];

        tmp.cx1 = x1;
        nv4_3d_written(b, &tmp, y0, y1 - 1);
    }
    b->ntris++;
    return 1;
}

/* A plain store of <val> */
int
nv4_3d_fill(nv4_t *nv4, uint32_t base, uint32_t pitch, uint32_t lim, int bpp, int32_t x0, int32_t y0, int32_t x1,
            int32_t y1, uint32_t val)
{
    return nv4_3d_fill_words(nv4, base, pitch, lim, bpp, x0, y0, x1, y1, &val, 1, NV4_FILL_STORE);
}

/* A ROP3 with a solid source and no pattern or a mono one: each pixel
   becomes (d & A) | (~d & B), A and B by the pattern bit (nv4_fill) */
int
nv4_3d_fill_rop(nv4_t *nv4, uint32_t base, uint32_t pitch, uint32_t lim, int bpp, int32_t x0, int32_t y0, int32_t x1,
                int32_t y1, const uint32_t rop[7])
{
    return nv4_3d_fill_words(nv4, base, pitch, lim, bpp, x0, y0, x1, y1, rop, 7, NV4_FILL_ROP);
}

/* A DrawPrimitive word: two triangles by vertex index. */
static void
nv4_3d_draw(nv4_t *nv4, int chid, int cls, uint32_t data)
{
    int n = (cls == 0x55) ? 8 : 16;
    int i[6];

    for (int k = 0; k < 6; k++)
        i[k] = (data >> (k * 4)) & 0xf;
    if ((i[0] < n) && (i[1] < n) && (i[2] < n))
        nv4_3d_triangle(nv4, chid, cls, i[0], i[1], i[2]);
    if ((i[3] < n) && (i[4] < n) && (i[5] < n) && ((i[3] != i[4]) || (i[4] != i[5])))
        nv4_3d_triangle(nv4, chid, cls, i[3], i[4], i[5]);
}

/* ------------------------------------------------------------------ */
/* Methods.                                                            */
/* ------------------------------------------------------------------ */
static void
nv4_3d_vertex(nv4_d3d_t *d, int idx, int field, uint32_t data)
{
    nv4_d3d_vtx_t *v = &d->vtx[idx];

    switch (field) {
        case 0:
            v->sx = f32(data);
            break;
        case 1:
            v->sy = f32(data);
            break;
        case 2:
            v->sz = f32(data);
            break;
        case 3:
            v->rhw = f32(data);
            break;
        case 4:
            v->color = data;
            break;
        case 5:
            v->specular = data;
            break;
        case 6:
            v->tu[0] = f32(data);
            break;
        case 7:
            v->tv[0] = f32(data);
            break;
        case 8:
            v->tu[1] = f32(data);
            break;
        case 9:
            v->tv[1] = f32(data);
            break;
        default:
            break;
    }
}

int
nv4_pgraph_3d_method(nv4_t *nv4, int chid, int cls, uint32_t mthd, uint32_t data)
{
    nv4_d3d_t *d   = &nv4->d3d[chid];
    int        dx6 = (cls == 0x55);

    /* vertices and draws */
    if (!dx6 && (mthd >= 0x400) && (mthd < 0x600)) {
        nv4_3d_vertex(d, (mthd - 0x400) >> 5, ((mthd - 0x400) >> 2) & 7, data);
        return NV4_GR_OK;
    }
    if (!dx6 && (mthd >= 0x600) && (mthd < 0x700)) {
        nv4_3d_draw(nv4, chid, cls, data);
        return NV4_GR_OK;
    }
    if (dx6 && (mthd >= 0x400) && (mthd < 0x540)) {
        nv4_3d_vertex(d, (mthd - 0x400) / 0x28, ((mthd - 0x400) % 0x28) >> 2, data);
        return NV4_GR_OK;
    }
    if (dx6 && (mthd >= 0x540) && (mthd < 0x600)) {
        nv4_3d_draw(nv4, chid, cls, data);
        return NV4_GR_OK;
    }

    /* state: a new draw state is derived at the next triangle */
    d->state = -1;
    switch (mthd) {
        case 0x184:
            nv4_pgraph_set_grobj(nv4, 2, 0, 16, data);
            return NV4_GR_OK;
        case 0x188:
            nv4_pgraph_set_grobj(nv4, 2, 16, 16, data);
            return NV4_GR_OK;
        case 0x18c:
            /* unlike the 2D classes' set-context methods, the chip takes
               this one itself on the TNT too */
            nv4_pgraph_set_grobj(nv4, 0, 25, 1, data && ((nv4_ramin_read32(nv4, data << 4) & 0xfff) == 0x53));
            return NV4_GR_OK;
        case 0x200:
            return NV4_GR_MISSING;
        default:
            break;
    }
    if (!dx6) {
        switch (mthd) {
            case 0x300:
                d->color_key = data;
                break;
            case 0x304:
                d->offset[0] = data;
                break;
            case 0x308:
                d->format[0] = data;
                break;
            case 0x30c:
                d->filter[0] = data;
                break;
            case 0x310:
                d->blend = data;
                break;
            case 0x314:
                d->control[0] = data;
                break;
            case 0x318:
                d->fog_color = data;
                break;
            default:
                return NV4_GR_ILLEGAL;
        }
        return NV4_GR_OK;
    }
    switch (mthd) {
        case 0x308:
        case 0x30c:
            d->offset[(mthd - 0x308) >> 2] = data;
            break;
        case 0x310:
        case 0x314:
            d->format[(mthd - 0x310) >> 2] = data;
            break;
        case 0x318:
        case 0x31c:
            d->filter[(mthd - 0x318) >> 2] = data;
            break;
        case 0x320:
            d->comb_alpha[0] = data;
            break;
        case 0x324:
            d->comb_color[0] = data;
            break;
        case 0x32c:
            d->comb_alpha[1] = data;
            break;
        case 0x330:
            d->comb_color[1] = data;
            break;
        case 0x334:
            d->comb_factor = data;
            break;
        case 0x338:
            d->blend = data;
            break;
        case 0x33c:
        case 0x340:
        case 0x344:
            d->control[(mthd - 0x33c) >> 2] = data;
            break;
        case 0x348:
            d->fog_color = data;
            break;
        default:
            return NV4_GR_ILLEGAL;
    }
    return NV4_GR_OK;
}

void
nv4_3d_reset(nv4_t *nv4)
{
    if (nv4->batch3d) {
        if (nv4->batch3d->gpu)
            nv4_gpu_wait(nv4->batch3d->gpu, nv4);
        nv4->batch3d->nspans   = 0;
        nv4->batch3d->ntris    = 0;
        nv4->batch3d->nstates  = 0;
        nv4->batch3d->nregions = 0;
        nv4->batch3d->serial   = 0;
        nv4->batch3d->tex_lo   = 0xffffffff;
        nv4->batch3d->tex_hi   = 0;
    }
    memset(nv4->d3d, 0, sizeof(nv4->d3d));
    for (int c = 0; c < NV4_CHANNELS; c++)
        nv4->d3d[c].state = -1;
}

void
nv4_3d_init(nv4_t *nv4)
{
    nv4_3d_batch_t *b = calloc(1, sizeof(nv4_3d_batch_t));
    int             n = nv4->render_threads;
    const char     *v = getenv("NV4_3D_VERIFY");

    nv4->batch3d = b;
    nv4_3d_reset(nv4);
    if (n < 1)
        n = 1;
    if (n > B3D_MAX_WORKERS)
        n = B3D_MAX_WORKERS;
    b->nthreads = n;
    b->run      = 1;
    if (nv4->gpu_raster)
        b->gpu = nv4_gpu_init(nv4);
    b->parallel = (n > 1) || b->gpu;
    if (b->parallel && v && ((*v == '1') || !strcmp(v, "full"))) {
        b->vsnap       = malloc(nv4->vram_size);
        b->vres        = malloc(nv4->vram_size);
        b->verify      = b->vsnap && b->vres;
        b->verify_full = !strcmp(v, "full");
    }
    {
        const char *r = getenv("NV4_3D_RECORD");

        if (r && *r) {
            const char *c = strchr(r, ',');
            size_t      l = c ? (size_t) (c - r) : strlen(r);

            if (l >= sizeof(b->rec_dir))
                l = sizeof(b->rec_dir) - 1;
            memcpy(b->rec_dir, r, l);
            b->rec_dir[l] = 0;
            b->rec_every  = 25;
            b->rec_max    = 100;
            if (c) {
                b->rec_every = (uint32_t) atoi(c + 1);
                if ((c = strchr(c + 1, ',')))
                    b->rec_max = (uint32_t) atoi(c + 1);
            }
            if (!b->rec_every)
                b->rec_every = 1;
            pclog("nv4 3D: recording every %u-th flush, %u at most, in %s\n", b->rec_every, b->rec_max, b->rec_dir);
        }
    }
    for (int i = 0; (n > 1) && (i < n); i++) {
        nv4_3d_worker_t *w = &b->workers[i];

        w->nv4    = nv4;
        w->id     = i;
        w->wake   = thread_create_event();
        w->done   = thread_create_event();
        w->thread = thread_create_named(nv4_3d_worker, w, "nv4_3d");
    }
}

void
nv4_3d_close(nv4_t *nv4)
{
    nv4_3d_batch_t *b = nv4->batch3d;

    if (!b)
        return;
    if (b->nthreads > 1) {
        b->run = 0;
        for (int i = 0; i < b->nthreads; i++)
            thread_set_event(b->workers[i].wake);
        for (int i = 0; i < b->nthreads; i++) {
            thread_wait(b->workers[i].thread);
            thread_destroy_event(b->workers[i].wake);
            thread_destroy_event(b->workers[i].done);
        }
    }
    if (b->gpu)
        nv4_gpu_close(b->gpu);
    free(b->vsnap);
    free(b->vres);
    free(b);
    nv4->batch3d = NULL;
}
