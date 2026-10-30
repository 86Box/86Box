/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PGRAPH 2D classes.
 *
 *          The context objects (beta, rop, pattern, chroma, clip,
 *          surfaces) set PGRAPH registers; the drawing objects (rectangle,
 *          line, triangle, blit, image from CPU, scaled image, GDI text,
 *          memory to memory) draw with them.
 *
 *          Every pixel goes through one pipeline: clipped to the user clip
 *          (when the object has a clip context) and to the image classes'
 *          own clip, keyed against the chroma color, then combined with
 *          the destination by the object's operation -- SRCCOPY, ROP3 with
 *          the pattern, or a blend by beta -- and written in the
 *          destination surface's format. The register layout and method
 *          meanings follow envytools (hwtest, nvhw; MIT licensed);
 *          envytools does not model the TNT's pixel output, so the
 *          pipeline here is ours.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/nv/vid_nv4.h>

#define GR(a) NV4_GR(nv4, a)

/* ------------------------------------------------------------------ */
/* Colors.                                                             */
/* ------------------------------------------------------------------ */
/* Object color formats (CTX_SWITCH2 13:8) */
#define CF_Y8          0x01
#define CF_X16A8Y8     0x02
#define CF_X24Y8       0x03
#define CF_A1R5G5B5    0x06
#define CF_X1R5G5B5    0x07
#define CF_X16A1R5G5B5 0x08
#define CF_X17R5G5B5   0x09
#define CF_R5G6B5      0x0a
#define CF_A16R5G6B5   0x0b
#define CF_X16R5G6B5   0x0c
#define CF_A8R8G8B8    0x0d
#define CF_X8R8G8B8    0x0e
#define CF_YUYV        0x12
#define CF_UYVY        0x13

/* A source pixel: as ARGB, and as given (what an indexed or Y surface
   takes unchanged). */
typedef struct {
    uint32_t argb;
    uint32_t raw;
} nv4_px_t;

static uint32_t
x555(uint32_t v)
{
    uint32_t r = (v >> 10) & 0x1f;
    uint32_t g = (v >> 5) & 0x1f;
    uint32_t b = v & 0x1f;

    return ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
}

static uint32_t
x565(uint32_t v)
{
    uint32_t r = (v >> 11) & 0x1f;
    uint32_t g = (v >> 5) & 0x3f;
    uint32_t b = v & 0x1f;

    return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

static nv4_px_t
nv4_color(uint32_t v, int cfmt)
{
    nv4_px_t p;
    uint32_t y = v & 0xff;

    p.raw = v;
    switch (cfmt) {
        case CF_Y8:
        case CF_X24Y8:
            p.argb = 0xff000000 | (y * 0x010101);
            break;
        case CF_X16A8Y8:
            p.argb = ((v & 0xff00) << 16) | (y * 0x010101);
            break;
        case CF_A1R5G5B5:
        case CF_X16A1R5G5B5:
            p.argb = ((v & 0x8000) ? 0xff000000 : 0) | x555(v);
            break;
        case CF_X1R5G5B5:
        case CF_X17R5G5B5:
            p.argb = 0xff000000 | x555(v);
            break;
        case CF_R5G6B5:
        case CF_X16R5G6B5:
            p.argb = 0xff000000 | x565(v);
            break;
        case CF_A16R5G6B5:
            p.argb = (v & 0xff000000) | x565(v);
            break;
        case CF_A8R8G8B8:
            p.argb = v;
            break;
        case CF_X8R8G8B8:
            p.argb = v | 0xff000000;
            break;
        default:
            p.argb = v;
            break;
    }
    return p;
}

/* Surface formats (BPIXEL) */
static int
nv4_surf_bpp(int sfmt)
{
    if (sfmt == 1)
        return 1;
    if ((sfmt >= 2 && sfmt <= 6) || sfmt >= 0xe)
        return 2;
    if (sfmt >= 7)
        return 4;
    return 0;
}

static uint32_t
nv4_pack(nv4_px_t p, int sfmt)
{
    uint32_t c   = p.argb;
    uint32_t rgb = c & 0xffffff;
    uint32_t a   = c >> 24;
    uint32_t v555 = ((c >> 9) & 0x7c00) | ((c >> 6) & 0x03e0) | ((c >> 3) & 0x1f);

    switch (sfmt) {
        case 0x1:
            return p.raw & 0xff;
        case 0x2:
            return v555;
        case 0x3:
            return v555 | 0x8000;
        case 0x4:
            return v555 | ((a & 0x80) << 8);
        case 0x5:
            return ((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x1f);
        case 0x6:
            return p.raw & 0xffff;
        case 0x7:
            return rgb;
        case 0x8:
            return rgb | 0x80000000;
        case 0x9:
            return rgb | ((a >> 1) << 24);
        case 0xa:
            return rgb | ((a >> 1) << 24) | 0x80000000;
        case 0xb:
            return rgb | 0xff000000;
        case 0xc:
            return c;
        case 0xd:
            return p.raw;
        default:
            return p.raw & 0xffff;
    }
}

static uint32_t
nv4_unpack(uint32_t v, int sfmt)
{
    switch (sfmt) {
        case 0x1:
            return 0xff000000 | ((v & 0xff) * 0x010101);
        case 0x2:
        case 0x3:
            return 0xff000000 | x555(v);
        case 0x4:
            return ((v & 0x8000) ? 0xff000000 : 0) | x555(v);
        case 0x5:
            return 0xff000000 | x565(v);
        case 0x7:
        case 0x8:
        case 0xb:
            return v | 0xff000000;
        case 0x9:
        case 0xa:
            return (v & 0xffffff) | (((v >> 24) & 0x7f) << 25);
        default:
            return v;
    }
}

/* The color bits of a surface format: what a chroma key compares. */
static uint32_t
nv4_color_mask(int sfmt)
{
    switch (sfmt) {
        case 0x1:
            return 0xff;
        case 0x2:
        case 0x3:
        case 0x4:
            return 0x7fff;
        case 0x5:
        case 0x6:
            return 0xffff;
        case 0xc:
        case 0xd:
            return 0xffffffff;
        default:
            return (nv4_surf_bpp(sfmt) == 4) ? 0xffffff : 0xffff;
    }
}

static uint32_t
nv4_mul8(uint32_t a, uint32_t b)
{
    uint32_t t = a * b + 0x80;

    return (t + (t >> 8)) >> 8;
}

/* Per channel a*b/255 */
static uint32_t
nv4_mul_argb(uint32_t c, uint32_t m)
{
    uint32_t r = 0;

    for (int s = 0; s < 32; s += 8)
        r |= nv4_mul8((c >> s) & 0xff, (m >> s) & 0xff) << s;
    return r;
}

/* s*f + d*(255-f), per channel */
static uint32_t
nv4_lerp_argb(uint32_t s, uint32_t d, uint32_t f)
{
    uint32_t r = 0;

    for (int i = 0; i < 32; i += 8)
        r |= (nv4_mul8((s >> i) & 0xff, f) + nv4_mul8((d >> i) & 0xff, 255 - f)) << i;
    return r;
}

static uint32_t
nv4_rop3(uint8_t rop, uint32_t p, uint32_t s, uint32_t d)
{
    uint32_t r = 0;

    switch (rop) {
        case 0x00:
            return 0;
        case 0x55:
            return ~d;
        case 0x5a:
            return p ^ d;
        case 0x66:
            return s ^ d;
        case 0x88:
            return s & d;
        case 0xaa:
            return d;
        case 0xcc:
            return s;
        case 0xee:
            return s | d;
        case 0xf0:
            return p;
        case 0xff:
            return 0xffffffff;
        default:
            break;
    }
    for (int i = 0; i < 8; i++) {
        if (rop & (1 << i))
            r |= ((i & 4) ? p : ~p) & ((i & 2) ? s : ~s) & ((i & 1) ? d : ~d);
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* Surfaces.                                                           */
/* ------------------------------------------------------------------ */
static uint32_t
nv4_offset_mask(const nv4_t *nv4)
{
    return (nv4->chip == NV4_CHIP_TNT2) ? 0x01ffffff : 0x00ffffff;
}

static int
nv4_surf_fmt(nv4_t *nv4, int which)
{
    return (GR(NV4_PGRAPH_BPIXEL) >> (which * 4)) & 0xf;
}

static void
nv4_set_surf_fmt(nv4_t *nv4, int which, int fmt)
{
    GR(NV4_PGRAPH_BPIXEL) = (GR(NV4_PGRAPH_BPIXEL) & ~(0xfu << (which * 4))) | ((uint32_t) fmt << (which * 4));
}

/* Binds the DMA object <inst> as surface <which>. */
static void
nv4_dma_surf(nv4_t *nv4, int which, uint32_t inst)
{
    uint32_t mask = nv4_offset_mask(nv4);
    uint32_t w0;
    uint32_t limit;
    uint32_t w2;

    if (!inst) {
        GR(NV4_PGRAPH_BBASE + which * 4)  = 0;
        GR(NV4_PGRAPH_BLIMIT + which * 4) = 0x8000000f;
        return;
    }
    w0    = nv4_ramin_read32(nv4, inst << 4);
    limit = nv4_ramin_read32(nv4, (inst << 4) + 4);
    w2    = nv4_ramin_read32(nv4, (inst << 4) + 8);
    GR(NV4_PGRAPH_BBASE + which * 4)  = ((w2 & ~0xfffu) | (w0 >> 20)) & mask;
    GR(NV4_PGRAPH_BLIMIT + which * 4) = (limit & mask) | 0xf | (((w0 & 0xfff) == 0x30) ? 0x80000000 : 0);
}

typedef struct {
    uint32_t base;  /* BBASE */
    uint32_t off;   /* BOFFSET */
    uint32_t limit; /* last valid byte past BBASE */
    uint32_t pitch;
    int      fmt;
    int      bpp;
    int      swz;   /* swizzled: texels interleave x and y bits */
    int      wl, hl;
} nv4_surf_t;

/* Offset of texel x,y in a swizzled surface of 2^wl by 2^hl: the bits
   of x and y interleave, x first, until the smaller runs out. */
static uint32_t
nv4_swz_offset(uint32_t x, uint32_t y, int wl, int hl)
{
    uint32_t r   = 0;
    int      bit = 0;

    for (int i = 0; (i < wl) || (i < hl); i++) {
        if (i < wl)
            r |= ((x >> i) & 1) << bit++;
        if (i < hl)
            r |= ((y >> i) & 1) << bit++;
    }
    return r;
}

static int
nv4_surf_get(nv4_t *nv4, int which, nv4_surf_t *s)
{
    uint32_t lim = GR(NV4_PGRAPH_BLIMIT + which * 4);

    s->fmt   = nv4_surf_fmt(nv4, which);
    s->bpp   = nv4_surf_bpp(s->fmt);
    s->base  = GR(NV4_PGRAPH_BBASE + which * 4);
    s->off   = GR(NV4_PGRAPH_BOFFSET + which * 4);
    s->limit = lim & 0x7fffffff;
    s->pitch = (which < 5) ? (GR(NV4_PGRAPH_BPITCH + which * 4) & 0xffff) : 0;
    s->swz   = (which == NV4_SURF_SWZ);
    s->wl    = (GR(NV4_PGRAPH_BSWIZZLE5) >> 16) & 0xf;
    s->hl    = (GR(NV4_PGRAPH_BSWIZZLE5) >> 24) & 0xf;
    return s->bpp && !(lim & 0x80000000);
}

static uint32_t
nv4_vram_rd(nv4_t *nv4, uint32_t a, int bpp)
{
    uint8_t *v = nv4->svga.vram;
    uint32_t m = nv4->vram_mask;

    switch (bpp) {
        case 1:
            return v[a & m];
        case 2:
            return v[a & m] | (v[(a + 1) & m] << 8);
        default:
            return v[a & m] | (v[(a + 1) & m] << 8) | (v[(a + 2) & m] << 16) | ((uint32_t) v[(a + 3) & m] << 24);
    }
}

static void
nv4_vram_wr(nv4_t *nv4, uint32_t a, int bpp, uint32_t val)
{
    uint8_t *v = nv4->svga.vram;
    uint32_t m = nv4->vram_mask;

    for (int i = 0; i < bpp; i++)
        v[(a + i) & m] = val >> (i * 8);
    nv4->svga.changedvram[(a & m) >> 12] = nv4->svga.monitor->mon_changeframecount;
}

/* Reads pixel x,y of surface <s>; 0 outside it. */
static uint32_t
nv4_surf_read(nv4_t *nv4, const nv4_surf_t *s, int x, int y)
{
    uint32_t rel;

    if ((x < 0) || (y < 0))
        return 0;
    rel = s->off + (uint32_t) y * s->pitch + (uint32_t) x * s->bpp;
    if (rel + s->bpp - 1 > s->limit)
        return 0;
    return nv4_vram_rd(nv4, s->base + rel, s->bpp);
}

/* ------------------------------------------------------------------ */
/* The pixel pipeline.                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    nv4_t     *nv4;
    nv4_surf_t dst;
    int        op;
    uint8_t    rop;
    int        rop_d;    /* the operation reads the destination */
    int        rop_p;    /* ... the pattern */
    int32_t    cx0, cy0, cx1, cy1;
    int        chroma;
    uint32_t   chroma_val;
    uint32_t   cmask;
    int        pat_shape;
    int        pat_color;
    uint32_t   pat_mono[2];
    uint32_t   pat_c[2];
    int        pat_t[2]; /* a mono pattern color with zero alpha: not drawn */
    uint32_t   pat_ram[64];
    uint32_t   beta;
    uint32_t   beta4;
    int        plain; /* every pixel a plain store of the source */
    int        ropfill; /* a rectangle fill the 3D batch can take as a ROP (nv4_3d_fill_rop) */
    int        defer; /* plain stores into rows the 3D batch draws: join it */
    int        touch; /* wait for the 3D work reaching lo..hi before the first pixel */
    uint32_t   lo, hi;
} nv4_draw_t;

static int32_t
sext(uint32_t v, int bits)
{
    return (int32_t) (v << (32 - bits)) >> (32 - bits);
}

static void
nv4_clip_and(nv4_t *nv4, nv4_draw_t *d, uint32_t min_reg, uint32_t max_reg)
{
    int32_t x0 = sext(GR(min_reg), 16);
    int32_t y0 = sext(GR(min_reg + 4), 16);
    int32_t x1 = sext(GR(max_reg), 18);
    int32_t y1 = sext(GR(max_reg + 4), 18);

    if (x0 > d->cx0)
        d->cx0 = x0;
    if (y0 > d->cy0)
        d->cy0 = y0;
    if (x1 < d->cx1)
        d->cx1 = x1;
    if (y1 < d->cy1)
        d->cy1 = y1;
}

/* Sets up a drawing to the destination surface. <oclip>: the class
   clips to its own rectangle. <use_uclip>: the user clip applies. */
/* <fill>: the caller is a rectangle fill (nv4_fill), which may join the
   3D batch under any ROP */
static int
nv4_draw_begin_fill(nv4_t *nv4, nv4_draw_t *d, int oclip, int use_uclip, int fill)
{
    uint32_t sw1 = GR(NV4_PGRAPH_CTX_SWITCH1);

    d->nv4 = nv4;
    /* an object bound to a swizzled surface draws into it */
    if (!nv4_surf_get(nv4, (sw1 & (1u << 14)) ? NV4_SURF_SWZ : NV4_SURF_DST, &d->dst))
        return 0;
    d->op    = (sw1 >> 15) & 7;
    d->rop   = GR(NV4_PGRAPH_ROP3) & 0xff;
    d->rop_p = (d->op == 1) && (((d->rop >> 4) ^ d->rop) & 0x0f);
    d->rop_d = ((d->op == 1) && (((d->rop >> 1) ^ d->rop) & 0x55)) || (d->op == 2) || (d->op == 5);
    d->cx0   = 0;
    d->cy0   = 0;
    /* A linear surface has no width: x past the pitch is the next
       row's bytes (the OpenGL ICD's 8-pixel progress surface writes at
       x = 8); the clips and the limit bound it. */
    d->cx1   = d->dst.swz ? (1 << d->dst.wl) : 0x8000;
    d->cy1   = d->dst.swz ? (1 << d->dst.hl) : 0x8000;
    if (use_uclip && (sw1 & (1u << 13)))
        nv4_clip_and(nv4, d, NV4_PGRAPH_UCLIP_MIN, NV4_PGRAPH_UCLIP_MAX);
    if (oclip)
        nv4_clip_and(nv4, d, NV4_PGRAPH_UCLIPA_MIN, NV4_PGRAPH_UCLIPA_MAX);
    if ((d->cx0 >= d->cx1) || (d->cy0 >= d->cy1))
        return 0;

    d->cmask  = nv4_color_mask(d->dst.fmt);
    d->chroma = 0;
    if (sw1 & (1u << 12)) {
        nv4_px_t c = nv4_color(GR(NV4_PGRAPH_CHROMA), (GR(NV4_PGRAPH_STORED_FMT) >> 24) & 0x3f);

        if (c.argb >> 24) {
            d->chroma     = 1;
            d->chroma_val = nv4_pack(c, d->dst.fmt) & d->cmask;
        }
    }

    if (d->rop_p) {
        int pfmt = (GR(NV4_PGRAPH_STORED_FMT) >> 8) & 0x3f;

        d->pat_shape   = GR(NV4_PGRAPH_PATTERN_SHAPE) & 3;
        d->pat_color   = (GR(NV4_PGRAPH_PATTERN_SHAPE) >> 4) & 1;
        d->pat_mono[0] = GR(NV4_PGRAPH_PATTERN);
        d->pat_mono[1] = GR(NV4_PGRAPH_PATTERN + 4);
        d->pat_c[0]    = nv4_pack(nv4_color(GR(NV4_PGRAPH_PATT_COLOR0), pfmt), d->dst.fmt);
        d->pat_c[1]    = nv4_pack(nv4_color(GR(NV4_PGRAPH_PATT_COLOR1), pfmt), d->dst.fmt);
        /* A mono pattern color whose alpha is zero leaves its pixels
           alone: the display driver's transparent hatch brushes set
           color 0 to 0 and the opaque ones give it full alpha. Into an
           8-bit surface the alpha is bits 15:8 (the driver ORs 0xff00
           into an index there). */
        for (int i = 0; i < 2; i++) {
            uint32_t c = GR(NV4_PGRAPH_PATT_COLOR0 + i * 4);

            if (d->dst.fmt == 1)
                d->pat_t[i] = !(c & 0xff00);
            else
                d->pat_t[i] = !(nv4_color(c, pfmt).argb >> 24);
        }
        if (d->pat_color) {
            for (int i = 0; i < 64; i++) {
                uint32_t c = GR(NV4_PGRAPH_PATT_COLORRAM + i * 4) & 0xffffff;
                nv4_px_t p = { 0xff000000 | c, c };

                d->pat_ram[i] = nv4_pack(p, d->dst.fmt);
            }
        }
    }
    d->beta  = (GR(NV4_PGRAPH_BETA_AND) >> 23) & 0xff;
    d->beta4 = GR(NV4_PGRAPH_BETA_PREMULT);

    /* The bytes it can reach: its clip's rows. Plain stores into rows
       the 3D batch already draws join it; anything else waits for the
       3D work only if that reaches these bytes. */
    {
        uint32_t lo = d->dst.base + d->dst.off;
        uint32_t hi = d->dst.base + d->dst.limit;

        if (!d->dst.swz) {
            uint32_t rhi = lo + (uint32_t) (d->cy1 - 1) * d->dst.pitch + (uint32_t) d->cx1 * d->dst.bpp - 1;

            lo += (uint32_t) d->cy0 * d->dst.pitch;
            if (rhi < hi)
                hi = rhi;
        }
        d->plain = !d->dst.swz && !d->chroma && !d->rop_d && !d->rop_p
            && (((d->op != 1) && (d->op != 2) && (d->op != 4) && (d->op != 5)) || ((d->op == 1) && (d->rop == 0xcc)));
        /* a fill under a ROP3 whose pattern, if any, is mono: each pixel
           is (d & A) | (~d & B), A and B from the pattern color */
        d->ropfill = fill && !d->plain && !d->dst.swz && !d->chroma && (d->op == 1) && (!d->rop_p || !d->pat_color);
        d->defer   = d->plain && nv4_3d_joins(nv4, d->dst.base + d->dst.off, d->dst.pitch, lo, hi);
        d->lo      = lo;
        d->hi      = hi;
        /* a plain store or a ROP fill may still join as a fill
           (nv4_fill): the wait, if any, comes with the first pixel drawn
           here */
        d->touch = !d->defer;
        if (d->touch && !d->plain && !d->ropfill) {
            nv4_3d_touch(nv4, lo, hi, GR(NV4_PGRAPH_CTX_SWITCH1) & 0xff);
            d->touch = 0;
        }
    }
    return 1;
}

static int
nv4_draw_begin(nv4_t *nv4, nv4_draw_t *d, int oclip, int use_uclip)
{
    return nv4_draw_begin_fill(nv4, d, oclip, use_uclip, 0);
}

/* The pending run of plain-store pixels: into the 3D batch, or drawn
   here when it cannot join any more. */
void
nv4_pgraph_2d_flush_run(nv4_t *nv4)
{
    uint32_t base, pitch, lim, v;
    int      bpp;
    int32_t  y, x0, x1;

    if (!nv4->run2d.n)
        return;
    nv4->run2d.n = 0;
    base         = nv4->run2d.base;
    pitch        = nv4->run2d.pitch;
    lim          = nv4->run2d.lim;
    bpp          = nv4->run2d.bpp;
    v            = nv4->run2d.v;
    y            = nv4->run2d.y;
    x0           = nv4->run2d.x0;
    x1           = nv4->run2d.x1;
    if (nv4_3d_fill(nv4, base, pitch, lim, bpp, x0, y, x1, y + 1, v))
        return;
    nv4_3d_touch(nv4, base + (uint32_t) y * pitch + (uint32_t) x0 * bpp, base + (uint32_t) y * pitch + (uint32_t) x1 * bpp - 1,
                 GR(NV4_PGRAPH_CTX_SWITCH1) & 0xff);
    for (int32_t x = x0; x < x1; x++) {
        uint32_t a = base + (uint32_t) y * pitch + (uint32_t) x * bpp;

        if (a + bpp - 1 <= lim)
            nv4_vram_wr(nv4, a, bpp, v);
    }
}

/* A plain-store pixel joining the run, or starting the next one. */
static void
nv4_run_put(nv4_draw_t *d, int32_t x, int32_t y, uint32_t s)
{
    nv4_t   *nv4  = d->nv4;
    uint32_t base = d->dst.base + d->dst.off;

    if (nv4->run2d.n && (nv4->run2d.y == y) && (nv4->run2d.x1 == x) && (nv4->run2d.v == s) && (nv4->run2d.base == base)
        && (nv4->run2d.pitch == d->dst.pitch) && (nv4->run2d.bpp == d->dst.bpp)) {
        nv4->run2d.x1++;
        return;
    }
    nv4_pgraph_2d_flush_run(nv4);
    nv4->run2d.n     = 1;
    nv4->run2d.base  = base;
    nv4->run2d.pitch = d->dst.pitch;
    nv4->run2d.lim   = d->dst.base + d->dst.limit;
    nv4->run2d.bpp   = d->dst.bpp;
    nv4->run2d.v     = s;
    nv4->run2d.y     = y;
    nv4->run2d.x0    = x;
    nv4->run2d.x1    = x + 1;
}

/* The pattern at x,y; *skip is set when a transparent mono color
   leaves the pixel alone. */
static uint32_t
nv4_pattern(const nv4_draw_t *d, int x, int y, int *skip)
{
    int i, b;

    switch (d->pat_shape) {
        case 1:
            i = x & 63;
            break;
        case 2:
            i = y & 63;
            break;
        default:
            i = ((y & 7) << 3) | (x & 7);
            break;
    }
    if (d->pat_color)
        return d->pat_ram[i];
    b = (d->pat_mono[i >> 5] >> (i & 31)) & 1;
    *skip = d->pat_t[b];
    return d->pat_c[b];
}

/* One pixel of source <sp> (whose value packed for the destination is
   <s>) at x,y. */
static void
nv4_put(nv4_draw_t *d, int32_t x, int32_t y, nv4_px_t sp, uint32_t s)
{
    nv4_t   *nv4 = d->nv4;
    uint32_t rel;
    uint32_t a;
    uint32_t dv = 0;
    uint32_t pv = 0;
    uint32_t out;

    if ((x < d->cx0) || (x >= d->cx1) || (y < d->cy0) || (y >= d->cy1))
        return;
    if (d->chroma && ((s & d->cmask) == d->chroma_val))
        return;
    if (d->rop_p) {
        int skip = 0;

        pv = nv4_pattern(d, x, y, &skip);
        if (skip)
            return;
    }
    nv4->eng_bytes += d->dst.bpp * (d->rop_d ? 2 : 1);
    if (d->defer) {
        /* the 3D batch keeps a row's pixels on its row's bytes */
        if ((uint32_t) (x + 1) * d->dst.bpp <= d->dst.pitch) {
            nv4_run_put(d, x, y, s);
            return;
        }
        nv4_pgraph_2d_flush_run(nv4);
        d->touch = 1;
    }
    if (d->touch) {
        nv4_3d_touch(nv4, d->lo, d->hi, GR(NV4_PGRAPH_CTX_SWITCH1) & 0xff);
        d->touch = 0;
    }
    if (d->dst.swz)
        rel = d->dst.off + nv4_swz_offset(x, y, d->dst.wl, d->dst.hl) * d->dst.bpp;
    else
        rel = d->dst.off + (uint32_t) y * d->dst.pitch + (uint32_t) x * d->dst.bpp;
    if (rel + d->dst.bpp - 1 > d->dst.limit)
        return;
    a = d->dst.base + rel;
    if (d->rop_d)
        dv = nv4_vram_rd(nv4, a, d->dst.bpp);

    switch (d->op) {
        case 1: /* ROP_AND */
            out = nv4_rop3(d->rop, pv, s, dv);
            break;
        case 2: /* BLEND_AND */
            {
                nv4_px_t p = { nv4_lerp_argb(sp.argb, nv4_unpack(dv, d->dst.fmt), d->beta), sp.raw };
                out        = nv4_pack(p, d->dst.fmt);
            }
            break;
        case 4: /* SRCCOPY_PREMULT */
            {
                nv4_px_t p = { nv4_mul_argb(sp.argb, d->beta4), sp.raw };
                out        = nv4_pack(p, d->dst.fmt);
            }
            break;
        case 5: /* BLEND_PREMULT */
            {
                uint32_t sm = nv4_mul_argb(sp.argb, d->beta4);
                uint32_t dc = nv4_unpack(dv, d->dst.fmt);
                uint32_t ia = 255 - (sm >> 24);
                uint32_t r  = 0;

                for (int i = 0; i < 32; i += 8) {
                    uint32_t c = ((sm >> i) & 0xff) + nv4_mul8((dc >> i) & 0xff, ia);
                    r |= ((c > 255) ? 255 : c) << i;
                }
                {
                    nv4_px_t p = { r, sp.raw };
                    out        = nv4_pack(p, d->dst.fmt);
                }
            }
            break;
        default: /* SRCCOPY, SRCCOPY_AND */
            out = s;
            break;
    }
    nv4_vram_wr(nv4, a, d->dst.bpp, out);
}

static void
nv4_put_color(nv4_draw_t *d, int32_t x, int32_t y, nv4_px_t sp)
{
    nv4_put(d, x, y, sp, nv4_pack(sp, d->dst.fmt));
}

static void
nv4_fill(nv4_draw_t *d, int32_t x0, int32_t y0, int32_t x1, int32_t y1, nv4_px_t sp)
{
    nv4_t   *nv4 = d->nv4;
    uint32_t s   = nv4_pack(sp, d->dst.fmt);

    if (x0 < d->cx0)
        x0 = d->cx0;
    if (y0 < d->cy0)
        y0 = d->cy0;
    if (x1 > d->cx1)
        x1 = d->cx1;
    if (y1 > d->cy1)
        y1 = d->cy1;
    /* a plain store (SRCCOPY, no chroma key) into a linear surface can
       join the 3D batch; anything else is drawn here, after it */
    if (!d->dst.swz && !d->chroma && (((d->op != 1) && (d->op != 2) && (d->op != 4) && (d->op != 5)) || ((d->op == 1) && (d->rop == 0xcc)))
        && ((uint32_t) x1 * d->dst.bpp <= d->dst.pitch)
        && nv4_3d_fill(nv4, d->dst.base + d->dst.off, d->dst.pitch, d->dst.base + d->dst.limit, d->dst.bpp, x0, y0, x1, y1,
                       s)) {
        if ((x1 > x0) && (y1 > y0))
            nv4->eng_bytes += (uint64_t) (x1 - x0) * (uint64_t) (y1 - y0) * d->dst.bpp;
        return;
    }
    /* a ROP fill: the bits each pattern color makes of a destination bit
       of 1 (A) and of 0 (B), the ROP being bitwise */
    if (d->ropfill && ((uint32_t) x1 * d->dst.bpp <= d->dst.pitch)) {
        uint32_t r[7] = { 0 };

        for (int i = 0; i < 2; i++) {
            uint32_t p = d->rop_p ? d->pat_c[i] : 0;

            r[i * 2]     = nv4_rop3(d->rop, p, s, 0xffffffff);
            r[i * 2 + 1] = nv4_rop3(d->rop, p, s, 0);
        }
        r[4] = d->pat_mono[0];
        r[5] = d->pat_mono[1];
        r[6] = d->rop_p ? (NV4_ROPFILL_PAT | (uint32_t) d->pat_shape | (d->pat_t[0] ? NV4_ROPFILL_SKIP0 : 0)
                           | (d->pat_t[1] ? NV4_ROPFILL_SKIP1 : 0))
                        : 0;
        if (nv4_3d_fill_rop(nv4, d->dst.base + d->dst.off, d->dst.pitch, d->dst.base + d->dst.limit, d->dst.bpp, x0, y0, x1,
                            y1, r)) {
            if ((x1 > x0) && (y1 > y0))
                nv4->eng_bytes += (uint64_t) (x1 - x0) * (uint64_t) (y1 - y0) * d->dst.bpp * (d->rop_d ? 2 : 1);
            return;
        }
    }
    /* (nv4_draw_begin waited for the 3D work if it reaches these bytes,
       or the pixels join the batch as runs) */
    for (int32_t y = y0; y < y1; y++)
        for (int32_t x = x0; x < x1; x++)
            nv4_put(d, x, y, sp, s);
}

/* ------------------------------------------------------------------ */
/* Object state.                                                       */
/* ------------------------------------------------------------------ */
static int
nv4_src_fmt(nv4_t *nv4)
{
    return (GR(NV4_PGRAPH_CTX_SWITCH2) >> 8) & 0x3f;
}

static void
nv4_set_color_fmt(nv4_t *nv4, int fmt)
{
    nv4_pgraph_set_grobj(nv4, 1, 8, 6, fmt);
}

static nv4_px_t
nv4_obj_color(nv4_t *nv4, uint32_t v)
{
    return nv4_color(v, nv4_src_fmt(nv4));
}

/* Rectangle, line, triangle and GDI color formats */
static int
nv4_solid_fmt(uint32_t v, int is_new)
{
    switch (v & 0xf) {
        case 1:
            return is_new ? CF_X16R5G6B5 : CF_X24Y8;
        case 2:
            return CF_X17R5G5B5;
        case 3:
            return CF_X8R8G8B8;
        case 4:
            return is_new ? 0 : 0x11;
        default:
            return 0;
    }
}

/* Image from CPU formats */
static int
nv4_ifc_fmt(uint32_t v, int is_new)
{
    static const int fmts[8] = { 0, 0, CF_A1R5G5B5, CF_X1R5G5B5, CF_A8R8G8B8, CF_X8R8G8B8, 0, 0 };

    if ((v & 7) == 1)
        return is_new ? CF_R5G6B5 : CF_Y8;
    return fmts[v & 7];
}

/* Scaled image from memory formats */
static int
nv4_sifm_fmt(uint32_t v)
{
    static const int fmts[10] = { 0, CF_A1R5G5B5, CF_X1R5G5B5, CF_A8R8G8B8, CF_X8R8G8B8,
                                  CF_YUYV, CF_UYVY, CF_R5G6B5, CF_Y8, 0x15 };

    return ((v & 0xf) < 10) ? fmts[v & 0xf] : 0;
}

/* Context object color formats: pattern, chroma */
static int
nv4_ctx_fmt(uint32_t v, int cls)
{
    switch (v & 3) {
        case 1:
            return ((cls == 0x44) || (cls == 0x57)) ? CF_A16R5G6B5 : CF_X16A8Y8;
        case 2:
            return CF_X16A1R5G5B5;
        case 3:
            return CF_A8R8G8B8;
        default:
            return 0;
    }
}

/* A set-context method for an object of the class: the TNT leaves it to
   the driver; the TNT2 does it when DEBUG_3 bit 29 is set. <bit> is the
   object's CTX_SWITCH1 bit that says the context is bound. */
static int
nv4_ctx_method(nv4_t *nv4, int bit, uint32_t inst)
{
    uint32_t cls;

    if ((nv4->chip != NV4_CHIP_TNT2) || !(GR(NV4_PGRAPH_DEBUG_3) & (1u << 29)))
        return NV4_GR_ILLEGAL;
    cls = inst ? (nv4_ramin_read32(nv4, inst << 4) & 0xfff) : 0x30;
    if (bit >= 0)
        nv4_pgraph_set_grobj(nv4, 0, bit, 1, cls != 0x30);
    if (bit == 25)
        nv4_pgraph_set_grobj(nv4, 0, 14, 1, cls == 0x52);
    return NV4_GR_OK;
}

static int
nv4_operation_method(nv4_t *nv4, uint32_t data)
{
    if ((nv4->chip != NV4_CHIP_TNT2) || !(GR(NV4_PGRAPH_DEBUG_3) & (1u << 30)))
        return NV4_GR_ILLEGAL;
    nv4_pgraph_set_grobj(nv4, 0, 15, 3, data);
    return NV4_GR_OK;
}

/* The user clip registers shift: a write moves the old maximum into the
   minimum. A point then a size (or a second point) leaves min..max. */
static void
nv4_uclip_write(nv4_t *nv4, int which, int xy, int32_t coord)
{
    uint32_t minr = (which ? NV4_PGRAPH_UCLIPA_MIN : NV4_PGRAPH_UCLIP_MIN) + xy * 4;
    uint32_t maxr = (which ? NV4_PGRAPH_UCLIPA_MAX : NV4_PGRAPH_UCLIP_MAX) + xy * 4;

    GR(minr) = GR(maxr) & 0xffff;
    GR(maxr) = (uint32_t) coord & 0x3ffff;
}

/* idx 0: top left point, 1: bottom right point, 2: size */
static void
nv4_set_clip(nv4_t *nv4, int which, int idx, uint32_t val)
{
    uint32_t maxr = which ? NV4_PGRAPH_UCLIPA_MAX : NV4_PGRAPH_UCLIP_MAX;

    for (int xy = 0; xy < 2; xy++) {
        int32_t v = (int16_t) (val >> (xy * 16));

        if (idx == 2)
            v = sext(GR(maxr + xy * 4), 18) + (int32_t) ((val >> (xy * 16)) & 0xffff);
        nv4_uclip_write(nv4, which, xy, v);
    }
}

static uint32_t
nv4_expand_mono(nv4_t *nv4, uint32_t v)
{
    uint32_t r;

    if ((GR(NV4_PGRAPH_CTX_SWITCH2) & 3) != 1)
        return v;
    /* CGA6: the first pixel is each byte's top bit */
    r = 0;
    for (int i = 0; i < 32; i++)
        if (v & (1u << i))
            r |= 1u << (i ^ 7);
    return r;
}

/* ------------------------------------------------------------------ */
/* Drawing objects.                                                    */
/* ------------------------------------------------------------------ */
static void
nv4_rect(nv4_t *nv4, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color, int oclip, int uclip)
{
    nv4_draw_t d;

    if (!w || !h || !nv4_draw_begin_fill(nv4, &d, oclip, uclip, 1))
        return;
    nv4_fill(&d, x, y, x + (int32_t) w, y + (int32_t) h, nv4_obj_color(nv4, color));
}

static void
nv4_line(nv4_t *nv4, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color)
{
    nv4_draw_t d;
    nv4_px_t   sp;
    uint32_t   s;
    int32_t    dx = abs(x1 - x0);
    int32_t    dy = -abs(y1 - y0);
    int32_t    sx = (x0 < x1) ? 1 : -1;
    int32_t    sy = (y0 < y1) ? 1 : -1;
    int32_t    err = dx + dy;

    if (!nv4_draw_begin(nv4, &d, 0, 1))
        return;
    sp = nv4_obj_color(nv4, color);
    s  = nv4_pack(sp, d.dst.fmt);
    /* the last point is not drawn, so polylines do not overlap */
    while ((x0 != x1) || (y0 != y1)) {
        int32_t e2 = 2 * err;

        nv4_put(&d, x0, y0, sp, s);
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static int64_t
nv4_edge(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t px, int32_t py)
{
    return (int64_t) (bx - ax) * (py - ay) - (int64_t) (by - ay) * (px - ax);
}

/* A flat triangle; pixel centers, top-left rule */
static void
nv4_triangle(nv4_t *nv4, const int32_t *vx, const int32_t *vy, uint32_t color)
{
    nv4_draw_t d;
    nv4_px_t   sp;
    uint32_t   s;
    int32_t    x0 = vx[0], y0 = vy[0], x1 = vx[1], y1 = vy[1], x2 = vx[2], y2 = vy[2];
    int32_t    minx, maxx, miny, maxy;
    int64_t    area;

    if (!nv4_draw_begin(nv4, &d, 0, 1))
        return;
    area = nv4_edge(x0, y0, x1, y1, x2, y2);
    if (!area)
        return;
    if (area < 0) {
        int32_t t = x1;
        x1        = x2;
        x2        = t;
        t         = y1;
        y1        = y2;
        y2        = t;
    }
    sp   = nv4_obj_color(nv4, color);
    s    = nv4_pack(sp, d.dst.fmt);
    minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    if (minx < d.cx0)
        minx = d.cx0;
    if (miny < d.cy0)
        miny = d.cy0;
    if (maxx > d.cx1 - 1)
        maxx = d.cx1 - 1;
    if (maxy > d.cy1 - 1)
        maxy = d.cy1 - 1;
    for (int32_t y = miny; y <= maxy; y++) {
        for (int32_t x = minx; x <= maxx; x++) {
            /* sample at the pixel center, in half pixels */
            int32_t px = 2 * x + 1;
            int32_t py = 2 * y + 1;
            int64_t w0 = nv4_edge(2 * x1, 2 * y1, 2 * x2, 2 * y2, px, py);
            int64_t w1 = nv4_edge(2 * x2, 2 * y2, 2 * x0, 2 * y0, px, py);
            int64_t w2 = nv4_edge(2 * x0, 2 * y0, 2 * x1, 2 * y1, px, py);

            if ((w0 >= 0) && (w1 >= 0) && (w2 >= 0))
                nv4_put(&d, x, y, sp, s);
        }
    }
}

/* A plain copy between linear surfaces of one format: whole rows,
   with the per-pixel path's results (clipped to the destination's
   clip, the source read as is). Returns 0 when it does not apply. */
static int
nv4_blit_rows(nv4_t *nv4, nv4_gr_chan_t *ch, nv4_draw_t *d, const nv4_surf_t *src, uint32_t w, uint32_t h)
{
    uint8_t *v   = nv4->svga.vram;
    int      bpp = d->dst.bpp;
    int32_t  dx0 = ch->blit_dx, dy0 = ch->blit_dy;
    int32_t  dx1 = dx0 + (int32_t) w, dy1 = dy0 + (int32_t) h;
    int32_t  sx, sy, n, rows, step;
    uint32_t sa, da, len, s_end, d_end;

    if (!d->plain || d->defer || src->swz || (src->fmt != d->dst.fmt) || (src->bpp != bpp))
        return 0;
    if (dx0 < d->cx0)
        dx0 = d->cx0;
    if (dy0 < d->cy0)
        dy0 = d->cy0;
    if (dx1 > d->cx1)
        dx1 = d->cx1;
    if (dy1 > d->cy1)
        dy1 = d->cy1;
    if ((dx0 >= dx1) || (dy0 >= dy1))
        return 1;
    sx   = ch->blit_sx + (dx0 - ch->blit_dx);
    sy   = ch->blit_sy + (dy0 - ch->blit_dy);
    n    = dx1 - dx0;
    rows = dy1 - dy0;
    if ((sx < 0) || (sy < 0) || (dx0 < 0) || (dy0 < 0))
        return 0;
    len = (uint32_t) n * bpp;
    /* every byte inside both surfaces' limits and the VRAM */
    s_end = src->off + (uint32_t) (sy + rows - 1) * src->pitch + (uint32_t) sx * bpp + len - 1;
    d_end = d->dst.off + (uint32_t) (dy0 + rows - 1) * d->dst.pitch + (uint32_t) dx0 * bpp + len - 1;
    if ((s_end > src->limit) || (d_end > d->dst.limit) || (src->base + s_end > nv4->vram_mask)
        || (d->dst.base + d_end > nv4->vram_mask))
        return 0;
    sa = src->base + src->off + (uint32_t) sy * src->pitch + (uint32_t) sx * bpp;
    da = d->dst.base + d->dst.off + (uint32_t) dy0 * d->dst.pitch + (uint32_t) dx0 * bpp;
    if ((src->pitch != d->dst.pitch) && (sa <= src->base + s_end) && (da <= d->dst.base + d_end)
        && (da <= src->base + s_end) && (sa <= d->dst.base + d_end))
        return 0; /* overlapping with different pitches */
    if (d->touch) {
        nv4_3d_touch(nv4, d->lo, d->hi, GR(NV4_PGRAPH_CTX_SWITCH1) & 0xff);
        d->touch = 0;
    }
    /* the source is read before it is overwritten, as the per-pixel
       path reads all of it first */
    step = (da > sa) ? -1 : 1;
    for (int32_t i = 0; i < rows; i++) {
        int32_t  r  = (step > 0) ? i : (rows - 1 - i);
        uint32_t s0 = sa + (uint32_t) r * src->pitch;
        uint32_t d0 = da + (uint32_t) r * d->dst.pitch;

        memmove(&v[d0], &v[s0], len);
        for (uint32_t pg = d0 >> 12; pg <= ((d0 + len - 1) >> 12); pg++)
            nv4->svga.changedvram[pg] = nv4->svga.monitor->mon_changeframecount;
    }
    nv4->eng_bytes += (uint64_t) n * rows * bpp * 2;
    return 1;
}

/* Copies a rectangle from the source surface to the destination. */
static void
nv4_blit(nv4_t *nv4, nv4_gr_chan_t *ch, uint32_t size)
{
    nv4_draw_t d;
    nv4_surf_t src;
    uint32_t   w = size & 0xffff;
    uint32_t   h = size >> 16;
    uint32_t  *buf;

    if (!w || !h || !nv4_draw_begin(nv4, &d, 0, 1) || !nv4_surf_get(nv4, NV4_SURF_SRC, &src))
        return;
    if (nv4_blit_rows(nv4, ch, &d, &src, w, h))
        return;
    /* read it all first: the rectangles may overlap */
    buf = malloc(w * h * sizeof(uint32_t));
    if (!buf)
        return;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
            buf[y * w + x] = nv4_surf_read(nv4, &src, ch->blit_sx + x, ch->blit_sy + y);
    nv4->eng_bytes += (uint64_t) w * h * src.bpp;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t raw = buf[y * w + x];
            nv4_px_t sp  = { nv4_unpack(raw, src.fmt), raw };
            uint32_t s   = (src.fmt == d.dst.fmt) ? raw : nv4_pack(sp, d.dst.fmt);

            nv4_put(&d, ch->blit_dx + x, ch->blit_dy + y, sp, s);
        }
    }
    free(buf);
}

/* ------------------------------------------------------------------ */
/* Image transfers.                                                    */
/* ------------------------------------------------------------------ */
#define IMG_NONE  0
#define IMG_IFC   1 /* color pixels, size in/out */
#define IMG_SIFC  2 /* color pixels, scaled */
#define IMG_MONO1 3 /* GDI: 1 bits in color 1, 0 bits transparent */
#define IMG_MONO2 4 /* GDI: both colors */
#define IMG_IIFC  5 /* palette indexes */

static void
nv4_img_start(nv4_gr_chan_t *ch, int kind, uint32_t point)
{
    ch->img_kind = kind;
    ch->img_x    = (int16_t) point;
    ch->img_y    = (int16_t) (point >> 16);
    ch->pos      = 0;
}

static int
nv4_img_done(const nv4_gr_chan_t *ch)
{
    return !ch->in_w || !ch->in_h || (ch->pos >= ch->in_w * ch->in_h);
}

/* Places the next input pixel. */
static void
nv4_img_pixel(nv4_draw_t *d, nv4_gr_chan_t *ch, nv4_px_t sp, int draw)
{
    uint32_t i = ch->pos++;
    uint32_t x = i % ch->in_w;
    uint32_t y = i / ch->in_w;

    if (!draw || (y >= ch->in_h))
        return;
    if (ch->img_kind == IMG_SIFC) {
        /* output span of input pixel x,y in 12.20 */
        int64_t ox0 = ((int64_t) ch->img_fx << 16) + (int64_t) x * ch->img_dx;
        int64_t oy0 = ((int64_t) ch->img_fy << 16) + (int64_t) y * ch->img_dy;
        int64_t ox1 = ox0 + ch->img_dx;
        int64_t oy1 = oy0 + ch->img_dy;
        int32_t xa, xb, ya, yb;
        uint32_t s  = nv4_pack(sp, d->dst.fmt);

        if (ox1 < ox0) {
            int64_t t = ox0;
            ox0       = ox1;
            ox1       = t;
        }
        if (oy1 < oy0) {
            int64_t t = oy0;
            oy0       = oy1;
            oy1       = t;
        }
        xa = (int32_t) ((ox0 + 0xfffff) >> 20);
        xb = (int32_t) ((ox1 + 0xfffff) >> 20);
        ya = (int32_t) ((oy0 + 0xfffff) >> 20);
        yb = (int32_t) ((oy1 + 0xfffff) >> 20);
        for (int32_t py = ya; py < yb; py++)
            for (int32_t px = xa; px < xb; px++)
                nv4_put(d, px, py, sp, s);
        return;
    }
    if ((x < ch->out_w) && (y < ch->out_h))
        nv4_put_color(d, ch->img_x + x, ch->img_y + y, sp);
}

/* A data word of color pixels in the object's format. */
static void
nv4_img_color_data(nv4_t *nv4, nv4_gr_chan_t *ch, uint32_t data, int oclip)
{
    nv4_draw_t d;
    int        cfmt = nv4_src_fmt(nv4);
    int        dst  = (GR(NV4_PGRAPH_CTX_SWITCH1) & (1u << 14)) ? NV4_SURF_SWZ : NV4_SURF_DST;
    int        ok;

    if (nv4_img_done(ch))
        return;
    /* Into a Y8 surface (index mode) a 32-bit colour format carries four
       8-bit indices a word: NVIDIA's drivers set X8R8G8B8 at 8 bpp and
       pack indices (45.23's desktop icons). */
    if ((nv4_surf_fmt(nv4, dst) == 1) && ((cfmt == CF_A8R8G8B8) || (cfmt == CF_X8R8G8B8)))
        cfmt = CF_Y8;
    ok = nv4_draw_begin(nv4, &d, oclip, 1);
    switch (cfmt) {
        case CF_Y8:
        case CF_X24Y8:
            for (int i = 0; i < 4 && !nv4_img_done(ch); i++)
                nv4_img_pixel(&d, ch, nv4_color((data >> (i * 8)) & 0xff, CF_Y8), ok);
            break;
        case CF_A1R5G5B5:
        case CF_X1R5G5B5:
        case CF_R5G6B5:
            for (int i = 0; i < 2 && !nv4_img_done(ch); i++)
                nv4_img_pixel(&d, ch, nv4_color((data >> (i * 16)) & 0xffff, cfmt), ok);
            break;
        default:
            nv4_img_pixel(&d, ch, nv4_color(data, cfmt), ok);
            break;
    }
}

/* A data word of 32 monochrome pixels. */
static void
nv4_img_mono_data(nv4_t *nv4, nv4_gr_chan_t *ch, uint32_t data, int oclip)
{
    nv4_draw_t d;
    int        ok;
    nv4_px_t   c0;
    nv4_px_t   c1;

    if (nv4_img_done(ch))
        return;
    ok   = nv4_draw_begin(nv4, &d, oclip, 1);
    data = nv4_expand_mono(nv4, data);
    c0   = nv4_obj_color(nv4, ch->color0);
    c1   = nv4_obj_color(nv4, ch->color);
    for (int i = 0; i < 32 && !nv4_img_done(ch); i++) {
        if (data & (1u << i))
            nv4_img_pixel(&d, ch, c1, ok);
        else
            nv4_img_pixel(&d, ch, c0, ok && (ch->img_kind == IMG_MONO2));
    }
}

/* A data word of palette indexes: 8-bit (format 0, bytes in memory
   order) or 4-bit (format 1, SVGA order: in each byte the high nibble
   is the left pixel, as in a 4 bpp DIB). */
static void
nv4_img_index_data(nv4_t *nv4, nv4_gr_chan_t *ch, uint32_t data)
{
    nv4_draw_t d;
    int        ok;
    int        cfmt  = nv4_src_fmt(nv4);
    int        i4    = ch->iifc_fmt == 1;
    int        n     = i4 ? 8 : 4;
    int        esize = ((cfmt == CF_A8R8G8B8) || (cfmt == CF_X8R8G8B8)) ? 4 : 2;
    uint32_t   lut   = GR(NV4_PGRAPH_CTX_SWITCH3) & 0xffff;

    if (nv4_img_done(ch))
        return;
    ok = nv4_draw_begin(nv4, &d, 0, 1);
    for (int i = 0; i < n && !nv4_img_done(ch); i++) {
        uint32_t idx = i4 ? ((data >> ((i >> 1) * 8 + ((i & 1) ? 0 : 4))) & 0xf) : ((data >> (i * 8)) & 0xff);
        uint32_t c   = 0;

        if (lut)
            nv4_dma_read32(nv4, lut, ch->iifc_pal + idx * esize, &c);
        if (esize == 2)
            c &= 0xffff;
        nv4_img_pixel(&d, ch, nv4_color(c, cfmt), ok);
    }
}

/* GDI blocks F/G: a character from the font. */
static void
nv4_gdi_char(nv4_t *nv4, nv4_gr_chan_t *ch, int32_t x, int32_t y, uint32_t index)
{
    nv4_draw_t d;
    uint32_t   dma   = GR(NV4_PGRAPH_CTX_SWITCH3) & 0xffff;
    uint32_t   pitch = 1u << (ch->font >> 28);
    uint32_t   off   = (ch->font & 0x0fffffff) + index * pitch;
    uint32_t   size;
    uint32_t   w, h;
    uint32_t   word = 0;
    nv4_px_t   c1;

    if (!dma || !nv4_draw_begin(nv4, &d, 1, 1))
        return;
    if (nv4_dma_read32(nv4, dma, off, &size) != NV4_DMA_OK)
        return;
    w  = size & 0xffff;
    h  = size >> 16;
    c1 = nv4_obj_color(nv4, ch->color);
    for (uint32_t i = 0; i < w * h; i++) {
        if (!(i & 31)) {
            if (nv4_dma_read32(nv4, dma, off + 4 + (i >> 5) * 4, &word) != NV4_DMA_OK)
                return;
            word = nv4_expand_mono(nv4, word);
        }
        if (word & (1u << (i & 31)))
            nv4_put_color(&d, x + (int32_t) (i % w), y + (int32_t) (i / w), c1);
    }
}

/* ------------------------------------------------------------------ */
/* Scaled image from memory.                                           */
/* ------------------------------------------------------------------ */
static uint32_t
nv4_yuv_to_argb(int y, int u, int v)
{
    int c = y - 16;
    int d = u - 128;
    int e = v - 128;
    int r = (298 * c + 409 * e + 128) >> 8;
    int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int b = (298 * c + 516 * d + 128) >> 8;

    r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
    g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
    b = (b < 0) ? 0 : ((b > 255) ? 255 : b);
    return 0xff000000 | (r << 16) | (g << 8) | b;
}

static nv4_px_t
nv4_sifm_fetch(nv4_t *nv4, nv4_gr_chan_t *ch, int cfmt, uint32_t dma, int32_t u, int32_t v)
{
    uint32_t w     = ch->sifm_in_size & 0xffff;
    uint32_t h     = ch->sifm_in_size >> 16;
    uint32_t pitch = ch->sifm_in_fmt & 0xffff;
    uint32_t off;
    uint32_t val = 0;
    nv4_px_t p;

    if (u < 0)
        u = 0;
    if (v < 0)
        v = 0;
    if (w && ((uint32_t) u >= w))
        u = w - 1;
    if (h && ((uint32_t) v >= h))
        v = h - 1;
    off = ch->sifm_in_off + (uint32_t) v * pitch;
    switch (cfmt) {
        case CF_Y8:
            nv4_dma_read32(nv4, dma, (off + u) & ~3u, &val);
            return nv4_color((val >> (((off + u) & 3) * 8)) & 0xff, cfmt);
        case CF_A1R5G5B5:
        case CF_X1R5G5B5:
        case CF_R5G6B5:
            nv4_dma_read32(nv4, dma, (off + u * 2) & ~3u, &val);
            return nv4_color((val >> (((off + u * 2) & 2) * 8)) & 0xffff, cfmt);
        case CF_YUYV:
        case CF_UYVY:
            {
                int y0, y1, cu, cv;

                nv4_dma_read32(nv4, dma, off + (u & ~1) * 2, &val);
                if (cfmt == CF_YUYV) {
                    y0 = val & 0xff;
                    cu = (val >> 8) & 0xff;
                    y1 = (val >> 16) & 0xff;
                    cv = val >> 24;
                } else {
                    cu = val & 0xff;
                    y0 = (val >> 8) & 0xff;
                    cv = (val >> 16) & 0xff;
                    y1 = val >> 24;
                }
                p.argb = nv4_yuv_to_argb((u & 1) ? y1 : y0, cu, cv);
                p.raw  = p.argb;
                return p;
            }
        default:
            nv4_dma_read32(nv4, dma, off + u * 4, &val);
            return nv4_color(val, cfmt);
    }
}

/* a + (b - a) * f / 256 in each byte */
static uint32_t
nv4_lerp8888(uint32_t a, uint32_t b, uint32_t f)
{
    uint32_t r = 0;

    for (int s = 0; s < 32; s += 8) {
        uint32_t ca = (a >> s) & 0xff;
        uint32_t cb = (b >> s) & 0xff;

        r |= ((ca * (256 - f) + cb * f) >> 8) << s;
    }
    return r;
}

/* IMAGE_IN_FORMAT: pitch 15:0, origin 23:16 (1 the centre of each
   output pixel samples, 2 its corner), interpolator 31:24 (0 nearest
   texel, 1 bilinear). The Windows driver asks for centre + bilinear,
   so stretched DIBs come out filtered, as on the real card. */
static void
nv4_sifm_draw(nv4_t *nv4, nv4_gr_chan_t *ch, uint32_t point)
{
    nv4_draw_t d;
    uint32_t   dma  = GR(NV4_PGRAPH_CTX_SWITCH3) & 0xffff;
    int        cfmt = nv4_src_fmt(nv4);
    int        center = ((ch->sifm_in_fmt >> 16) & 0xff) == 1;
    int        foh    = (ch->sifm_in_fmt >> 24) == 1;
    /* the point is 12.4; positions are 12.20 */
    int64_t    u0 = (int64_t) (point & 0xffff) << 16;
    int64_t    v0 = (int64_t) (point >> 16) << 16;

    if (!dma || !nv4_draw_begin(nv4, &d, 1, 1))
        return;
    if (center) {
        u0 += ch->sifm_dudx / 2;
        v0 += ch->sifm_dvdy / 2;
        /* bilinear weights are measured from texel centres */
        if (foh) {
            u0 -= 1 << 19;
            v0 -= 1 << 19;
        }
    }
    for (uint32_t oy = 0; oy < ch->sifm_h; oy++) {
        int64_t v  = v0 + (int64_t) oy * ch->sifm_dvdy;
        int32_t iv = (int32_t) (v >> 20);
        uint32_t fv = (uint32_t) (v >> 12) & 0xff;

        if ((ch->sifm_y + (int32_t) oy < d.cy0) || (ch->sifm_y + (int32_t) oy >= d.cy1))
            continue;
        for (uint32_t ox = 0; ox < ch->sifm_w; ox++) {
            int64_t  u  = u0 + (int64_t) ox * ch->sifm_dudx;
            int32_t  iu = (int32_t) (u >> 20);
            uint32_t fu = (uint32_t) (u >> 12) & 0xff;
            nv4_px_t p;

            if ((ch->sifm_x + (int32_t) ox < d.cx0) || (ch->sifm_x + (int32_t) ox >= d.cx1))
                continue;
            p = nv4_sifm_fetch(nv4, ch, cfmt, dma, iu, iv);
            if (foh && (fu || fv)) {
                uint32_t t0 = nv4_lerp8888(p.argb, nv4_sifm_fetch(nv4, ch, cfmt, dma, iu + 1, iv).argb, fu);
                uint32_t t1 = nv4_lerp8888(nv4_sifm_fetch(nv4, ch, cfmt, dma, iu, iv + 1).argb,
                                           nv4_sifm_fetch(nv4, ch, cfmt, dma, iu + 1, iv + 1).argb, fu);

                p.argb = nv4_lerp8888(t0, t1, fv);
                p.raw  = ((cfmt == CF_Y8) || (cfmt == CF_X24Y8)) ? (p.argb & 0xff) : p.argb;
            }
            nv4_put_color(&d, ch->sifm_x + ox, ch->sifm_y + oy, p);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Memory to memory.                                                   */
/* ------------------------------------------------------------------ */
static void
nv4_m2mf(nv4_t *nv4, nv4_gr_chan_t *ch, uint32_t data)
{
    uint32_t in     = GR(NV4_PGRAPH_CTX_SWITCH3) & 0xffff;
    uint32_t out    = GR(NV4_PGRAPH_CTX_SWITCH3) >> 16;
    int      inc_in = ch->m2mf_fmt & 7;
    int      inc_out = (ch->m2mf_fmt >> 8) & 7;
    uint8_t *line;

    if (!inc_in)
        inc_in = 1;
    if (!inc_out)
        inc_out = 1;
    line = malloc(ch->m2mf_len ? ch->m2mf_len : 1);
    if (in && out && line) {
        for (uint32_t l = 0; l < ch->m2mf_count; l++) {
            uint32_t src = ch->m2mf_in + l * ch->m2mf_pitch_in;
            uint32_t dst = ch->m2mf_out + l * ch->m2mf_pitch_out;

            for (uint32_t i = 0; i < ch->m2mf_len; i++) {
                uint8_t b = 0;

                nv4_dma_read(nv4, in, src + i * inc_in, &b, 1);
                line[i] = b;
            }
            if (inc_out == 1)
                nv4_dma_write(nv4, out, dst, line, ch->m2mf_len);
            else {
                for (uint32_t i = 0; i < ch->m2mf_len; i++)
                    nv4_dma_write(nv4, out, dst + i * inc_out, &line[i], 1);
            }
        }
    }
    free(line);

    /* the buffer notifier */
    nv4_pgraph_notify_write(nv4, 1, 0);
    if (data & 1) {
        GR(NV4_PGRAPH_INTR) |= NV4_PGRAPH_INTR_BUFFER_NOTIFY;
        GR(NV4_PGRAPH_FIFO) &= ~1u;
        nv4_pmc_update_irq(nv4);
    }
}

/* ------------------------------------------------------------------ */
/* Classes.                                                            */
/* ------------------------------------------------------------------ */
void
nv4_pgraph_2d_reset_chan(nv4_gr_chan_t *ch)
{
    ch->img_kind = IMG_NONE;
    ch->pos      = 0;
    ch->nvtx     = 0;
}

static int
nv4_class_beta(nv4_t *nv4, uint32_t mthd, uint32_t data)
{
    if (mthd != 0x300)
        return (mthd == 0x200) ? NV4_GR_MISSING : NV4_GR_ILLEGAL;
    GR(NV4_PGRAPH_BETA_AND) = (data & 0x80000000) ? 0 : (data & 0x7f800000);
    return NV4_GR_OK;
}

static int
nv4_class_beta4(nv4_t *nv4, uint32_t mthd, uint32_t data)
{
    if (mthd != 0x300)
        return (mthd == 0x200) ? NV4_GR_MISSING : NV4_GR_ILLEGAL;
    GR(NV4_PGRAPH_BETA_PREMULT) = data;
    return NV4_GR_OK;
}

static int
nv4_class_rop(nv4_t *nv4, uint32_t mthd, uint32_t data)
{
    if (mthd != 0x300)
        return (mthd == 0x200) ? NV4_GR_MISSING : NV4_GR_ILLEGAL;
    GR(NV4_PGRAPH_ROP3) = data & 0xff;
    return NV4_GR_OK;
}

static int
nv4_class_chroma(nv4_t *nv4, int cls, uint32_t mthd, uint32_t data)
{
    int fmt;

    switch (mthd) {
        case 0x300:
            fmt = nv4_ctx_fmt(data, cls);
            nv4_set_color_fmt(nv4, fmt);
            GR(NV4_PGRAPH_STORED_FMT) = (GR(NV4_PGRAPH_STORED_FMT) & ~0x3f000000u) | ((uint32_t) fmt << 24);
            return NV4_GR_OK;
        case 0x304:
            GR(NV4_PGRAPH_CHROMA) = data;
            return NV4_GR_OK;
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

static int
nv4_class_pattern(nv4_t *nv4, int cls, uint32_t mthd, uint32_t data)
{
    int fmt;

    if ((cls == 0x44) && (mthd >= 0x400) && (mthd < 0x800)) {
        uint32_t *ram = &GR(NV4_PGRAPH_PATT_COLORRAM);

        if (mthd < 0x440) { /* Y8, 4 per word */
            for (int i = 0; i < 4; i++)
                ram[((mthd - 0x400) >> 2) * 4 + i] = ((data >> (i * 8)) & 0xff) * 0x010101;
        } else if ((mthd >= 0x500) && (mthd < 0x580)) { /* R5G6B5, 2 per word */
            for (int i = 0; i < 2; i++)
                ram[((mthd - 0x500) >> 2) * 2 + i] = x565(data >> (i * 16));
        } else if ((mthd >= 0x600) && (mthd < 0x680)) { /* X1R5G5B5 */
            for (int i = 0; i < 2; i++)
                ram[((mthd - 0x600) >> 2) * 2 + i] = x555(data >> (i * 16));
        } else if (mthd >= 0x700) /* X8R8G8B8 */
            ram[(mthd - 0x700) >> 2] = data & 0xffffff;
        else
            return NV4_GR_ILLEGAL;
        return NV4_GR_OK;
    }

    switch (mthd) {
        case 0x300:
            fmt = nv4_ctx_fmt(data, cls);
            nv4_set_color_fmt(nv4, fmt);
            GR(NV4_PGRAPH_STORED_FMT) = (GR(NV4_PGRAPH_STORED_FMT) & ~0x003f3f00u) | ((uint32_t) fmt << 8)
                                        | ((uint32_t) fmt << 16);
            return NV4_GR_OK;
        case 0x304:
            nv4_pgraph_set_grobj(nv4, 1, 0, 2, data & 3);
            return NV4_GR_OK;
        case 0x308:
            GR(NV4_PGRAPH_PATTERN_SHAPE) = (GR(NV4_PGRAPH_PATTERN_SHAPE) & ~3u) | (data & 3);
            return NV4_GR_OK;
        case 0x30c:
            if (cls != 0x44)
                return NV4_GR_ILLEGAL;
            GR(NV4_PGRAPH_PATTERN_SHAPE) = (GR(NV4_PGRAPH_PATTERN_SHAPE) & ~0x10u) | (((data >> 1) & 1) << 4);
            return NV4_GR_OK;
        case 0x310:
        case 0x314:
            GR(NV4_PGRAPH_PATT_COLOR0 + (mthd - 0x310)) = data;
            return NV4_GR_OK;
        case 0x318:
        case 0x31c:
            GR(NV4_PGRAPH_PATTERN + (mthd - 0x318)) = nv4_expand_mono(nv4, data);
            return NV4_GR_OK;
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

static int
nv4_class_clip(nv4_t *nv4, uint32_t mthd, uint32_t data)
{
    switch (mthd) {
        case 0x300:
            nv4_set_clip(nv4, 0, 0, data);
            return NV4_GR_OK;
        case 0x304:
            nv4_set_clip(nv4, 0, 2, data);
            return NV4_GR_OK;
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

static int
nv4_class_surf2d(nv4_t *nv4, uint32_t mthd, uint32_t data)
{
    static const int fmts[12] = { 0, 0x1, 0x2, 0x3, 0x5, 0x6, 0x7, 0xb, 0x9, 0xa, 0xc, 0xd };
    uint32_t         pmask    = 0x1ff0;

    switch (mthd) {
        case 0x184:
            nv4_dma_surf(nv4, NV4_SURF_SRC, data);
            return NV4_GR_OK;
        case 0x188:
            nv4_dma_surf(nv4, NV4_SURF_DST, data);
            return NV4_GR_OK;
        case 0x300:
            if ((data & 0xf) < 12) {
                nv4_set_surf_fmt(nv4, NV4_SURF_DST, fmts[data & 0xf]);
                nv4_set_surf_fmt(nv4, NV4_SURF_SRC, fmts[data & 0xf]);
            }
            return NV4_GR_OK;
        case 0x304:
            GR(NV4_PGRAPH_BPITCH + NV4_SURF_SRC * 4) = data & pmask;
            GR(NV4_PGRAPH_BPITCH + NV4_SURF_DST * 4) = (data >> 16) & pmask;
            return NV4_GR_OK;
        case 0x308:
            GR(NV4_PGRAPH_BOFFSET + NV4_SURF_SRC * 4) = data & nv4_offset_mask(nv4);
            return NV4_GR_OK;
        case 0x30c:
            GR(NV4_PGRAPH_BOFFSET + NV4_SURF_DST * 4) = data & nv4_offset_mask(nv4);
            return NV4_GR_OK;
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

/* The single surfaces of the RIVA 128 interface: 0x58 destination,
   0x59 source, 0x5a color, 0x5b depth */
static int
nv4_class_surf(nv4_t *nv4, int cls, uint32_t mthd, uint32_t data)
{
    int which = cls & 3;

    switch (mthd) {
        case 0x184:
            nv4_dma_surf(nv4, which, data);
            return NV4_GR_OK;
        case 0x300:
            if (data == 1)
                nv4_set_surf_fmt(nv4, which, 0x7);
            else if (data == 0x01010000)
                nv4_set_surf_fmt(nv4, which, 0x1);
            else if (data == 0x01000000)
                nv4_set_surf_fmt(nv4, which, 0x2);
            else if (data == 0x01010001)
                nv4_set_surf_fmt(nv4, which, 0x6);
            return NV4_GR_OK;
        case 0x308:
            GR(NV4_PGRAPH_BPITCH + which * 4) = data & 0x1ff0;
            return NV4_GR_OK;
        case 0x30c:
            GR(NV4_PGRAPH_BOFFSET + which * 4) = data & nv4_offset_mask(nv4);
            return NV4_GR_OK;
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

/* Swizzled surface: textures */
static int
nv4_class_swzsurf(nv4_t *nv4, uint32_t mthd, uint32_t data)
{
    static const int fmts[12] = { 0, 0x1, 0x2, 0x3, 0x5, 0x6, 0x7, 0xb, 0x9, 0xa, 0xc, 0xd };

    switch (mthd) {
        case 0x184:
            nv4_dma_surf(nv4, NV4_SURF_SWZ, data);
            return NV4_GR_OK;
        case 0x300:
            if ((data & 0xf) < 12)
                nv4_set_surf_fmt(nv4, NV4_SURF_SWZ, fmts[data & 0xf]);
            GR(NV4_PGRAPH_BSWIZZLE5) = data & 0x0f0f0000;
            return NV4_GR_OK;
        case 0x304:
            GR(NV4_PGRAPH_BOFFSET + NV4_SURF_SWZ * 4) = data & nv4_offset_mask(nv4);
            return NV4_GR_OK;
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

/* 3D surfaces: color and depth */
static int
nv4_class_surf3d(nv4_t *nv4, uint32_t mthd, uint32_t data)
{
    switch (mthd) {
        case 0x184:
            nv4_dma_surf(nv4, NV4_SURF_COLOR, data);
            return NV4_GR_OK;
        case 0x188:
            nv4_dma_surf(nv4, NV4_SURF_ZETA, data);
            return NV4_GR_OK;
        case 0x2f8:
        case 0x2fc:
            if (nv4->chip != NV4_CHIP_TNT2)
                return NV4_GR_ILLEGAL;
            nv4_uclip_write(nv4, 0, (mthd - 0x2f8) >> 2, data & 0xffff);
            nv4_uclip_write(nv4, 0, (mthd - 0x2f8) >> 2, (data & 0xffff) + (int16_t) (data >> 16));
            return NV4_GR_OK;
        case 0x300:
            {
                /* color 7:0, type 15:8 (1 pitch, 2 swizzled), log2 width
                   and height 23:16, 31:24 */
                static const int fmts[9] = { 0, 0x2, 0x3, 0x5, 0x7, 0xb, 0x9, 0xa, 0xc };

                if ((data & 0xff) < 9)
                    nv4_set_surf_fmt(nv4, NV4_SURF_COLOR, fmts[data & 0xff]);
                GR(NV4_PGRAPH_SURFACE)   = (data >> 8) & 3;
                GR(NV4_PGRAPH_BSWIZZLE2) = data & 0x0f0f0000;
            }
            return NV4_GR_OK;
        case 0x304:
            nv4_uclip_write(nv4, 0, 0, 0);
            nv4_uclip_write(nv4, 0, 0, data & 0xffff);
            nv4_uclip_write(nv4, 0, 1, 0);
            nv4_uclip_write(nv4, 0, 1, data >> 16);
            return NV4_GR_OK;
        case 0x308:
            GR(NV4_PGRAPH_BPITCH + NV4_SURF_COLOR * 4) = data & 0x1ff0;
            GR(NV4_PGRAPH_BPITCH + NV4_SURF_ZETA * 4)  = (data >> 16) & 0x1ff0;
            return NV4_GR_OK;
        case 0x30c:
            GR(NV4_PGRAPH_BOFFSET + NV4_SURF_COLOR * 4) = data & nv4_offset_mask(nv4);
            return NV4_GR_OK;
        case 0x310:
            GR(NV4_PGRAPH_BOFFSET + NV4_SURF_ZETA * 4) = data & nv4_offset_mask(nv4);
            return NV4_GR_OK;
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

/* Rectangles: 0x5e, and 0x1e of the RIVA 128 interface */
static int
nv4_class_rect(nv4_t *nv4, nv4_gr_chan_t *ch, int cls, uint32_t mthd, uint32_t data)
{
    int is_new = cls != 0x1e;

    if ((mthd >= 0x400) && (mthd < 0x480)) {
        if (!(mthd & 4)) {
            ch->vx[0] = (int16_t) data;
            ch->vy[0] = (int16_t) (data >> 16);
        } else
            nv4_rect(nv4, ch->vx[0], ch->vy[0], data & 0xffff, data >> 16, ch->color, 0, 1);
        return NV4_GR_OK;
    }
    switch (mthd) {
        case 0x184:
        case 0x188:
        case 0x18c:
        case 0x190:
            return nv4_ctx_method(nv4, (mthd == 0x184) ? 13 : ((mthd - 0x188) / 4 + 27), data);
        case 0x194:
            return nv4_ctx_method(nv4, is_new ? 30 : 25, data);
        case 0x198:
            return is_new ? nv4_ctx_method(nv4, 25, data) : NV4_GR_ILLEGAL;
        case 0x2fc:
            return nv4_operation_method(nv4, data);
        case 0x300:
            nv4_set_color_fmt(nv4, nv4_solid_fmt(data, is_new));
            return NV4_GR_OK;
        case 0x304:
            ch->color                     = data;
            GR(NV4_PGRAPH_SOURCE_COLOR) = data;
            return NV4_GR_OK;
        case 0x10c:
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

static void
nv4_polyline(nv4_t *nv4, nv4_gr_chan_t *ch, int32_t x, int32_t y)
{
    if (ch->nvtx)
        nv4_line(nv4, ch->vx[0], ch->vy[0], x, y, ch->color);
    ch->vx[0] = x;
    ch->vy[0] = y;
    ch->nvtx  = 1;
}

/* Lines: 0x5c, 0x1c */
static int
nv4_class_lin(nv4_t *nv4, nv4_gr_chan_t *ch, int cls, uint32_t mthd, uint32_t data)
{
    int is_new = cls != 0x1c;

    if ((mthd >= 0x400) && (mthd < 0x480)) { /* lines: two points */
        if (!(mthd & 4)) {
            ch->vx[0] = (int16_t) data;
            ch->vy[0] = (int16_t) (data >> 16);
            ch->nvtx  = 1;
        } else
            nv4_polyline(nv4, ch, (int16_t) data, (int16_t) (data >> 16));
        return NV4_GR_OK;
    }
    if ((mthd >= 0x480) && (mthd < 0x500)) { /* 32-bit lines: x0 y0 x1 y1 */
        int k = (mthd >> 2) & 3;

        if (k < 2) {
            (k ? ch->vy : ch->vx)[0] = (int32_t) data;
            ch->nvtx = 1;
        }
        else if (k == 2)
            ch->vx[1] = (int32_t) data;
        else
            nv4_polyline(nv4, ch, ch->vx[1], (int32_t) data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x500) && (mthd < 0x580)) { /* polyline */
        nv4_polyline(nv4, ch, (int16_t) data, (int16_t) (data >> 16));
        return NV4_GR_OK;
    }
    if ((mthd >= 0x580) && (mthd < 0x600)) { /* 32-bit polyline: x, y */
        if (!(mthd & 4))
            ch->vx[2] = (int32_t) data;
        else
            nv4_polyline(nv4, ch, ch->vx[2], (int32_t) data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x600) && (mthd < 0x680)) { /* colored polyline: color, xy */
        if (!(mthd & 4))
            ch->color = data;
        else
            nv4_polyline(nv4, ch, (int16_t) data, (int16_t) (data >> 16));
        return NV4_GR_OK;
    }
    /* each line leaves its end as the polyline's current point */
    switch (mthd) {
        case 0x184:
        case 0x188:
        case 0x18c:
        case 0x190:
            return nv4_ctx_method(nv4, (mthd == 0x184) ? 13 : ((mthd - 0x188) / 4 + 27), data);
        case 0x194:
            return nv4_ctx_method(nv4, is_new ? 30 : 25, data);
        case 0x198:
            return is_new ? nv4_ctx_method(nv4, 25, data) : NV4_GR_ILLEGAL;
        case 0x2fc:
            return nv4_operation_method(nv4, data);
        case 0x300:
            nv4_set_color_fmt(nv4, nv4_solid_fmt(data, is_new));
            return NV4_GR_OK;
        case 0x304:
            ch->color = data;
            return NV4_GR_OK;
        case 0x10c:
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

/* A whole triangle: a mesh vertex after it closes the next triangle
   with its last two points (the display driver fans a convex polygon
   out as a triangle, then mesh vertices). */
static void
nv4_tri_whole(nv4_t *nv4, nv4_gr_chan_t *ch)
{
    nv4_triangle(nv4, ch->vx, ch->vy, ch->color);
    ch->vx[0] = ch->vx[1];
    ch->vy[0] = ch->vy[1];
    ch->vx[1] = ch->vx[2];
    ch->vy[1] = ch->vy[2];
    ch->nvtx  = 2;
}

static void
nv4_tri_vertex(nv4_t *nv4, nv4_gr_chan_t *ch, int32_t x, int32_t y)
{
    /* a mesh: each vertex after the second closes a triangle with the
       two before it */
    if (ch->nvtx < 2) {
        ch->vx[ch->nvtx] = x;
        ch->vy[ch->nvtx] = y;
        ch->nvtx++;
        return;
    }
    ch->vx[2] = x;
    ch->vy[2] = y;
    nv4_triangle(nv4, ch->vx, ch->vy, ch->color);
    ch->vx[0] = ch->vx[1];
    ch->vy[0] = ch->vy[1];
    ch->vx[1] = x;
    ch->vy[1] = y;
}

/* Triangles: 0x5d, 0x1d */
static int
nv4_class_tri(nv4_t *nv4, nv4_gr_chan_t *ch, int cls, uint32_t mthd, uint32_t data)
{
    int is_new = cls != 0x1d;

    if ((mthd >= 0x310) && (mthd < 0x31c)) {
        int k     = (mthd - 0x310) >> 2;
        ch->vx[k] = (int16_t) data;
        ch->vy[k] = (int16_t) (data >> 16);
        if (k == 2)
            nv4_tri_whole(nv4, ch);
        else
            ch->nvtx = 0;
        return NV4_GR_OK;
    }
    if ((mthd >= 0x320) && (mthd < 0x338)) {
        int k = (mthd - 0x320) >> 3;

        if (!(mthd & 4))
            ch->vx[k] = (int32_t) data;
        else {
            ch->vy[k] = (int32_t) data;
            if (k == 2) {
                nv4_tri_whole(nv4, ch);
                return NV4_GR_OK;
            }
        }
        ch->nvtx = 0;
        return NV4_GR_OK;
    }
    if ((mthd >= 0x400) && (mthd < 0x480)) {
        nv4_tri_vertex(nv4, ch, (int16_t) data, (int16_t) (data >> 16));
        return NV4_GR_OK;
    }
    if ((mthd >= 0x480) && (mthd < 0x500)) {
        if (!(mthd & 4))
            ch->img_x = (int32_t) data;
        else
            nv4_tri_vertex(nv4, ch, ch->img_x, (int32_t) data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x500) && (mthd < 0x580)) { /* colored triangles: color, 3 points */
        int k = (mthd >> 2) & 3;

        if (k == 0) {
            ch->color = data;
            ch->nvtx  = 0;
        } else {
            ch->vx[k - 1] = (int16_t) data;
            ch->vy[k - 1] = (int16_t) (data >> 16);
            if (k == 3)
                nv4_tri_whole(nv4, ch);
        }
        return NV4_GR_OK;
    }
    if ((mthd >= 0x580) && (mthd < 0x600)) { /* colored mesh: color, xy */
        if (!(mthd & 4))
            ch->color = data;
        else
            nv4_tri_vertex(nv4, ch, (int16_t) data, (int16_t) (data >> 16));
        return NV4_GR_OK;
    }
    ch->nvtx = 0;
    switch (mthd) {
        case 0x184:
        case 0x188:
        case 0x18c:
        case 0x190:
            return nv4_ctx_method(nv4, (mthd == 0x184) ? 13 : ((mthd - 0x188) / 4 + 27), data);
        case 0x194:
            return nv4_ctx_method(nv4, is_new ? 30 : 25, data);
        case 0x198:
            return is_new ? nv4_ctx_method(nv4, 25, data) : NV4_GR_ILLEGAL;
        case 0x2fc:
            return nv4_operation_method(nv4, data);
        case 0x300:
            nv4_set_color_fmt(nv4, nv4_solid_fmt(data, is_new));
            return NV4_GR_OK;
        case 0x304:
            ch->color = data;
            return NV4_GR_OK;
        case 0x10c:
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

/* Image blits: 0x5f, 0x1f */
static int
nv4_class_blit(nv4_t *nv4, nv4_gr_chan_t *ch, int cls, uint32_t mthd, uint32_t data)
{
    int is_new = cls != 0x1f;

    switch (mthd) {
        case 0x184:
            return nv4_ctx_method(nv4, 12, data);
        case 0x188:
            return nv4_ctx_method(nv4, 13, data);
        case 0x18c:
        case 0x190:
        case 0x194:
            return nv4_ctx_method(nv4, (mthd - 0x18c) / 4 + 27, data);
        case 0x198:
            return nv4_ctx_method(nv4, is_new ? 30 : -1, data);
        case 0x19c:
            return nv4_ctx_method(nv4, 25, data);
        case 0x2fc:
            return nv4_operation_method(nv4, data);
        case 0x300:
            ch->blit_sx = (int16_t) data;
            ch->blit_sy = (int16_t) (data >> 16);
            return NV4_GR_OK;
        case 0x304:
            ch->blit_dx = (int16_t) data;
            ch->blit_dy = (int16_t) (data >> 16);
            return NV4_GR_OK;
        case 0x308:
            nv4_blit(nv4, ch, data);
            return NV4_GR_OK;
        case 0x10c:
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

/* Image from CPU: 0x61, 0x21 */
static int
nv4_class_ifc(nv4_t *nv4, nv4_gr_chan_t *ch, int cls, uint32_t mthd, uint32_t data)
{
    int is_new = cls != 0x21;

    if ((mthd >= 0x400) && (mthd < (is_new ? 0x2000u : 0x480u))) {
        if (ch->img_kind == IMG_IFC)
            nv4_img_color_data(nv4, ch, data, 0);
        return NV4_GR_OK;
    }
    switch (mthd) {
        case 0x184:
            return nv4_ctx_method(nv4, 12, data);
        case 0x188:
            return nv4_ctx_method(nv4, 13, data);
        case 0x18c:
        case 0x190:
        case 0x194:
            return nv4_ctx_method(nv4, (mthd - 0x18c) / 4 + 27, data);
        case 0x198:
            return nv4_ctx_method(nv4, is_new ? 30 : 25, data);
        case 0x19c:
            return is_new ? nv4_ctx_method(nv4, 25, data) : NV4_GR_ILLEGAL;
        case 0x2fc:
            return nv4_operation_method(nv4, data);
        case 0x300:
            nv4_set_color_fmt(nv4, nv4_ifc_fmt(data, is_new));
            return NV4_GR_OK;
        case 0x304:
            nv4_img_start(ch, IMG_IFC, data);
            return NV4_GR_OK;
        case 0x308:
            ch->out_w = data & 0xffff;
            ch->out_h = data >> 16;
            return NV4_GR_OK;
        case 0x30c:
            ch->in_w = data & 0xffff;
            ch->in_h = data >> 16;
            ch->pos  = 0;
            return NV4_GR_OK;
        case 0x10c:
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

/* Stretched image from CPU: 0x76, 0x36 */
static int
nv4_class_sifc(nv4_t *nv4, nv4_gr_chan_t *ch, int cls, uint32_t mthd, uint32_t data)
{
    int is_new = cls != 0x36;

    if ((mthd >= 0x400) && (mthd < 0x2000)) {
        if (ch->img_kind == IMG_SIFC)
            nv4_img_color_data(nv4, ch, data, 1);
        return NV4_GR_OK;
    }
    switch (mthd) {
        case 0x184:
            return nv4_ctx_method(nv4, 12, data);
        case 0x188:
        case 0x18c:
        case 0x190:
            return nv4_ctx_method(nv4, (mthd - 0x188) / 4 + 27, data);
        case 0x194:
            return nv4_ctx_method(nv4, is_new ? 30 : 25, data);
        case 0x198:
            return is_new ? nv4_ctx_method(nv4, 25, data) : NV4_GR_ILLEGAL;
        case 0x2fc:
            return nv4_operation_method(nv4, data);
        case 0x300:
            nv4_set_color_fmt(nv4, nv4_ifc_fmt(data, is_new));
            return NV4_GR_OK;
        case 0x304:
            ch->sifc_size = data;
            return NV4_GR_OK;
        case 0x308:
            ch->img_dx = (int32_t) data;
            return NV4_GR_OK;
        case 0x30c:
            ch->img_dy = (int32_t) data;
            return NV4_GR_OK;
        case 0x310:
            nv4_set_clip(nv4, 1, 0, data);
            return NV4_GR_OK;
        case 0x314:
            nv4_set_clip(nv4, 1, 2, data);
            return NV4_GR_OK;
        case 0x318:
            nv4_img_start(ch, IMG_SIFC, 0);
            ch->img_fx = (int16_t) data;
            ch->img_fy = (int16_t) (data >> 16);
            ch->in_w   = ch->sifc_size & 0xffff;
            ch->in_h   = ch->sifc_size >> 16;
            return NV4_GR_OK;
        case 0x10c:
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

/* Indexed image from CPU: 0x60 */
static int
nv4_class_iifc(nv4_t *nv4, nv4_gr_chan_t *ch, uint32_t mthd, uint32_t data)
{
    if ((mthd >= 0x400) && (mthd < 0x2000)) {
        if (ch->img_kind == IMG_IIFC)
            nv4_img_index_data(nv4, ch, data);
        return NV4_GR_OK;
    }
    switch (mthd) {
        case 0x184:
            nv4_pgraph_set_grobj(nv4, 2, 0, 16, data);
            return NV4_GR_OK;
        case 0x188:
            return nv4_ctx_method(nv4, 12, data);
        case 0x18c:
            return nv4_ctx_method(nv4, 13, data);
        case 0x190:
        case 0x194:
        case 0x198:
        case 0x19c:
            return nv4_ctx_method(nv4, (mthd - 0x190) / 4 + 27, data);
        case 0x1a0:
            return nv4_ctx_method(nv4, 25, data);
        case 0x3e4:
            return nv4_operation_method(nv4, data);
        case 0x3e8:
            nv4_set_color_fmt(nv4, nv4_ifc_fmt(data, 1));
            return NV4_GR_OK;
        case 0x3ec:
            ch->iifc_fmt = data;
            return NV4_GR_OK;
        case 0x3f0:
            ch->iifc_pal = data;
            return NV4_GR_OK;
        case 0x3f4:
            nv4_img_start(ch, IMG_IIFC, data);
            return NV4_GR_OK;
        case 0x3f8:
            ch->out_w = data & 0xffff;
            ch->out_h = data >> 16;
            return NV4_GR_OK;
        case 0x3fc:
            ch->in_w = data & 0xffff;
            ch->in_h = data >> 16;
            ch->pos  = 0;
            return NV4_GR_OK;
        case 0x10c:
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

/* GDI rectangles and text: 0x4a, and 0x4b of the RIVA 128 interface.
   Blocks: A unclipped rectangles, B clipped rectangles, C one-color
   bitmaps, (0x4b) D one-color bitmaps with an output size, E two-color
   bitmaps, (0x4a) F/G characters from a font. */
typedef struct {
    uint32_t a_color, a_rect, a_end;
    uint32_t b_clip, b_rect, b_end;
    uint32_t c_clip, c_data, c_end;
    uint32_t d_clip, d_data, d_end;
    uint32_t e_clip, e_data, e_end;
} nv4_gdi_layout_t;

static const nv4_gdi_layout_t nv4_gdi_nv4 = {
    0x3fc, 0x400, 0x500, 0x5f4, 0x600, 0x700, 0x7ec, 0x800, 0xa00,
    0, 0, 0, 0xbe4, 0xc00, 0xe00
};
static const nv4_gdi_layout_t nv4_gdi_nv3 = {
    0x3fc, 0x400, 0x600, 0x7f4, 0x800, 0xa00, 0xbec, 0xc00, 0xe00,
    0xfe8, 0x1000, 0x1200, 0x13e4, 0x1400, 0x1600
};

/* A data word of a C, D or E bitmap. The blocks' clip, colour, size and
   point methods all set the same engine state, so a bitmap may be set
   up through one block and its data sent through another (Windows
   2000's driver gives the size and point through E and the glyphs
   through C): the data method decides whether 0 bits are drawn. */
static void
nv4_gdi_mono_data(nv4_t *nv4, nv4_gr_chan_t *ch, int kind, uint32_t data)
{
    if ((ch->img_kind != IMG_MONO1) && (ch->img_kind != IMG_MONO2))
        return;
    ch->img_kind = kind;
    nv4_img_mono_data(nv4, ch, data, 1);
}

static int
nv4_class_gdi(nv4_t *nv4, nv4_gr_chan_t *ch, int cls, uint32_t mthd, uint32_t data)
{
    const nv4_gdi_layout_t *l = (cls == 0x4a) ? &nv4_gdi_nv4 : &nv4_gdi_nv3;

    /* A */
    if (mthd == l->a_color) {
        ch->color = data;
        return NV4_GR_OK;
    }
    if ((mthd >= l->a_rect) && (mthd < l->a_end)) {
        /* the TNT's block A packs x and width in the high half (x_y,
           width_height), unlike every other point and size */
        int hi_x = (cls == 0x4a);

        if (!(mthd & 4)) {
            ch->vx[0] = (int16_t) (hi_x ? (data >> 16) : data);
            ch->vy[0] = (int16_t) (hi_x ? data : (data >> 16));
        } else if (hi_x)
            nv4_rect(nv4, ch->vx[0], ch->vy[0], data >> 16, data & 0xffff, ch->color, 0, 0);
        else
            nv4_rect(nv4, ch->vx[0], ch->vy[0], data & 0xffff, data >> 16, ch->color, 0, 0);
        return NV4_GR_OK;
    }
    /* B */
    if ((mthd == l->b_clip) || (mthd == l->b_clip + 4)) {
        nv4_set_clip(nv4, 1, (mthd - l->b_clip) >> 2, data);
        return NV4_GR_OK;
    }
    if (mthd == l->b_clip + 8) {
        ch->color = data;
        return NV4_GR_OK;
    }
    if ((mthd >= l->b_rect) && (mthd < l->b_end)) {
        if (!(mthd & 4)) {
            ch->vx[0] = (int16_t) data;
            ch->vy[0] = (int16_t) (data >> 16);
        } else {
            int32_t  x1 = (int16_t) data;
            int32_t  y1 = (int16_t) (data >> 16);
            nv4_draw_t d;

            if ((x1 > ch->vx[0]) && (y1 > ch->vy[0]) && nv4_draw_begin_fill(nv4, &d, 1, 0, 1))
                nv4_fill(&d, ch->vx[0], ch->vy[0], x1, y1, nv4_obj_color(nv4, ch->color));
        }
        return NV4_GR_OK;
    }
    /* C: clip, color 1, size, point, data */
    if ((mthd == l->c_clip) || (mthd == l->c_clip + 4)) {
        nv4_set_clip(nv4, 1, (mthd - l->c_clip) >> 2, data);
        return NV4_GR_OK;
    }
    if (mthd == l->c_clip + 8) {
        ch->color = data;
        return NV4_GR_OK;
    }
    if (mthd == l->c_clip + 12) {
        ch->in_w = ch->out_w = data & 0xffff;
        ch->in_h = ch->out_h = data >> 16;
        return NV4_GR_OK;
    }
    if (mthd == l->c_clip + 16) {
        nv4_img_start(ch, IMG_MONO1, data);
        return NV4_GR_OK;
    }
    if ((mthd >= l->c_data) && (mthd < l->c_end)) {
        nv4_gdi_mono_data(nv4, ch, IMG_MONO1, data);
        return NV4_GR_OK;
    }
    /* D (0x4b): clip, color 1, size in, size out, point, data */
    if (l->d_clip) {
        if ((mthd == l->d_clip) || (mthd == l->d_clip + 4)) {
            nv4_set_clip(nv4, 1, (mthd - l->d_clip) >> 2, data);
            return NV4_GR_OK;
        }
        if (mthd == l->d_clip + 8) {
            ch->color = data;
            return NV4_GR_OK;
        }
        if (mthd == l->d_clip + 12) {
            ch->in_w = data & 0xffff;
            ch->in_h = data >> 16;
            return NV4_GR_OK;
        }
        if (mthd == l->d_clip + 16) {
            ch->out_w = data & 0xffff;
            ch->out_h = data >> 16;
            return NV4_GR_OK;
        }
        if (mthd == l->d_clip + 20) {
            nv4_img_start(ch, IMG_MONO1, data);
            return NV4_GR_OK;
        }
        if ((mthd >= l->d_data) && (mthd < l->d_end)) {
            nv4_gdi_mono_data(nv4, ch, IMG_MONO1, data);
            return NV4_GR_OK;
        }
    }
    /* E: clip, color 0, color 1, size in, size out, point, data */
    if ((mthd == l->e_clip) || (mthd == l->e_clip + 4)) {
        nv4_set_clip(nv4, 1, (mthd - l->e_clip) >> 2, data);
        return NV4_GR_OK;
    }
    if (mthd == l->e_clip + 8) {
        ch->color0 = data;
        return NV4_GR_OK;
    }
    if (mthd == l->e_clip + 12) {
        ch->color = data;
        return NV4_GR_OK;
    }
    if (mthd == l->e_clip + 16) {
        ch->in_w = data & 0xffff;
        ch->in_h = data >> 16;
        return NV4_GR_OK;
    }
    if (mthd == l->e_clip + 20) {
        ch->out_w = data & 0xffff;
        ch->out_h = data >> 16;
        return NV4_GR_OK;
    }
    if (mthd == l->e_clip + 24) {
        nv4_img_start(ch, IMG_MONO2, data);
        return NV4_GR_OK;
    }
    if ((mthd >= l->e_data) && (mthd < l->e_end)) {
        nv4_gdi_mono_data(nv4, ch, IMG_MONO2, data);
        return NV4_GR_OK;
    }

    if (cls == 0x4a) {
        /* F and G: font, clip, color 1, characters */
        if ((mthd == 0xff0) || (mthd == 0x17f0)) {
            ch->font = data;
            return NV4_GR_OK;
        }
        if ((mthd == 0xff4) || (mthd == 0xff8) || (mthd == 0x17f4) || (mthd == 0x17f8)) {
            nv4_set_clip(nv4, 1, ((mthd & 0xff) - 0xf4) >> 2, data);
            return NV4_GR_OK;
        }
        if ((mthd == 0xffc) || (mthd == 0x17fc)) {
            ch->color = data;
            return NV4_GR_OK;
        }
        if ((mthd >= 0x1000) && (mthd < 0x1400)) {
            nv4_gdi_char(nv4, ch, sext(data >> 8, 12), sext(data >> 20, 12), data & 0xff);
            return NV4_GR_OK;
        }
        if ((mthd >= 0x1800) && (mthd < 0x2000)) {
            if (!(mthd & 4)) {
                ch->vx[0] = (int16_t) data;
                ch->vy[0] = (int16_t) (data >> 16);
            } else
                nv4_gdi_char(nv4, ch, ch->vx[0], ch->vy[0], data & 0xffff);
            return NV4_GR_OK;
        }
    }

    switch (mthd) {
        case 0x184:
            if (cls == 0x4a) {
                nv4_pgraph_set_grobj(nv4, 2, 0, 16, data);
                return NV4_GR_OK;
            }
            return nv4_ctx_method(nv4, 27, data);
        case 0x188:
        case 0x18c:
            return nv4_ctx_method(nv4, (mthd - 0x188) / 4 + ((cls == 0x4a) ? 27 : 28), data);
        case 0x190:
            return nv4_ctx_method(nv4, (cls == 0x4a) ? 29 : 25, data);
        case 0x194:
            return (cls == 0x4a) ? nv4_ctx_method(nv4, 30, data) : NV4_GR_ILLEGAL;
        case 0x198:
            return (cls == 0x4a) ? nv4_ctx_method(nv4, 25, data) : NV4_GR_ILLEGAL;
        case 0x2fc:
            return nv4_operation_method(nv4, data);
        case 0x300:
            nv4_set_color_fmt(nv4, nv4_solid_fmt(data, 1));
            return NV4_GR_OK;
        case 0x304:
            nv4_pgraph_set_grobj(nv4, 1, 0, 2, data & 3);
            return NV4_GR_OK;
        case 0x10c:
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

/* Memory to memory format: 0x39 */
static int
nv4_class_m2mf(nv4_t *nv4, nv4_gr_chan_t *ch, uint32_t mthd, uint32_t data)
{
    switch (mthd) {
        case 0x184: /* the input DMA clears the output one */
            nv4_pgraph_set_grobj(nv4, 2, 0, 32, data & 0xffff);
            return NV4_GR_OK;
        case 0x188:
            nv4_pgraph_set_grobj(nv4, 2, 16, 16, data);
            return NV4_GR_OK;
        case 0x30c:
            ch->m2mf_in = data;
            return NV4_GR_OK;
        case 0x310:
            ch->m2mf_out = data;
            return NV4_GR_OK;
        case 0x314:
            ch->m2mf_pitch_in = (int32_t) data;
            return NV4_GR_OK;
        case 0x318:
            ch->m2mf_pitch_out = (int32_t) data;
            return NV4_GR_OK;
        case 0x31c:
            ch->m2mf_len = data;
            return NV4_GR_OK;
        case 0x320:
            ch->m2mf_count = data;
            return NV4_GR_OK;
        case 0x324:
            ch->m2mf_fmt = data;
            return NV4_GR_OK;
        case 0x328:
            nv4_m2mf(nv4, ch, data);
            return NV4_GR_OK;
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

/* Scaled image from memory: 0x77, 0x37 */
static int
nv4_class_sifm(nv4_t *nv4, nv4_gr_chan_t *ch, int cls, uint32_t mthd, uint32_t data)
{
    int is_new = cls != 0x37;

    switch (mthd) {
        case 0x184:
            nv4_pgraph_set_grobj(nv4, 2, 0, 16, data);
            return NV4_GR_OK;
        case 0x188:
        case 0x18c:
        case 0x190:
            return nv4_ctx_method(nv4, (mthd - 0x188) / 4 + 27, data);
        case 0x194:
            return nv4_ctx_method(nv4, is_new ? 30 : 25, data);
        case 0x198:
            return is_new ? nv4_ctx_method(nv4, 25, data) : NV4_GR_ILLEGAL;
        case 0x300:
            nv4_set_color_fmt(nv4, nv4_sifm_fmt(data));
            return NV4_GR_OK;
        case 0x304:
            return nv4_operation_method(nv4, data);
        case 0x308:
            nv4_set_clip(nv4, 1, 0, data);
            return NV4_GR_OK;
        case 0x30c:
            nv4_set_clip(nv4, 1, 2, data);
            return NV4_GR_OK;
        case 0x310:
            ch->sifm_x = (int16_t) data;
            ch->sifm_y = (int16_t) (data >> 16);
            return NV4_GR_OK;
        case 0x314:
            ch->sifm_w = data & 0xffff;
            ch->sifm_h = data >> 16;
            return NV4_GR_OK;
        case 0x318:
            ch->sifm_dudx = (int32_t) data;
            return NV4_GR_OK;
        case 0x31c:
            ch->sifm_dvdy = (int32_t) data;
            return NV4_GR_OK;
        case 0x400:
            ch->sifm_in_size = data;
            return NV4_GR_OK;
        case 0x404:
            ch->sifm_in_fmt = data;
            return NV4_GR_OK;
        case 0x408:
            ch->sifm_in_off = data;
            return NV4_GR_OK;
        case 0x40c:
            nv4_sifm_draw(nv4, ch, data);
            return NV4_GR_OK;
        case 0x10c:
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

/* ------------------------------------------------------------------ */
/* DVD subpicture.                                                     */
/* ------------------------------------------------------------------ */
/* 0x38 blends a subpicture (the "overlay": alpha + YUV) over a 4:2:2
   image, each scaled from its own DMA object, into a 4:2:2 surface
   (surface 4, its own DMA object). Video drivers also use it as a
   scaled YUV copy with the overlay transparent: NVIDIA's later Win9x
   drivers put every DirectDraw overlay frame into the overlay buffer
   this way. Sources are sampled at pixel centres, nearest. Formats:
   image/output 1 = YUY2 (Y0 U Y1 V), 2 = UYVY (U Y0 V Y1); overlay
   1 = A8V8U8Y8 per pixel, 2 = A4V6Y6A4U6Y6 per pixel pair (the high
   half the second pixel's), 3 = transparent. */
#define DVD_IMG 0
#define DVD_OV  1

static void
nv4_dvd_sample(nv4_t *nv4, const nv4_gr_chan_t *ch, int which, uint32_t dma, int32_t ox, int32_t oy, int yuva[4])
{
    uint32_t pitch = ch->dvd[which].fmt & 0xffff;
    int      fmt   = (ch->dvd[which].fmt >> 16) & 3;
    int32_t  w     = ch->dvd[which].size & 0xffff;
    int32_t  h     = ch->dvd[which].size >> 16;
    /* the point is 12.4, the steps 12.20 */
    int64_t  u     = ((int64_t) (int16_t) ch->dvd[which].point * 65536) + ch->dvd[which].dudx / 2 + (int64_t) ox * ch->dvd[which].dudx;
    int64_t  v     = ((int64_t) (int16_t) (ch->dvd[which].point >> 16) * 65536) + ch->dvd[which].dvdy / 2 + (int64_t) oy * ch->dvd[which].dvdy;
    int32_t  iu    = (int32_t) (u >> 20);
    int32_t  iv    = (int32_t) (v >> 20);
    uint8_t  b[4]  = { 0 };

    yuva[3] = 0;
    if ((which == DVD_OV) && ((fmt == 0) || (fmt == 3)))
        return;
    if (iu >= w)
        iu = w - 1;
    if (iu < 0)
        iu = 0;
    if (iv >= h)
        iv = h - 1;
    if (iv < 0)
        iv = 0;
    if ((which == DVD_OV) && (fmt == 1)) {
        nv4_dma_read(nv4, dma, ch->dvd[which].off + (uint32_t) iv * pitch + (uint32_t) iu * 4, b, 4);
        yuva[0] = b[0];
        yuva[1] = b[1];
        yuva[2] = b[2];
        yuva[3] = b[3];
        return;
    }
    nv4_dma_read(nv4, dma, ch->dvd[which].off + (uint32_t) iv * pitch + (uint32_t) (iu & ~1) * 2, b, 4);
    if (which == DVD_OV) {
        uint32_t p  = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t) b[3] << 24);
        uint32_t y  = (iu & 1) ? ((p >> 16) & 0x3f) : (p & 0x3f);
        uint32_t cu = (p >> 6) & 0x3f;
        uint32_t cv = (p >> 22) & 0x3f;

        yuva[0] = (int) ((y << 2) | (y >> 4));
        yuva[1] = (int) ((cu << 2) | (cu >> 4));
        yuva[2] = (int) ((cv << 2) | (cv >> 4));
        yuva[3] = (int) (((iu & 1) ? (p >> 28) : ((p >> 12) & 0xf)) * 17);
    } else if (fmt == 2) {
        yuva[0] = (iu & 1) ? b[3] : b[1];
        yuva[1] = b[0];
        yuva[2] = b[2];
    } else {
        yuva[0] = (iu & 1) ? b[2] : b[0];
        yuva[1] = b[1];
        yuva[2] = b[3];
    }
}

static void
nv4_dvd_draw(nv4_t *nv4, const nv4_gr_chan_t *ch)
{
    uint32_t dma_img = GR(NV4_PGRAPH_CTX_SWITCH3) & 0xffff;
    uint32_t dma_ov  = GR(NV4_PGRAPH_CTX_SWITCH3) >> 16;
    uint32_t base    = GR(NV4_PGRAPH_BBASE + 4 * 4) + GR(NV4_PGRAPH_BOFFSET + 4 * 4);
    uint32_t lim     = GR(NV4_PGRAPH_BBASE + 4 * 4) + (GR(NV4_PGRAPH_BLIMIT + 4 * 4) & 0x7fffffff);
    uint32_t pitch   = GR(NV4_PGRAPH_BPITCH + 4 * 4) & 0xffff;
    int      uyvy    = nv4_surf_fmt(nv4, 4) == 0xf;

    if (!dma_img || (GR(NV4_PGRAPH_BLIMIT + 4 * 4) & 0x80000000))
        return;
    for (uint32_t oy = 0; oy < ch->dvd_h; oy++) {
        uint32_t row = base + (uint32_t) (ch->dvd_y + (int32_t) oy) * pitch;

        /* the output is written in pixel pairs: one U and V for both */
        for (uint32_t ox = 0; ox < ch->dvd_w; ox += 2) {
            uint32_t a = row + (uint32_t) (ch->dvd_x + (int32_t) ox) * 2;
            int      px[2][4];
            int      cu, cv;

            for (int k = 0; k < 2; k++) {
                int ov[4];

                nv4_dvd_sample(nv4, ch, DVD_IMG, dma_img, (int32_t) (ox + k), (int32_t) oy, px[k]);
                nv4_dvd_sample(nv4, ch, DVD_OV, dma_ov, (int32_t) (ox + k), (int32_t) oy, ov);
                for (int c = 0; ov[3] && (c < 3); c++)
                    px[k][c] += ((ov[c] - px[k][c]) * ov[3] + 127) / 255;
            }
            cu = (px[0][1] + px[1][1] + 1) >> 1;
            cv = (px[0][2] + px[1][2] + 1) >> 1;
            if (a + 3 > lim)
                continue;
            if (uyvy)
                nv4_vram_wr(nv4, a, 4, (uint32_t) cu | (px[0][0] << 8) | (cv << 16) | ((uint32_t) px[1][0] << 24));
            else
                nv4_vram_wr(nv4, a, 4, (uint32_t) px[0][0] | (cu << 8) | (px[1][0] << 16) | ((uint32_t) cv << 24));
        }
    }
}

static int
nv4_class_dvd(nv4_t *nv4, nv4_gr_chan_t *ch, uint32_t mthd, uint32_t data)
{
    int which = (mthd >= 0x328) ? DVD_OV : DVD_IMG;

    switch (mthd) {
        case 0x184: /* the overlay's DMA */
            nv4_pgraph_set_grobj(nv4, 2, 16, 16, data);
            return NV4_GR_OK;
        case 0x188: /* the image's */
            nv4_pgraph_set_grobj(nv4, 2, 0, 16, data);
            return NV4_GR_OK;
        case 0x18c: /* the output surface's */
            nv4_dma_surf(nv4, 4, data);
            return NV4_GR_OK;
        case 0x300:
            ch->dvd_x = (int16_t) data;
            ch->dvd_y = (int16_t) (data >> 16);
            return NV4_GR_OK;
        case 0x304:
            ch->dvd_w = data & 0xffff;
            ch->dvd_h = data >> 16;
            return NV4_GR_OK;
        case 0x308:
            GR(NV4_PGRAPH_BPITCH + 4 * 4) = data & 0xfff0;
            nv4_set_surf_fmt(nv4, 4, (((data >> 16) & 0xf) == 2) ? 0xf : 0xe);
            return NV4_GR_OK;
        case 0x30c:
            GR(NV4_PGRAPH_BOFFSET + 4 * 4) = data & nv4_offset_mask(nv4);
            return NV4_GR_OK;
        case 0x310:
        case 0x328:
            ch->dvd[which].dudx = (int32_t) data;
            return NV4_GR_OK;
        case 0x314:
        case 0x32c:
            ch->dvd[which].dvdy = (int32_t) data;
            return NV4_GR_OK;
        case 0x318:
        case 0x330:
            ch->dvd[which].size = data;
            return NV4_GR_OK;
        case 0x31c:
        case 0x334:
            ch->dvd[which].fmt = data;
            return NV4_GR_OK;
        case 0x320:
        case 0x338:
            ch->dvd[which].off = data;
            return NV4_GR_OK;
        case 0x324:
            ch->dvd[DVD_IMG].point = data;
            return NV4_GR_OK;
        case 0x33c: /* the overlay's point draws */
            ch->dvd[DVD_OV].point = data;
            nv4_dvd_draw(nv4, ch);
            return NV4_GR_OK;
        case 0x200:
            return NV4_GR_MISSING;
        default:
            return NV4_GR_ILLEGAL;
    }
}

int
nv4_pgraph_2d_method(nv4_t *nv4, nv4_gr_chan_t *ch, int cls, uint32_t mthd, uint32_t data)
{
    switch (cls) {
        case 0x12:
            return nv4_class_beta(nv4, mthd, data);
        case 0x72:
            return nv4_class_beta4(nv4, mthd, data);
        case 0x43:
            return nv4_class_rop(nv4, mthd, data);
        case 0x17:
        case 0x57:
            return nv4_class_chroma(nv4, cls, mthd, data);
        case 0x18:
        case 0x44:
            return nv4_class_pattern(nv4, cls, mthd, data);
        case 0x19:
            return nv4_class_clip(nv4, mthd, data);
        case 0x42:
            return nv4_class_surf2d(nv4, mthd, data);
        case 0x52:
            return nv4_class_swzsurf(nv4, mthd, data);
        case 0x53:
            return nv4_class_surf3d(nv4, mthd, data);
        case 0x58:
        case 0x59:
        case 0x5a:
        case 0x5b:
            return nv4_class_surf(nv4, cls, mthd, data);
        case 0x1c:
        case 0x5c:
            return nv4_class_lin(nv4, ch, cls, mthd, data);
        case 0x1d:
        case 0x5d:
            return nv4_class_tri(nv4, ch, cls, mthd, data);
        case 0x1e:
        case 0x5e:
            return nv4_class_rect(nv4, ch, cls, mthd, data);
        case 0x1f:
        case 0x5f:
            return nv4_class_blit(nv4, ch, cls, mthd, data);
        case 0x21:
        case 0x61:
            return nv4_class_ifc(nv4, ch, cls, mthd, data);
        case 0x36:
        case 0x76:
            return nv4_class_sifc(nv4, ch, cls, mthd, data);
        case 0x60:
            return nv4_class_iifc(nv4, ch, mthd, data);
        /* The TNT2's own image classes: the TNT's with a colour conversion
           method (dither or truncate to a 16 bpp surface; 2D is drawn
           truncated). On the TNT these numbers are not image classes. */
        case 0x64:
            if (nv4->chip != NV4_CHIP_TNT2)
                break;
            return (mthd == 0x3e0) ? NV4_GR_OK : nv4_class_iifc(nv4, ch, mthd, data);
        case 0x65:
            if (nv4->chip != NV4_CHIP_TNT2)
                break;
            return (mthd == 0x2f8) ? NV4_GR_OK : nv4_class_ifc(nv4, ch, 0x61, mthd, data);
        case 0x66:
            if (nv4->chip != NV4_CHIP_TNT2)
                break;
            return (mthd == 0x2f8) ? NV4_GR_OK : nv4_class_sifc(nv4, ch, 0x76, mthd, data);
        case 0x4a:
        case 0x4b:
            return nv4_class_gdi(nv4, ch, cls, mthd, data);
        case 0x38:
            return nv4_class_dvd(nv4, ch, mthd, data);
        case 0x39:
            return nv4_class_m2mf(nv4, ch, mthd, data);
        case 0x37:
        case 0x77:
            return nv4_class_sifm(nv4, ch, cls, mthd, data);
        default:
            break;
    }
    /* the DX3 triangle (0x48) is not drawn yet */
    nv4_gap(nv4, cls, mthd, "class not drawn");
    return NV4_GR_OK;
}
