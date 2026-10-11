/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- span JIT host harness.
 *
 *          The harness generates blocks with the ARM64 and x86-64 span
 *          JIT emitters for a matrix of gated draw states, executes them
 *          on synthetic buffers, and compares the result byte for byte
 *          against a C reference that replicates the interpreter's pixel
 *          loop for exactly the gated subset: Gouraud or flat shading,
 *          optional Z, the 1555, 565, 8888 and 4444 pixel formats, plane
 *          masks, blend, dither, a texture stage and the alpha test.
 *
 *          There are two texture references. States that call the
 *          texture helper keep a synthetic adversarial stand-in
 *          (deterministic, heavy register clobber, about one pixel in
 *          eight discarded), which proves the emitted call glue. States
 *          in the inline texture family compare against a verbatim copy
 *          of the interpreter's texture code (the rr_ functions below)
 *          over fuzzed textures, mip chains, filters and combine modes.
 *          On a host that cannot execute a block's code, the harness
 *          still generates the block and runs the reference alone for
 *          that row, folding its results into a checksum.
 *
 *          The dump mode writes emitted blocks to files for disassembly
 *          with capstone. Exit status 0 means every row is bit-exact.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* Executable pages follow the emulator's platform contract. macOS arm64
   uses MAP_JIT pages and a per-thread write-protect toggle; Windows and
   other POSIX hosts use executable read/write pages and a no-op toggle,
   matching vid_ati_rage128_jit.c through plat_mmap(size, 1). */
#if defined(_WIN32)
#    include <windows.h>
#    undef near /* windef.h legacy pointer macros; rr_* uses them as idents */
#    undef far
static uint8_t *
map_exec(size_t sz)
{
    return (uint8_t *) VirtualAlloc(NULL, sz, MEM_COMMIT | MEM_RESERVE,
                                    PAGE_EXECUTE_READWRITE);
}
#    define pthread_jit_write_protect_np(w) ((void) (w))
#else
#    include <sys/mman.h>
#    if !defined(MAP_JIT)
#        define MAP_JIT 0
#    endif
#    if !defined(__APPLE__)
#        define pthread_jit_write_protect_np(w) ((void) (w))
#    endif
static uint8_t *
map_exec(size_t sz)
{
    uint8_t *p = mmap(0, sz, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_ANON | MAP_PRIVATE | MAP_JIT, -1, 0);

    return (p == MAP_FAILED) ? NULL : p;
}
#endif

/* Host arch: can the arm64 emitter's output execute here? On any other
   host the fuzz driver still generates every block (the emitters are
   plain C) but runs reference-only, folding the oracle's results into a
   checksum -- proves the C reference and build flags on this toolchain
   and is comparable against a same-seed run on the Mac. */
#if defined(__aarch64__) || defined(_M_ARM64)
#    define JHT_EXEC_A64 1
#else
#    define JHT_EXEC_A64 0
#endif

/* The device's own struct definitions (draw state, jit tri, stage
   desc/texctx: the emitters bake offsets from them), through the device
   header. Types and prototypes only; nothing links against 86Box. */
#include <86box/86box.h>
#include <86box/video.h>
#include <86box/vid_ati_rage128.h>

#include <86box/vid_ati_rage128_codegen_arm64.h>
#include <86box/vid_ati_rage128_codegen_x86_64.h>

/* Host can execute x86-64 emitter output natively. */
#if defined(__x86_64__) || defined(_M_X64)
#    define JHT_EXEC_X64 1
#else
#    define JHT_EXEC_X64 0
#endif

/* Forced-Win64 blocks on a SysV x86-64 host (built with
   R128_X64_ABI_WIN defined as one): the blocks expect the Win64
   convention, so every execution goes through a naked Win64-convention caller and
   only leaf blocks stay executable -- a helper-call block would
   Win64-call the SysV rage128_texstage_run. */
#if JHT_EXEC_X64 && R128_X64_ABI_WIN && !defined(_WIN32)
#    define JHT_WIN64_THUNK 1
#else
#    define JHT_WIN64_THUNK 0
#endif
#if JHT_WIN64_THUNK
/* SysV in: rdi=tri rsi=e0 rdx=e1 rcx=e2 xmm0=zline r8d=drow r9d=zrow
   stack: [rsp+8]=py [rsp+16]=fn.  Win64 out: rcx=tri rdx=e0 r8=e1
   r9=e2, zline/drow/zrow/py in the callee's [rsp+40..64] above a
   32-byte shadow, rsp 16-aligned at the call. */
uint64_t jht_win64_call(const r128_jit_tri_t *tri, int64_t e0, int64_t e1,
                        int64_t e2, double zline, uint32_t drow,
                        uint32_t zrow, int32_t py, r128_jit_span_fn fn)
    __asm__("jht_win64_call");
__asm__(
    ".text\n"
    ".globl jht_win64_call\n"
    "jht_win64_call:\n"
    "  pushq %rbp\n"
    "  movq  %rsp, %rbp\n"
    "  subq  $64, %rsp\n"          /* 32B shadow + 4 stack args; rsp
                                      was 16-aligned after the push */
    "  movl  %r8d, %eax\n"         /* drow (before r8 takes e1) */
    "  movl  %r9d, %r10d\n"        /* zrow (before r9 takes e2) */
    "  movq  %rcx, %r9\n"          /* e2  */
    "  movq  %rdx, %r8\n"          /* e1  */
    "  movq  %rsi, %rdx\n"         /* e0  */
    "  movq  %rdi, %rcx\n"         /* tri */
    "  movsd %xmm0, 32(%rsp)\n"    /* zline -> callee [rsp+40] */
    "  movl  %eax, 40(%rsp)\n"     /* drow  -> callee [rsp+48] */
    "  movl  %r10d, 48(%rsp)\n"    /* zrow  -> callee [rsp+56] */
    "  movl  16(%rbp), %eax\n"     /* py    (SysV stack arg)   */
    "  movl  %eax, 56(%rsp)\n"     /* py    -> callee [rsp+64] */
    "  callq *24(%rbp)\n"          /* fn    (SysV stack arg)   */
    "  movq  %rbp, %rsp\n"
    "  popq  %rbp\n"
    "  retq\n");
#    define JHT_SPAN_CALL(fn, tri, e0, e1, e2, zl, dr, zr, py) \
        jht_win64_call(tri, e0, e1, e2, zl, dr, zr, py, fn)
#else
#    define JHT_SPAN_CALL(fn, tri, e0, e1, e2, zl, dr, zr, py) \
        fn(tri, e0, e1, e2, zl, dr, zr, py)
#endif

#ifndef R128_A64_SOA_STEN_W
#    define R128_A64_SOA_STEN_W 0 /* Headers without a stencil weight
                                     use a zero rider cost. */
#endif

/* ------------------------------------------------------------------------
 * Real texture-stage reference for inline-family states: verbatim copies
 * of the interpreter's sampler chain (vid_ati_rage128_3d.c), with the
 * lazy r3d_stage_slot resolve replaced by a direct read of the
 * pre-resolved slot cache (the harness resolves every slot up front,
 * exactly like rage128_3d_tri does for inline-textured blocks).
 * ---------------------------------------------------------------------- */

static int
rr_tex_wrap(int c, int n, uint32_t mode)
{
    int m;

    switch (mode & 3) {
        case 0:
            return c & (n - 1);
        case 1:
            m = c & (2 * n - 1);
            return (m < n) ? m : (2 * n - 1 - m);
        case 2:
            return (c < 0) ? 0 : (c >= n) ? n - 1 : c;
        default:
            return (c < 0 || c >= n) ? -1 : c;
    }
}

static int
rr_tex_wrap_next(int c, int n, uint32_t mode)
{
    int cap = (mode & 3) == 2 && c >= n ? n - 1 : c;

    return rr_tex_wrap((int32_t) ((uint32_t) cap + 1u), n, mode);
}

static inline float
rr_log2f_fast(float x)
{
    union { float f; uint32_t u; } v = { x };
    float e = (float) (int) ((v.u >> 23) & 0xff) - 127.0f;
    float m;

    v.u = (v.u & 0x007fffffu) | 0x3f800000u;
    m   = v.f;
    return e + (-2.133847707f + m * (3.010783972f + m * (-1.029521946f + m * 0.153918478f)));
}

static inline float
rr_texcoord_fx(float f)
{
    return nearbyintf(f * 4096.0f) * (1.0f / 4096.0f);
}

/* verbatim mirror of r3d_f2i / r3d_f2u: truncate, saturate, NaN gives 0 */
static inline int32_t
rr_f2i(float f)
{
    if (f != f)
        return 0;
    if (f >= 2147483648.0f)
        return INT32_MAX;
    if (f < -2147483648.0f)
        return INT32_MIN;
    return (int32_t) f;
}

static inline uint32_t
rr_f2u(float f)
{
    if (!(f > 0.0f))
        return 0;
    if (f >= 4294967296.0f)
        return UINT32_MAX;
    return (uint32_t) f;
}

static inline uint32_t
rr_yuv_to_argb(uint32_t a, int y, int cb, int cr)
{
    int c = y - 16;
    int d = cb - 128;
    int e = cr - 128;
    int r = (298 * c + 409 * e + 128) >> 8;
    int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int b = (298 * c + 516 * d + 128) >> 8;

    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return (a << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
}

static inline uint32_t
rr_texel(const uint8_t *texbase, uint32_t base, uint32_t mask, uint32_t lw, uint32_t dt,
         uint32_t amask, uint32_t s3tc, const uint32_t *pal, int u, int v)
{
    uint32_t       off = (uint32_t) v * lw + u;
    const uint8_t *p;
    uint32_t       texel;

    switch (dt) {
        case 0: { /* S3TC, verbatim from r3d_texel (pclog stripped) */
            uint32_t bpitch = (lw + 3u) >> 2;
            uint32_t boff   = ((uint32_t) (v >> 2) * bpitch + (uint32_t) (u >> 2))
                            * ((s3tc >= 2u) ? 16u : 8u);
            uint32_t coff   = boff + ((s3tc >= 2u) ? 8u : 0u);
            uint32_t texidx = ((uint32_t) (v & 3) << 2) | (uint32_t) (u & 3);
            uint32_t a      = 0xffu;
            uint16_t c0, c1;
            uint32_t bits, sel, r0, g0, b0, r1, g1, b1;

            if (s3tc == 2u) {
                uint32_t an = texbase[(base + boff + (texidx >> 1)) & mask];
                a = (texidx & 1u) ? (an >> 4) : (an & 0xfu);
                a *= 0x11u;
            } else if (s3tc == 3u) {
                uint32_t a0 = texbase[(base + boff) & mask];
                uint32_t a1 = texbase[(base + boff + 1) & mask];
                uint32_t bp = texidx * 3u;
                uint32_t aw = *(const uint32_t *) &texbase[(base + boff + 2 + (bp >> 3)) & mask];
                uint32_t ac = (aw >> (bp & 7u)) & 7u;

                if (ac == 0)
                    a = a0;
                else if (ac == 1)
                    a = a1;
                else if (a0 > a1)
                    a = ((8u - ac) * a0 + (ac - 1u) * a1) / 7u;
                else if (ac == 6)
                    a = 0;
                else if (ac == 7)
                    a = 255;
                else
                    a = ((6u - ac) * a0 + (ac - 1u) * a1) / 5u;
            }

            a |= amask >> 24;
            c0   = *(const uint16_t *) &texbase[(base + coff) & mask];
            c1   = *(const uint16_t *) &texbase[(base + coff + 2) & mask];
            bits = *(const uint32_t *) &texbase[(base + coff + 4) & mask];
            sel  = (bits >> (texidx * 2u)) & 3;
            r0 = ((c0 >> 11) & 0x1f) * 255 / 31;
            g0 = ((c0 >> 5) & 0x3f) * 255 / 63;
            b0 = (c0 & 0x1f) * 255 / 31;
            r1 = ((c1 >> 11) & 0x1f) * 255 / 31;
            g1 = ((c1 >> 5) & 0x3f) * 255 / 63;
            b1 = (c1 & 0x1f) * 255 / 31;
            switch (sel) {
                case 0: return (a << 24) | (r0 << 16) | (g0 << 8) | b0;
                case 1: return (a << 24) | (r1 << 16) | (g1 << 8) | b1;
                case 2:
                    if (c0 > c1 || s3tc >= 2u)
                        return (a << 24) | (((2 * r0 + r1) / 3) << 16)
                             | (((2 * g0 + g1) / 3) << 8) | ((2 * b0 + b1) / 3);
                    return (a << 24) | (((r0 + r1) >> 1) << 16)
                         | (((g0 + g1) >> 1) << 8) | ((b0 + b1) >> 1);
                default:
                    if (c0 > c1 || s3tc >= 2u)
                        return (a << 24) | (((r0 + 2 * r1) / 3) << 16)
                             | (((g0 + 2 * g1) / 3) << 8) | ((b0 + 2 * b1) / 3);
                    return amask;
            }
        }
        case 1: /* CI4: low nibble */
            p = &texbase[(base + off) & mask];
            return pal[*p & 0xf];
        case 2: /* CI8 */
            p = &texbase[(base + off) & mask];
            return pal[*p];
        case 5: /* RGB888 */
            p = &texbase[(base + off * 3) & mask];
            return 0xff000000 | ((uint32_t) p[2] << 16) | ((uint32_t) p[1] << 8) | p[0];
        case 7: /* RGB332 */
            p     = &texbase[(base + off) & mask];
            texel = *p;
            return 0xff000000 | ((texel & 0xe0) << 16) | ((texel & 0x1c) << 11) | ((texel & 0x03) << 6);
        case 8: /* Y8 */
            p     = &texbase[(base + off) & mask];
            texel = *p;
            return 0xff000000 | (texel << 16) | (texel << 8) | texel;
        case 9: /* RGB8 */
            p     = &texbase[(base + off) & mask];
            texel = *p;
            return (texel << 24) | (texel << 16) | (texel << 8) | texel | amask;
        case 3: /* ARGB1555 */
            p     = &texbase[(base + off * 2) & mask];
            texel = *(const uint16_t *) p;
            return ((texel & 0x8000) ? 0xff000000 : 0)
                 | ((texel & 0x7c00) << 9) | ((texel & 0x03e0) << 6) | ((texel & 0x001f) << 3) | amask;
        case 4: /* RGB565 */
            p     = &texbase[(base + off * 2) & mask];
            texel = *(const uint16_t *) p;
            return 0xff000000 | ((texel & 0xf800) << 8) | ((texel & 0x07e0) << 5) | ((texel & 0x001f) << 3);
        case 15: /* ARGB4444 */
            p     = &texbase[(base + off * 2) & mask];
            texel = *(const uint16_t *) p;
            return ((texel & 0xf000) << 16) | ((texel & 0x0f00) << 12)
                 | ((texel & 0x00f0) << 8) | ((texel & 0x000f) << 4) | amask;
        case 6: /* ARGB8888 */
            p = &texbase[(base + off * 4) & mask];
            return *(const uint32_t *) p | amask;
        case 11:   /* VYUY422, verbatim from r3d_texel */
        case 12: {
            uint32_t poff = (base + ((uint32_t) v * lw + (uint32_t) (u & ~1)) * 2u) & mask;
            int      yb, cb, cr;

            if (dt == 11) {
                yb = texbase[(poff + ((uint32_t) (u & 1) * 2u)) & mask];
                cb = texbase[(poff + 1u) & mask];
                cr = texbase[(poff + 3u) & mask];
            } else {
                yb = texbase[(poff + 1u + ((uint32_t) (u & 1) * 2u)) & mask];
                cb = texbase[poff];
                cr = texbase[(poff + 2u) & mask];
            }
            return rr_yuv_to_argb(0xffu, yb, cb, cr);
        }
        case 14: /* aYUV444 */
            p     = &texbase[(base + off * 4) & mask];
            texel = *(const uint32_t *) p;
            return rr_yuv_to_argb(texel >> 24, (int) ((texel >> 16) & 0xff),
                                  (int) ((texel >> 8) & 0xff), (int) (texel & 0xff)) | amask;
        default:
            abort(); /* outside the inline family */
    }
}

static inline uint32_t
rr_lerp_packed(uint32_t x, uint32_t y, uint32_t w)
{
    uint32_t iw = 256u - w;
    uint32_t rb = ((x & 0x00ff00ffu) * iw + (y & 0x00ff00ffu) * w + 0x00800080u) >> 8;
    uint32_t ag = (((x >> 8) & 0x00ff00ffu) * iw + ((y >> 8) & 0x00ff00ffu) * w + 0x00800080u) >> 8;

    return ((ag & 0x00ff00ffu) << 8) | (rb & 0x00ff00ffu);
}

static uint32_t
rr_tex_level(const r3d_stage_desc_t *d, int slot, float s, float t, int linear,
             uint32_t *nearest)
{
    int      cslot = slot < 0 ? 0 : (slot > 10 ? 10 : slot);
    int      u, v;
    uint32_t near;
    const struct r3d_slot_desc_t *sl = &d->slot[cslot];
    const uint8_t *texbase = sl->texbase;
    uint32_t       basesel = sl->base;
    uint32_t       masksel = sl->mask;
    uint32_t       lw = sl->lw, lh = sl->lh;

    if (!linear || nearest) {
        u    = rr_tex_wrap(rr_f2i(floorf(rr_texcoord_fx(s * (float) lw))), lw, d->clamp_s);
        v    = rr_tex_wrap(rr_f2i(floorf(rr_texcoord_fx(t * (float) lh))), lh, d->clamp_t);
        near = (u < 0 || v < 0) ? d->border
                                : rr_texel(texbase, basesel, masksel, lw, d->dt, d->amask, d->s3tc, d->pal, u, v);
        if (nearest) {
            /* CI formats key on the raw palette index (r3d_texel_ci_raw) */
            if ((d->dt == 1 || d->dt == 2) && u >= 0 && v >= 0) {
                uint32_t idx = texbase[(basesel + (uint32_t) v * lw + (uint32_t) u) & masksel];

                *nearest = d->dt == 1 ? (idx & 0xfu) : idx;
            } else
                *nearest = near;
        }
        if (!linear)
            return near;
    }

    {
        float    fu = rr_texcoord_fx(s * (float) lw) - 0.5f;
        float    fv = rr_texcoord_fx(t * (float) lh) - 0.5f;
        int      u0 = rr_f2i(floorf(fu));
        int      v0 = rr_f2i(floorf(fv));
        uint32_t wu = rr_f2u((fu - (float) u0) * 256.0f + 0.5f);
        uint32_t wv = rr_f2u((fv - (float) v0) * 256.0f + 0.5f);
        uint32_t c[4];
        int      uu0 = rr_tex_wrap(u0, lw, d->clamp_s),  uu1 = rr_tex_wrap_next(u0, lw, d->clamp_s);
        int      vv0 = rr_tex_wrap(v0, lh, d->clamp_t),  vv1 = rr_tex_wrap_next(v0, lh, d->clamp_t);

        c[0] = (uu0 < 0 || vv0 < 0) ? d->border : rr_texel(texbase, basesel, masksel, lw, d->dt, d->amask, d->s3tc, d->pal, uu0, vv0);
        c[1] = (uu1 < 0 || vv0 < 0) ? d->border : rr_texel(texbase, basesel, masksel, lw, d->dt, d->amask, d->s3tc, d->pal, uu1, vv0);
        c[2] = (uu0 < 0 || vv1 < 0) ? d->border : rr_texel(texbase, basesel, masksel, lw, d->dt, d->amask, d->s3tc, d->pal, uu0, vv1);
        c[3] = (uu1 < 0 || vv1 < 0) ? d->border : rr_texel(texbase, basesel, masksel, lw, d->dt, d->amask, d->s3tc, d->pal, uu1, vv1);
        return rr_lerp_packed(rr_lerp_packed(c[0], c[1], wu),
                              rr_lerp_packed(c[2], c[3], wu), wv);
    }
}

static uint32_t
rr_lerp_argb(uint32_t x, uint32_t y, float f)
{
    return rr_lerp_packed(x, y, rr_f2u(f * 256.0f + 0.5f));
}

static uint32_t
rr_tex_sample(const r3d_stage_desc_t *d, float s, float t, float lod, int has_lod,
              uint32_t *nearest)
{
    uint32_t   minb   = d->minb;
    uint32_t   mag    = d->mag;
    int        mipdis = d->mipdis;
    int        top    = d->top;

    if (!has_lod || mipdis || minb < 2) {
        int linear = has_lod ? (lod > 0.0f ? (int) (minb & 1) : (mag == 1))
                             : !(minb == 0 && mag == 0);
        return rr_tex_level(d, top, s, t, linear, nearest);
    }

    if (lod <= 0.0f)
        return rr_tex_level(d, top, s, t, mag == 1, nearest);

    {
        float lvl          = lod > (float) top ? (float) top : lod;
        int   texel_linear = (minb == 3 || minb == 5);
        int   mip_linear   = (minb == 4 || minb == 5);

        if (mip_linear) {
            int      l0    = rr_f2i(floorf(lvl));
            float    f     = lvl - (float) l0;
            int      slotA = top - l0;
            int      slotB = top - (l0 + 1);
            uint32_t ca, cb;

            if (slotB < 0) slotB = 0;
            ca = rr_tex_level(d, slotA, s, t, texel_linear, nearest);
            cb = rr_tex_level(d, slotB, s, t, texel_linear, NULL);
            return rr_lerp_argb(ca, cb, f);
        }
        return rr_tex_level(d, top - rr_f2i(lvl + 0.5f), s, t, texel_linear, nearest);
    }
}

/* combine -- verbatim mirror of r3d_tex_combine (full code book) */
static void
rr_tex_combine(const r3d_comb_desc_t *cd, float col[4], uint32_t tx,
               const float int_color[4], const float cc[4], int first)
{
    uint32_t   comb  = cd->comb;
    uint32_t   fmsb  = cd->fmsb;
    uint32_t   cfac  = cd->cfac;
    uint32_t   ifac  = cd->ifac;
    uint32_t   comba = cd->comba;
    uint32_t   afac  = cd->afac;
    uint32_t   ifaca = cd->ifaca;
    float      tr    = ((tx >> 16) & 0xff) / 255.0f;
    float      tg    = ((tx >> 8) & 0xff) / 255.0f;
    float      tb    = (tx & 0xff) / 255.0f;
    float      ta    = (tx >> 24) / 255.0f;
    float      prev[4];
    float      fc[3];
    float      ci[3];
    float      fa, ia;
    int        i;

    prev[0] = col[0]; prev[1] = col[1]; prev[2] = col[2]; prev[3] = col[3];

    switch (cfac) {
        case 0: fc[0] = cc[0];        fc[1] = cc[1];        fc[2] = cc[2];        break;
        case 1: fc[0] = 1.0f - cc[0]; fc[1] = 1.0f - cc[1]; fc[2] = 1.0f - cc[2]; break;
        case 5: fc[0] = 1.0f - tr;    fc[1] = 1.0f - tg;    fc[2] = 1.0f - tb;    break;
        case 6: fc[0] = fc[1] = fc[2] = ta;        break;
        case 7: fc[0] = fc[1] = fc[2] = 1.0f - ta; break;
        case 8: fc[0] = prev[0]; fc[1] = prev[1]; fc[2] = prev[2]; break;
        default: fc[0] = tr; fc[1] = tg; fc[2] = tb; break;
    }
    switch (ifac) {
        case 2: ci[0] = cc[0]; ci[1] = cc[1]; ci[2] = cc[2]; break;
        case 3: ci[0] = ci[1] = ci[2] = cc[3]; break;
        case 5: ci[0] = ci[1] = ci[2] = int_color[3]; break;
        case 8: ci[0] = prev[0]; ci[1] = prev[1]; ci[2] = prev[2]; break;
        case 9: ci[0] = ci[1] = ci[2] = prev[3]; break;
        default: ci[0] = int_color[0]; ci[1] = int_color[1]; ci[2] = int_color[2]; break;
    }

    switch (comb) {
        case 0:
            if (fmsb) {
                for (i = 0; i < 3; i++) { col[i] = fc[i] - ci[i]; if (col[i] < 0.0f) col[i] = 0.0f; }
                break;
            }
            col[0] = tr; col[1] = tg; col[2] = tb;
            break;
        case 1:
            col[0] = fc[0]; col[1] = fc[1]; col[2] = fc[2];
            break;
        case 2:
            col[0] = ci[0]; col[1] = ci[1]; col[2] = ci[2];
            break;
        case 4:
            if (fmsb) {
                col[0] = ci[0] * (1.0f - tr) + fc[0] * tr;
                col[1] = ci[1] * (1.0f - tg) + fc[1] * tg;
                col[2] = ci[2] * (1.0f - tb) + fc[2] * tb;
            } else {
                for (i = 0; i < 3; i++) { col[i] = ci[i] * fc[i] * 2.0f; if (col[i] > 1.0f) col[i] = 1.0f; }
            }
            break;
        case 5:
            if (fmsb) {
                float t[3] = { tr, tg, tb };

                for (i = 0; i < 3; i++) { col[i] = fc[i] + ci[i] * (1.0f - t[i]); if (col[i] > 1.0f) col[i] = 1.0f; }
            } else {
                for (i = 0; i < 3; i++) { col[i] = ci[i] * fc[i] * 4.0f; if (col[i] > 1.0f) col[i] = 1.0f; }
            }
            break;
        case 6:
            if (fmsb) {
                float t[3] = { tr, tg, tb };

                for (i = 0; i < 3; i++) { col[i] = fc[i] + ci[i] * t[i]; if (col[i] > 1.0f) col[i] = 1.0f; }
            } else {
                for (i = 0; i < 3; i++) { col[i] = ci[i] + fc[i]; if (col[i] > 1.0f) col[i] = 1.0f; }
            }
            break;
        case 7:
            for (i = 0; i < 3; i++) { col[i] = ci[i] + fc[i] - 0.5f; col[i] = col[i] < 0.0f ? 0.0f : (col[i] > 1.0f ? 1.0f : col[i]); }
            break;
        case 8:
        {
            float f = int_color[3];

            for (i = 0; i < 3; i++) col[i] = ci[i] * (1.0f - f) + fc[i] * f;
            break;
        }
        case 9:
            for (i = 0; i < 3; i++) col[i] = ci[i] * (1.0f - ta) + fc[i] * ta;
            break;
        case 10:
            for (i = 0; i < 3; i++) col[i] = ci[i] * (1.0f - cc[3]) + fc[i] * cc[3];
            break;
        case 11:
            for (i = 0; i < 3; i++) { col[i] = fc[i] + ci[i] * (1.0f - ta); if (col[i] > 1.0f) col[i] = 1.0f; }
            break;
        case 12:
            for (i = 0; i < 3; i++) col[i] = ci[i] * (1.0f - prev[3]) + fc[i] * prev[3];
            break;
        case 13:
            for (i = 0; i < 3; i++) { col[i] = fc[i] + ci[i] * ta; if (col[i] > 1.0f) col[i] = 1.0f; }
            break;
        case 14:
            for (i = 0; i < 3; i++) { col[i] = (ci[i] + fc[i] - 0.5f) * 2.0f; col[i] = col[i] < 0.0f ? 0.0f : (col[i] > 1.0f ? 1.0f : col[i]); }
            break;
        case 15:
            for (i = 0; i < 3; i++) col[i] = ci[i] * (1.0f - cc[i]) + fc[i] * cc[i];
            break;
        default:
            /* fall through */
        case 3:
            col[0] = ci[0] * fc[0]; col[1] = ci[1] * fc[1]; col[2] = ci[2] * fc[2];
            break;
    }

    fa = (afac == 7) ? (1.0f - ta) : ta;
    switch (ifaca) {
        case 1:  ia = cc[3];        break;
        case 2:  ia = int_color[3]; break;
        default: ia = prev[3];      break;
    }
    switch (comba) {
        case 0: /* the texel alpha on stage 0, the incoming alpha on stage 1 */
            col[3] = first ? ta : prev[3];
            break;
        case 1:
            col[3] = fa;
            break;
        case 2:
            col[3] = ia;
            break;
        case 4:
            col[3] = ia * fa * 2.0f; if (col[3] > 1.0f) col[3] = 1.0f;
            break;
        case 6:
            col[3] = ia + fa; if (col[3] > 1.0f) col[3] = 1.0f;
            break;
        case 7:
            col[3] = ia + fa - 0.5f; col[3] = col[3] < 0.0f ? 0.0f : (col[3] > 1.0f ? 1.0f : col[3]);
            break;
        case 5:
            col[3] = ia * fa * 4.0f; if (col[3] > 1.0f) col[3] = 1.0f;
            break;
        case 14:
            col[3] = (ia + fa - 0.5f) * 2.0f; col[3] = col[3] < 0.0f ? 0.0f : (col[3] > 1.0f ? 1.0f : col[3]);
            break;
        default:
        case 3:
            col[3] = ia * fa;
            break;
    }
}

static int
rr_texstage_run(const r3d_texctx_t *tc, const rage128_draw_state_t *ds,
                float w0, float w1, float w2, float *col)
{
    float int_color[4];
    float ir  = 1.0f;
    float rhw = 0.0f;

    int_color[0] = col[0]; int_color[1] = col[1];
    int_color[2] = col[2]; int_color[3] = col[3];

    if (ds->do_persp) {
        rhw = w0 * tc->arhw + w1 * tc->brhw + w2 * tc->crhw;
        if (rhw != 0.0f)
            ir = 1.0f / rhw;
    }
    if (ds->tex_en) {
        float sp = w0 * tc->sta + w1 * tc->stb + w2 * tc->stc;
        float tp = w0 * tc->tta + w1 * tc->ttb + w2 * tc->ttc;
        float s  = sp * ir;
        float t  = tp * ir;
        float lod = 0.0f;
        uint32_t tx;

        if (ds->need_lod) {
            float dsx, dsy, dtx, dty, ax2, ay2, rho2;

            if (ds->do_persp) {
                float wp  = rhw;
                float iw2 = (wp != 0.0f) ? 1.0f / (wp * wp) : 0.0f;

                dsx = (tc->dSdx * wp - sp * tc->dWdx) * iw2;
                dsy = (tc->dSdy * wp - sp * tc->dWdy) * iw2;
                dtx = (tc->dTdx * wp - tp * tc->dWdx) * iw2;
                dty = (tc->dTdy * wp - tp * tc->dWdy) * iw2;
            } else {
                dsx = tc->dSdx; dsy = tc->dSdy; dtx = tc->dTdx; dty = tc->dTdy;
            }
            dsx *= ds->texw0; dsy *= ds->texw0;
            dtx *= ds->texh0; dty *= ds->texh0;
            ax2  = dsx * dsx + dtx * dtx;
            ay2  = dsy * dsy + dty * dty;
            rho2 = ax2 > ay2 ? ax2 : ay2;
            lod  = rho2 > 0.0f ? 0.5f * rr_log2f_fast(rho2) + ds->lod_bias
                               : -1000.0f;
        }
        uint32_t tnear = 0;
        tx = rr_tex_sample(&tc->sd0, s, t, lod, ds->need_lod,
                           ds->need_ck ? &tnear : NULL);
        if (ds->ck3d_on) {
            int eq = (tnear & ds->ck3d_msk) == (ds->ck3d_clr & ds->ck3d_msk);

            if (ds->ckfn == 3 ? eq : !eq)
                return 0;
        }
        if (ds->ckc_on) {
            if ((tnear & ds->ckc_msk) == (ds->ckc_clr & ds->ckc_msk))
                return 0;
        }
        rr_tex_combine(&ds->comb[0], col, tx, int_color, ds->cc, 1);
    }
    if (ds->sec_en) {
        int      sel    = ds->sec_sel;
        int      persp2 = ds->do_persp ^ ds->sec_persp_diff;
        int      own    = ds->sel_w || ds->sec_persp_diff;
        float    rhw2   = rhw;
        float    ir2    = ir;
        float    sp2  = sel ? (w0 * tc->s2a + w1 * tc->s2b + w2 * tc->s2c)
                            : (w0 * tc->sta + w1 * tc->stb + w2 * tc->stc);
        float    tp2  = sel ? (w0 * tc->t2a + w1 * tc->t2b + w2 * tc->t2c)
                            : (w0 * tc->tta + w1 * tc->ttb + w2 * tc->ttc);
        float    s, t;
        float    lod2 = 0.0f;
        uint32_t tx;

        if (own) {
            rhw2 = 0.0f;
            ir2  = 1.0f;
            if (persp2) {
                rhw2 = w0 * tc->a2rhw + w1 * tc->b2rhw + w2 * tc->c2rhw;
                if (rhw2 != 0.0f)
                    ir2 = 1.0f / rhw2;
            }
        }
        s = sp2 * ir2;
        t = tp2 * ir2;

        if (ds->need_lod2) {
            float gsx = sel ? tc->dS2dx : tc->dSdx, gsy = sel ? tc->dS2dy : tc->dSdy;
            float gtx = sel ? tc->dT2dx : tc->dTdx, gty = sel ? tc->dT2dy : tc->dTdy;
            float dsx, dsy, dtx, dty, ax2, ay2, rho2;

            if (persp2) {
                float wp  = rhw2;
                float iw2 = (wp != 0.0f) ? 1.0f / (wp * wp) : 0.0f;
                float gwx = ds->sel_w ? tc->dW2dx : tc->dWdx;
                float gwy = ds->sel_w ? tc->dW2dy : tc->dWdy;

                dsx = (gsx * wp - sp2 * gwx) * iw2;
                dsy = (gsy * wp - sp2 * gwy) * iw2;
                dtx = (gtx * wp - tp2 * gwx) * iw2;
                dty = (gty * wp - tp2 * gwy) * iw2;
            } else {
                dsx = gsx; dsy = gsy; dtx = gtx; dty = gty;
            }
            dsx *= ds->texw1; dsy *= ds->texw1;
            dtx *= ds->texh1; dty *= ds->texh1;
            ax2  = dsx * dsx + dtx * dtx;
            ay2  = dsy * dsy + dty * dty;
            rho2 = ax2 > ay2 ? ax2 : ay2;
            lod2 = rho2 > 0.0f ? 0.5f * rr_log2f_fast(rho2) + ds->lod_bias
                               : -1000.0f;
        }
        tx = rr_tex_sample(&tc->sd1, s, t, lod2, ds->need_lod2, NULL);
        rr_tex_combine(&ds->comb[1], col, tx, int_color, ds->cc, 0);
    }
    return 1;
}

static int          g_real_tex; /* 1 = inline family, compare vs rr_* */
static r3d_texctx_t g_tctx;
static rage128_raster_state_t g_stipple_rs[2];

/* ---- synthetic texture stage: adversarial stand-in for the shared
   interpreter code. Deterministic in (seed, w0, w1, w2, col); burns
   plenty of caller-saved FP/GPR registers; discards ~1/8 of pixels. ---- */
static uint32_t
fbits(float f)
{
    uint32_t u;

    memcpy(&u, &f, 4);
    return u;
}

static const rage128_draw_state_t *g_helper_ds; /* bench force-call mode */

int
rage128_texstage_run(void *tcv, float w0, float w1, float w2, float *col)
{
    if (g_real_tex) /* Real-context calls share the reference sampler to preserve its bytes. */
        return rr_texstage_run((const r3d_texctx_t *) tcv, g_helper_ds, w0, w1, w2, col);
    uint32_t seed = *(uint32_t *) tcv;
    uint32_t h    = seed ^ 0x9e3779b9u;
    double   clob[14];
    double   s = 0.0;

    h = (h ^ fbits(w0)) * 2654435761u;
    h = (h ^ fbits(w1)) * 2654435761u;
    h = (h ^ fbits(w2)) * 2654435761u;
    for (int i = 0; i < 4; i++)
        h = (h ^ fbits(col[i])) * 2654435761u;
    h ^= h >> 15;

    /* register pressure: force lots of live FP values across a loop */
    for (int i = 0; i < 14; i++)
        clob[i] = (double) ((h >> i) & 0xffff) * 1.0009765625;
    for (int i = 0; i < 14; i++)
        s += clob[i] * clob[(i + 5) % 14];
    if (s < 0.0) /* never true; keeps the loop from folding away */
        return 1;

    if ((h & 0x70000000u) == 0x70000000u)
        return 0;

    for (int i = 0; i < 4; i++)
        col[i] = (float) ((h >> (i * 8)) & 0xff) / 255.0f * (0.5f + 0.5f * col[i]);
    return 1;
}

/* ---- C reference: the interpreter's pixel loop for the gated subset ---- */
static const uint8_t ref_bayer4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 },
};

static uint32_t
ref_dq(uint32_t v8, uint32_t add)
{
    v8 += add;
    return v8 > 255 ? 255 : v8;
}

static uint32_t
ref_dst_read(const uint8_t *p, uint32_t dt)
{
    uint32_t v;

    if (dt == 3) {
        v = *(const uint16_t *) p;
        return ((v & 0x8000) ? 0xff000000u : 0)
             | ((v & 0x7c00) << 9) | ((v & 0x03e0) << 6) | ((v & 0x001f) << 3);
    }
    if (dt == 15) {
        v = *(const uint16_t *) p;
        return ((v & 0xf000) << 16) | ((v & 0x0f00) << 12)
             | ((v & 0x00f0) << 8) | ((v & 0x000f) << 4);
    }
    if (dt == 4) {
        v = *(const uint16_t *) p;
        return 0xff000000 | ((v & 0xf800) << 8) | ((v & 0x07e0) << 5) | ((v & 0x001f) << 3);
    }
    return *(const uint32_t *) p;
}

static int
ref_cmp(uint32_t fn, uint32_t a, uint32_t b)
{
    switch (fn & 7) {
        case 0: return 0;
        case 1: return a < b;
        case 2: return a <= b;
        case 3: return a == b;
        case 4: return a >= b;
        case 5: return a > b;
        case 6: return a != b;
        default: return 1;
    }
}

static void
ref_blend_factor(uint32_t code, const float sc[4], const float dc[4], float out[4])
{
    float f;

    switch (code & 0xf) {
        case 0x0: out[0] = out[1] = out[2] = out[3] = 0.0f; return;
        case 0x1: out[0] = out[1] = out[2] = out[3] = 1.0f; return;
        case 0x2: out[0] = sc[0]; out[1] = sc[1]; out[2] = sc[2]; out[3] = sc[3]; return;
        case 0x3: out[0] = 1.0f - sc[0]; out[1] = 1.0f - sc[1]; out[2] = 1.0f - sc[2]; out[3] = 1.0f - sc[3]; return;
        case 0x4: out[0] = out[1] = out[2] = out[3] = sc[3]; return;
        case 0x5: out[0] = out[1] = out[2] = out[3] = 1.0f - sc[3]; return;
        case 0x6: out[0] = out[1] = out[2] = out[3] = dc[3]; return;
        case 0x7: out[0] = out[1] = out[2] = out[3] = 1.0f - dc[3]; return;
        case 0x8: out[0] = dc[0]; out[1] = dc[1]; out[2] = dc[2]; out[3] = dc[3]; return;
        case 0x9: out[0] = 1.0f - dc[0]; out[1] = 1.0f - dc[1]; out[2] = 1.0f - dc[2]; out[3] = 1.0f - dc[3]; return;
        case 0xa: /* The modeled source-alpha-saturate factor applies to either
                     blend operand: RGB uses min(src alpha, 1 - dst alpha),
                     while the alpha factor is one. */
            f = sc[3] < 1.0f - dc[3] ? sc[3] : 1.0f - dc[3];
            out[0] = out[1] = out[2] = f;
            out[3] = 1.0f;
            return;
        default:
            out[0] = out[1] = out[2] = out[3] = 1.0f;
            return;
    }
}

/* mirrors rage128_aux_sc_pass on the decoded ds fields */
static int
ref_aux_pass(const rage128_draw_state_t *d, int x, int y)
{
    int have_add = 0, in_add = 0;

    for (int i = 0; i < 3; i++) {
        if (!(d->aux_cntl & (1u << (i * 2))))
            continue;
        int in = x >= d->aux_x0[i] && x <= d->aux_x1[i]
              && y >= d->aux_y0[i] && y <= d->aux_y1[i];

        if (d->aux_cntl & (2u << (i * 2))) {
            if (in)
                return 0;
        } else {
            have_add = 1;
            if (in)
                in_add = 1;
        }
    }
    return have_add ? in_add : 1;
}

static uint64_t
ref_span(const r128_jit_tri_t *t, const rage128_draw_state_t *d,
         int64_t e0, int64_t e1, int64_t e2, double zline,
         uint32_t drow, uint32_t zrow, int32_t py)
{
    int32_t rx0 = -1, rx1 = -1;

    for (int32_t px = t->x0; px <= t->x1;
         px++, e0 += t->e0dxi, e1 += t->e1dxi, e2 += t->e2dxi, zline += t->dZdx) {
        if (e0 < 0 || e1 < 0 || e2 < 0)
            continue;
        if (d->aux_on && !ref_aux_pass(d, px, py))
            continue;
        if (d->stip_en
            && !((((const r3d_texctx_t *) t->texctx)->rs->stipple[py & 31]
                  >> (31 - (px & 31))) & 1))
            continue;

        /* the fill-rule bias decides coverage only: the weights take
           the unbiased edge values, as the interpreter does */
        float w0 = (float) (e0 + t->e0b) * t->invs;
        float w1 = (float) (e1 + t->e1b) * t->invs;
        float w2 = (float) (e2 + t->e2b) * t->invs;

        double zc = zline;
        if (!(zc > 0.0)) zc = 0.0;
        if (zc > 1.0) zc = 1.0;

        uint32_t zi = 0;
        uint8_t *zcell = NULL;
        int      zres = 1, sres = 1;
        uint32_t sbuf = 0;

        if (d->z_en || d->sten_on) {
            double zq = zc * (double) d->zmax + 0.5;

            if (zq > (double) d->zmax)
                zq = (double) d->zmax;
            zi = (uint32_t) zq;
            uint32_t zaddr = zrow + (uint32_t) px * d->zbpp;
            if (t->zptr) {
                uint32_t zoff = zaddr - t->z_base;
                if (zoff + (uint32_t) d->zbpp > t->z_lim)
                    continue;
                zcell = t->zptr + zoff;
            } else
                zcell = &t->vram[zaddr & t->vram_mask];
            uint32_t zbuf;
            if (d->zbpp == 2)
                zbuf = *(uint16_t *) zcell & d->zmax;
            else
                zbuf = (*(uint32_t *) zcell >> d->zshift) & d->zmax;
            if (d->z_en)
                zres = ref_cmp(d->zfn, zi, zbuf);
            if (d->sten_on) {
                sbuf = (*(uint32_t *) zcell >> d->sshift) & 0xff;
                sres = ref_cmp(d->sfn, d->sref & d->svmask, sbuf & d->svmask);
            } else if (!zres)
                continue;
        }

        float col[4];
        col[0] = w0 * t->vca[0] + w1 * t->vcb[0] + w2 * t->vcc[0];
        col[1] = w0 * t->vca[1] + w1 * t->vcb[1] + w2 * t->vcc[1];
        col[2] = w0 * t->vca[2] + w1 * t->vcb[2] + w2 * t->vcc[2];
        col[3] = w0 * t->vca[3] + w1 * t->vcb[3] + w2 * t->vcc[3];

        if (d->tex_en || d->sec_en) {
            if (g_real_tex) {
                if (!rr_texstage_run((const r3d_texctx_t *) t->texctx, d, w0, w1, w2, col))
                    continue;
            } else if (!rage128_texstage_run(t->texctx, w0, w1, w2, col))
                continue;
        }

        if (d->spec_en) {
            col[0] += w0 * t->spa[0] + w1 * t->spb[0] + w2 * t->spc[0];
            col[1] += w0 * t->spa[1] + w1 * t->spb[1] + w2 * t->spc[1];
            col[2] += w0 * t->spa[2] + w1 * t->spb[2] + w2 * t->spc[2];
            if (col[0] > 1.0f) col[0] = 1.0f;
            if (col[1] > 1.0f) col[1] = 1.0f;
            if (col[2] > 1.0f) col[2] = 1.0f;
        }

        if (d->fog_en) {
            float f;

            if (d->fog_table_en) {
                /* sub-entry blend, same split as the interpreter */
                uint64_t q  = (uint64_t) (zc * 255.0 * 4294967296.0);
                uint32_t i  = (uint32_t) (q >> 32);
                uint32_t i1 = i < 255 ? i + 1 : i;
                float    tf = (float) (uint32_t) q * (1.0f / 4294967296.0f);
                float    fa = (float) t->fog_table[i];
                float    fd = (float) ((int) t->fog_table[i1] - (int) t->fog_table[i]);

                f = (fa + fd * tf) / 255.0f;
            } else
                f = w0 * t->fog[0] + w1 * t->fog[1] + w2 * t->fog[2];
            if (f < 0.0f) f = 0.0f;
            if (f > 1.0f) f = 1.0f;
            col[0] = col[0] * f + d->fogr * (1.0f - f);
            col[1] = col[1] * f + d->fogg * (1.0f - f);
            col[2] = col[2] * f + d->fogb * (1.0f - f);
        }

        if (d->atest_en
            && !ref_cmp(d->atest_fn, (uint32_t) (col[3] * 255.0f + 0.5f), d->atest_ref))
            continue;

        /* Stencil update + deferred discard (device sec 3.5): op by
           outcome, RMW of the stencil byte under swmask, then discard on a
           failed depth/stencil test -- after the alpha test, before color. */
        if (d->sten_on) {
            uint32_t sop  = !sres ? d->sfail_op : (zres ? d->zpass_op : d->zfail_op);
            uint32_t snew = sbuf;

            switch (sop) {
                case 0:                                        break;
                case 1: snew = 0;                              break;
                case 2: snew = d->sref;                        break;
                case 3: snew = sbuf == 0xff ? 0xff : sbuf + 1; break;
                case 4: snew = sbuf == 0 ? 0 : sbuf - 1;       break;
                case 5: snew = ~sbuf & 0xff;                   break;
                case 6: snew = (sbuf + 1) & 0xff;              break;
                default: snew = (sbuf - 1) & 0xff;             break;
            }
            snew = (sbuf & ~d->swmask) | (snew & d->swmask);
            if (snew != sbuf) {
                uint32_t *zp = (uint32_t *) zcell;

                *zp = (*zp & ~(0xffu << d->sshift)) | (snew << d->sshift);
            }
            if (!sres || !zres)
                continue;
        }

        /* resolve dcell early for the blend read (same cell the write
           uses); the skip must come before rx/z updates as in the C */
        uint32_t daddr = drow + (uint32_t) px * d->bpp;
        uint8_t *dcell;
        if (t->cptr) {
            uint32_t coff = daddr - t->c_base;
            if (coff + (uint32_t) d->bpp > t->c_lim)
                continue;
            dcell = t->cptr + coff;
        } else
            dcell = &t->vram[daddr & t->vram_mask];

        if (d->alpha_en) {
            uint32_t dst = ref_dst_read(dcell, d->dst_dt);
            float dc[4], fs[4], fd[4];

            dc[0] = ((dst >> 16) & 0xff) / 255.0f;
            dc[1] = ((dst >> 8) & 0xff) / 255.0f;
            dc[2] = (dst & 0xff) / 255.0f;
            dc[3] = (dst >> 24) / 255.0f;
            if (d->bsrc == 0xb || d->bsrc == 0xc) {
                float sa = (d->bsrc == 0xb) ? col[3] : 1.0f - col[3];
                fs[0] = fs[1] = fs[2] = fs[3] = sa;
                fd[0] = fd[1] = fd[2] = fd[3] = 1.0f - sa;
            } else {
                ref_blend_factor(d->bsrc, col, dc, fs);
                ref_blend_factor(d->bdst, col, dc, fd);
            }
            for (int c = 0; c < 4; c++) {
                float v = (d->bfcn & 2) ? col[c] * fs[c] - dc[c] * fd[c]
                                        : col[c] * fs[c] + dc[c] * fd[c];
                if (d->bfcn & 1)
                    v = (float) (lrintf(v * 255.0f) & 0xff) / 255.0f;
                else
                    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                col[c] = v;
            }
        }

        uint32_t out = ((uint32_t) (col[3] * 255.0f + 0.5f) << 24)
                     | ((uint32_t) (col[0] * 255.0f + 0.5f) << 16)
                     | ((uint32_t) (col[1] * 255.0f + 0.5f) << 8)
                     | (uint32_t) (col[2] * 255.0f + 0.5f);

        if (d->dst_dt == 6) {
            if (d->wmask != 0xffffffffu)
                out = (out & d->wmask) | (*(uint32_t *) dcell & ~d->wmask);
            *(uint32_t *) dcell = out;
        } else {
            uint32_t a = out >> 24;
            uint32_t r = (out >> 16) & 0xff;
            uint32_t g = (out >> 8) & 0xff;
            uint32_t b = out & 0xff;
            if (d->dither) {
                uint32_t bay = ref_bayer4[py & 3][px & 3];
                if (d->dst_dt == 15)
                    a = ref_dq(a, bay);
                r = ref_dq(r, d->dst_dt == 15 ? bay : bay >> 1);
                g = ref_dq(g, d->dst_dt == 15 ? bay : bay >> (d->dst_dt == 3 ? 1 : 2));
                b = ref_dq(b, d->dst_dt == 15 ? bay : bay >> 1);
            }
            uint32_t raw;
            if (d->dst_dt == 3)
                raw = ((a << 8) & 0x8000) | ((r << 7) & 0x7c00)
                    | ((g << 2) & 0x03e0) | (b >> 3);
            else if (d->dst_dt == 15)
                raw = ((a << 8) & 0xf000) | ((r << 4) & 0x0f00)
                    | (g & 0x00f0) | (b >> 4);
            else
                raw = ((r << 8) & 0xf800) | ((g << 3) & 0x07e0) | (b >> 3);
            uint32_t m   = d->wmask & 0xffff;
            if (m != 0xffff)
                raw = (raw & m) | (*(uint16_t *) dcell & ~m);
            *(uint16_t *) dcell = (uint16_t) raw;
        }
        if (rx0 < 0)
            rx0 = px;
        rx1 = px;

        if (d->z_en && d->z_wr) {
            if (d->zbpp == 2)
                *(uint16_t *) zcell = (uint16_t) zi;
            else {
                uint32_t *zp = (uint32_t *) zcell;
                *zp = (*zp & ~(d->zmax << d->zshift)) | (zi << d->zshift);
            }
        }
    }
    return ((uint64_t) (uint32_t) rx1 << 32) | (uint32_t) rx0;
}

#define VRAM_SZ (1u << 20)
#define ARENA_SZ 65536u
#define TEXA_SZ (1u << 23) /* a full 1024x1024 ARGB8888 chain (5.33 MB) under
                              a mask that sees every address bit it uses */

/* Texel fetches mask the start address only; the 1-3 tail bytes of a
   wide read (RGB888, DXT alpha words) run past the mask. Production
   vram is alloced with a 4KB zeroed guard (svga_init: memsize + 4096)
   so every lane reads the same guard bytes there. Without the same
   guard here, a mask-top fetch reads past the array into whatever
   object follows -- DIFFERENT per lane -- and the oracle diverges on
   self-sampled wrap-straddling rows. Guards are never written (all
   writes are masked/bounded), so they stay zero on both sides. */
#define GUARD_SZ 4096u
static uint8_t vram_j[VRAM_SZ + GUARD_SZ], vram_r[VRAM_SZ + GUARD_SZ];
static uint8_t zar_j[ARENA_SZ], zar_r[ARENA_SZ];
static uint8_t car_j[ARENA_SZ], car_r[ARENA_SZ];
static uint8_t  texarena[TEXA_SZ + GUARD_SZ]; /* read-only during runs: shared */
static uint32_t g_texpal[256];     /* CI4/CI8 palette, random ARGB */
static uint8_t  g_fog_table[256];  /* table-fog factors, random per row */

/* chroma-key mask axis, shared by every keyed phase (converted-texel
   space). 0: vacuous compare -- fn 3 (the code-1 decode) rejects all,
   fn 2 rejects none. The last three are what r3d_ck_to_argb makes of
   an all-ones raw mask for 565, 1555 and 4444 texels: each channel is truncated and its low
   bits are clear. */
static const uint32_t g_ckmsks[] = {
    0xff000000u, 0x00ff0000u, 0x000000ffu,
    0x00080000u, 0x80808080u, 0xffffffffu,
    0x00000000u,
    0x00f8fcf8u, 0xfff8f8f8u, 0xf0f0f0f0u
};
#define N_CKMSKS (sizeof(g_ckmsks) / sizeof(g_ckmsks[0]))

static uint32_t rngs = 0x12345678;
/* LCG state, output mixed (lowbias32): the raw state's bit k has period
   2^(k+1), so a bare `rngs & 1` alternates and any two low-bit choices a
   fixed number of draws apart are locked together. */
static uint32_t
rng_mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
static uint32_t rng(void) { rngs = rngs * 1664525u + 1013904223u; return rng_mix(rngs); }
/* Table-fog contents use an independent random stream so enabling table
   fog does not change the draw-state and row-input stream. */
static uint32_t fog_rngs = 0xf06ab1e5;
static uint32_t fog_rng(void) { fog_rngs = fog_rngs * 1664525u + 1013904223u; return rng_mix(fog_rngs); }
/* independent stream for the secondary stage's own W (the g_sec_w knob
   and the a2rhw lanes), for the same reason */
static uint32_t secw_rngs = 0x5ec0e1d7;
static uint32_t secw_rng(void) { secw_rngs = secw_rngs * 1664525u + 1013904223u; return rng_mix(secw_rngs); }

/* Every shard visits the same row stream. Only the selected row executes;
   discarded buffer contents advance the linear recurrence by their exact
   draw count, so later states and row inputs keep the same seed. */
static unsigned shard_index, shard_count = 1;
static uint64_t shard_row;

static void
rng_advance(uint32_t *state, uint32_t count)
{
    uint32_t mul = 1664525u, add = 1013904223u;
    uint32_t acc_mul = 1, acc_add = 0;

    while (count) {
        if (count & 1) {
            acc_add = acc_add * mul + add;
            acc_mul *= mul;
        }
        add *= mul + 1;
        mul *= mul;
        count >>= 1;
    }
    *state = *state * acc_mul + acc_add;
}

static int
shard_init(void)
{
    const char *env = getenv("JHT_SHARD");
    char *end;
    unsigned long k, n;

    if (!env)
        return 0;
    if (*env < '0' || *env > '9')
        goto invalid;
    k = strtoul(env, &end, 10);
    if (*end != '/' || end[1] < '0' || end[1] > '9')
        goto invalid;
    n = strtoul(end + 1, &end, 10);
    if (*end || !n || n > 2147483647ul || k >= n)
        goto invalid;
    shard_index = (unsigned) k;
    shard_count = (unsigned) n;
    return 0;
invalid:
    fprintf(stderr, "invalid JHT_SHARD: expected k/N with 0 <= k < N\n");
    return -1;
}

static float
frand(float lo, float hi)
{
    return lo + (hi - lo) * (float) (rng() & 0xffffff) / 16777216.0f;
}

static float
frand_secw(float lo, float hi)
{
    return lo + (hi - lo) * (float) (secw_rng() & 0xffffff) / 16777216.0f;
}

static uint64_t total_rows = 0, total_fail = 0;

#if !JHT_EXEC_A64
/* Ref-only oracle checksum (FNV-1a over every row's return value and
   post-row buffers). Deterministic per seed; FMA contraction or any
   toolchain float divergence in the reference changes it. */
static uint64_t ref_ck = 0xcbf29ce484222325ull;
static uint64_t rows_refonly = 0;

/* Fold only the reference's writes: in ref-only mode the jit-side buffers
   keep the pre-row state (fn never runs), so post-vs-pre word diffs are
   exactly the oracle's write set (offset + value, order-sensitive). */
static uint64_t
ck_diff(uint64_t h, const uint8_t *post, const uint8_t *pre, size_t n)
{
    const uint64_t *a = (const uint64_t *) post;
    const uint64_t *b = (const uint64_t *) pre;

    for (size_t i = 0; i < n / 8; i++)
        if (a[i] != b[i]) {
            h ^= i;
            h *= 0x100000001b3ull;
            h ^= a[i];
            h *= 0x100000001b3ull;
        }
    return h;
}
#endif

/* SoA lane-divergence phase: short rows (pure-tail through 1-3 vector
   groups, every remainder length) and rows that straddle the vram wrap
   mask (group bail path). Z/coverage divergence inside a group comes
   from the usual random buffer contents and edge crossings. */
/* Sampling the render target needs separate texture contexts: each side
   must read its own destination buffer to expose pixel-order differences.
   JHT_RTT binds every texture slot to that buffer for each row. The default
   static texarena is disjoint from both destinations, so random inputs
   alone cannot exercise this aliasing. The switch is off by default to
   keep the ordinary fuzz stream deterministic. */
static r3d_texctx_t g_tctx_ref;
static int          g_rtt;

static int g_soa_short;
static int g_sec_w = -1; /* the secondary stage's W in a dual state: -1 =
                            follow the primary stage (same perspective
                            enable, primary W), as every driver seen does
                            and as a zeroed state means; otherwise bit 0
                            = the perspective enable differs, bit 1 =
                            SEC_SRC_SEL_W (the context's a2rhw lanes) */
static float g_huge_s; /* nonzero: every row's first ST set is this S at all
                          three vertices, for coordinates past 2^31 texels */
static int g_bigpx; /* force screen x into 500..949 instead of <64 */
static int g_zov;   /* force color/z row adjacency (both unstaged, zrow within
                       +/-1KB of drow): drives the row-aliasing guard */

/* generate + fuzz one draw state: random plausible rows, random staging */
static int
run_config(uint8_t *code, const rage128_draw_state_t *d, int iters, size_t ci)
{
    rage128_draw_state_t d_rtt, d_ax;

    if (g_rtt && g_real_tex && (d->tex_en || d->sec_en)) {
        /* Render-target texture slots alias the destination rows. Set
           soa_selftex as the device does so the modeled generator preserves
           scalar pixel order instead of gathering a vector group first. */
        d_rtt             = *d;
        d_rtt.soa_selftex = 1;
        d                 = &d_rtt;
    }
    /* Match the device's alpha-bearing formats before mirroring the
       resolved mask into both sampler contexts. */
    d_ax = *d;
    for (int st = 0; st < 2; st++) {
        uint32_t dt = d_ax.sh[st].dt;
        if (!(dt == 0 || dt == 3 || dt == 6 || dt == 9 || dt == 14 || dt == 15))
            d_ax.sh[st].aone = 0;
    }
    /* the secondary stage's W, from the g_sec_w knob (see it) */
    d_ax.sec_persp_diff = d_ax.sec_en && g_sec_w >= 0 && (g_sec_w & 1);
    d_ax.sel_w          = d_ax.sec_en && g_sec_w >= 0 && (g_sec_w & 2);
    d = &d_ax;
    g_helper_ds = d;
    g_tctx.sd0.amask     = (d->sh[0].aone) ? 0xff000000u : 0u;
    g_tctx.sd1.amask     = (d->sh[1].aone) ? 0xff000000u : 0u;
    g_tctx_ref.sd0.amask = g_tctx.sd0.amask;
    g_tctx_ref.sd1.amask = g_tctx.sd1.amask;
    if (!r128_jit_arm64_can(d)) {
        printf("case %zu: gate rejected (unexpected)\n", ci);
        return -1;
    }
    pthread_jit_write_protect_np(0);
    int len = getenv("JHT_NOSOA") ? r128_jit_arm64_generate_1(code, d, 1)
                                   : r128_jit_arm64_generate(code, d);
    pthread_jit_write_protect_np(1);
    if (len <= 0) {
        printf("case %zu: generate failed\n", ci);
        return -1;
    }
    __builtin___clear_cache((char *) code, (char *) code + len);

    int can_exec = JHT_EXEC_A64; /* arm64 block runs on an arm64 host */
#if JHT_EXEC_X64
    /* x86-64 host: re-generate with the x64 backend when its gate covers
       the state; the block in `code` is then native and comparable.
       Gate-rejected states (unported slices) stay reference-only. */
    can_exec = r128_jit_x64_can(d);
#if JHT_WIN64_THUNK
    /* forced-Win64 leaf-only: helper-call shapes stay reference-only */
    if (can_exec && (d->tex_en || d->sec_en)
        && !(r128_x64_texinline_can(d) && !(d->fog_en && d->fog_table_en)))
        can_exec = 0;
#endif
    if (can_exec) {
        int xlen = getenv("JHT_NOSOA") ? r128_jit_x64_generate_1(code, d, 1)
                                        : r128_jit_x64_generate(code, d);

        if (xlen <= 0) {
            printf("case %zu: x64 generate failed\n", ci);
            return -1;
        }
        if (getenv("JHT_XLEN"))
            printf("case %zu: x64 block %d bytes\n", ci, xlen);
    }
#endif
    r128_jit_span_fn fn = (r128_jit_span_fn) (void *) code;

    for (int it = 0; it < iters; it++) {
        r128_jit_tri_t tj, tr;
        int execute = shard_row++ % shard_count == shard_index;
        int staged_z = (d->z_en
                        || (d->sten_on && d->fog_en && d->fog_table_en))
            && (rng() & 1);
        int staged_c = rng() & 1;

        if (g_zov) /* aliasing needs both rows in the shared vram */
            staged_c = staged_z = 0;
        uint32_t texseed = rng();

        memset(&tj, 0, sizeof(tj));
        tj.vram_mask = VRAM_SZ - 1;
        tj.x0        = g_bigpx ? (int32_t) (500 + rng() % 450)
                               : (int32_t) (rng() % 64);
        tj.x1        = tj.x0 + (int32_t) (rng() % 200);
        if (g_soa_short)
            tj.x1 = tj.x0 + (int32_t) (rng() % 13);
        /* Realistic barycentric inputs: edge functions of a real
           triangle satisfy e0+e1+e2 == area (minus the 0..3 fill-rule
           bias) with gradients summing to 0 and invs = 1/area, so
           weights can never sum past 1 -- the invariant the dither
           path's no-bleed argument rests on. */
        int64_t area = (int64_t) ((rng() % 2000000) + 1000);

        tj.e0dxi = (int64_t) (int32_t) (rng() % 20000) - 10000;
        tj.e1dxi = (int64_t) (int32_t) (rng() % 20000) - 10000;
        tj.e2dxi = -(tj.e0dxi + tj.e1dxi);
        tj.invs  = 1.0f / (float) area;
        tj.dZdx  = ((double) (int32_t) rng() / 4e12);
        for (int k = 0; k < 4; k++) {
            tj.vca[k] = (float) (rng() & 0xff) / 255.0f;
            tj.vcb[k] = (float) (rng() & 0xff) / 255.0f;
            tj.vcc[k] = (float) (rng() & 0xff) / 255.0f;
        }
        if (g_real_tex) {
            /* fresh per-row interpolants; descriptors stay per-config.
               ST magnitudes bounded so s*lw*4096 stays deep inside int32
               (the float->int converts saturate identically anyway, but
               the C source's casts are UB there -- stay in-range). */
            float *f = &g_tctx.sta;

            for (int k = 0; k < 12; k++)
                f[k] = frand(-8.0f, 8.0f);
            if (g_huge_s != 0.0f)
                g_tctx.sta = g_tctx.stb = g_tctx.stc = g_huge_s;
            g_tctx.arhw = (rng() % 10) ? frand(0.05f, 3.0f) : 0.0f;
            g_tctx.brhw = (rng() % 10) ? frand(0.05f, 3.0f) : 0.0f;
            g_tctx.crhw = (rng() % 10) ? frand(0.05f, 3.0f) : 0.0f;
            g_tctx.dSdx = frand(-2.0f, 2.0f);
            g_tctx.dSdy = frand(-2.0f, 2.0f);
            g_tctx.dTdx = frand(-2.0f, 2.0f);
            g_tctx.dTdy = frand(-2.0f, 2.0f);
            g_tctx.dWdx = frand(-0.2f, 0.2f);
            g_tctx.dWdy = frand(-0.2f, 0.2f);
            g_tctx.dS2dx = frand(-2.0f, 2.0f);
            g_tctx.dS2dy = frand(-2.0f, 2.0f);
            g_tctx.dT2dx = frand(-2.0f, 2.0f);
            g_tctx.dT2dy = frand(-2.0f, 2.0f);
            /* the secondary stage's own W (rhw2 under SEC_SRC_SEL_W):
               its own values when a fixture selects it, the primary
               W otherwise, as the device fills the context */
            if (g_sec_w >= 0 && (g_sec_w & 2)) {
                g_tctx.a2rhw = (secw_rng() % 10) ? frand_secw(0.05f, 3.0f) : 0.0f;
                g_tctx.b2rhw = (secw_rng() % 10) ? frand_secw(0.05f, 3.0f) : 0.0f;
                g_tctx.c2rhw = (secw_rng() % 10) ? frand_secw(0.05f, 3.0f) : 0.0f;
                g_tctx.dW2dx = frand_secw(-0.2f, 0.2f);
                g_tctx.dW2dy = frand_secw(-0.2f, 0.2f);
            } else {
                g_tctx.a2rhw = g_tctx.arhw;
                g_tctx.b2rhw = g_tctx.brhw;
                g_tctx.c2rhw = g_tctx.crhw;
                g_tctx.dW2dx = 0.0f;
                g_tctx.dW2dy = 0.0f;
            }
            if ((rng() & 15) == 0) {
                /* degenerate gradients: rho2 == 0 -> the lod = -1000
                   arm (needs dW zeroed too under persp) */
                g_tctx.dSdx = g_tctx.dSdy = 0.0f;
                g_tctx.dTdx = g_tctx.dTdy = 0.0f;
                g_tctx.dWdx = g_tctx.dWdy = 0.0f;
                g_tctx.dW2dx = g_tctx.dW2dy = 0.0f;
            }
            tj.texctx = &g_tctx;
        } else
            tj.texctx = &texseed;
        for (int k = 0; k < 3; k++) {
            tj.fog[k] = (float) (rng() & 0x1ff) / 255.0f - 0.5f; /* out-of-range too */
            tj.spa[k] = (float) (rng() & 0xff) / 255.0f;
            tj.spb[k] = (float) (rng() & 0xff) / 255.0f;
            tj.spc[k] = (float) (rng() & 0xff) / 255.0f;
        }
        /* The block reads the runtime fog-table pointer, so contents can change
           without recompiling. A separate random stream fills a fresh table
           per row without changing draw-state and row inputs. */
        if (execute)
            for (int k = 0; k < 256; k++)
                g_fog_table[k] = (uint8_t) fog_rng();
        else
            rng_advance(&fog_rngs, 256);
        tj.fog_table = g_fog_table;
        if (d->stip_en) {
            /* One compiled state serves triangles with changing patterns.
               Empty, full, random and single-bit rows expose stale pattern
               constants and the destination tile's reversed bit order. */
            rage128_raster_state_t *rs = &g_stipple_rs[it & 1];

            for (int k = 0; k < 32; k++)
                rs->stipple[k] = (it & 3) == 0 ? 0
                    : (it & 3) == 1 ? UINT32_MAX
                    : (it & 3) == 2 ? rng() : 1u << ((it + k) & 31);
            g_tctx.rs = g_tctx_ref.rs = rs;
            tj.texctx = &g_tctx;
        }
        uint32_t drow = (rng() % (VRAM_SZ / 2)) & ~3u;
        uint32_t zrow = (rng() % (VRAM_SZ / 2)) & ~3u;

        if (g_zov) {
            /* zrow lands within +/-1KB of drow, covering overlaps, straddles
               and near misses in both directions. Only g_zov consumes the
               extra random draw, preserving the ordinary row stream. */
            int64_t zr = (int64_t) drow + (int32_t) (rng() % 2048) - 1024;

            if (zr < 0)
                zr = 0;
            if (zr >= VRAM_SZ / 2)
                zr = VRAM_SZ / 2 - 4;
            zrow = (uint32_t) zr & ~3u;
        }

        if (g_soa_short && !staged_c && (rng() & 3) == 0)
            drow = (VRAM_SZ - 4 - (rng() % 96)) & ~3u; /* wrap mid-row */
        if (g_soa_short && !staged_z && (rng() & 3) == 0)
            zrow = (VRAM_SZ - 4 - (rng() % 96)) & ~3u;

        if (staged_c) {
            tj.c_base = drow;                 /* row starts inside arena */
            tj.c_lim  = 1 + (rng() % ARENA_SZ);
        }
        if (staged_z) {
            tj.z_base = zrow;
            tj.z_lim  = 1 + (rng() % ARENA_SZ);
        }

        if (execute) {
            for (uint32_t k = 0; k < VRAM_SZ; k += 4)
                *(uint32_t *) &vram_j[k] = rng();
            memcpy(vram_r, vram_j, VRAM_SZ);
            for (uint32_t k = 0; k < ARENA_SZ; k += 4) {
                *(uint32_t *) &zar_j[k] = rng();
                *(uint32_t *) &car_j[k] = rng();
            }
            memcpy(zar_r, zar_j, ARENA_SZ);
            memcpy(car_r, car_j, ARENA_SZ);
        } else
            rng_advance(&rngs, (VRAM_SZ + 2 * ARENA_SZ) / 4);

        int64_t e0 = (int64_t) (rng() % (uint32_t) area) - area / 8;
        int64_t e1 = (int64_t) (rng() % (uint32_t) area) - area / 8;
        int64_t e2 = area - e0 - e1 - (int64_t) (rng() % 4); /* fill-rule bias */
        double  zl = (double) (int32_t) (rng() % 3000) / 2000.0 - 0.2;

        if (d->fog_en && d->fog_table_en && !d->z_en) {
            /* Z-off table fog follows the serial double DDA through
               clamp endpoints, nonfinite inputs and fractional entries.
               Select fixed probes without changing other states' RNG. */
            static const double depth[][2] = {
                { 0.0, 0.125 }, { 1.0, -0.125 },
                { -0.25, 0.125 }, { 1.25, -0.125 },
                { NAN, 0.001 }, { INFINITY, -INFINITY },
                { -INFINITY, INFINITY }, { 0.5, NAN },
                { 0x1p-1074, 0x1p-1074 }, { 0.5, 0x1p-54 },
                { 0x1.fffffffffffffp-1, 0.0 }, { 1.0 / 255.0, 0x1p-40 }
            };
            unsigned probe = (unsigned) it % 16u;

            if (probe < sizeof(depth) / sizeof(depth[0])) {
                zl = depth[probe][0];
                tj.dZdx = depth[probe][1];
            }
        }

        if (g_rtt && g_real_tex) {
            for (int sl = 0; sl <= 10; sl++) {
                g_tctx.sd0.slot[sl].texbase = vram_j;
                g_tctx.sd0.slot[sl].base    = drow;
                g_tctx.sd0.slot[sl].mask    = VRAM_SZ - 1;
                g_tctx.sd1.slot[sl].texbase = vram_j;
                g_tctx.sd1.slot[sl].base    = drow;
                g_tctx.sd1.slot[sl].mask    = VRAM_SZ - 1;
            }
            g_tctx_ref = g_tctx;
            for (int sl = 0; sl <= 10; sl++) {
                g_tctx_ref.sd0.slot[sl].texbase = vram_r;
                g_tctx_ref.sd1.slot[sl].texbase = vram_r;
            }
        }

        tr = tj;
        tj.vram = vram_j; tj.zptr = staged_z ? zar_j : NULL; tj.cptr = staged_c ? car_j : NULL;
        tr.vram = vram_r; tr.zptr = staged_z ? zar_r : NULL; tr.cptr = staged_c ? car_r : NULL;
        /* mismatch forensics (TEXPROBE): the span's local-VRAM color and Z
           words before the run, so a differing cell can be read as
           "written by one side" vs "seeded and untouched" */
        uint32_t seed_c[64], seed_z[64];
        int      probe_cells = getenv("TEXPROBE") && tj.x1 - tj.x0 < 64;

        if (probe_cells)
            for (int32_t px = tj.x0; px <= tj.x1; px++) {
                uint32_t ca = (drow + (uint32_t) px * (uint32_t) d->bpp) & tj.vram_mask;
                uint32_t za = (zrow + (uint32_t) px * (uint32_t) d->zbpp) & tj.vram_mask;

                seed_c[px - tj.x0] = *(uint32_t *) &vram_j[ca & ~3u];
                seed_z[px - tj.x0] = *(uint32_t *) &vram_j[za & ~3u];
            }
        if (g_rtt && g_real_tex)
            tr.texctx = &g_tctx_ref;

        int32_t py = (int32_t) (rng() % 1024);

        if (!execute)
            continue;

        uint64_t rj = can_exec
            ? JHT_SPAN_CALL(fn, &tj, e0, e1, e2, zl, drow, zrow, py) : 0;
        uint64_t rr = ref_span(&tr, d, e0, e1, e2, zl, drow, zrow, py);

        total_rows++;
#if !JHT_EXEC_A64
        if (!can_exec) {
            /* ref-only: fold the oracle's writes into the checksum */
            rows_refonly++;
            ref_ck ^= rr;
            ref_ck *= 0x100000001b3ull;
            ref_ck = ck_diff(ref_ck, vram_r, vram_j, VRAM_SZ);
            ref_ck = ck_diff(ref_ck, zar_r, zar_j, ARENA_SZ);
            ref_ck = ck_diff(ref_ck, car_r, car_j, ARENA_SZ);
            continue;
        }
#endif
        if (rj != rr || memcmp(vram_j, vram_r, VRAM_SZ)
            || memcmp(zar_j, zar_r, ARENA_SZ) || memcmp(car_j, car_r, ARENA_SZ)) {
            total_fail++;
            if (total_fail <= 5) {
                printf("REPLAY e0dxi=%lldLL e1dxi=%lldLL e2dxi=%lldLL invs=%.9g dZdx=%.17g seed=%08x\n"
                       "REPLAY vca={%.9gf,%.9gf,%.9gf,%.9gf} vcb={%.9gf,%.9gf,%.9gf,%.9gf} vcc={%.9gf,%.9gf,%.9gf,%.9gf}\n"
                       "REPLAY e0=%lldLL e1=%lldLL e2=%lldLL zl=%.17g\n",
                       (long long) tj.e0dxi, (long long) tj.e1dxi, (long long) tj.e2dxi,
                       tj.invs, tj.dZdx, texseed,
                       tj.vca[0], tj.vca[1], tj.vca[2], tj.vca[3],
                       tj.vcb[0], tj.vcb[1], tj.vcb[2], tj.vcb[3],
                       tj.vcc[0], tj.vcc[1], tj.vcc[2], tj.vcc[3],
                       (long long) e0, (long long) e1, (long long) e2, zl);
                printf("MISMATCH case=%zu it=%d py=%d x=[%d..%d] drow=%08x zrow=%08x cbase=%08x clim=%u stc=%d stz=%d auxc=%02x rx jit=%016llx ref=%016llx\n",
                       ci, it, py, tj.x0, tj.x1, drow, zrow, tj.c_base,
                       tj.c_lim, staged_c, staged_z, d->aux_cntl,
                       (unsigned long long) rj, (unsigned long long) rr);
                for (uint32_t k = 0; k < VRAM_SZ; k++)
                    if (vram_j[k] != vram_r[k]) {
                        printf("  vram[%06x] px=%d: jitw=%04x refw=%04x\n", k,
                               (int) (((k & ~1u) - drow) / 2),
                               *(uint16_t *) &vram_j[k & ~1u], *(uint16_t *) &vram_r[k & ~1u]);
                        break;
                    }
                for (uint32_t k = 0; k < ARENA_SZ; k++)
                    if (zar_j[k] != zar_r[k]) { printf("  zar[%04x]: jit=%02x ref=%02x\n", k, zar_j[k], zar_r[k]); break; }
                for (uint32_t k = 0; k < ARENA_SZ; k++)
                    if (car_j[k] != car_r[k]) {
                        printf("  car[%04x] px=%d: jitw=%04x refw=%04x\n", k,
                               (int) ((k & ~1u) / 2),
                               *(uint16_t *) &car_j[k & ~1u], *(uint16_t *) &car_r[k & ~1u]);
                        break;
                    }
                if (getenv("TEXPROBE")) {
                    /* mismatch forensics: full tex state + emitted block */
                    printf("PROBE sh0: tsp=%08x dt=%u s3tc=%u cs=%u ct=%u bord=%08x minb=%u mag=%u mipdis=%d top=%d\n",
                           d->sh[0].tsp, d->sh[0].dt, d->sh[0].s3tc, d->sh[0].clamp_s,
                           d->sh[0].clamp_t, d->sh[0].border, d->sh[0].minb, d->sh[0].mag,
                           d->sh[0].mipdis, d->sh[0].top);
                    printf("PROBE sh1: tsp=%08x dt=%u s3tc=%u cs=%u ct=%u bord=%08x minb=%u mag=%u mipdis=%d top=%d\n",
                           d->sh[1].tsp, d->sh[1].dt, d->sh[1].s3tc, d->sh[1].clamp_s,
                           d->sh[1].clamp_t, d->sh[1].border, d->sh[1].minb, d->sh[1].mag,
                           d->sh[1].mipdis, d->sh[1].top);
                    printf("PROBE comb0: comb=%u comba=%u fmsb=%u cfac=%u afac=%u ifac=%u ifaca=%u\n",
                           d->comb[0].comb, d->comb[0].comba, d->comb[0].fmsb,
                           d->comb[0].cfac, d->comb[0].afac, d->comb[0].ifac, d->comb[0].ifaca);
                    printf("PROBE comb1: comb=%u comba=%u fmsb=%u cfac=%u afac=%u ifac=%u ifaca=%u\n",
                           d->comb[1].comb, d->comb[1].comba, d->comb[1].fmsb,
                           d->comb[1].cfac, d->comb[1].afac, d->comb[1].ifac, d->comb[1].ifaca);
                    printf("PROBE misc: tex=%d/%d persp=%d sel=%d lod=%d/%d bias=%.9g ck3d=%d ckc=%d ckfn=%u ck3dm=%08x ck3dc=%08x ckcm=%08x ckcc=%08x cc={%.9g,%.9g,%.9g,%.9g} texwh={%g,%g,%g,%g}\n",
                           d->tex_en, d->sec_en, d->do_persp, d->sec_sel,
                           d->need_lod, d->need_lod2, d->lod_bias,
                           d->ck3d_on, d->ckc_on, d->ckfn,
                           d->ck3d_msk, d->ck3d_clr, d->ckc_msk, d->ckc_clr,
                           d->cc[0], d->cc[1], d->cc[2], d->cc[3],
                           d->texw0, d->texh0, d->texw1, d->texh1);
                    printf("PROBE tctx: st={%g,%g,%g} tt={%g,%g,%g} s2={%g,%g,%g} t2={%g,%g,%g} rhw={%g,%g,%g} dS={%g,%g} dT={%g,%g} dW={%g,%g} dS2={%g,%g} dT2={%g,%g}\n",
                           g_tctx.sta, g_tctx.stb, g_tctx.stc, g_tctx.tta, g_tctx.ttb, g_tctx.ttc,
                           g_tctx.s2a, g_tctx.s2b, g_tctx.s2c, g_tctx.t2a, g_tctx.t2b, g_tctx.t2c,
                           g_tctx.arhw, g_tctx.brhw, g_tctx.crhw,
                           g_tctx.dSdx, g_tctx.dSdy, g_tctx.dTdx, g_tctx.dTdy,
                           g_tctx.dWdx, g_tctx.dWdy, g_tctx.dS2dx, g_tctx.dS2dy,
                           g_tctx.dT2dx, g_tctx.dT2dy);
                    {
                        FILE *bf = fopen("mismatch_block.bin", "wb");

                        if (bf) {
                            fwrite(code, 1, 16384, bf);
                            fclose(bf);
                            printf("PROBE block dumped to mismatch_block.bin\n");
                        }
                    }
                    if (probe_cells)
                        for (int32_t px = tj.x0; px <= tj.x1; px++) {
                            uint32_t ca = (drow + (uint32_t) px * (uint32_t) d->bpp) & tj.vram_mask;
                            uint32_t za = (zrow + (uint32_t) px * (uint32_t) d->zbpp) & tj.vram_mask;

                            printf("PROBE px=%d c@%06x seed=%08x jit=%08x ref=%08x | z@%06x seed=%08x jit=%08x ref=%08x\n",
                                   px, ca, seed_c[px - tj.x0],
                                   *(uint32_t *) &vram_j[ca & ~3u], *(uint32_t *) &vram_r[ca & ~3u],
                                   za, seed_z[px - tj.x0],
                                   *(uint32_t *) &vram_j[za & ~3u], *(uint32_t *) &vram_r[za & ~3u]);
                        }
                }
            }
        }
    }
    return 0;
}

/* case matrix: dt x z-config x dither x blend x tex x atest x staging */
typedef struct case_ent {
    uint32_t dt; int z_en, z_wr; uint32_t zfn; int zbpp; uint32_t zmax;
    int zshift; int dith; int ab; uint32_t bsrc, bdst, bfcn;
    int tex, sec, at; uint32_t atfn, atref;
    int spec, fogv, fogt;
} case_ent;

static void
mk_state(rage128_draw_state_t *d, const case_ent *c)
{
    memset(d, 0, sizeof(*d));
    d->draw_ok = 1;
    d->dst_dt  = c->dt;
    d->bpp     = (c->dt == 6) ? 4 : 2;
    d->wmask   = (c->dt == 6) ? 0xffffffffu : 0xffff;
    d->z_en    = c->z_en;
    d->z_wr    = c->z_wr;
    d->zfn     = c->zfn;
    d->zbpp     = c->zbpp;
    d->zmax     = c->zmax;
    d->zshift   = c->zshift;
    d->dither   = c->dith;
    d->alpha_en = c->ab;
    d->bsrc     = c->bsrc;
    d->bdst     = c->bdst;
    d->bfcn     = c->bfcn;
    d->tex_en   = c->tex;
    d->sec_en   = c->sec;
    /* Helper-call tests use dt 10, which the modeled inline sampler does
       not decode. mk_tex_stage supplies supported formats for the separate
       inline-family phase, so both call glue and sampling are checked. */
    d->sh[0].dt = 10;
    d->sh[1].dt = 10;
    d->atest_en  = c->at;
    d->atest_fn  = c->atfn;
    d->atest_ref = c->atref;
    d->spec_en   = c->spec;
    d->fog_en    = c->fogv;
    d->fog_table_en = c->fogt;
    d->fogr      = 0.25f;
    d->fogg      = 0.75f;
    d->fogb      = 0.0f;
}

#if JHT_EXEC_A64 || JHT_EXEC_X64
/* Finite vertex fog values overflow and cancel in selected lanes. The
   vector clamp must keep NaN factors to match the reference lane. */
static int
fog_nan_vectors(uint8_t *code)
{
    rage128_draw_state_t d;
    case_ent ce = { .dt = 6, .fogv = 1 };
    static const float fog[][3] = {
        { 0x1.fffffep127f, -0x1.fffffep127f, 0.0f },
        { 0.5f, 0.0f, -1.0f }
    };
    int failures = 0;

    mk_state(&d, &ce);
    pthread_jit_write_protect_np(0);
#if JHT_EXEC_X64
    int len = r128_jit_x64_generate(code, &d);
#else
    int len = r128_jit_arm64_generate(code, &d);
#endif
    pthread_jit_write_protect_np(1);
    if (len <= 0)
        return -1;
    __builtin___clear_cache((char *) code, (char *) code + len);

    for (size_t vi = 0; vi < sizeof(fog) / sizeof(fog[0]); vi++) {
        uint32_t dst_j[16], dst_r[16];
        r128_jit_tri_t tj = {0}, tr;
        int nan_lanes = 0, finite_lanes = 0;

        memset(dst_j, 0xa5, sizeof(dst_j));
        memcpy(dst_r, dst_j, sizeof(dst_j));
        tj.vram = (uint8_t *) dst_j;
        tj.vram_mask = sizeof(dst_j) - 1;
        tj.x1 = 6;
        tj.e0dxi = tj.e1dxi = 1;
        tj.invs = 1.0f;
        tj.vcc[0] = 0.75f; tj.vcc[1] = 0.25f;
        tj.vcc[2] = 0.5f; tj.vcc[3] = 1.0f;
        memcpy(tj.fog, fog[vi], sizeof(tj.fog));
        for (int px = 0; px <= tj.x1; px++) {
            float w = (float) (px + 1);
            float f = w * tj.fog[0] + w * tj.fog[1] + tj.fog[2];

            nan_lanes += isnan(f);
            finite_lanes += isfinite(f);
        }
        if (!vi && (nan_lanes != 6 || finite_lanes != 1))
            return -1;
        tr = tj; tr.vram = (uint8_t *) dst_r;
        r128_jit_span_fn fn = (r128_jit_span_fn) (void *) code;
        uint64_t rj = JHT_SPAN_CALL(fn, &tj, 1, 1, 1, 0.0, 0, 0, 0);
        uint64_t rr = ref_span(&tr, &d, 1, 1, 1, 0.0, 0, 0, 0);
        int bad = rj != rr || memcmp(dst_j, dst_r, sizeof(dst_j));

        total_rows++; total_fail += bad; failures += bad;
        printf("soa vertex-fog %s: nan=%d finite=%d jit=%08x ref=%08x %s\n",
               vi ? "finite-clamps" : "mixed-NaN", nan_lanes, finite_lanes,
               dst_j[1], dst_r[1], bad ? "FAIL" : "PASS");
    }
    return failures ? -1 : 0;
}
#endif

/* One-pixel triangles (area2 = 256, invs = 1/256) whose edge seeds carry
   the fill-rule bias on one or two edges. The bias decides coverage only,
   so a constant attribute at its maximum must land unscaled: the weights
   come from the unbiased seeds, which sum to the area. With the biased
   seeds the weights would sum to 255/256 or 254/256 and the pixel would
   read 254 or 253 in place of 255. The covered pixel walks through every
   lane of the first two 4-pixel groups and the scalar tail, and the
   states cover the plain color path, specular, vertex fog and the
   texture-stage call with specular and vertex fog after it, which
   recomputes the weights after the call. */
static int
bias_vectors(uint8_t *code)
{
    static const case_ent ces[] = {
        { .dt = 6 },
        { .dt = 6, .spec = 1 },
        { .dt = 6, .fogv = 1 },
        { .dt = 6, .tex = 1, .spec = 1, .fogv = 1 },
        { .dt = 4, .spec = 1, .fogv = 1 },
    };
    /* unbiased seeds at the covered pixel, summing to 256, and the
       biased edges: three with one, three with two */
    static const int64_t u[6][3] = {
        { 64, 64, 128 }, { 128, 64, 64 }, { 64, 128, 64 },
        { 100, 100, 56 }, { 56, 100, 100 }, { 100, 56, 100 },
    };
    static const int64_t b[6][3] = {
        { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 },
        { 1, 1, 0 }, { 0, 1, 1 }, { 1, 0, 1 },
    };
    int failures = 0;

    for (size_t ci = 0; ci < sizeof(ces) / sizeof(ces[0]); ci++) {
        rage128_draw_state_t d;
        int len;

#if JHT_WIN64_THUNK
        /* forced-Win64 leaf-only: the helper-call block cannot run here */
        if (ces[ci].tex)
            continue;
#endif
        mk_state(&d, &ces[ci]);
        d.fogr = d.fogg = d.fogb = 0.0f;
#if JHT_EXEC_X64
        if (!r128_jit_x64_can(&d))
            return -1;
        len = r128_jit_x64_generate(code, &d);
#else
        if (!r128_jit_arm64_can(&d))
            return -1;
        pthread_jit_write_protect_np(0);
        len = r128_jit_arm64_generate(code, &d);
        pthread_jit_write_protect_np(1);
        if (len > 0)
            __builtin___clear_cache((char *) code, (char *) code + len);
#endif
        if (len <= 0)
            return -1;
        r128_jit_span_fn fn = (r128_jit_span_fn) (void *) code;
        int case_fail = 0, ref_bad = 0;

        for (int vi = 0; vi < 6; vi++)
            for (int cov = 0; cov < 12; cov++) {
                uint32_t       dst_j[16], dst_r[16], seed = 0x5eed0000u + (uint32_t) (vi * 12 + cov);
                r128_jit_tri_t tj = {0}, tr;
                /* neighbors of the covered pixel fail one edge each */
                int64_t        e0 = u[vi][0] - b[vi][0] - (int64_t) cov * 300;
                int64_t        e1 = u[vi][1] - b[vi][1] + (int64_t) cov * 300;
                int64_t        e2 = u[vi][2] - b[vi][2];

                memset(dst_j, 0, sizeof(dst_j));
                memcpy(dst_r, dst_j, sizeof(dst_j));
                tj.vram      = (uint8_t *) dst_j;
                tj.vram_mask = sizeof(dst_j) - 1;
                tj.x1        = 11;
                tj.e0dxi     = 300;
                tj.e1dxi     = -300;
                tj.e2dxi     = 0;
                tj.invs      = 1.0f / 256.0f;
                tj.e0b       = b[vi][0];
                tj.e1b       = b[vi][1];
                tj.e2b       = b[vi][2];
                tj.texctx    = &seed;
                tj.fog_table = g_fog_table;
                for (int k = 0; k < 4; k++) {
                    float c = ces[ci].spec ? (k == 3 ? 1.0f : 0.0f) : 1.0f;

                    tj.vca[k] = tj.vcb[k] = tj.vcc[k] = c;
                }
                for (int k = 0; k < 3; k++) {
                    tj.spa[k] = tj.spb[k] = tj.spc[k] = 1.0f;
                    tj.fog[k] = 1.0f;
                }
                tr = tj; tr.vram = (uint8_t *) dst_r;
                uint64_t rj = JHT_SPAN_CALL(fn, &tj, e0, e1, e2, 0.0, 0, 0, 0);
                uint64_t rr = ref_span(&tr, &d, e0, e1, e2, 0.0, 0, 0, 0);
                int      bad = rj != rr || memcmp(dst_j, dst_r, sizeof(dst_j));
                uint32_t want = d.bpp == 4 ? 0xffffffffu : 0xffffu;
                uint32_t got  = d.bpp == 4 ? dst_r[cov]
                                           : ((uint16_t *) dst_r)[cov];

                /* the reference itself lands the constant maximum, except
                   through the hashing texture stand-in */
                if (!ces[ci].tex && (got != want || rr != ((uint64_t) cov << 32 | (uint32_t) cov)))
                    ref_bad++;
                total_rows++;
                total_fail += bad;
                case_fail += bad;
                if (bad && case_fail <= 2)
                    printf("bias case %zu vi=%d cov=%d: jit=%08x ref=%08x rx jit=%016llx ref=%016llx\n",
                           ci, vi, cov,
                           d.bpp == 4 ? dst_j[cov] : ((uint16_t *) dst_j)[cov], got,
                           (unsigned long long) rj, (unsigned long long) rr);
            }
        total_rows++;
        total_fail += ref_bad != 0;
        failures += case_fail + (ref_bad != 0);
        printf("bias one-pixel case %zu: rows=72 fail=%d ref_bad=%d %s\n",
               ci, case_fail, ref_bad, (case_fail || ref_bad) ? "FAIL" : "PASS");
    }
    return failures ? -1 : 0;
}

#if JHT_EXEC_A64 || JHT_EXEC_X64
/* Alpha-bearing destinations must match at every byte boundary and
   Bayer phase. Constant colors sweep all bytes and both sides of the
   half-byte rounding boundary; nine pixels exercise two vector groups
   plus a scalar tail, and forced scalar blocks cover the same inputs.
   Whole-buffer comparison also checks preserved mask bits and guards. */
static int
alpha_dst_vectors(uint8_t *code)
{
    static const uint32_t dt[] = { 3, 15 };
    static const uint32_t masks[] = { 0xffff, 0, 0x8000, 0xa5a55a5a };
    static const float bias[] = { 0.0f, -0.5f, -0.499f, 0.499f };
    int failures = 0;

    for (size_t di = 0; di < sizeof(dt) / sizeof(dt[0]); di++)
        for (int dith = 0; dith < 2; dith++)
            for (size_t mi = 0; mi < sizeof(masks) / sizeof(masks[0]); mi++)
                for (int scalar = 0; scalar < 2; scalar++) {
                    rage128_draw_state_t d;
                    case_ent ce = { .dt = dt[di], .dith = dith };
                    int len;

                    mk_state(&d, &ce);
                    d.wmask = masks[mi];
                    if (!r128_jit_state_can(&d) || !r128_jit_soa_can(&d))
                        return -1;
                    pthread_jit_write_protect_np(0);
#if JHT_EXEC_X64
                    len = r128_jit_x64_generate_1(code, &d, scalar);
#else
                    len = r128_jit_arm64_generate_1(code, &d, scalar);
#endif
                    pthread_jit_write_protect_np(1);
                    if (len <= 0)
                        return -1;
                    __builtin___clear_cache((char *) code, (char *) code + len);
                    r128_jit_span_fn fn = (r128_jit_span_fn) (void *) code;

                    for (int v = 0; v < 256; v++)
                        for (size_t bi = 0; bi < sizeof(bias) / sizeof(bias[0]); bi++)
                            for (int py = 0; py < 4; py++) {
                                uint16_t dst_j[32], dst_r[32];
                                r128_jit_tri_t tj = {0}, tr;
                                float color = ((float) v + bias[bi]) / 255.0f;

                                if (color < 0.0f)
                                    color = 0.0f;
                                for (int k = 0; k < 32; k++)
                                    dst_j[k] = (uint16_t) (0xa55a ^ (k * 0x1234));
                                memcpy(dst_r, dst_j, sizeof(dst_j));
                                tj.vram = (uint8_t *) dst_j;
                                tj.vram_mask = sizeof(dst_j) - 1;
                                tj.x0 = v & 3;
                                tj.x1 = tj.x0 + 8;
                                tj.invs = 1.0f;
                                for (int k = 0; k < 4; k++)
                                    tj.vca[k] = color;
                                tr = tj;
                                tr.vram = (uint8_t *) dst_r;
                                uint64_t rj = JHT_SPAN_CALL(fn, &tj, 1, 0, 0, 0.0, 0, 0, py);
                                uint64_t rr = ref_span(&tr, &d, 1, 0, 0, 0.0, 0, 0, py);
                                int bad = rj != rr || memcmp(dst_j, dst_r, sizeof(dst_j));

                                total_rows++;
                                total_fail += bad;
                                if (bad && failures++ < 4)
                                    printf("alpha dst dt=%u dith=%d mask=%08x scalar=%d byte=%d bias=%zu py=%d: FAIL\n",
                                           dt[di], dith, masks[mi], scalar, v, bi, py);
                            }
                }
    printf("alpha destination vectors: %s\n", failures ? "FAIL" : "PASS");
    return failures ? -1 : 0;
}
#endif

/* chroma key drawn mostly from a real decoded texel so compares hit */
static uint32_t
pick_key(uint32_t dt, uint32_t s3tc)
{
    if ((rng() & 3) == 0)
        return rng();
    return rr_texel(texarena, rng() % TEXA_SZ, TEXA_SZ - 1, 4, dt, 0, s3tc, g_texpal, 0, 0);
}

/* randomize one stage's family params (header + combine) */
static void
mk_tex_stage(rage128_draw_state_t *d, int st)
{
    static const uint32_t dts[16]   = { 3, 4, 6, 15, 1, 2, 5, 7,
                                        8, 9, 0, 11, 12, 14, 0, 4 };
    static const uint32_t cmb[][2]  = {
        {3,3}, {1,1}, {3,1}, {1,3}, {0,0}, {3,2}, {1,2}, {0,2},
        {6,0}, {6,1}, {6,2}, {6,3}, {6,7},
        {4,0}, {4,1}, {4,2}, {4,3}, {4,7},
        {2,0}, {2,1}, {2,2}, {2,3}, {2,7},
        {3,7}, {1,7}, {0,7},
        /* inline color ops x alphas */
        {5,3}, {5,4}, {5,5}, {5,6}, {5,7}, {5,14},
        {7,2}, {7,4}, {7,5}, {7,6}, {7,14},
        {14,3}, {14,4}, {14,5}, {14,6}, {14,7}, {14,14},
        /* inline alpha ops x colors */
        {0,4}, {1,5}, {2,6}, {3,14}, {6,4}, {4,5}, {6,6}, {4,14},
        /* blend color operations paired with alpha operations */
        {8,3}, {8,7}, {9,2}, {9,3}, {10,6}, {10,14}, {11,3}, {11,7},
        {12,2}, {12,4}, {13,3}, {13,5}, {15,6}, {15,3},
        /* comba undefined codes (interpreter: -> modulate) */
        {3,8}, {9,9}, {11,13}, {6,15},
    };
    /* operand selects, running-color/alpha weighted (the dominant real
       configs); one undefined code apiece to pin the default decode */
    static const uint32_t cfacs[]  = { 4, 4, 4, 0, 1, 5, 6, 7, 8, 2 };
    static const uint32_t ifacs[]  = { 4, 4, 8, 8, 2, 3, 5, 9, 0 };
    static const uint32_t ifacas[] = { 2, 2, 4, 4, 1, 0 };
    r3d_stage_hdr_t *h  = &d->sh[st];
    r3d_comb_desc_t *cd = &d->comb[st];
    /* 4..1024 wide, 2..1024 high, top 0..10: titles program 256 and 512
       (top 9) on both stages, a 32x2 stage 0, and 1024x1024 / 1024x512
       single-level on stage 0 */
    int              wexp = 2 + (int) (rng() % 9);
    int              hexp = 1 + (int) (rng() % 10);
    int              top  = (int) (rng() % 11);
    const uint32_t  *c    = cmb[rng() % (sizeof(cmb) / sizeof(cmb[0]))];

    h->tsp     = (uint32_t) wexp | ((uint32_t) top << 4) | ((uint32_t) hexp << 8);
    h->clamp_s = rng() % 4;
    h->clamp_t = rng() % 4;
    h->dt      = dts[rng() & 15];
    h->s3tc    = (h->dt == 0) ? rng() % 4 : 0;
    h->border  = (uint32_t) rng();
    h->aone    = h->border >> 31;
    h->minb    = rng() % 8; /* Undocumented minification codes 6/7 use
                               modeled mip-nearest rules on the mip path. */
    h->mag     = rng() % 2;
    h->mipdis  = (int) (rng() % 2);
    { /* Keep the random draws unchanged while JHT_MINB pins minification
          and enables mipmapping for a directed filter sweep. */
        const char *mb = getenv("JHT_MINB");
        if (mb) { h->minb = (uint32_t) strtoul(mb, NULL, 0); h->mipdis = 0; }
    }
    h->top     = top;
    cd->comb  = c[0];
    cd->comba = c[1];
    /* fmsb toggles the MSB equations for color 0/4/5/6 and is a no-op for
       the rest (interpreter ignores it there) -- fuzz both states. */
    cd->fmsb  = rng() & 1;
    cd->cfac  = cfacs[rng() % (sizeof(cfacs) / sizeof(cfacs[0]))];
    cd->ifac  = ifacs[rng() % (sizeof(ifacs) / sizeof(ifacs[0]))];
    cd->ifaca = ifacas[rng() % (sizeof(ifacas) / sizeof(ifacas[0]))];
    cd->afac  = (rng() & 1) ? 7 : 6;
}

/* per-triangle descriptor with every slot pre-resolved, mirroring
   r3d_stage_desc_init + the eager r3d_stage_slot loop (r3d_level_dims
   inlined: pitch == lw, dims halve per slot below top) */
static void
setup_stage_desc(const rage128_draw_state_t *d, int st, r3d_stage_desc_t *sd)
{
    const r3d_stage_hdr_t *h = &d->sh[st];

    memset(sd, 0, sizeof(*sd));
    sd->tsp     = h->tsp;
    sd->clamp_s = h->clamp_s;
    sd->clamp_t = h->clamp_t;
    sd->dt      = h->dt;
    sd->s3tc    = h->s3tc;
    sd->amask   = h->aone ? 0xff000000u : 0u;
    sd->border  = h->border;
    sd->minb    = h->minb;
    sd->mag     = h->mag;
    sd->mipdis  = h->mipdis;
    sd->top     = h->top;
    sd->pal     = g_texpal;
    sd->slot_valid = 0x7ff;
    for (int sl = 0; sl <= h->top && sl <= 10; sl++) {
        int      shift = h->top - sl;
        uint32_t w     = 1u << (h->tsp & 0xf);
        uint32_t hh    = 1u << ((h->tsp >> 8) & 0xf);

        if (shift > 31)
            shift = 31;
        w >>= shift;
        hh >>= shift;
        sd->slot[sl].lw      = w ? w : 1;
        sd->slot[sl].lh      = hh ? hh : 1;
        sd->slot[sl].texbase = texarena;
        sd->slot[sl].base    = rng() % TEXA_SZ;
        sd->slot[sl].mask    = (rng() & 3) ? (TEXA_SZ - 1)
                                           : ((1u << (10 + rng() % 14)) - 1);
    }
}

/* Runtime inputs, so the compiler cannot fold the exceptional
   conversions away. The reference uses the same expressions as
   rr_tex_level. */
__attribute__((noinline)) static uint32_t
weight_ref(float coord, uint32_t dim, int *raw)
{
    float fu = rr_texcoord_fx(coord * (float) dim) - 0.5f;

    *raw = rr_f2i(floorf(fu));
    return rr_f2u((fu - (float) *raw) * 256.0f + 0.5f);
}

typedef struct {
    float    coord[4];
    uint32_t dim[4];
    uint32_t out[4][4]; /* wrapped c0, wrapped c1, weight, packed lerp */
} weight_vec_t;

/* Call the production axis and lerp emitters directly so one exceptional
   lane can rotate without non-finite barycentric inputs contaminating
   its finite neighbors. Both fixed and per-lane mip dimensions are live. */
static int
weight_axis_block(uint8_t *code, const r3d_stage_hdr_t *h, int axis,
                  int perlane, int x64)
{
    if (x64) {
        rage128_draw_state_t d = {0};
        r128_x64_emit_t e = { .base = code };
        float tfx[8] = { 4096, 4096, 4096, 4096,
                        1.0f / 4096, 1.0f / 4096, 1.0f / 4096, 1.0f / 4096 };
        int slots[3] = {
            axis ? R128_X64_SP_SOAT_V0 : R128_X64_SP_SOAT_U0,
            axis ? R128_X64_SP_SOAT_V1 : R128_X64_SP_SOAT_U1,
            axis ? R128_X64_SP_SOAT_WV : R128_X64_SP_SOAT_WU
        };
        int dimreg = axis ? X64_RDX : X64_R10;

        r128_x64_pool_init(&e, &d);
        e.cp_tfx = r128_x64_pool_add(&e, tfx, sizeof(tfx), 16);
        r128_x64_alu_r_imm(&e, 5, 1, X64_RSP, 1032);
        for (int k = 6; k < 16; k++)
            r128_x64_movaps_st(&e, k, X64_RSP, 800 + (k - 6) * 16);
        r128_x64_mov_r_r(&e, 1, X64_R8,
                         R128_X64_ABI_WIN ? X64_RCX : X64_RDI);
        r128_x64_movups_ld(&e, 12, X64_R8, offsetof(weight_vec_t, coord));
        r128_x64_movups_ld(&e, 0, X64_R8, offsetof(weight_vec_t, dim));
        r128_x64_movaps_st(&e, 0, X64_RSP,
                          axis ? R128_X64_SP_SOAT_VLH : R128_X64_SP_SOAT_VLW);
        r128_x64_ld(&e, 0, dimreg, X64_R8, offsetof(weight_vec_t, dim));
        r128_x64_soa_tex_axis(&e, h, axis, 1, perlane, 12, dimreg, 11, 15, 13);
        for (int k = 0; k < 3; k++) {
            r128_x64_movaps_ld(&e, 0, X64_RSP, slots[k]);
            r128_x64_sse_rm(&e, 0, 0x11, 0, X64_R8, offsetof(weight_vec_t, out) + k * 16);
        }
        r128_x64_splat_imm(&e, 11, 0xff000000u, X64_RAX);
        r128_x64_splat_imm(&e, 12, 0xff000001u, X64_RAX);
        r128_x64_soa_lerp(&e, 11, 12, slots[2]);
        r128_x64_sse_rm(&e, 0, 0x11, 11, X64_R8, offsetof(weight_vec_t, out) + 48);
        for (int k = 6; k < 16; k++)
            r128_x64_movaps_ld(&e, k, X64_RSP, 800 + (k - 6) * 16);
        r128_x64_alu_r_imm(&e, 0, 1, X64_RSP, 1032);
        r128_x64_ret(&e);
        return e.overflow ? -1 : e.pos;
    } else {
        r128_a64_emit_t e = { .base = code };
        int slots[3] = {
            axis ? R128_A64_SP_SOA_V0 : R128_A64_SP_SOA_U0,
            axis ? R128_A64_SP_SOA_V1 : R128_A64_SP_SOA_U1,
            axis ? R128_A64_SP_SOA_WV : R128_A64_SP_SOA_WU
        };

        r128_a64_sub_x_imm(&e, 31, 31, 1024);
        r128_a64_str_d(&e, 14, 31, 800);
        r128_a64_str_d(&e, 15, 31, 808);
        r128_a64_ldr_q(&e, axis ? 26 : 3, 0, offsetof(weight_vec_t, coord));
        r128_a64_ldr_q(&e, 15, 0, offsetof(weight_vec_t, dim));
        r128_a64_str_q(&e, 15, 31,
                       axis ? R128_A64_SP_SOA_VLH : R128_A64_SP_SOA_VLW);
        r128_a64_ldr_w(&e, 17, 0, offsetof(weight_vec_t, dim));
        r128_a64_str_w(&e, 17, 31, axis ? R128_A64_TS_LH : R128_A64_TS_LW);
        r128_a64_ucvtf_s_w(&e, 15, 17);
        r128_a64_str_s(&e, 15, 31,
                       axis ? R128_A64_SP_SOA_FLH : R128_A64_SP_SOA_FLW);
        r128_a64_soa_tex_axis(&e, h, axis, perlane, 1);
        for (int k = 0; k < 3; k++) {
            r128_a64_ldr_q(&e, 3, 31, slots[k]);
            r128_a64_str_q(&e, 3, 0, offsetof(weight_vec_t, out) + k * 16);
        }
        r128_a64_mov_w_imm32(&e, 17, 0xff000000u);
        r128_a64_dup_4s_w(&e, 3, 17);
        r128_a64_mov_w_imm32(&e, 17, 0xff000001u);
        r128_a64_dup_4s_w(&e, 26, 17);
        r128_a64_soa_lerp(&e, 3, 26, slots[2], 7, 14, 15, 25, 27);
        r128_a64_str_q(&e, 3, 0, offsetof(weight_vec_t, out) + 48);
        r128_a64_ldr_d(&e, 14, 31, 800);
        r128_a64_ldr_d(&e, 15, 31, 808);
        r128_a64_add_x_imm(&e, 31, 31, 1024);
        r128_a64_ret(&e);
        return e.overflow ? -1 : e.pos;
    }
}

static int
weight_vectors(uint8_t *code)
{
    /* 0x1.018p31 saturates u0 and leaves a weight of 0xc0000000, the
       range r3d_f2u converts through its high form */
    static const float special[] = { NAN, INFINITY, -INFINITY,
                                     0x1p31f, -0x1.01p31f, 0x1p60f,
                                     0x1.018p31f, -NAN, 2147483648.0f,
                                     2147483904.0f, -2147483648.0f,
                                     -1.0f, 0.0f, 7.0f, 8.0f };
    unsigned rows = 0, weight_fail = 0, coord_fail = 0, pixel_fail = 0;
#if JHT_WIN64_THUNK
    typedef void (__attribute__((ms_abi)) *weight_fn)(weight_vec_t *);
#else
    typedef void (*weight_fn)(weight_vec_t *);
#endif

    for (int axis = 0; axis < 2; axis++)
        for (int mip = 0; mip < 2; mip++)
            for (int wrap = 0; wrap < 4; wrap++) {
                r3d_stage_hdr_t h = { .clamp_s = wrap, .clamp_t = wrap };

                pthread_jit_write_protect_np(0);
                int len = weight_axis_block(code, &h, axis, mip, JHT_EXEC_X64);
                pthread_jit_write_protect_np(1);
                if (len <= 0)
                    return 1;
                __builtin___clear_cache((char *) code, (char *) code + len);
                for (size_t s = 0; s <= sizeof(special) / sizeof(special[0]); s++)
                    for (int lane = 0; lane < 4; lane++) {
                        weight_vec_t v = {0};

                        for (int k = 0; k < 4; k++) {
                            v.dim[k] = mip ? 2u << k : 8u;
                            v.coord[k] = (k & 1 ? 1.0f : 0.5f) / v.dim[k];
                        }
                        if (s < sizeof(special) / sizeof(special[0]))
                            v.coord[lane] = special[s] / (float) v.dim[lane];
                        ((weight_fn) (void *) code)(&v);
                        for (int k = 0; k < 4; k++) {
                            int raw;
                            uint32_t w = weight_ref(v.coord[k], v.dim[k], &raw);
                            int c0 = rr_tex_wrap(raw, v.dim[k], wrap);
                            int c1 = rr_tex_wrap_next(raw, v.dim[k], wrap);
                            uint32_t pixel = rr_lerp_packed(0xff000000u, 0xff000001u, w);

                            if ((v.out[2][k] != w || (int) v.out[0][k] != c0
                                 || (int) v.out[1][k] != c1 || v.out[3][k] != pixel)
                                && weight_fail + coord_fail + pixel_fail < 5)
                                printf("weight axis mismatch axis=%d mip=%d wrap=%d lane=%d coord=%g weight=%08x/%08x pixel=%08x/%08x\n",
                                       axis, mip, wrap, k, v.coord[k], v.out[2][k], w,
                                       v.out[3][k], pixel);
                            weight_fail += v.out[2][k] != w;
                            coord_fail += (int) v.out[0][k] != c0 || (int) v.out[1][k] != c1;
                            pixel_fail += v.out[3][k] != pixel;
                            if (!mip && !lane && !k && s == 8)
                                printf("neighbor axis=%d c=%d n=%u mode=%d oracle=%d jit=%d\n",
                                       axis, raw, v.dim[k], wrap, c1, (int) v.out[1][k]);
                            if (!axis && !mip && !wrap && !lane && !k)
                                printf("weight vector coord=%g oracle=%08x jit=%08x pixel=%08x\n",
                                       v.coord[k], w, v.out[2][k], v.out[3][k]);
                            if (!axis && !mip && !wrap && !lane && k == 1
                                && s == sizeof(special) / sizeof(special[0]))
                                printf("weight control frac=0.5 oracle=%08x jit=%08x pixel=%08x\n",
                                       w, v.out[2][k], v.out[3][k]);
                        }
                        rows++;
                    }
            }
    printf("weight axes: rows=%u weight_fail=%u coord_fail=%u pixel_fail=%u\n",
           rows, weight_fail, coord_fail, pixel_fail);
    total_rows += rows;
    total_fail += weight_fail + coord_fail + pixel_fail;
    return weight_fail || coord_fail || pixel_fail;
}

/* Complete rows exercise both stages and the sampler's fixed-level,
   mip-nearest, trilinear and magnification paths against ref_span.
   The isolated axis fixtures above supply the mixed exceptional lanes. */
static int
weight_rows(uint8_t *code)
{
    static const float coords[] = { NAN, INFINITY, -INFINITY, 0.25f,
                                    0.5f, 0x1p31f, -0x1.01p31f,
                                    0x1.018p31f, 2147483648.0f, 2147483904.0f };
    unsigned rows = 0, fail = 0, maxlen = 0, scalar_states = 0, tail_rows = 0;
    case_ent ce = { .dt = 6, .tex = 1 };

    for (int st = 0; st < 2; st++)
        for (int axis = 0; axis < 2; axis++)
            for (int wrap = 0; wrap < 4; wrap++)
                for (int filter = 0; filter < 4; filter++) {
                    rage128_draw_state_t d;
                    r3d_texctx_t tc = {0};

                    mk_state(&d, &ce);
                    d.sec_en = st;
                    d.sec_sel = st;
                    d.texw0 = d.texh0 = d.texw1 = d.texh1 = 2;
                    d.need_lod = !st && filter != 0;
                    d.need_lod2 = st && filter != 0;
                    for (int j = 0; j <= st; j++) {
                        r3d_stage_hdr_t *h = &d.sh[j];
                        r3d_stage_desc_t *sd = j ? &tc.sd1 : &tc.sd0;
                        r3d_comb_desc_t *cd = &d.comb[j];

                        h->dt = 6;
                        h->tsp = 0x111;
                        h->top = 1;
                        h->clamp_s = h->clamp_t = wrap;
                        h->minb = j < st ? 0 : filter == 2 ? 5 : filter == 1 ? 3 : filter == 3 ? 0 : 1;
                        h->mag = j < st ? 0 : 1;
                        h->mipdis = j < st || filter == 0 || filter == 3;
                        cd->comb = cd->comba = 1;
                        cd->cfac = 4;
                        cd->afac = 6;
                        sd->dt = h->dt;
                        sd->tsp = h->tsp;
                        sd->top = h->top;
                        sd->clamp_s = sd->clamp_t = wrap;
                        sd->minb = h->minb;
                        sd->mag = h->mag;
                        sd->mipdis = h->mipdis;
                        sd->slot_valid = 3;
                        sd->border = 0xff000000u;
                        h->border = sd->border;
                    }
                    if (!r128_jit_x64_can(&d) || !r128_jit_arm64_can(&d))
                        return 1;
                    scalar_states += JHT_EXEC_X64 ? !r128_jit_x64_soa_tex_can(&d)
                                                 : !r128_a64_soa_tex_can(&d);
                    pthread_jit_write_protect_np(0);
                    int len;
                    int scalar = getenv("JHT_NOSOA") != NULL;
                    if (JHT_EXEC_X64)
                        len = scalar ? r128_jit_x64_generate_1(code, &d, 1)
                                     : r128_jit_x64_generate(code, &d);
                    else
                        len = scalar ? r128_jit_arm64_generate_1(code, &d, 1)
                                     : r128_jit_arm64_generate(code, &d);
                    pthread_jit_write_protect_np(1);
                    if (len <= 0) {
                        printf("weight rows: generate failed stage=%d axis=%d wrap=%d filter=%d\n",
                               st, axis, wrap, filter);
                        return 1;
                    }
                    if ((unsigned) len > maxlen)
                        maxlen = len;
                    __builtin___clear_cache((char *) code, (char *) code + len);
                    for (size_t c = 0; c < sizeof(coords) / sizeof(coords[0]); c++) {
                        uint32_t texture[4] = { 0xff000000u, 0xff000001u,
                                                0xff000000u, 0xff000001u };
                        uint32_t cj[7] = {0}, cr[7] = {0};
                        r128_jit_tri_t tj = {0}, tr;
                        float *f = &tc.sta;

                        if (axis) {
                            texture[1] = 0xff000000u;
                            texture[2] = 0xff000001u;
                        }
                        for (int j = 0; j < 12; j++)
                            f[j] = 0.25f;
                        f[st * 6 + axis * 3] = coords[c];
                        tc.dSdx = tc.dS2dx = filter == 3 ? 0.01f : 0.625f;
                        for (int j = 0; j <= st; j++) {
                            r3d_stage_desc_t *sd = j ? &tc.sd1 : &tc.sd0;
                            for (int sl = 0; sl < 2; sl++) {
                                sd->slot[sl].lw = sd->slot[sl].lh = sl ? 2 : 1;
                                sd->slot[sl].texbase = (const uint8_t *) texture;
                                sd->slot[sl].mask = 15;
                            }
                        }
                        tj.x1 = 3 + (c & 3);
                        tail_rows += !scalar && (tj.x1 + 1) % 4 != 0;
                        tj.invs = 1;
                        tj.texctx = &tc;
                        tj.cptr = (uint8_t *) cj;
                        tj.c_lim = sizeof(cj);
                        tr = tj;
                        tr.cptr = (uint8_t *) cr;
                        r128_jit_span_fn fn = (r128_jit_span_fn) (void *) code;
                        uint64_t rj = JHT_SPAN_CALL(fn,
                                                    &tj, 1, 0, 0, 0, 0, 0, 0);
                        uint64_t rr = ref_span(&tr, &d, 1, 0, 0, 0, 0, 0, 0);
                        if (rj != rr || memcmp(cj, cr, sizeof(cj))) {
                            if (fail < 5)
                                printf("weight row mismatch stage=%d axis=%d wrap=%d filter=%d coord=%g jit=%08x ref=%08x\n",
                                       st, axis, wrap, filter, coords[c], cj[0], cr[0]);
                            fail++;
                        }
                        if (!st && !axis && !wrap && !filter && c < 3)
                            printf("weight row coord=%g jit=%08x ref=%08x\n",
                                   coords[c], cj[0], cr[0]);
                        rows++;
                    }
                }
    printf("weight rows: rows=%u fail=%u max_block=%u scalar=%d gated_states=%u tail_rows=%u\n",
           rows, fail, maxlen, getenv("JHT_NOSOA") != NULL, scalar_states, tail_rows);
    total_rows += rows;
    total_fail += fail;
    return fail != 0;
}

static int
weight_main(uint8_t *code)
{
    if (!JHT_EXEC_A64 && !JHT_EXEC_X64) {
        fprintf(stderr, "weight vectors require a native backend\n");
        return 2;
    }
    int axes = weight_vectors(code);
    int real_tex = g_real_tex;
    g_real_tex = 1;
    int rows = weight_rows(code);
    g_real_tex = real_tex;

    return axes || rows;
}

/* ------------------------------------------------------------------------
 * bench mode ("bench" argv): time ns/px of emitted blocks over long,
 * fully covered spans with one or two stages, bilinear and mip filters,
 * dt 4/6/0, modulate or doubled modulate, 16-bit depth less-or-equal,
 * RGB565 with dither, and alpha blend on/off. Every pixel passes
 * coverage/depth/key, so timing measures the shaded-pixel cost.
 * Less-or-equal depth keeps the block's own writes passing on reruns.
 * ---------------------------------------------------------------------- */
#include <time.h>
#if defined(__APPLE__)
#    include <pthread/qos.h>
#else
/* QoS shim: macOS classes -> Windows thread priorities (bench wants a
   quiet, front-of-queue thread); plain no-op elsewhere. */
#    define QOS_CLASS_USER_INTERACTIVE 0x21
#    define QOS_CLASS_USER_INITIATED   0x19
#    define QOS_CLASS_BACKGROUND       0x09
#    if defined(_WIN32)
static int
pthread_set_qos_class_self_np(int qos, int rel)
{
    int pri = (qos == QOS_CLASS_USER_INTERACTIVE) ? THREAD_PRIORITY_HIGHEST
        : (qos == QOS_CLASS_BACKGROUND)           ? THREAD_PRIORITY_LOWEST
                                                  : THREAD_PRIORITY_ABOVE_NORMAL;

    (void) rel;
    return SetThreadPriority(GetCurrentThread(), pri) ? 0 : -1;
}
#    else
#        define pthread_set_qos_class_self_np(q, r) ((void) (q), (void) (r), 0)
#    endif
#    ifndef CLOCK_MONOTONIC_RAW
#        define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#    endif
#endif

static double
bench_now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double) ts.tv_sec * 1e9 + (double) ts.tv_nsec;
}

typedef struct bench_shape {
    const char *name;
    int      tex, sec;          /* stage enables */
    uint32_t dt0, s3tc0, dt1;   /* texel formats */
    uint32_t comb1;             /* stage1 color op (stage0 = modulate) */
    uint32_t minb, mag;         /* filter/mip select (both stages) */
    int      mipdis;
    int      persp;
    int      dith;
    int      ab;                /* alpha blend srcalpha/invsrcalpha */
    int      z;                 /* z16 less-or-equal rd+wr */
    int      span;              /* pixels per row call */
    int      force_call;        /* generate with dt=10 (helper call glue),
                                   helper runs the REAL C replica on the
                                   true state via g_helper_ds */
    int      ck;                /* 1: stage-0 chroma key, never-matching
                                   (fn=3 full mask vs key 0: 565 texels
                                   decode alpha 0xff), so the rx sanity
                                   check still sees a fully shaded span;
                                   2: mask 0 (the CLR_CMP_FCN_3D code-1
                                   decode), every pixel rejected */
    int      sten;              /* D24S8 depth + always-pass stencil
                                   (sfn 7, replace on zpass, full swmask):
                                   fully shaded, so ns/px measures the
                                   stencil read/modify/write cost
                                   of a shaded span */
    int      f1;                /* use the stage-1 filter fields below
                                   (else stage 1 mirrors minb/mag/mipdis) */
    uint32_t minb1, mag1;
    int      mipdis1;
    int      sf;                /* specular + vertex fog (RGB channel
                                   stage; bench tri fills spa/fog) */
    int      aux;               /* aux scissors that never reject: one
                                   subtractive rect left of the span,
                                   one additive rect covering it -- the
                                   span stays fully shaded */
    int      tfog;              /* depth-indexed table fog: per-lane
                                   byte gather off the stashed raw zline
                                   lanes; the shape must carry z */
} bench_shape;

static void
bench_mk_stage(rage128_draw_state_t *d, int st, const bench_shape *s)
{
    r3d_stage_hdr_t *h  = &d->sh[st];
    r3d_comb_desc_t *cd = &d->comb[st];
    int              wexp = 8, hexp = 8, top = 8; /* 256x256, full chain */

    h->tsp     = (uint32_t) wexp | ((uint32_t) top << 4) | ((uint32_t) hexp << 8);
    h->clamp_s = 0; /* repeat */
    h->clamp_t = 0;
    h->dt      = st ? s->dt1 : s->dt0;
    h->s3tc    = st ? 0 : s->s3tc0;
    h->border  = 0;
    h->minb    = (st && s->f1) ? s->minb1 : s->minb;
    h->mag     = (st && s->f1) ? s->mag1 : s->mag;
    h->mipdis  = (st && s->f1) ? s->mipdis1 : s->mipdis;
    h->top     = top;
    cd->comb   = st ? s->comb1 : 3;  /* stage0 modulate */
    cd->comba  = st ? 2 : 3;         /* stage1 alpha R128_COMB_ALPHA_COPY_INP */
    cd->fmsb   = 0;
    cd->cfac   = 4;                  /* dominant operand selects */
    cd->ifac   = 4;
    cd->ifaca  = 2;
    cd->afac   = 6;
}

static int g_bench_dump; /* benchdump mode: write blocks, no timing */

static int
bench_main(uint8_t *code, uint64_t npx)
{
    /* dominant-first; tail entries are DIFFERENTIALS for hot-spot ranking */
    static const bench_shape shapes[] = {
        /* name              tex sec dt0 s3 dt1 cmb1 minb mag mip persp dith ab z span fc ck sten */
        { "base_gouraud_z",   0, 0,  0, 0, 0,  0,   0,   0,  0,  0,    1,   0, 1, 256 },
        { "base_z32_sten",    0, 0,  0, 0, 0,  0,   0,   0,  0,  0,    1,   0, 0, 256, 0, 0, 1 },
        { "t1_565_bl_mip",    1, 0,  4, 0, 0,  0,   3,   1,  0,  1,    1,   0, 1, 256 },
        { "t1_565_bl_mip_ab", 1, 0,  4, 0, 0,  0,   3,   1,  0,  1,    1,   1, 1, 256 },
        { "t1_8888_bl_mip",   1, 0,  6, 0, 0,  0,   3,   1,  0,  1,    1,   0, 1, 256 },
        { "t1_dxt1_bl_mip",   1, 0,  0, 0, 0,  0,   3,   1,  0,  1,    1,   0, 1, 256 },
        { "t2_565_mod2x",     1, 1,  4, 0, 4,  4,   3,   1,  0,  1,    1,   0, 1, 256 },
        /* differentials */
        { "t1_565_near_mip",  1, 0,  4, 0, 0,  0,   2,   0,  0,  1,    1,   0, 1, 256 },
        { "t1_565_bl_nomip",  1, 0,  4, 0, 0,  0,   1,   1,  1,  1,    1,   0, 1, 256 },
        { "t1_565_trilin",    1, 0,  4, 0, 0,  0,   5,   1,  0,  1,    1,   0, 1, 256 },
        { "t1_565_split",     1, 0,  4, 0, 0,  0,   3,   0,  0,  1,    1,   0, 1, 256 },
        { "t1_565_nopersp",   1, 0,  4, 0, 0,  0,   3,   1,  0,  0,    1,   0, 1, 256 },
        { "t1_565_noz",       1, 0,  4, 0, 0,  0,   3,   1,  0,  1,    1,   0, 0, 256 },
        { "t1_565_np_nm",     1, 0,  4, 0, 0,  0,   1,   1,  1,  0,    1,   0, 1, 256 },
        { "t1_565_span28",    1, 0,  4, 0, 0,  0,   3,   1,  0,  1,    1,   0, 1, 28  },
        { "t1_565_CALL",      1, 0,  4, 0, 0,  0,   3,   1,  0,  1,    1,   0, 1, 256, 1 },
        { "t1_565_ck_near",   1, 0,  4, 0, 0,  0,   0,   0,  0,  1,    1,   0, 1, 256, 0, 1 },
        { "t1_yuv_bl_nomip",  1, 0, 11, 0, 0,  0,   1,   1,  1,  1,    1,   0, 1, 256 },
        /* sub-dt tri/split classes + relocated-sub parity checks */
        { "t1_dxt1_trilin",   1, 0,  0, 0, 0,  0,   5,   1,  0,  1,    1,   0, 1, 256 },
        { "t1_dxt5_trilin",   1, 0,  0, 3, 0,  0,   5,   1,  0,  1,    1,   0, 1, 256 },
        { "t1_yuv_trilin",    1, 0, 11, 0, 0,  0,   5,   1,  0,  1,    1,   0, 1, 256 },
        { "t1_dxt1_split",    1, 0,  0, 0, 0,  0,   3,   0,  0,  1,    1,   0, 1, 256 },
        { "t1_dxt5_bl_mip",   1, 0,  0, 3, 0,  0,   3,   1,  0,  1,    1,   0, 1, 256 },
        { "t1_yuv444_bl",     1, 0, 14, 0, 0,  0,   1,   1,  1,  1,    1,   0, 1, 256 },
        /* dual tri/split classes (16 KB gate-admitted pairs only) */
        { "t2_565_tri_nn",    1, 1,  4, 0, 4,  4,   5,   1,  0,  1,    1,   0, 1, 256,
          0, 0, 0, 1, 0, 0, 1 },
        { "t2_dxt1_tri_nn",   1, 1,  0, 0, 4,  4,   5,   1,  0,  1,    1,   0, 1, 256,
          0, 0, 0, 1, 0, 0, 1 },
        { "t2_565_split_m",   1, 1,  4, 0, 4,  4,   2,   1,  0,  1,    1,   0, 1, 256,
          0, 0, 0, 1, 3, 1, 0 },
        { "t2_565_ts_tn",     1, 1,  4, 0, 4,  4,   4,   1,  0,  1,    1,   0, 1, 256,
          0, 0, 0, 1, 4, 0, 0 },
        /* dual + stage-0 chroma key (never-matching): nearest no-LOD
           stage 0 (the ck class gate) under a bilinear+mip stage 1 */
        { "t2_565_ck_nn",     1, 1,  4, 0, 4,  4,   0,   0,  0,  1,    1,   0, 1, 256,
          0, 1, 0, 1, 3, 1, 0 },
        /* Filtered chroma-key states use a dedicated tnear pass alongside
           the bilinear, mip and trilinear sampling pipelines. */
        { "t1_565_ck_bl",     1, 0,  4, 0, 0,  0,   1,   1,  1,  1,    1,   0, 1, 256,
          0, 1 },
        { "t1_565_ck_blmip",  1, 0,  4, 0, 0,  0,   3,   1,  0,  1,    1,   0, 1, 256,
          0, 1 },
        { "t1_565_ck_tri",    1, 0,  4, 0, 0,  0,   5,   1,  0,  1,    1,   0, 1, 256,
          0, 1 },
        /* reject-all key (mask 0 = the CLR_CMP_FCN_3D code-1 decode):
           banks a block whose every pixel drops out of the store mask */
        { "t1_565_ck_true",   1, 0,  4, 0, 0,  0,   0,   0,  0,  1,    1,   0, 1, 256,
          0, 2 },
        { "t2_565_ck_bl",     1, 1,  4, 0, 4,  4,   1,   1,  1,  1,    1,   0, 1, 256,
          0, 1, 0, 1, 3, 1, 0 },
        /* Specular and fog run on the saved float channels; auxiliary
           scissors mask each lane's x window. */
        { "base_565_sf",      0, 0,  0, 0, 0,  0,   0,   0,  0,  0,    1,   0, 1, 256,
          0, 0, 0, 0, 0, 0, 0, 1, 0 },
        { "t1_565_bl_mip_sf", 1, 0,  4, 0, 0,  0,   3,   1,  0,  1,    1,   0, 1, 256,
          0, 0, 0, 0, 0, 0, 0, 1, 0 },
        { "t1_565_aux",       1, 0,  4, 0, 0,  0,   3,   1,  0,  1,    1,   0, 1, 256,
          0, 0, 0, 0, 0, 0, 0, 0, 1 },
        /* Pairs over the inline budget use a coordinate/gather subroutine:
           trilinear-linear stage 0 with no-LOD bilinear stage 1, or with
           trilinear-nearest stage 1 (both stages use their subroutines). */
        { "t2_565_tri_bl",    1, 1,  4, 0, 4,  4,   5,   1,  0,  1,    1,   0, 1, 256,
          0, 0, 0, 1, 1, 1, 1 },
        { "t2_565_tri_tn",    1, 1,  4, 0, 4,  4,   5,   1,  0,  1,    1,   0, 1, 256,
          0, 0, 0, 1, 4, 0, 0 },
        /* Untextured 16-bit depth less-or-equal with dither gathers the fog
           factor per lane. This group consumes no texture-descriptor
           random draws, keeping earlier shapes' descriptor streams fixed. */
        { "base_565_tfog",    0, 0,  0, 0, 0,  0,   0,   0,  0,  0,    1,   0, 1, 256,
          0, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
        /* D24S8 depth less-or-equal with always-pass stencil replacement on
           a RGB565 mip-bilinear span. The sten flag supplies its depth
           config; base_z32_sten covers untextured output. This group
           keeps earlier shapes' descriptor random streams fixed. */
        { "t1_565_bl_mip_sten", 1, 0,  4, 0, 0,  0,   3,   1,  0,  1,    1,   0, 0, 256,
          0, 0, 1 },
        /* TEXTURED table fog: the generators' tex_inline term subtracts
           (fog_en && fog_table_en), so this inline-family state compiles
           to the per-pixel helper-call shape while the classifier still
           counts it as texinline. The force_call twin is the known-call
           reference to size it against; base_565_tfog above is untextured
           and t1_565_bl_mip is the inline control. Appended LAST so the
           earlier shapes' descriptor rng stream is untouched. */
        { "t1_565_tfog",      1, 0,  4, 0, 0,  0,   3,   1,  0,  1,    1,   0, 1, 256,
          0, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
        { "t1_565_tfog_call", 1, 0,  4, 0, 0,  0,   3,   1,  0,  1,    1,   0, 1, 256,
          1, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
    };

    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

    for (uint32_t k = 0; k < TEXA_SZ; k += 4)
        *(uint32_t *) &texarena[k] = rng();
    for (int k = 0; k < 256; k++)
        g_texpal[k] = rng();
    for (uint32_t k = 0; k < VRAM_SZ; k += 4)
        *(uint32_t *) &vram_j[k] = rng();
    for (int k = 0; k < 256; k++) /* separate stream: block-invariant */
        g_fog_table[k] = (uint8_t) fog_rng();
    g_real_tex = 1;

    printf("bench: npx/run=%llu runs=5 (median)\n", (unsigned long long) npx);
    for (size_t si = 0; si < sizeof(shapes) / sizeof(shapes[0]); si++) {
        const bench_shape          *s = &shapes[si];
        static rage128_draw_state_t d; /* static: g_helper_ds may point here */
        case_ent                    ce;

        memset(&ce, 0, sizeof(ce));
        ce.dt   = 4; /* dst 565 */
        ce.dith = s->dith;
        if (s->z) {
            ce.z_en = 1; ce.z_wr = 1; ce.zfn = 2; /* less-or-equal */
            ce.zbpp = 2; ce.zmax = 0xffff; ce.zshift = 0;
        }
        if (s->ab) {
            ce.ab = 1; ce.bsrc = 0x4; ce.bdst = 0x5; ce.bfcn = 0;
        }
        ce.tex = s->tex;
        ce.sec = s->sec;
        mk_state(&d, &ce);
        d.do_persp = s->persp;
        d.lod_bias = 0.0f;
        for (int k = 0; k < 4; k++)
            d.cc[k] = 0.25f * (float) (k + 1);
        if (s->sten) {
            /* D24S8 (stencil needs zwidth != 0) with depth less-or-equal
               against the far-prefilled row and an always-pass stencil test
               -> fully shaded; replace under a full write mask hits the op +
               RMW every px. */
            d.z_en   = 1; d.z_wr = 1; d.zfn = 2;
            d.zbpp   = 4; d.zmax = 0xffffff; d.zshift = 8;
            d.sten_on  = 1; d.sshift = 0;
            d.sfn      = 7;
            d.sfail_op = 0; d.zpass_op = 2; d.zfail_op = 0;
            d.sref     = 0x80; d.svmask = 0xff; d.swmask = 0xff;
        }
        if (d.tex_en)
            bench_mk_stage(&d, 0, s);
        if (d.sec_en)
            bench_mk_stage(&d, 1, s);
        if (s->ck) {
            d.ck3d_on  = 1;
            d.ckc_on   = 0;
            d.ckfn     = 3; /* reject on eq -- key 0 never matches ... */
            d.ck3d_msk = s->ck == 2 ? 0 : 0xffffffffu; /* ... unless the
                              mask is 0 (the code-1 decode): vacuously
                              equal, every pixel rejected */
            d.ck3d_clr = 0;
            d.need_ck  = 1;
        }
        if (s->sf) {
            d.spec_en = 1;
            d.fog_en  = 1;
            d.fogr = 0.25f; d.fogg = 0.75f; d.fogb = 0.5f;
        }
        if (s->tfog) {
            d.fog_en       = 1;
            d.fog_table_en = 1;
            d.fogr = 0.25f; d.fogg = 0.75f; d.fogb = 0.5f;
        }
        if (s->aux) {
            /* never-rejecting rects: subtractive fully left of x=0,
               additive covering the whole span/row range */
            d.aux_on   = 1;
            d.aux_cntl = 0x1 | 0x4 | 0x8; /* r0 add, r1 sub */
            d.aux_x0[0] = -4096; d.aux_x1[0] = 4096;
            d.aux_y0[0] = -4096; d.aux_y1[0] = 4096;
            d.aux_x0[1] = -4096; d.aux_x1[1] = -16;
            d.aux_y0[1] = -4096; d.aux_y1[1] = 4096;
        }
        if (d.tex_en || d.sec_en) {
            d.need_lod = d.tex_en
                && ((!d.sh[0].mipdis && d.sh[0].minb >= 2)
                    || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1)));
            d.need_lod2 = d.sec_en
                && ((!d.sh[1].mipdis && d.sh[1].minb >= 2)
                    || ((int) (d.sh[1].minb & 1) != (d.sh[1].mag == 1)));
            d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
            d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
            d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
            d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
            if (!r128_a64_texinline_can(&d)) {
                printf("%s: NOT inline family -- shape bug\n", s->name);
                return 2;
            }
            memset(&g_tctx, 0, sizeof(g_tctx));
            if (d.tex_en)
                setup_stage_desc(&d, 0, &g_tctx.sd0);
            if (d.sec_en)
                setup_stage_desc(&d, 1, &g_tctx.sd1);
            /* full-arena masks: no artificial aliasing of fetches */
            for (int sl = 0; sl <= 10; sl++) {
                g_tctx.sd0.slot[sl].mask = TEXA_SZ - 1;
                g_tctx.sd1.slot[sl].mask = TEXA_SZ - 1;
            }
            /* fixed per-triangle interpolants: s walks ~0.75 texel/px,
               LOD gradients land the mip select around level 1 */
            g_tctx.sta = 0.10f; g_tctx.stb = 0.80f; g_tctx.stc = 0.40f;
            g_tctx.tta = 0.20f; g_tctx.ttb = 0.60f; g_tctx.ttc = 0.90f;
            g_tctx.s2a = 0.30f; g_tctx.s2b = 0.70f; g_tctx.s2c = 0.20f;
            g_tctx.t2a = 0.15f; g_tctx.t2b = 0.55f; g_tctx.t2c = 0.85f;
            g_tctx.arhw = 0.9f; g_tctx.brhw = 1.1f; g_tctx.crhw = 1.0f;
            g_tctx.dSdx = 0.009f; g_tctx.dSdy = 0.004f;
            g_tctx.dTdx = 0.006f; g_tctx.dTdy = 0.008f;
            g_tctx.dWdx = 0.0002f; g_tctx.dWdy = 0.0001f;
            g_tctx.dS2dx = 0.009f; g_tctx.dS2dy = 0.004f;
            g_tctx.dT2dx = 0.006f; g_tctx.dT2dy = 0.008f;
        }

        rage128_draw_state_t dgen = d;

        if (s->force_call) {
            /* helper-call glue: dt 10 fails the inline gate; the helper
               (rr_texstage_run) still runs the true state via g_helper_ds */
            dgen.sh[0].dt = 10;
            dgen.sh[1].dt = 10;
            g_helper_ds   = &d;
        }
        pthread_jit_write_protect_np(0);
#if JHT_EXEC_X64
        /* native x86-64 bench/dump: the x64 backend is the one under
           test here, not a cross-dump of ARM64 blocks */
        int len = r128_jit_x64_can(&dgen) ? r128_jit_x64_generate(code, &dgen)
                                          : -1;
#else
        int len = r128_jit_arm64_generate(code, &dgen);
#endif
        pthread_jit_write_protect_np(1);
        if (len <= 0) {
            printf("%s: generate failed\n", s->name);
            return 2;
        }
        __builtin___clear_cache((char *) code, (char *) code + len);

        if (g_bench_dump) {
            char  fnb[128];
            FILE *f;

            snprintf(fnb, sizeof(fnb), "block_%s.bin", s->name);
            f = fopen(fnb, "wb");
            fwrite(code, 1, (size_t) len, f);
            fclose(f);
            printf("%-18s len=%5d -> %s\n", s->name, len, fnb);
            continue;
        }

        r128_jit_span_fn fn = (r128_jit_span_fn) (void *) code;
        r128_jit_tri_t   tj;
        int64_t          area = 1 << 20;
        int64_t          e0 = 1000, e1 = 500000;
        int64_t          e2 = area - e0 - e1 - 1;
        double           zl = 0.5;
        uint32_t         drow = 0x1000, zrow = 0x40000;

        memset(&tj, 0, sizeof(tj));
        tj.vram      = vram_j;
        tj.vram_mask = VRAM_SZ - 1;
        tj.x0        = 0;
        tj.x1        = s->span - 1;
        tj.e0dxi     = 3000;
        tj.e1dxi     = -1200;
        tj.e2dxi     = -1800;
        tj.invs      = 1.0f / (float) area;
        tj.dZdx      = 1e-6;
        for (int k = 0; k < 4; k++) {
            tj.vca[k] = 0.9f - 0.1f * (float) k;
            tj.vcb[k] = 0.2f + 0.2f * (float) k;
            tj.vcc[k] = 0.5f;
            tj.spa[k] = 0.15f + 0.05f * (float) k;
            tj.spb[k] = 0.30f - 0.05f * (float) k;
            tj.spc[k] = 0.10f;
        }
        tj.fog[0] = 0.35f; tj.fog[1] = 0.85f; tj.fog[2] = 0.60f;
        tj.fog_table = g_fog_table;
        tj.texctx = &g_tctx;

        /* far-prefill the z row so the first less-or-equal pass shades every
           pixel; the block's own z writes keep it passing after that.
           span*4 covers the D24S8 stencil shapes too (z16 reads only the
           low span*2, so the extra fill is inert there). */
        memset(&vram_j[zrow], 0xff, (size_t) s->span * 4);

        /* sanity: the whole span must shade (rx = [0, span-1]), except
           the reject-all key shape (ck 2), where nothing may (rx = -1/-1) */
        uint64_t rx      = fn(&tj, e0, e1, e2, zl, drow, zrow, 0);
        uint64_t want_rx = s->ck == 2 ? ~0ull
                                      : (uint64_t) (uint32_t) (s->span - 1) << 32;
        if (rx != want_rx) {
            printf("%s: span shading rx=%016llx want %016llx -- shape bug\n",
                   s->name, (unsigned long long) rx, (unsigned long long) want_rx);
            return 2;
        }

        uint64_t calls = npx / (uint64_t) s->span;
        double   res[5];

        for (int c = 0; c < 2000; c++)
            fn(&tj, e0, e1, e2, zl, drow, zrow, (int32_t) (c & 31));
        for (int r = 0; r < 5; r++) {
            double t0 = bench_now_ns();

            for (uint64_t c = 0; c < calls; c++) {
                if (s->tex) /* walk v across 32 texture rows */
                    g_tctx.tta = 0.20f + (float) (c & 31) * 0.02f;
                fn(&tj, e0, e1, e2, zl, drow, zrow, (int32_t) (c & 31));
            }
            res[r] = (bench_now_ns() - t0) / ((double) calls * (double) s->span);
        }
        for (int a = 0; a < 4; a++) /* insertion sort, 5 elems */
            for (int b = a + 1; b < 5; b++)
                if (res[b] < res[a]) { double t = res[a]; res[a] = res[b]; res[b] = t; }
        printf("%-18s len=%5d span=%3d ns/px median=%.3f runs=[%.3f %.3f %.3f %.3f %.3f]\n",
               s->name, len, s->span, res[2], res[0], res[1], res[2], res[3], res[4]);
        fflush(stdout);
    }
    g_real_tex = 0;
    return 0;
}

/* ------------------------------------------------------------------------
 * contend mode ("contend" argv): measure contention from workers running
 * t1_565_bl_mip. Each streams a private 16 MB image (color at 0, depth at
 * +12 MB, 768 rows at 2 KB pitch walked round-robin); its texture context
 * walks 32 texture rows. The main thread uses user-interactive priority
 * to model the emulator CPU thread and measures a dependent 64 MB pointer
 * chase and a linear scan. Degradation against zero workers measures the
 * latency and bandwidth cost. CONTEND_QOS selects worker priority:
 * ui = user-interactive, in = user-initiated (default), bg = background.
 * On macOS, background priority probes efficiency-core cache placement.
 * ---------------------------------------------------------------------- */
#define CT_WSZ    (1u << 24) /* per-worker image */
#define CT_ROWS   768
#define CT_PITCH  2048
#define CT_ZBASE  (12u << 20)
#define CT_CHASEN (1u << 24) /* 16M entries = 64 MB */

typedef struct ct_worker {
    r128_jit_span_fn   fn;
    r128_jit_tri_t     tj;   /* template; vram/texctx patched in thread */
    r3d_texctx_t       tctx; /* private copy: tta walked per row */
    uint8_t           *buf;
    int                widx, wcnt; /* shared mode: own rows widx, widx+n.. */
    _Atomic int       *stop;
    unsigned long long spans;
    pthread_t          th;
} ct_worker;

static void *
ct_worker_run(void *arg)
{
    ct_worker  *w   = (ct_worker *) arg;
    const char *q   = getenv("CONTEND_QOS");
    uint32_t    row = 0;

    if (q && !strcmp(q, "ui"))
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    else if (q && !strcmp(q, "bg"))
        pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
    else
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);

    row = (uint32_t) w->widx;
    for (uint32_t r = row; r < CT_ROWS; r += (uint32_t) w->wcnt)
        memset(w->buf + CT_ZBASE + r * CT_PITCH, 0xff, CT_PITCH);
    w->tj.vram      = w->buf;
    w->tj.vram_mask = CT_WSZ - 1;
    w->tj.texctx    = &w->tctx;
    while (!atomic_load_explicit(w->stop, memory_order_relaxed)) {
        w->tctx.tta = 0.20f + (float) (row & 31) * 0.02f;
        w->fn(&w->tj, 1000, 500000, (1 << 20) - 501001, 0.5,
              row * CT_PITCH, CT_ZBASE + row * CT_PITCH,
              (int32_t) (row & 31));
        row += (uint32_t) w->wcnt;
        if (row >= CT_ROWS)
            row = (uint32_t) w->widx;
        w->spans++;
    }
    return NULL;
}

static int
contend_main(uint8_t *code)
{
    static const bench_shape s = { "t1_565_bl_mip", 1, 0, 4, 0, 0, 0,
                                   3, 1, 0, 1, 1, 0, 1, 256 };
    static rage128_draw_state_t d;
    case_ent  ce;
    uint32_t *chase;
    uint64_t *scanb;
    double    base_chase = 0.0, base_scan = 0.0, base_so = 0.0, base_alu = 0.0;

    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    for (uint32_t k = 0; k < TEXA_SZ; k += 4)
        *(uint32_t *) &texarena[k] = rng();
    for (int k = 0; k < 256; k++)
        g_texpal[k] = rng();
    g_real_tex = 1;

    memset(&ce, 0, sizeof(ce));
    ce.dt = 4; ce.dith = 1; ce.tex = 1;
    ce.z_en = 1; ce.z_wr = 1; ce.zfn = 2;
    ce.zbpp = 2; ce.zmax = 0xffff; ce.zshift = 0;
    mk_state(&d, &ce);
    d.do_persp = 1;
    d.lod_bias = 0.0f;
    for (int k = 0; k < 4; k++)
        d.cc[k] = 0.25f * (float) (k + 1);
    bench_mk_stage(&d, 0, &s);
    d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
        || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1));
    d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
    d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
    if (!r128_a64_texinline_can(&d)) {
        printf("contend: shape not inline -- shape bug\n");
        return 2;
    }
    memset(&g_tctx, 0, sizeof(g_tctx));
    setup_stage_desc(&d, 0, &g_tctx.sd0);
    for (int sl = 0; sl <= 10; sl++)
        g_tctx.sd0.slot[sl].mask = TEXA_SZ - 1;
    g_tctx.sta = 0.10f; g_tctx.stb = 0.80f; g_tctx.stc = 0.40f;
    g_tctx.tta = 0.20f; g_tctx.ttb = 0.60f; g_tctx.ttc = 0.90f;
    g_tctx.arhw = 0.9f; g_tctx.brhw = 1.1f; g_tctx.crhw = 1.0f;
    g_tctx.dSdx = 0.009f; g_tctx.dSdy = 0.004f;
    g_tctx.dTdx = 0.006f; g_tctx.dTdy = 0.008f;
    g_tctx.dWdx = 0.0002f; g_tctx.dWdy = 0.0001f;

    pthread_jit_write_protect_np(0);
    int len = r128_jit_arm64_generate(code, &d);
    pthread_jit_write_protect_np(1);
    if (len <= 0) {
        printf("contend: generate failed\n");
        return 2;
    }
    __builtin___clear_cache((char *) code, (char *) code + len);

    r128_jit_tri_t tj;

    memset(&tj, 0, sizeof(tj));
    tj.x0    = 0;
    tj.x1    = s.span - 1;
    tj.e0dxi = 3000; tj.e1dxi = -1200; tj.e2dxi = -1800;
    tj.invs  = 1.0f / (float) (1 << 20);
    tj.dZdx  = 1e-6;
    for (int k = 0; k < 4; k++) {
        tj.vca[k] = 0.9f - 0.1f * (float) k;
        tj.vcb[k] = 0.2f + 0.2f * (float) k;
        tj.vcc[k] = 0.5f;
    }
    tj.fog_table = g_fog_table;

    /* sanity on the shared 1 MB arena before the workers get real bufs */
    tj.vram      = vram_j;
    tj.vram_mask = VRAM_SZ - 1;
    tj.texctx    = &g_tctx;
    memset(&vram_j[0x40000], 0xff, (size_t) s.span * 4);
    uint64_t rx = ((r128_jit_span_fn) (void *) code)(&tj, 1000, 500000,
                                                     (1 << 20) - 501001,
                                                     0.5, 0x1000, 0x40000, 0);
    if ((uint32_t) rx != 0 || (uint32_t) (rx >> 32) != (uint32_t) (s.span - 1)) {
        printf("contend: span not fully shaded -- shape bug\n");
        return 2;
    }

    chase = malloc((size_t) CT_CHASEN * 4);
    scanb = malloc((size_t) CT_CHASEN * 4);
    if (!chase || !scanb) { perror("malloc"); return 2; }
    /* Sattolo: one full cycle, no short loops */
    for (uint32_t i = 0; i < CT_CHASEN; i++)
        chase[i] = i;
    for (uint32_t i = CT_CHASEN - 1; i > 0; i--) {
        uint32_t j = rng() % i, t = chase[i];
        chase[i] = chase[j]; chase[j] = t;
    }
    for (uint32_t i = 0; i < CT_CHASEN / 2; i++)
        ((uint64_t *) scanb)[i] = rng();

    /* one full lap of each proxy before measuring: page/TLB warm, and
       the up-then-down ladder exposes DVFS hysteresis (M-series fabric
       clocks respond to load; a cold single-leg baseline reads slow) */
    {
        uint32_t          idx = 0;
        volatile uint64_t snk = 0;

        for (uint32_t k = 0; k < CT_CHASEN; k++)
            idx = chase[idx];
        for (uint32_t k = 0; k < CT_CHASEN / 2; k++)
            snk += ((uint64_t *) scanb)[k];
        snk += idx;
        /* the sink is read once at the end so the compiler neither drops
           the accumulations above nor reports a variable that is set but
           never used */
        (void) snk;
    }
    double   leg_ns = getenv("CONTEND_MS")
        ? strtod(getenv("CONTEND_MS"), NULL) * 1e6 : 2e9;
    int      shared = getenv("CONTEND_SHARED") != NULL;
    uint8_t *shbuf  = malloc(CT_WSZ); /* shared image; scanout target */

    if (!shbuf) { perror("malloc"); return 2; }
    memset(shbuf, 0, CT_WSZ);
    printf("contend: block len=%d qos=%s shared=%d chase=64MB scan=64MB\n",
           len, getenv("CONTEND_QOS") ? getenv("CONTEND_QOS") : "in", shared);
    static const int ladder[] = { 0, 1, 2, 3, 4, 3, 2, 1, 0 };

    for (int li = 0; li < 9; li++) {
        int n = ladder[li];
        _Atomic int stop = 0;
        ct_worker  *ws   = calloc((size_t) (n ? n : 1), sizeof(ct_worker));

        for (int i = 0; i < n; i++) {
            ws[i].fn   = (r128_jit_span_fn) (void *) code;
            ws[i].tj   = tj;
            ws[i].tctx = g_tctx;
            ws[i].stop = &stop;
            ws[i].widx = shared ? i : 0;
            ws[i].wcnt = shared ? n : 1;
            ws[i].buf  = shared ? shbuf : malloc(CT_WSZ);
            if (!ws[i].buf) { perror("malloc"); return 2; }
            pthread_create(&ws[i].th, NULL, ct_worker_run, &ws[i]);
        }

        /* warm-up, then measure: chase 2 s, scan 2 s */
        double   t0, el;
        uint32_t idx = 0;
        volatile uint64_t sink = 0;
        unsigned long long steps = 0, bytes = 0;

        t0 = bench_now_ns();
        while (bench_now_ns() - t0 < 3e8)
            for (int k = 0; k < 1 << 16; k++)
                idx = chase[idx];
        t0 = bench_now_ns();
        while ((el = bench_now_ns() - t0) < leg_ns) {
            for (int k = 0; k < 1 << 20; k++)
                idx = chase[idx];
            steps += 1 << 20;
        }
        sink += idx;
        double ch = (double) steps / (el / 1e9) / 1e6;

        t0 = bench_now_ns();
        while ((el = bench_now_ns() - t0) < leg_ns) {
            uint64_t sum = 0;

            for (uint32_t k = 0; k < CT_CHASEN / 2; k++)
                sum += ((uint64_t *) scanb)[k];
            sink += sum;
            bytes += (uint64_t) CT_CHASEN * 4;
        }
        double sc = (double) bytes / (el / 1e9) / 1e9;

        /* ALU proxy: dependent integer ops, zero memory traffic --
           throughput is pure core clock. A slowdown here with workers
           active is FREQUENCY (P-cluster clock vs active-core count),
           not cache/memory contention. */
        unsigned long long ops = 0;
        uint64_t           acc = 0x9e3779b97f4a7c15ull;

        t0 = bench_now_ns();
        while ((el = bench_now_ns() - t0) < leg_ns) {
            for (int k = 0; k < 1 << 20; k++) {
                acc = acc * 6364136223846793005ull + 1442695040888963407ull;
                acc ^= acc >> 29;
            }
            ops += 1 << 20;
        }
        sink += acc;
        double al = (double) ops / (el / 1e9) / 1e6;

        /* scanout proxy: CPU-thread reads of the color band the workers
           are writing (svga_poll's per-scanline fetch). In shared mode
           these lines are modified in other cores' caches -- the
           coherence-miss cost is the discriminator. */
        const uint8_t *sob = (shared || !n) ? shbuf : ws[0].buf;

        bytes = 0;
        t0    = bench_now_ns();
        while ((el = bench_now_ns() - t0) < leg_ns) {
            uint64_t sum = 0;

            for (uint32_t r = 0; r < CT_ROWS; r++)
                for (uint32_t k = 0; k < CT_PITCH; k += 8)
                    sum += *(const uint64_t *) (sob + r * CT_PITCH + k);
            sink += sum;
            bytes += (uint64_t) CT_ROWS * CT_PITCH;
        }
        /* the sink is read once after the last proxy so the compiler
           neither drops the accumulations nor reports a variable that is
           set but never used */
        (void) sink;
        double so = (double) bytes / (el / 1e9) / 1e9;

        atomic_store(&stop, 1);
        unsigned long long spans = 0;

        for (int i = 0; i < n; i++) {
            pthread_join(ws[i].th, NULL);
            spans += ws[i].spans;
            if (!shared)
                free(ws[i].buf);
        }
        if (!n && !base_chase) {
            base_chase = ch; base_scan = sc; base_so = so; base_alu = al;
        }
        printf("workers=%d  alu=%7.2f Mops/s (%+5.1f%%)  "
               "chase=%7.2f Msteps/s (%+5.1f%%)  "
               "scan=%6.2f GB/s (%+5.1f%%)  scanout=%6.2f GB/s (%+5.1f%%)  "
               "raster=%7.1f Mpx/s\n",
               n, al, base_alu ? 100.0 * (al - base_alu) / base_alu : 0.0,
               ch, base_chase ? 100.0 * (ch - base_chase) / base_chase : 0.0,
               sc, base_scan ? 100.0 * (sc - base_scan) / base_scan : 0.0,
               so, base_so ? 100.0 * (so - base_so) / base_so : 0.0,
               (double) spans * s.span / 4.0 / 1e6);
        fflush(stdout);
        free(ws);
    }
    free(shbuf);
    g_real_tex = 0;
    return 0;
}

/* ------------------------------------------------------------------------
 * sizes mode ("sizes" argv): worst-case emitted length per dual-stage
 * filter-class pair, maxed across the family dt pairs and heavy riders
 * (dither/blend/atest/partial-wmask/z24-in-32/persp/worst clamps and
 * combine ops). Deterministic, no rng. Data source for the 16 KB
 * block-cap gate on dual tri/split: a pair whose max prints as
 * OVERFLOW cannot be admitted to the SoA loop (generate() would return
 * -1 and the state would lose its scalar block to the interpreter).
 * On x86-64 a single-stage sweep (spec/fog/aux riders) runs first;
 * "sizes single" stops after it.
 * ---------------------------------------------------------------------- */
typedef struct size_class {
    const char *name;
    uint32_t    minb, mag;
    int         mipdis;
} size_class;

static uint32_t g_sizes_comb; /* prepass comb override:
                                 0x8000 | fmsb<<8 | comb<<4 | comba */
static int      g_sizes_ck;   /* dual-ck sweep: stage-0 chroma key with
                                 both compares on (worst key emission) */
static int      g_sizes_sten; /* stencil rider: D24S8 depth not-equal +
                                 worst-emission sten config on the state */
static int      g_sizes_noat; /* drop the baked alpha test: the sten
                                 delta must also cover states where sten
                                 alone forces the defer/cover plumbing */
static int      g_sizes_req_soa; /* delta prepass: return -3 unless the
                                    state is SoA-admitted, so both sides
                                    of a same-state pair compare vector
                                    blocks (a gated side would emit the
                                    small scalar block and poison the
                                    delta) */
static int      g_sizes_single;  /* single-stage cells: sec_en off     */
static int      g_sizes_sf;      /* rider: bit 0 spec, bit 1 vertex fog */
static uint32_t g_sizes_aux;     /* rider: aux scissor cntl, 0 = off    */
static int      g_sizes_soa;     /* out: the last sizes_gen state was
                                    SoA-admitted (else a scalar block)  */
#if JHT_EXEC_X64
static int      g_sizes_nosoa;   /* emit the scalar-only block (the
                                    production wrapper's overflow retry)
                                    to measure what an OVERFLOW state
                                    falls back to */
#endif

static void
sizes_mk_stage(rage128_draw_state_t *d, int st, const size_class *sc,
               uint32_t dt, uint32_t s3tc, uint32_t clamp)
{
    r3d_stage_hdr_t *h  = &d->sh[st];
    r3d_comb_desc_t *cd = &d->comb[st];

    h->tsp     = 8u | (8u << 4) | (8u << 8);
    h->clamp_s = clamp & 3;
    h->clamp_t = (clamp >> 2) & 3;
    h->dt      = dt;
    h->s3tc    = s3tc;
    h->border  = 0xdeadbeefu;
    h->minb    = sc->minb;
    h->mag     = sc->mag;
    h->mipdis  = sc->mipdis;
    h->top     = 8;
    /* worst-size combine (prepass 0 argmax): per-channel cc lerp color
       op, clamp-chain alpha op, subtract selects */
    cd->comb   = 15;
    cd->comba  = 14;
    cd->fmsb   = 0;
    if (g_sizes_comb & 0x8000u) {
        cd->comb  = (g_sizes_comb >> 4) & 0xf;
        cd->comba = g_sizes_comb & 0xf;
        cd->fmsb  = (g_sizes_comb >> 8) & 1;
    }
    cd->cfac   = 5;
    cd->ifac   = 2;
    cd->ifaca  = 1;
    cd->afac   = 7;
}

static int
sizes_gen(uint8_t *code, const size_class *c0, const size_class *c1,
          uint32_t dt0, uint32_t s3tc0, uint32_t dt1, uint32_t s3tc1,
          uint32_t clamp, int dst8888, uint32_t bsrc, uint32_t bdst)
{
    rage128_draw_state_t d;
    case_ent             ce;
    int                  len;

    memset(&ce, 0, sizeof(ce));
    ce.dt   = dst8888 ? 6 : 4;
    ce.dith = !dst8888;
    ce.z_en = 1; ce.z_wr = 1; ce.zfn = 6; /* not-equal: cmeq+not, widest */
    ce.zbpp = 4; ce.zmax = 0xffffff; ce.zshift = 8;
    ce.ab   = 1; ce.bsrc = bsrc; ce.bdst = bdst; ce.bfcn = 0;
    ce.tex  = 1; ce.sec = !g_sizes_single;
    ce.at   = 1; ce.atfn = 3; ce.atref = 0x7f;
    ce.spec = g_sizes_sf & 1;
    ce.fogv = (g_sizes_sf >> 1) & 1;
    mk_state(&d, &ce);
    if (g_sizes_noat)
        d.atest_en = 0;
    if (g_sizes_aux) {
        /* rect bounds past imm8 range: the widest compare encodings */
        d.aux_on   = 1;
        d.aux_cntl = g_sizes_aux;
        for (int k = 0; k < 3; k++) {
            d.aux_x0[k] = 1000 + k; d.aux_x1[k] = 3000 + k;
            d.aux_y0[k] = 500 + k;  d.aux_y1[k] = 2500 + k;
        }
    }
    if (g_sizes_sten) {
        /* worst-emission stencil rider on the (already D24S8) z config:
           not-equal stencil func, three saturating ops, partial masks */
        d.sten_on  = 1;
        d.sshift   = 0;
        d.sfn      = 6;
        d.sfail_op = 3;
        d.zpass_op = 4;
        d.zfail_op = 3;
        d.sref     = 0xa5;
        d.svmask   = 0x5a;
        d.swmask   = 0xa5;
    }
    d.wmask    = dst8888 ? 0x12345678u : 0x1234u;
    d.do_persp = 1;
    d.sec_sel  = 1;
    d.lod_bias = 0.333333f;
    d.premult  = 0;
    for (int k = 0; k < 4; k++)
        d.cc[k] = 0.333333f + 0.1f * (float) k;
    sizes_mk_stage(&d, 0, c0, dt0, s3tc0, clamp);
    if (d.sec_en)
        sizes_mk_stage(&d, 1, c1, dt1, s3tc1, clamp);
    if (g_sizes_ck) {
        /* worst-case key config: both compares live, full masks */
        d.ck3d_on  = 1;
        d.ckc_on   = 1;
        d.ckfn     = 3;
        d.ck3d_msk = 0xffffffffu;
        d.ck3d_clr = 0x00c0ffeeu;
        d.ckc_msk  = 0xffffffffu;
        d.ckc_clr  = 0x00c0ffeeu;
        d.need_ck  = 1;
    }
    d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
              || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1));
    d.need_lod2 = d.sec_en
               && ((!d.sh[1].mipdis && d.sh[1].minb >= 2)
                   || ((int) (d.sh[1].minb & 1) != (d.sh[1].mag == 1)));
    d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
    d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
    d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
    d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
#if JHT_EXEC_X64
    /* x86-64 host: raw first pass, no scalar retry -- -1 is the overflow
       the probe must see (the production wrapper would re-emit scalar) */
    if (!r128_jit_texinline_can(&d) || !r128_jit_x64_can(&d))
        return -2;
    g_sizes_soa = r128_jit_x64_soa_tex_can(&d) && !g_sizes_nosoa;
    if (g_sizes_req_soa && !g_sizes_soa)
        return -3;
    len = r128_jit_x64_generate_1(code, &d, g_sizes_nosoa);
    return len;
#else
    if (!r128_a64_texinline_can(&d))
        return -2; /* not an inline-family state: out of scope */
    g_sizes_soa = r128_a64_soa_tex_can(&d);
    if (g_sizes_req_soa && !g_sizes_soa)
        return -3; /* budget-gated: no vector block to measure */
    pthread_jit_write_protect_np(0);
    /* raw first pass, no scalar retry: -1 = the state as gated would
       overflow, which the probe must see */
    len = r128_jit_arm64_generate_1(code, &d, 0);
    pthread_jit_write_protect_np(1);
    return len;
#endif
}

/* Class matrix per backend: both sweep all 12 filter classes. The
   prepasses want an always-admitted mid-weight pair: m-bl x m-bl on
   arm64, n-bl x n-bl on x64 (its m-bl pair overflows the cap). */
#if JHT_EXEC_X64
#    define SIZES_NCLS 12
#    define SIZES_MID  1
#else
#    define SIZES_NCLS 12
#    define SIZES_MID  3
#endif

#if JHT_EXEC_X64
static int      g_sizes_single_only; /* "sizes single": skip the dual sweeps */
static int      g_sizes_wsf;  /* worst spec/fog/aux riders, picked by the
                                 single-stage prepass, applied to the dual
                                 sweeps too (the pair budget carries them) */
static uint32_t g_sizes_waux;

/* Single-stage sweep (x64): every filter class, the undocumented
   PRIM_MIN_BLEND_FCN codes 6/7 included (modeled as mip-nearest), x 15 dts x
   2 dsts x 16 clamp pairs x stage-0 key off/on, at the worst spec/fog/aux
   riders (prepass on an admitted n-bl state; that code is
   class-independent). Refused states emit their scalar block and count as
   gated; an OVERFLOW is a state generate cannot fit under the 16 KB cap.
   Each admitted state reruns riders-off for the same-state rider delta. */
static void
sizes_single(uint8_t *code, const size_class *cls, const uint32_t dts[][2],
             uint32_t bs, uint32_t bd)
{
    static const size_class res67[2] = { { "m6-nr", 6, 0, 0 }, { "m7-nr", 7, 0, 0 } };
    int      best = -1, l0;
    int      wsf  = 0;
    uint32_t waux = 0;

    int      bsf = -1, bax = -1; /* spec/fog only, aux only */

    g_sizes_single = 1;
    for (int sf = 0; sf < 4; sf++)
        for (uint32_t ax = 0; ax < 64; ax++) {
            int len;

            g_sizes_sf  = sf;
            g_sizes_aux = ax;
            len = sizes_gen(code, &cls[1], NULL, 4, 0, 0, 0, 0, 0, bs, bd);
            if (len > best) { best = len; wsf = sf; waux = ax; }
            if (ax == 0 && len > bsf) bsf = len;
            if (sf == 0 && len > bax) bax = len;
        }
    g_sizes_sf  = 0;
    g_sizes_aux = 0;
    l0 = sizes_gen(code, &cls[1], NULL, 4, 0, 0, 0, 0, 0, bs, bd);
    printf("sizes single: worst riders spec=%d fog=%d aux_cntl=%#x len=%d"
           " (riders off %d; spec/fog alone +%d, aux alone +%d)\n",
           wsf & 1, wsf >> 1, waux, best, l0, bsf - l0, bax - l0);
    g_sizes_wsf  = wsf;
    g_sizes_waux = waux;

    for (int c = 0; c < 14; c++) {
        const size_class *sc = (c < 12) ? &cls[c] : &res67[c - 12];

        for (int ck = 0; ck < 2; ck++) {
            int mx = -1, mxi = 0, mxdst = 0, over = 0, soa = 0, gated = 0;
            int dmax = -1, dmin = 1 << 30;
            int soa0 = 0, mx0 = -1, over0 = 0;
            int rmax = -1, rover = 0; /* the overflow states' scalar retry */
            uint32_t mxcl = 0;

            for (int i = 0; i < 15; i++)
                for (int dst = 0; dst < 2; dst++)
                    for (uint32_t cl = 0; cl < 16; cl++) {
                        int len;

                        g_sizes_ck  = ck;
                        g_sizes_sf  = wsf;
                        g_sizes_aux = waux;
                        len = sizes_gen(code, sc, NULL, dts[i][0], dts[i][1],
                                        0, 0, cl, dst, bs, bd);
                        if (len == -2)
                            continue;
                        if (len == -1 || !g_sizes_soa) {
                            /* overflow or refused with the riders on:
                               the riders-off state is its own cell (a
                               rider-keyed refusal admits it, and it
                               must fit too) */
                            if (len == -1) {
                                /* what the production wrapper re-emits
                                   for this state: the scalar block */
                                int lr;

                                over++;
                                g_sizes_nosoa = 1;
                                lr = sizes_gen(code, sc, NULL, dts[i][0], dts[i][1],
                                               0, 0, cl, dst, bs, bd);
                                g_sizes_nosoa = 0;
                                if (lr == -1)
                                    rover++;
                                else if (lr > rmax)
                                    rmax = lr;
                            } else
                                gated++;
                            g_sizes_sf  = 0;
                            g_sizes_aux = 0;
                            l0 = sizes_gen(code, sc, NULL, dts[i][0], dts[i][1],
                                           0, 0, cl, dst, bs, bd);
                            if (l0 == -1)
                                over0++;
                            else if (l0 > 0 && g_sizes_soa) {
                                soa0++;
                                if (l0 > mx0)
                                    mx0 = l0;
                            }
                            continue;
                        }
                        soa++;
                        if (len > mx) {
                            mx = len; mxi = i; mxdst = dst; mxcl = cl;
                        }
                        g_sizes_sf  = 0;
                        g_sizes_aux = 0;
                        l0 = sizes_gen(code, sc, NULL, dts[i][0], dts[i][1],
                                       0, 0, cl, dst, bs, bd);
                        if (l0 > 0 && g_sizes_soa) {
                            if (len - l0 > dmax) dmax = len - l0;
                            if (len - l0 < dmin) dmin = len - l0;
                        }
                    }
            printf("single %s%-6s max=%5d (dt %u/%u dst %s clamp %u)"
                   " soa=%d gated=%d", ck ? "ck+" : "   ", sc->name, mx,
                   dts[mxi][0], dts[mxi][1], mxdst ? "8888" : "565", mxcl,
                   soa, gated);
            if (soa)
                printf(" rider delta %d..%d", dmin, dmax);
            if (soa0)
                printf(" riders-off soa=%d max=%d", soa0, mx0);
            printf("%s\n", (over || over0) ? "  OVERFLOWS" : "");
            if (over)
                printf("    %d states OVERFLOW (generate -1): scalar retry max=%d,"
                       " %d still overflow\n", over, rmax, rover);
            if (over0)
                printf("    %d riders-off states OVERFLOW (generate -1)\n", over0);
            fflush(stdout);
        }
    }
    g_sizes_ck     = 0;
    g_sizes_sf     = 0;
    g_sizes_aux    = 0;
    g_sizes_single = 0;
}
#endif

static int
sizes_main(uint8_t *code)
{
    static const size_class cls[12] = {
        { "n-nr", 0, 0, 1 }, { "n-bl", 1, 1, 1 },
        { "m-nr", 2, 0, 0 }, { "m-bl", 3, 1, 0 },
        { "sb-nb", 0, 1, 1 }, { "sb-bn", 1, 0, 1 },
        { "sm-nb", 2, 1, 0 }, { "sm-bn", 3, 0, 0 },
        { "t-nr", 4, 0, 0 }, { "t-bl", 5, 1, 0 },
        { "ts-nb", 4, 1, 0 }, { "ts-bn", 5, 0, 0 },
    };
    static const uint32_t dts[15][2] = {
        { 0, 0 }, { 0, 3 }, { 1, 0 }, { 2, 0 }, { 3, 0 },
        { 4, 0 }, { 5, 0 }, { 6, 0 }, { 7, 0 }, { 8, 0 },
        { 9, 0 }, { 11, 0 }, { 12, 0 }, { 14, 0 }, { 15, 0 },
    };
    uint32_t worst_bs = 3, worst_bd = 3;
    int      best = -1;

    /* prepass 0: confirm the worst-size combine op baked into
       sizes_mk_stage -- sweep the full comb x comba x fmsb space on an
       always-admitted mid-weight SoA pair (blend/comb code is emitted
       once per block, class-independent, so the argmax transfers) */
    {
        int mx = -1;
        uint32_t mc = 0, ma = 0, mf = 0;

        for (uint32_t cb = 0; cb < 16; cb++)
            for (uint32_t ca = 0; ca < 16; ca++)
                for (uint32_t fm = 0; fm < 2; fm++) {
                    g_sizes_comb = 0x8000u | (fm << 8) | (cb << 4) | ca;
                    int len = sizes_gen(code, &cls[SIZES_MID], &cls[SIZES_MID], 4, 0, 4, 0,
                                        0, 0, 3, 3);

                    if (len > mx) { mx = len; mc = cb; ma = ca; mf = fm; }
                }
        g_sizes_comb = 0;
        printf("sizes: worst comb=%u comba=%u fmsb=%u len=%d"
               " (baked: comb=15 comba=14 fmsb=0)\n", mc, ma, mf, mx);
    }

    /* prepass 1: worst blend factor pair, same mid-weight SoA pair */
    for (uint32_t bs = 0; bs <= 0xc; bs++)
        for (uint32_t bd = 0; bd <= 0xb; bd++) {
            int len = sizes_gen(code, &cls[SIZES_MID], &cls[SIZES_MID], 4, 0, 4, 0,
                                0, 0, bs, bd);

            if (len > best) { best = len; worst_bs = bs; worst_bd = bd; }
        }
    printf("sizes: worst bsrc=%#x bdst=%#x\n", worst_bs, worst_bd);

    if (g_sizes_sten) {
        /* stencil rider prepass: exact same-state deltas (sten on
           vs off) across the class matrix on a bounded dt/clamp slice
           -- the sten emission (z-block pack + post-pack stage) is
           class-independent by construction, so flatness here bounds
           the rider. at=0 pairs cover states where sten alone forces
           the defer/cover plumbing; budget-gated cells are skipped
           (both sides must compare vector blocks). The main and ck
           sweeps below then run WITH the sten rider under the live
           gate: 0 overflows validates the R128_A64_SOA_STEN_W gate
           constant end to end. */
        static const uint32_t ddt[3][4] = {
            { 0, 3, 0, 3 }, { 5, 0, 5, 0 }, { 11, 0, 4, 0 }
        };
        /* x64 also runs the single-stage rows first (b unused) and
           prints no gate constant: its sten rider is a pair weight */
        for (int sg = JHT_EXEC_X64 ? 1 : 0; sg >= 0; sg--) {
            const int nb = sg ? 1 : 12;
            int dmax = -1, dmin = 1 << 30, skipped = 0, cells = 0;

            g_sizes_single  = sg;
            g_sizes_req_soa = 1;
            for (int noat = 0; noat < 2; noat++)
                for (int a = 0; a < 12; a++)
                    for (int b = 0; b < nb; b++)
                        for (int t = 0; t < 3; t++)
                            for (int dst = 0; dst < 2; dst++)
                                for (uint32_t cl = 0; cl < 16; cl += 5) {
                                    const size_class *c1 = sg ? NULL : &cls[b];
                                    int l0, l1;

                                    g_sizes_noat = noat;
                                    g_sizes_sten = 0;
                                    l0 = sizes_gen(code, &cls[a], c1,
                                                   ddt[t][0], ddt[t][1],
                                                   ddt[t][2], ddt[t][3],
                                                   cl, dst, worst_bs, worst_bd);
                                    g_sizes_sten = 1;
                                    l1 = sizes_gen(code, &cls[a], c1,
                                                   ddt[t][0], ddt[t][1],
                                                   ddt[t][2], ddt[t][3],
                                                   cl, dst, worst_bs, worst_bd);
                                    g_sizes_noat = 0;
                                    if (l0 <= 0 || l1 <= 0) {
                                        skipped++;
                                        continue;
                                    }
                                    cells++;
                                    if (l1 - l0 > dmax) dmax = l1 - l0;
                                    if (l1 - l0 < dmin) dmin = l1 - l0;
                                }
            g_sizes_req_soa = 0;
            g_sizes_single  = 0;
            printf("sizes: sten same-state delta min=%d max=%d over %d %s cells"
                   " (%d gated cells skipped)", dmin, dmax, cells,
                   sg ? "single-stage" : "dual", skipped);
#if JHT_EXEC_X64
            printf("\n");
#else
            printf("; gate constant=%d\n", R128_A64_SOA_STEN_W);
#endif
            fflush(stdout);
        }
    }

#if JHT_EXEC_X64
    sizes_single(code, cls, dts, worst_bs, worst_bd);
    if (g_sizes_single_only)
        return 0;
    /* the dual sweeps run with the same worst riders: the pair budget
       has to carry them, and each pair also prints its same-state
       riders-off delta */
    g_sizes_sf  = g_sizes_wsf;
    g_sizes_aux = g_sizes_waux;
    printf("sizes dual: riders spec=%d fog=%d aux_cntl=%#x on every pair\n",
           g_sizes_wsf & 1, g_sizes_wsf >> 1, g_sizes_waux);
#endif

    /* main sweep: 12x12 class pairs x 15x15 dt pairs x 2 dsts x all 16
       clamp mode pairs. Clamp cost rides every gather (emit_texel +
       wrap emitters), so its worst is class-dependent and must be
       swept per cell, not hoisted from a prepass. */
    for (int a = 0; a < SIZES_NCLS; a++)
        for (int b = 0; b < SIZES_NCLS; b++) {
            int      mx = -1, mxd0 = 0, mxd1 = 0, mxdst = 0, over = 0;
#if JHT_EXEC_X64
            int      soa = 0, dmax = -1, dmin = 1 << 30;
            int      soa0 = 0, mx0 = -1, over0 = 0;
#endif

            for (int i = 0; i < 15; i++)
                for (int j = 0; j < 15; j++)
                    for (int dst = 0; dst < 2; dst++)
                        for (uint32_t cl = 0; cl < 16; cl++) {
                            int len = sizes_gen(code, &cls[a], &cls[b],
                                                dts[i][0], dts[i][1],
                                                dts[j][0], dts[j][1],
                                                cl, dst, worst_bs, worst_bd);
#if JHT_EXEC_X64
                            int rider_on = len > 0 && g_sizes_soa;
                            int l0;
#endif

                            if (len == -1)
                                over++;
                            if (len > mx) {
                                mx = len; mxd0 = i; mxd1 = j; mxdst = dst;
                            }
#if JHT_EXEC_X64
                            /* the same state riders-off: its own cell
                               (the rider-keyed budget admits it on its
                               own), and the pair rider weight where
                               both sides are vector blocks */
                            g_sizes_sf  = 0;
                            g_sizes_aux = 0;
                            l0 = sizes_gen(code, &cls[a], &cls[b],
                                           dts[i][0], dts[i][1],
                                           dts[j][0], dts[j][1],
                                           cl, dst, worst_bs, worst_bd);
                            g_sizes_sf  = g_sizes_wsf;
                            g_sizes_aux = g_sizes_waux;
                            if (l0 == -1)
                                over0++;
                            if (l0 > 0 && g_sizes_soa) {
                                soa0++;
                                if (l0 > mx0)
                                    mx0 = l0;
                                if (rider_on) {
                                    soa++;
                                    if (len - l0 > dmax) dmax = len - l0;
                                    if (len - l0 < dmin) dmin = len - l0;
                                }
                            }
#endif
                        }
            printf("%-6s x %-6s max=%5d (dt %u/%u x %u/%u dst %s)%s\n",
                   cls[a].name, cls[b].name, mx,
                   dts[mxd0][0], dts[mxd0][1], dts[mxd1][0], dts[mxd1][1],
                   mxdst ? "8888" : "565",
                   over ? "  OVERFLOWS" : "");
            if (over)
                printf("    %d of 7200 states OVERFLOW (generate -1)\n", over);
#if JHT_EXEC_X64
            printf("    soa=%d of 7200", soa);
            if (dmax >= 0)
                printf(" rider delta %d..%d", dmin, dmax);
            printf(" riders-off soa=%d max=%d%s\n", soa0, mx0,
                   over0 ? "  OVERFLOWS" : "");
            if (over0)
                printf("    %d riders-off states OVERFLOW (generate -1)\n", over0);
#endif
            fflush(stdout);
        }

    /* The modeled chroma-key sampler fetches tnear separately from filtered
       texels, so its cost depends on the stage-0 class. Sweep each class
       with the worst key against every stage-1 class to measure the
       soa_stage_weight key rider. Use the main sweep's riders, including
       alpha test, which shares the coverage stash above the frame. */
    for (int a = 0; a < SIZES_NCLS; a++)
        for (int b = 0; b < SIZES_NCLS; b++) {
            int mx = -1, mxd0 = 0, mxd1 = 0, mxdst = 0, over = 0;
#if JHT_EXEC_X64
            int dmax = -1, donly = 0;
#endif

            for (int i = 0; i < 15; i++)
                for (int j = 0; j < 15; j++)
                    for (int dst = 0; dst < 2; dst++)
                        for (uint32_t cl = 0; cl < 16; cl++) {
                            int len, l0;

                            g_sizes_ck = 1;
                            len = sizes_gen(code, &cls[a], &cls[b],
                                            dts[i][0], dts[i][1],
                                            dts[j][0], dts[j][1],
                                            cl, dst, worst_bs, worst_bd);
                            if (len == -1)
                                over++;
                            if (len > mx) {
                                mx = len; mxd0 = i; mxd1 = j; mxdst = dst;
                            }
#if JHT_EXEC_X64
                            /* same-state ck delta (the x64 stage-0 ck
                               rider is read straight off this): both
                               sides must be vector blocks that fit */
                            g_sizes_ck = 0;
                            l0 = sizes_gen(code, &cls[a], &cls[b],
                                           dts[i][0], dts[i][1],
                                           dts[j][0], dts[j][1],
                                           cl, dst, worst_bs, worst_bd);
                            if (len > 0 && l0 > 0 && len - l0 > dmax)
                                dmax = len - l0;
                            if (len == -1 && l0 > 0)
                                donly++;
#else
                            (void) l0;
#endif
                        }
            g_sizes_ck = 0;
            printf("ck+%-4s x %-6s max=%5d (dt %u/%u x %u/%u dst %s)%s\n",
                   cls[a].name, cls[b].name, mx,
                   dts[mxd0][0], dts[mxd0][1], dts[mxd1][0], dts[mxd1][1],
                   mxdst ? "8888" : "565",
                   over ? "  OVERFLOWS" : "");
            if (over)
                printf("    %d of 7200 states OVERFLOW (generate -1)\n", over);
#if JHT_EXEC_X64
            printf("    ck same-state delta max=%d (%d states overflow "
                   "with ck only)\n", dmax, donly);
#endif
            fflush(stdout);
        }
    return 0;
}

/* The large-pixel probe checks dithered RGB565 with depth testing on/off,
   one or two texture stages, and nearest/bilinear filters. g_bigpx moves
   screen x into 500..949, outside the ordinary run_config range below 64.
   Partial coverage and full-buffer comparison expose writes past a lane. */
static void
bigpx_repro(uint8_t *code)
{
    static const case_ent gp3z0 =
        { 4, 0, 1, 2, 2, 0xffff, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    static const case_ent gp3z1 =
        { 4, 1, 1, 2, 2, 0xffff, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    for (uint32_t k = 0; k < TEXA_SZ; k += 4)
        *(uint32_t *) &texarena[k] = rng();
    for (int k = 0; k < 256; k++)
        g_texpal[k] = rng();

    long rounds = 400;
    { const char *E = getenv("JHT_BIGPX_REPRO_ITERS"); if (E) rounds = strtol(E, NULL, 0); }
    g_bigpx = 1;
    g_real_tex = 1;
    printf("bigpx repro: dual+single phase at large px, rounds=%ld\n", rounds);

    for (long r = 0; r < rounds; r++) {
        for (int zc0 = 0; zc0 < 2; zc0++) {
            for (int cfg = 0; cfg < 48; cfg++) {
                rage128_draw_state_t d;
                int dual  = (cfg & 4) != 0;
                int bilin = (cfg & 1) != 0;

                mk_state(&d, zc0 ? &gp3z1 : &gp3z0);
                d.tex_en   = 1;
                d.sec_en   = dual;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = (int) (rng() % 2);
                d.lod_bias = 0.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                mk_tex_stage(&d, 0);
                d.sh[0].dt   = 4;
                d.sh[0].s3tc = 0;
                d.sh[0].minb = bilin ? 1 : 0;
                d.sh[0].mag  = bilin ? 1 : 0;
                if (dual) {
                    mk_tex_stage(&d, 1);
                    d.sh[1].dt   = 3;
                    d.sh[1].s3tc = 0;
                    d.sh[1].minb = bilin ? 1 : 0;
                    d.sh[1].mag  = bilin ? 1 : 0;
                }
                d.ck3d_on = d.ckc_on = d.need_ck = 0;
                d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                          || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1));
                d.need_lod2 = dual
                    && ((!d.sh[1].mipdis && d.sh[1].minb >= 2)
                        || ((int) (d.sh[1].minb & 1) != (d.sh[1].mag == 1)));
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                if (d.need_lod || d.need_lod2 || !r128_a64_texinline_can(&d))
                    continue;
                if (!r128_a64_soa_tex_can(&d))
                    continue;
                memset(&g_tctx, 0, sizeof(g_tctx));
                setup_stage_desc(&d, 0, &g_tctx.sd0);
                if (dual)
                    setup_stage_desc(&d, 1, &g_tctx.sd1);
                g_soa_short = (cfg & 2) != 0;
                if (run_config(code, &d, 300,
                               90000 + (size_t) r * 100000
                                     + (size_t) zc0 * 1000 + (size_t) cfg) < 0)
                    { g_bigpx = 0; g_real_tex = 0; return; }
                if (total_fail >= 5) {
                    printf("bigpx repro: 5 fails, stopping (round %ld)\n", r);
                    g_bigpx = 0; g_real_tex = 0; g_soa_short = 0;
                    return;
                }
            }
        }
    }
    g_soa_short = 0;
    printf("bigpx repro: done, total_fail=%llu\n", (unsigned long long) total_fail);
    g_bigpx = 0;
    g_real_tex = 0;
}

static uint32_t
persp_bits(float f)
{
    uint32_t b;

    memcpy(&b, &f, sizeof(b));
    return b;
}

/* Execute the selection copied from the production emitter separately
   from sampling: a NaN reciprocal can leave the final pixel unchanged.
   The caller supplies the quotient so signed-zero division is not needed. */
static int
persp_select_block(uint8_t *code, int x64, int lod, int st)
{
    uint8_t block[16384], pattern[32], copy[16];
    rage128_draw_state_t d = { .do_persp = 1 };
    int skips[64], nskip = 0, at, size, len;

    if (x64) {
        r128_x64_emit_t e = { .base = block, .tex_sub = { -1, -1 } };
        r128_x64_emit_t p = { .base = pattern };
        r128_x64_emit_t c = { .base = copy };

        if (lod)
            r128_x64_emit_lod(&e, &d, st);
        else
            r128_x64_emit_texstage_inline(&e, &d, skips, &nskip);
        r128_x64_comiss(&p, lod ? 1 : 0, lod ? 14 : 15);
        r128_x64_movss_rr(&c, 14, lod ? 0 : 1);
        for (at = 0; at + p.pos + 6 + c.pos <= e.pos; at++)
            if (!memcmp(block + at, pattern, p.pos))
                break;
        if (at + p.pos + 6 + c.pos > e.pos)
            return -1;
        size = p.pos;
        if (block[at + size] == 0x0f && block[at + size + 1] == 0x8a)
            size += 6;
        if (at + size + 6 + c.pos > e.pos
            || block[at + size] != 0x0f || block[at + size + 1] != 0x84
            || memcmp(block + at + size + 6, copy, c.pos))
            return -1;
        size += 6 + c.pos;
        p = (r128_x64_emit_t) { .base = code };
        r128_x64_alu_r_imm(&p, 5, 1, X64_RSP, 32);
        r128_x64_sse_rm(&p, 0, 0x11, 14, X64_RSP, 0);
        r128_x64_sse_rm(&p, 0, 0x11, 15, X64_RSP, 16);
        if (lod) {
            r128_x64_movss_rr(&p, 14, 1);
            r128_x64_movss_rr(&p, 1, 0);
            r128_x64_movss_rr(&p, 0, 14);
            r128_x64_xorps(&p, 14, 14);
        } else {
            r128_x64_mov_r32_imm32(&p, X64_RAX, 0x3f800000);
            r128_x64_movd_x_r(&p, 14, X64_RAX);
            r128_x64_xorps(&p, 15, 15);
        }
        memcpy(code + p.pos, block + at, size);
        p.pos += size;
        r128_x64_movss_rr(&p, 0, 14);
        r128_x64_movups_ld(&p, 14, X64_RSP, 0);
        r128_x64_movups_ld(&p, 15, X64_RSP, 16);
        r128_x64_alu_r_imm(&p, 0, 1, X64_RSP, 32);
        r128_x64_e8(&p, 0xc3);
        len = p.pos;
    } else {
        r128_a64_emit_t e = { .base = block };
        r128_a64_emit_t p = { .base = pattern };

        if (lod)
            r128_a64_emit_lod(&e, &d, st);
        else
            r128_a64_emit_texstage_inline(&e, &d, skips, &nskip);
        r128_a64_fcmp_s0(&p, 15);
        r128_a64_fcsel_s(&p, lod ? 29 : 14, lod ? 29 : 26,
                         lod ? 30 : 14, A64_NE);
        size = p.pos;
        for (at = 0; at + size <= e.pos; at += 4)
            if (!memcmp(block + at, pattern, size))
                break;
        if (at + size > e.pos)
            return -1;
        p = (r128_a64_emit_t) { .base = code };
        r128_a64_stp_x_pre(&p, 29, 30, 32);
        r128_a64_str_d(&p, 14, 31, 16);
        r128_a64_str_d(&p, 15, 31, 24);
        r128_a64_fmov_s_s(&p, 15, 0);
        r128_a64_fmov_s_s(&p, lod ? 29 : 26, 1);
        if (lod)
            r128_a64_fmov_s_w(&p, 30, 31);
        else
            r128_a64_fmov_s_imm(&p, 14, 0x70);
        memcpy(code + p.pos, block + at, size);
        p.pos += size;
        r128_a64_fmov_s_s(&p, 0, lod ? 29 : 14);
        r128_a64_ldr_d(&p, 14, 31, 16);
        r128_a64_ldr_d(&p, 15, 31, 24);
        r128_a64_ldp_x_post(&p, 29, 30, 32);
        r128_a64_ret(&p);
        len = p.pos;
    }
    return len;
}

static int
persp_vectors(uint8_t *code, int dump)
{
    static const struct {
        const char *name;
        uint32_t rhw, ir, iw2;
    } vec[] = {
        { "qnan", 0x7fc00000, 0x7fc00000, 0x7fc00000 },
        { "+zero", 0x00000000, 0x3f800000, 0x00000000 },
        { "-zero", 0x80000000, 0x3f800000, 0x00000000 },
        { "two", 0x40000000, 0x3f000000, 0x3e800000 },
        { "+inf", 0x7f800000, 0x00000000, 0x00000000 },
        { "-inf", 0xff800000, 0x80000000, 0x00000000 },
    };
    int selections = 0, failures = 0, rows = 0;

    if (!dump && !JHT_EXEC_A64 && !JHT_EXEC_X64) {
        printf("persp vectors: skipped (no native backend)\n");
        return 0;
    }
    for (int site = 0; site < 3; site++) {
        int lod = site != 0;
        int st = site == 2;
        int x64 = dump || JHT_EXEC_X64;

        pthread_jit_write_protect_np(0);
        int len = persp_select_block(code, x64, lod, st);
        pthread_jit_write_protect_np(1);
        if (len <= 0) {
            printf("persp selection site=%d: generate failed\n", site);
            return -1;
        }
        if (dump) {
            static const char *paths[] = {
                "block_persp_ir_x64.bin", "block_persp_iw2_0_x64.bin",
                "block_persp_iw2_1_x64.bin"
            };
            FILE *f = fopen(paths[site], "wb");

            if (!f)
                return -1;
            int ok = fwrite(code, 1, len, f) == (size_t) len;
            if (fclose(f) || !ok)
                return -1;
            printf("persp dump %s: %d bytes (not executed)\n", paths[site], len);
            continue;
        }
        __builtin___clear_cache((char *) code, (char *) code + len);
        for (size_t vi = 0; vi < sizeof(vec) / sizeof(vec[0]); vi++) {
            float rhw, expected;
            uint32_t want = lod ? vec[vi].iw2 : vec[vi].ir;

            memcpy(&rhw, &vec[vi].rhw, sizeof(rhw));
            memcpy(&expected, &want, sizeof(expected));
            float quotient = rhw != 0.0f
                ? (lod ? 1.0f / (rhw * rhw) : 1.0f / rhw) : expected;
            float got = ((float (*)(float, float)) (void *) code)(rhw, quotient);
            int ok = isnan(expected) ? isnan(got) : persp_bits(got) == want;

            selections++;
            failures += !ok;
            printf("persp select %s %s st=%d: rhw=%08x C=%08x JIT=%08x %s\n",
                   x64 ? "x86-64" : "ARM64", lod ? "iw2" : "ir", st,
                   vec[vi].rhw, persp_bits(quotient), persp_bits(got), ok ? "PASS" : "FAIL");
        }
    }
    if (dump)
        return 0;
    int selection_failures = failures;

    /* Short rows cover vector groups and every scalar remainder. The
       stage masks cover independent LOD enables and both coordinate sets. */
    for (int stages = 1; stages <= 3; stages++)
        for (int lods = 0; lods <= 3; lods++) {
            if (lods & ~stages)
                continue;
            for (int sel = 0; sel <= !!(stages & 2); sel++) {
                rage128_draw_state_t d;
                case_ent ce = { .dt = 6, .tex = stages & 1, .sec = !!(stages & 2) };
                r3d_texctx_t tc = {0};
                uint32_t texels[16];

                mk_state(&d, &ce);
                d.do_persp = 1;
                d.sec_sel = sel;
                d.need_lod = lods & 1;
                d.need_lod2 = !!(lods & 2);
                d.texw0 = d.texh0 = d.texw1 = d.texh1 = 4.0f;
                for (int k = 0; k < 16; k++)
                    texels[k] = 0xff204080u + (uint32_t) k * 0x00030102u;
                for (int st = 0; st < 2; st++) {
                    r3d_stage_hdr_t *h = &d.sh[st];
                    r3d_comb_desc_t *c = &d.comb[st];
                    r3d_stage_desc_t *sd = st ? &tc.sd1 : &tc.sd0;
                    int has_lod = (lods >> st) & 1;

                    h->dt = sd->dt = 6;
                    h->tsp = sd->tsp = 0x222;
                    h->top = sd->top = 2;
                    h->minb = sd->minb = has_lod ? 2 : 0;
                    h->mipdis = sd->mipdis = !has_lod;
                    c->comb = c->comba = 3;
                    c->cfac = c->ifac = 4;
                    c->ifaca = 2; c->afac = 6;
                    sd->slot_valid = 7;
                    for (int sl = 0; sl <= 2; sl++) {
                        sd->slot[sl].lw = sd->slot[sl].lh = 1u << sl;
                        sd->slot[sl].texbase = (const uint8_t *) texels;
                        sd->slot[sl].mask = sizeof(texels) - 1;
                    }
                }
                tc.sta = tc.stb = tc.stc = 0.125f;
                tc.tta = tc.ttb = tc.ttc = 0.25f;
                tc.s2a = tc.s2b = tc.s2c = 0.75f;
                tc.t2a = tc.t2b = tc.t2c = 0.5f;
                tc.dSdx = tc.dTdy = tc.dS2dy = tc.dT2dx = 0.25f;
                tc.dWdx = 0.125f;
                pthread_jit_write_protect_np(0);
#if JHT_EXEC_X64
                int len = getenv("JHT_NOSOA") ? r128_jit_x64_generate_1(code, &d, 1)
                                                : r128_jit_x64_generate(code, &d);
#else
                int len = getenv("JHT_NOSOA") ? r128_jit_arm64_generate_1(code, &d, 1)
                                                : r128_jit_arm64_generate(code, &d);
#endif
                pthread_jit_write_protect_np(1);
                if (len <= 0) {
                    printf("persp rows stages=%d lods=%d sel=%d: generate failed\n", stages, lods, sel);
                    return -1;
                }
                __builtin___clear_cache((char *) code, (char *) code + len);
                for (size_t vi = 0; vi < sizeof(vec) / sizeof(vec[0]); vi++) {
                    float rhw;
                    int before = failures;

                    memcpy(&rhw, &vec[vi].rhw, sizeof(rhw));
                    tc.arhw = tc.brhw = tc.crhw = rhw;
                    for (int n = 1; n <= 9; n++) {
                        uint8_t dst_j[64], dst_r[64];
                        r128_jit_tri_t tj = {0}, tr;

                        memset(dst_j, 0xa5, sizeof(dst_j));
                        memcpy(dst_r, dst_j, sizeof(dst_j));
                        tj.vram = dst_j; tj.vram_mask = sizeof(dst_j) - 1;
                        tj.x1 = n - 1; tj.invs = 0.25f; tj.texctx = &tc;
                        for (int k = 0; k < 4; k++)
                            tj.vca[k] = tj.vcb[k] = tj.vcc[k] = 1.0f;
                        tr = tj; tr.vram = dst_r;
                        r128_jit_span_fn fn = (r128_jit_span_fn) (void *) code;
                        uint64_t rj = JHT_SPAN_CALL(fn, &tj, 2, 1, 1, 0.0, 0, 0, 0);
                        g_real_tex = 1;
                        uint64_t rr = ref_span(&tr, &d, 2, 1, 1, 0.0, 0, 0, 0);
                        g_real_tex = 0;
                        rows++; total_rows++;
                        int bad = rj != rr || memcmp(dst_j, dst_r, sizeof(dst_j));
                        failures += bad; total_fail += bad;
                    }
                    printf("persp rows stages=%d lods=%d sel=%d rhw=%s: rows=9 fail=%d\n",
                           stages, lods, sel, vec[vi].name, failures - before);
                }
            }
        }
    printf("persp vectors: selections=%d selection_failures=%d rows=%d fail=%d\n",
           selections, selection_failures, rows, failures);
    return failures ? -1 : 0;
}

/* One recorded skip after r128_a64_gen_pixskip patched it. The branch
   word is emitted with a zero displacement, so every bit outside the
   displacement field (opcode, register, condition, bit number) must
   read back unchanged, the field must hold the displacement to the
   pix_skip target in the kind's own width (26 bits at [25:0] for B, 19
   at [23:5] for B.cond and CBZ, 14 at [18:5] for TBZ and TBNZ), and
   decoding the field as that width must land on the target. */
static int
branch_patch_check(const char *name, const uint8_t *code, int at, int fix,
                   uint32_t before, int bits)
{
    uint32_t after, fmask, want;
    int32_t  off = (fix - at) >> 2, dec;
    int      ok;

    memcpy(&after, code + at, 4);
    if (bits == 26) {
        fmask = 0x03ffffffu;
        want  = (uint32_t) off & fmask;
        dec   = (int32_t) ((after & fmask) << 6) >> 6;
    } else {
        fmask = ((1u << bits) - 1u) << 5;
        want  = ((uint32_t) off << 5) & fmask;
        dec   = (int32_t) (((after & fmask) >> 5) << (32 - bits)) >> (32 - bits);
    }
    ok = (after & ~fmask) == (before & ~fmask) && (after & fmask) == want
         && at + dec * 4 == fix;
    printf("branch-patch %-9s %-8s at=%3d fix=%3d before=%08x after=%08x"
           " target=%3d %s\n",
           name, off < 0 ? "backward" : "forward", at, fix, before, after,
           at + dec * 4, ok ? "ok" : "FAIL");
    return !ok;
}

/* Directed fixture for the skip patcher's branch classifier. Every kind
   of branch a block records as a skip (TBZ, TBNZ on bit 63, B.cond, CBZ,
   B) is emitted once, recorded, and patched by r128_a64_gen_pixskip;
   each word is then checked by branch_patch_check. The forward set is
   the shape a block produces. The backward set places the pix_skip
   target before the branches, which no block does: a negative
   displacement fills every bit of the field, so a TBZ or TBNZ patched
   through the 19-bit form would overwrite its bit number in [23:19],
   while a forward displacement inside one block never reaches those
   bits and would hide the misclassification. */
static int
branch_patch_main(uint8_t *code)
{
    static const struct { const char *name; int bits; } kind[5] = {
        { "tbz",   14 }, { "tbnz63", 14 }, { "b.cond", 19 },
        { "cbz",   19 }, { "b",      26 },
    };
    int fail = 0;

    for (int backward = 0; backward < 2; backward++) {
        r128_a64_gen_t g;
        uint32_t       before[5];
        int            fix;

        memset(&g, 0, sizeof(g));
        g.e.base = code;
        pthread_jit_write_protect_np(0);
        if (backward) {
            /* room for the pix_skip tail (three adds and the loop
               branch) ahead of the branches */
            for (int i = 0; i < 8; i++)
                r128_a64_add_w_imm(&g.e, 12, 12, 1);
        }
        g.skips[g.nskip++] = r128_a64_tbz(&g.e, 17, 3);
        g.skips[g.nskip++] = r128_a64_tbnz63(&g.e, 16);
        g.skips[g.nskip++] = r128_a64_bcond(&g.e, A64_LE);
        g.skips[g.nskip++] = r128_a64_cbz_w(&g.e, 16);
        g.skips[g.nskip++] = r128_a64_b(&g.e);
        for (int i = 0; i < 5; i++)
            memcpy(&before[i], code + g.skips[i], 4);
        if (backward)
            g.e.pos = 8;
        else
            for (int i = 0; i < 3; i++)
                r128_a64_add_w_imm(&g.e, 12, 12, 1);
        fix = r128_a64_here(&g.e);
        r128_a64_gen_pixskip(&g);
        pthread_jit_write_protect_np(1);
        for (int i = 0; i < 5; i++)
            fail += branch_patch_check(kind[i].name, code, g.skips[i], fix,
                                       before[i], kind[i].bits);
    }
    printf("branch-patch: kinds=10 fail=%d\n", fail);
    return fail;
}

int
main(int argc, char **argv)
{
    if (shard_init() < 0)
        return 2;
    /* Standalone probes own their buffers and output files. Run them once
       and leave the row selector disabled inside the selected probe. */
    if (getenv("JHT_BIGPX_REPRO") || (argc > 1 &&
        (!strcmp(argv[1], "dump") || !strcmp(argv[1], "sizes") ||
         !strcmp(argv[1], "bench") || !strcmp(argv[1], "benchdump") ||
         !strcmp(argv[1], "contend") || !strcmp(argv[1], "rttone") ||
         !strcmp(argv[1], "texone") || !strcmp(argv[1], "persp") ||
         !strcmp(argv[1], "persp-dump") || !strcmp(argv[1], "weights") ||
         !strcmp(argv[1], "weights-dump") || !strcmp(argv[1], "branchpatch")))) {
        if (shard_index)
            return 0;
        shard_count = 1;
    }
    uint8_t *code = map_exec(16384);
    int dump = (argc > 1 && !strcmp(argv[1], "dump"));

    /* progress lines must land in a redirected log as they happen
       (Windows treats _IOLBF as full buffering, so unbuffered) */
    setvbuf(stdout, NULL, _IONBF, 0);

    if (!code) { perror("map_exec"); return 2; }
    g_rtt = getenv("JHT_RTT") != NULL;
#if !JHT_EXEC_A64
    /* exec modes need the emitted arch to match the host; generate-only
       modes (dump/benchdump/sizes) and the ref-only fuzz still work.
       JHT_BIGPX_REPRO goes through run_config, which re-generates with the x64
       backend on an x86-64 host, so it stays available there. bench and
       benchdump generate with the x64 backend on an x86-64 host, so
       only contend (arm64 blocks only) stays off. */
    if (argc > 1 && !strcmp(argv[1], "contend")) {
        fprintf(stderr, "arm64 blocks cannot execute on this host: "
                        "contend unavailable\n");
        return 2;
    }
#if !JHT_EXEC_X64
    if (argc > 1 && !strcmp(argv[1], "bench")) {
        fprintf(stderr, "no native backend on this host: "
                        "bench unavailable\n");
        return 2;
    }
#endif
#if JHT_WIN64_THUNK
    /* forced-Win64 build: fuzz executes leaf blocks through the thunk;
       the bench timing loop calls blocks directly, so it stays off
       (benchdump only generates and stays available) */
    if (argc > 1 && !strcmp(argv[1], "bench")) {
        fprintf(stderr, "forced-Win64 thunk build: bench unavailable\n");
        return 2;
    }
#endif
#if !JHT_EXEC_X64
    if (getenv("JHT_BIGPX_REPRO")) {
        fprintf(stderr, "no native backend on this host: "
                        "JHT_BIGPX_REPRO unavailable\n");
        return 2;
    }
#endif
#endif
    if (argc > 1 && !strcmp(argv[1], "weights"))
        return weight_main(code);
    if (argc > 2 && !strcmp(argv[1], "weights-dump")) {
        r3d_stage_hdr_t h = {0};

        pthread_jit_write_protect_np(0);
        int len = weight_axis_block(code, &h, 0, 0, 1);
        pthread_jit_write_protect_np(1);
        FILE *f = len > 0 ? fopen(argv[2], "wb") : NULL;
        if (!f)
            return 2;
        size_t written = fwrite(code, 1, (size_t) len, f);
        int closed = fclose(f);
        printf("x64 weight block: %d bytes, execution skipped\n", len);
        return written != (size_t) len || closed;
    }
    if (getenv("JHT_BIGPX_REPRO")) { bigpx_repro(code); return 0; }
    if (argc > 1 && !strcmp(argv[1], "benchdump")) {
        g_bench_dump = 1;
        return bench_main(code, 0);
    }
    if (argc > 1 && !strcmp(argv[1], "sizes")) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "sten"))
                g_sizes_sten = 1;
#if JHT_EXEC_X64
            if (!strcmp(argv[i], "single"))
                g_sizes_single_only = 1;
#endif
        }
        return sizes_main(code);
    }
    if (argc > 1 && !strcmp(argv[1], "contend"))
        return contend_main(code);
    if (argc > 1 && !strcmp(argv[1], "bench"))
        return bench_main(code, argc > 2 ? strtoull(argv[2], NULL, 0)
                                         : 100000000ull);

    if (argc > 1 && !strcmp(argv[1], "persp-dump"))
        return persp_vectors(code, 1) < 0 ? 1 : 0;
#if JHT_EXEC_A64 || JHT_EXEC_X64
    if (!dump && !shard_index && fog_nan_vectors(code) < 0)
        return 1;
#endif
    /* The perspective and weight fixtures are not part of the row stream
       the shards divide, so only shard 0 runs them; their rows are then
       counted once in the summed total. */
    if (!dump && !shard_index && persp_vectors(code, 0) < 0)
        return 1;
    if (argc > 1 && !strcmp(argv[1], "persp")) {
        printf("rows=%llu fail=%llu\n", (unsigned long long) total_rows,
               (unsigned long long) total_fail);
        return total_fail ? 1 : 0;
    }
    if (!dump && !shard_index && (JHT_EXEC_A64 || JHT_EXEC_X64) && weight_main(code))
        return 1;
#if JHT_EXEC_A64 || JHT_EXEC_X64
    if (!dump && !shard_index && bias_vectors(code) < 0)
        printf("bias one-pixel vectors: FAIL\n");
    if (!dump && !shard_index && alpha_dst_vectors(code) < 0)
        return 1;
#endif

    if (argc > 1 && !strcmp(argv[1], "branchpatch"))
        return branch_patch_main(code) ? 1 : 0;

    if (argc > 1 && !dump)
        rngs = (uint32_t) strtoul(argv[1], NULL, 0); /* alternate fuzz seed */

    static const case_ent zc[] = {
        /* regression set (untextured) */
        { 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,             0, 0, 0, 0, 0 , 0, 0 },
        { 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,             0, 0, 0, 0, 0 , 0, 0 },
        { 4, 1, 1, 2, 2, 0xffff, 0, 0, 0, 0, 0, 0,        0, 0, 0, 0, 0 , 0, 0 },
        { 6, 1, 1, 2, 4, 0xffffff, 0, 0, 0, 0, 0, 0,      0, 0, 0, 0, 0 , 0, 0 },
        { 6, 1, 0, 5, 4, 0xffffff, 8, 0, 0, 0, 0, 0,      0, 0, 0, 0, 0 , 0, 0 },
        { 4, 1, 1, 7, 2, 0xffff, 0, 0, 0, 0, 0, 0,        0, 0, 0, 0, 0 , 0, 0 },
        { 6, 1, 1, 4, 4, 0xffffffffu, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0 , 0, 0 },
        { 4, 1, 1, 3, 4, 0xffffff, 0, 0, 0, 0, 0, 0,      0, 0, 0, 0, 0 , 0, 0 },
        { 6, 1, 1, 6, 2, 0xffff, 0, 0, 0, 0, 0, 0,        0, 0, 0, 0, 0 , 0, 0 },
        { 4, 1, 1, 1, 4, 0xffffff, 8, 0, 0, 0, 0, 0,      0, 0, 0, 0, 0 , 0, 0 },
        { 4, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,             0, 0, 0, 0, 0 , 0, 0 },
        { 4, 1, 1, 2, 2, 0xffff, 0, 1, 0, 0, 0, 0,        0, 0, 0, 0, 0 , 0, 0 },
        { 4, 1, 1, 4, 4, 0xffffff, 0, 1, 0, 0, 0, 0,      0, 0, 0, 0, 0 , 0, 0 },
        { 6, 1, 1, 2, 4, 0xffffff, 0, 1, 0, 0, 0, 0,      0, 0, 0, 0, 0 , 0, 0 },
        { 6, 0, 0, 0, 0, 0, 0, 0, 1, 0x1, 0x1, 0,         0, 0, 0, 0, 0 , 0, 0 },
        { 6, 0, 0, 0, 0, 0, 0, 0, 1, 0x4, 0x5, 0,         0, 0, 0, 0, 0 , 0, 0 },
        { 4, 0, 0, 0, 0, 0, 0, 1, 1, 0x4, 0x5, 0,         0, 0, 0, 0, 0 , 0, 0 },
        { 6, 1, 1, 2, 4, 0xffffff, 0, 0, 1, 0x2, 0x9, 2,  0, 0, 0, 0, 0 , 0, 0 },
        { 4, 1, 1, 2, 2, 0xffff, 0, 0, 1, 0xb, 0x8, 1,    0, 0, 0, 0, 0 , 0, 0 },
        { 6, 0, 0, 0, 0, 0, 0, 0, 1, 0xc, 0x6, 3,         0, 0, 0, 0, 0 , 0, 0 },
        { 6, 0, 0, 0, 0, 0, 0, 0, 1, 0xa, 0x8, 0,         0, 0, 0, 0, 0 , 0, 0 },
        { 4, 0, 0, 0, 0, 0, 0, 0, 1, 0x3, 0x7, 0,         0, 0, 0, 0, 0 , 0, 0 },
        { 6, 0, 0, 0, 0, 0, 0, 0, 1, 0x0, 0x8, 0,         0, 0, 0, 0, 0 , 0, 0 },
        { 6, 0, 0, 0, 0, 0, 0, 0, 1, 0x9, 0x6, 2,         0, 0, 0, 0, 0 , 0, 0 },
        /* texture-stage call */
        { 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,             1, 0, 0, 0, 0 , 0, 0 },
        { 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,             1, 0, 0, 0, 0 , 0, 0 },
        { 4, 1, 1, 2, 2, 0xffff, 0, 1, 0, 0, 0, 0,        1, 0, 0, 0, 0 , 0, 0 },
        { 6, 1, 1, 2, 4, 0xffffff, 0, 0, 0, 0, 0, 0,      1, 0, 0, 0, 0 , 0, 0 },
        { 6, 1, 0, 5, 4, 0xffffff, 8, 0, 0, 0, 0, 0,      1, 1, 0, 0, 0 , 0, 0 },
        { 4, 1, 1, 4, 4, 0xffffff, 0, 1, 1, 0x4, 0x5, 0,  1, 0, 0, 0, 0 , 0, 0 },
        { 6, 0, 0, 0, 0, 0, 0, 0, 1, 0x1, 0x1, 0,         1, 0, 0, 0, 0 , 0, 0 },
        { 4, 0, 0, 0, 0, 0, 0, 1, 1, 0xb, 0x8, 1,         0, 1, 0, 0, 0 , 0, 0 },
        { 6, 1, 1, 6, 2, 0xffff, 0, 0, 1, 0x2, 0x9, 2,    1, 1, 0, 0, 0 , 0, 0 },
        /* alpha test (with and without texture) */
        { 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,             1, 0, 1, 5, 0x7f , 0, 0 },
        { 4, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,             1, 0, 1, 4, 0x80 , 0, 0 },
        { 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,             0, 0, 1, 1, 0xc0 , 0, 0 },
        { 4, 1, 1, 2, 2, 0xffff, 0, 0, 1, 0x4, 0x5, 0,    1, 0, 1, 2, 0x33 , 0, 0 },
        { 6, 1, 1, 2, 4, 0xffffff, 0, 0, 0, 0, 0, 0,      1, 0, 1, 3, 0x55 , 0, 0 },
        { 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,             1, 0, 1, 6, 0x00 , 0, 0 },
        { 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,             0, 0, 1, 5, 0xff , 0, 0 },
        /* specular + vertex fog (with/without texture, dither, blend) */
        { 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,             0, 0, 0, 0, 0,    1, 0 },
        { 4, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,             0, 0, 0, 0, 0,    0, 1 },
        { 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,             0, 0, 0, 0, 0,    1, 1 },
        { 4, 1, 1, 2, 2, 0xffff, 0, 1, 0, 0, 0, 0,        1, 0, 0, 0, 0,    0, 1 },
        { 6, 1, 1, 2, 4, 0xffffff, 0, 0, 0, 0, 0, 0,      1, 0, 0, 0, 0,    1, 1 },
        { 4, 1, 1, 4, 4, 0xffffff, 0, 1, 1, 0x4, 0x5, 0,  1, 0, 0, 0, 0,    1, 1 },
        { 6, 0, 0, 0, 0, 0, 0, 0, 1, 0x1, 0x1, 0,         1, 1, 0, 0, 0,    0, 1 },
        { 4, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,             1, 0, 1, 5, 0x60, 1, 1 },
    };

    if (argc > 1 && !strcmp(argv[1], "rttone")) {
        /* Render-target sampling must preserve scalar pixel order. Bind a
           texture so lane k samples the destination cell of lane k-1.
           Gathering all four texels before storing a vector group makes
           lanes 1..3 read bytes that the interpreter already overwrites;
           lane 0 reads the preceding group, already stored in either path.
           Build this geometry directly because random gradients do not
           reliably reach the three-texel window before the span. A separate
           reference texture context reads its own destination buffer so
           shared inputs cannot hide an ordering difference. */
        static rage128_draw_state_t d;
        case_ent     ce;
        r3d_texctx_t tctx_r;
        const uint32_t LW   = 1024;      /* level width, power of two    */
        const uint32_t DROW = 0x1000;    /* dst row base, < VRAM_SZ/4    */
        const int32_t  X0   = 64;        /* group-aligned span start     */
        const int32_t  SPAN = 256;
        /* s(px) = (px - 1) / LW exactly: e0 steps by 4096 per pixel,
           invs = 2^-22, so w0 * LW == px - 1 in exact binary floats and
           the 1/4096 coordinate quantizer is a no-op. */
        const int64_t  E0DXI = 4096;
        int rc = 0;

        memset(&ce, 0, sizeof(ce));
        ce.dt  = 4; /* dst RGB565, matching sh[0].dt so the texel byte
                       width equals the destination cell width */
        ce.tex = 1;
        mk_state(&d, &ce);
        d.do_persp = 0;
        d.sh[0].tsp     = 10;   /* wexp 10 (1024), top 0, hexp 0 (lh 1)  */
        d.sh[0].dt      = 4;
        d.sh[0].s3tc    = 0;
        d.sh[0].clamp_s = 0;    /* repeat */
        d.sh[0].clamp_t = 0;
        d.sh[0].border  = 0;
        d.sh[0].minb    = 0;    /* nearest, no LOD */
        d.sh[0].mag     = 0;
        d.sh[0].mipdis  = 1;
        d.sh[0].top     = 0;
        d.comb[0].comb  = 3;    /* modulate */
        d.comb[0].comba = 3;
        d.comb[0].cfac  = 4;
        d.comb[0].ifac  = 4;
        d.comb[0].ifaca = 2;
        d.comb[0].afac  = 6;
        d.need_lod = 0;
        d.texw0 = (float) LW;
        d.texh0 = 1.0f;
        /* the texture IS the render target: production capture routes
           this draw to the scalar block, so both legs must be clean */
        d.soa_selftex = 1;
        if (!r128_a64_texinline_can(&d)) {
            printf("rttone: state is not inline-family -- vector bug\n");
            return 2;
        }
        memset(&g_tctx, 0, sizeof(g_tctx));
        setup_stage_desc(&d, 0, &g_tctx.sd0);
        g_tctx.sd0.slot[0].lw      = LW;
        g_tctx.sd0.slot[0].lh      = 1;
        g_tctx.sd0.slot[0].texbase = vram_j;   /* the render target */
        g_tctx.sd0.slot[0].base    = DROW;
        g_tctx.sd0.slot[0].mask    = VRAM_SZ - 1;
        g_tctx.sta = 1.0f; g_tctx.stb = 0.0f; g_tctx.stc = 0.0f;
        g_tctx.tta = 0.0f; g_tctx.ttb = 0.0f; g_tctx.ttc = 0.0f;
        /* the reference's own view of the same texture: byte-identical
           except that it reads ITS destination buffer, so each side
           self-samples its own render target instead of sharing one */
        tctx_r = g_tctx;
        tctx_r.sd0.slot[0].texbase = vram_r;
        g_real_tex = 1;

        printf("rttone: soa_tex_can=%d texinline_can=%d\n",
               r128_a64_soa_tex_can(&d), r128_a64_texinline_can(&d));
        for (int leg = 0; leg < 2; leg++) {
            r128_jit_tri_t tj, tr;
            uint64_t       rj, rr;
            int            nd = 0, phase[4] = { 0, 0, 0, 0 };
            int            len;

            pthread_jit_write_protect_np(0);
            len = r128_jit_arm64_generate_1(code, &d, leg /* no_soa */);
            pthread_jit_write_protect_np(1);
            if (len <= 0) {
                printf("rttone: leg %d generate failed\n", leg);
                return 3;
            }
            __builtin___clear_cache((char *) code, (char *) code + len);

            for (uint32_t k = 0; k < VRAM_SZ; k += 4)
                *(uint32_t *) &vram_j[k] = rng();
            memcpy(vram_r, vram_j, VRAM_SZ);

            memset(&tj, 0, sizeof(tj));
            tj.vram_mask = VRAM_SZ - 1;
            tj.x0    = X0;
            tj.x1    = X0 + SPAN - 1;
            tj.e0dxi = E0DXI;
            tj.e1dxi = 0;
            tj.e2dxi = 0;
            tj.invs  = 1.0f / (float) (E0DXI * (int64_t) LW);
            tj.dZdx  = 0.0;
            for (int k = 0; k < 4; k++) {
                tj.vca[k] = 1.0f;
                tj.vcb[k] = 0.0f;
                tj.vcc[k] = 0.0f;
            }
            tr = tj;
            tj.vram = vram_j; tj.texctx = &g_tctx;
            tr.vram = vram_r; tr.texctx = &tctx_r;

            rj = ((r128_jit_span_fn) (void *) code)(
                &tj, E0DXI * (X0 - 1), 1, 1, 0.0, DROW, 0, 0);
            rr = ref_span(&tr, &d, E0DXI * (X0 - 1), 1, 1, 0.0, DROW, 0, 0);

            for (int32_t px = X0; px < X0 + SPAN; px++) {
                uint32_t a = DROW + (uint32_t) px * 2u;

                if (*(uint16_t *) &vram_j[a] != *(uint16_t *) &vram_r[a]) {
                    phase[(px - X0) & 3]++;
                    nd++;
                }
            }
            printf("rttone leg %d (%s): len=%d rx jit=%016llx ref=%016llx"
                   " diff=%d/%d phase=[%d %d %d %d]\n",
                   leg, leg ? "no_soa" : "SoA",
                   len, (unsigned long long) rj, (unsigned long long) rr,
                   nd, SPAN, phase[0], phase[1], phase[2], phase[3]);
            if (memcmp(vram_j, vram_r, VRAM_SZ) && !nd)
                printf("rttone leg %d: divergence OUTSIDE the span\n", leg);
            if (nd)
                rc = 1;
        }
        g_real_tex = 0;
        return rc;
    }

    if (argc > 1 && !strcmp(argv[1], "texone")) {
        /* The texture probe uses a fixed dual-stage state. Environment
           switches isolate depth, coordinates, filters, key and stencil. */
        rage128_draw_state_t d;

        for (uint32_t k = 0; k < TEXA_SZ; k += 4)
            *(uint32_t *) &texarena[k] = rng();
        for (int k = 0; k < 256; k++)
            g_texpal[k] = rng();
        mk_state(&d, &zc[getenv("TEXONE_Z") ? 2 : 0]);
        d.tex_en = 1; d.sec_en = 1;
        d.do_persp = 1; d.sec_sel = 0;
        d.lod_bias = 0.4609375f;
        d.premult = 0;
        d.cc[0] = 0.376470596f; d.cc[1] = 0.247058824f;
        d.cc[2] = 0.572549045f; d.cc[3] = 0.788235307f;
        d.sh[0].tsp = 0x372; d.sh[0].dt = 0; d.sh[0].s3tc = 1;
        d.sh[0].clamp_s = 0; d.sh[0].clamp_t = 3;
        d.sh[0].border = 0xe11195bcu;
        d.sh[0].minb = 5; d.sh[0].mag = 0; d.sh[0].mipdis = 1; d.sh[0].top = 7;
        d.sh[1].tsp = 0x475; d.sh[1].dt = 4; d.sh[1].s3tc = 0;
        d.sh[1].clamp_s = 3; d.sh[1].clamp_t = 2;
        d.sh[1].border = 0x49cca90cu;
        d.sh[1].minb = 1; d.sh[1].mag = 0; d.sh[1].mipdis = 1; d.sh[1].top = 7;
        d.comb[0].comb = 4; d.comb[0].comba = 3; d.comb[0].fmsb = 0;
        d.comb[0].cfac = 2; d.comb[0].afac = 6;
        d.comb[0].ifac = 4; d.comb[0].ifaca = 4;
        d.comb[1].comb = 5; d.comb[1].comba = 14; d.comb[1].fmsb = 0;
        d.comb[1].cfac = 4; d.comb[1].afac = 6;
        d.comb[1].ifac = 8; d.comb[1].ifaca = 4;
        d.ck3d_on = 0; d.ckc_on = 1; d.ckfn = 3;
        d.ck3d_msk = 0xff000000u; d.ck3d_clr = 0xffca695du;
        d.ckc_msk = 0x00080000u; d.ckc_clr = 0xea5f300bu;
        if (getenv("TEXONE_NOSEC")) d.sec_en = 0;
        if (getenv("TEXONE_NOCK")) d.ckc_on = 0;
        if (getenv("TEXONE_NOPERSP")) d.do_persp = 0;
        if (getenv("TEXONE_DT4")) { d.sh[0].dt = 4; d.sh[0].s3tc = 0; }
        if (getenv("TEXONE_WRAP")) {
            d.sh[0].clamp_s = d.sh[0].clamp_t = 0;
            d.sh[1].clamp_s = d.sh[1].clamp_t = 0;
        }
        if (getenv("TEXONE_SAME")) {
            d.sh[0].minb = 1; d.sh[0].mag = 1;
            d.sh[1].minb = 1; d.sh[1].mag = 1;
        }
        if (getenv("TEXONE_NEAREST")) {
            d.sh[0].minb = 0; d.sh[0].mag = 0;
            d.sh[1].minb = 0; d.sh[1].mag = 0;
        }
        if (getenv("TEXONE_MIP")) {
            /* mip'd stage 0: PRIM_MIN_BLEND_FCN code from the environment
               with mipping on and the matching magnification (no split); 2/3
               = mip-nearest, 4/5 = tri */
            d.sh[0].minb   = (uint32_t) strtoul(getenv("TEXONE_MIP"), NULL, 0);
            d.sh[0].mipdis = 0;
            d.sh[0].mag    = d.sh[0].minb & 1;
        }
        if (getenv("TEXONE_MIP2")) {
            /* same for stage 1 (its default is a base-level minification/magnification
               split, which the SoA gates keep scalar) */
            d.sh[1].minb   = (uint32_t) strtoul(getenv("TEXONE_MIP2"), NULL, 0);
            d.sh[1].mipdis = 0;
            d.sh[1].mag    = d.sh[1].minb & 1;
        }
        /* magnification override after the minification pick: a mismatch
           forces the minification/magnification split, and the reserved code
           7 is mip-nearest only with magnification nearest */
        if (getenv("TEXONE_MAG"))
            d.sh[0].mag = (uint32_t) strtoul(getenv("TEXONE_MAG"), NULL, 0);
        if (getenv("TEXONE_MAG2"))
            d.sh[1].mag = (uint32_t) strtoul(getenv("TEXONE_MAG2"), NULL, 0);
        if (getenv("TEXONE_STEN")) {
            /* D24S8 stencil on the textured state: not-equal test, saturating
               ops, partial masks, so lanes both pass and fail */
            d.z_en   = 1; d.z_wr = 1; d.zfn = 2;
            d.zbpp   = 4; d.zmax = 0xffffff; d.zshift = 8;
            d.sten_on  = 1; d.sshift = 0;
            d.sfn      = 6;
            d.sfail_op = 3; d.zpass_op = 4; d.zfail_op = 3;
            d.sref     = 0xa5; d.svmask = 0x5a; d.swmask = 0xa5;
        }
        d.need_ck = d.ck3d_on || d.ckc_on;
        d.need_lod = d.tex_en
            && ((!d.sh[0].mipdis && d.sh[0].minb >= 2)
                || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1)));
        d.need_lod2 = d.sec_en
            && ((!d.sh[1].mipdis && d.sh[1].minb >= 2)
                || ((int) (d.sh[1].minb & 1) != (d.sh[1].mag == 1)));
        d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
        d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
        d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
        d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
        memset(&g_tctx, 0, sizeof(g_tctx));
        if (d.tex_en)
            setup_stage_desc(&d, 0, &g_tctx.sd0);
        if (d.sec_en)
            setup_stage_desc(&d, 1, &g_tctx.sd1);
        g_real_tex = 1;
        printf("texone: lod=%d/%d run...\n", d.need_lod, d.need_lod2);
        if (run_config(code, &d, 4000, 9999) < 0)
            return 3;
        printf("texone: rows=%llu fail=%llu\n",
               (unsigned long long) total_rows, (unsigned long long) total_fail);
        return total_fail ? 1 : 0;
    }

    if (argc > 1 && !strcmp(argv[1], "weight")) {
        /* bilinear weights past 2^31 texels: one 256x256 ARGB8888
           stage, bilinear, wrap, texel copied through, no perspective,
           with S past 2^31 texels at every pixel. The weight is
           rr_f2u of (fu - u0) * 256 + 0.5 with u0 = rr_f2i(floor(fu));
           the conversions are printed for fu = 2147483904, then the
           block is compared with the reference on rows whose
           coordinates are all past 2^31. */
        rage128_draw_state_t d;
        volatile float fx = 2147483904.0f;
        float    fu = rr_texcoord_fx(fx) - 0.5f;
        int      u0 = rr_f2i(floorf(fu));
        float    wf = (fu - (float) u0) * 256.0f + 0.5f;
        uint32_t wu = rr_f2u(wf);

        printf("weight: conversions at fu=%.1f: u0=%d weight_float=%.1f wu=%u\n",
               fu, u0, wf, wu);
        for (uint32_t k = 0; k < TEXA_SZ; k += 4)
            *(uint32_t *) &texarena[k] = rng();
        for (int k = 0; k < 256; k++)
            g_texpal[k] = rng();
        mk_state(&d, &zc[0]);
        d.tex_en = 1; d.sec_en = 0;
        d.do_persp = 0; d.sec_sel = 0;
        d.sh[0].tsp = 0x0888; d.sh[0].dt = 6; d.sh[0].s3tc = 0;
        d.sh[0].clamp_s = 0; d.sh[0].clamp_t = 0;
        d.sh[0].minb = 1; d.sh[0].mag = 1; d.sh[0].mipdis = 1; d.sh[0].top = 0;
        d.comb[0].comb = 0; d.comb[0].comba = 0; d.comb[0].fmsb = 0;
        d.comb[0].cfac = 4; d.comb[0].afac = 6;
        d.comb[0].ifac = 4; d.comb[0].ifaca = 2;
        d.need_ck  = 0;
        d.need_lod = 0;
        d.texw0 = 256.0f; d.texh0 = 256.0f;
        memset(&g_tctx, 0, sizeof(g_tctx));
        setup_stage_desc(&d, 0, &g_tctx.sd0);
        g_real_tex = 1;
        g_huge_s   = 8398000.0f; /* S * 256 past 2^31 at every pixel */
        if (run_config(code, &d, 2000, 9998) < 0)
            return 3;
        g_huge_s = 0.0f;
        printf("weight: rows=%llu fail=%llu\n",
               (unsigned long long) total_rows, (unsigned long long) total_fail);
        return total_fail ? 1 : 0;
    }

    /* the skip patcher's classifier, before any block is generated; like
       the other fixtures outside the row stream, shard 0 only */
    if (!shard_index)
        total_fail += (uint64_t) branch_patch_main(code);

    for (size_t ci = 0; ci < sizeof(zc) / sizeof(zc[0]); ci++) {
        rage128_draw_state_t d;

        mk_state(&d, &zc[ci]);
        if (run_config(code, &d, dump ? 0 : 4000, ci) < 0)
            return 3;

        if (dump && ci == 11) { /* SoA: 565 + z16 less-or-equal + dither */
            pthread_jit_write_protect_np(0);
            int len = r128_jit_arm64_generate(code, &d);
            pthread_jit_write_protect_np(1);
            FILE *f = fopen("block_soa.bin", "wb");
            fwrite(code, 1, (size_t) len, f);
            fclose(f);
            printf("dumped SoA case %zu, %d bytes\n", ci, len);
            __builtin___clear_cache((char *) code, (char *) code + len);
        }
        if (dump && ci == 12) { /* SoA: 565 + z24-in-32 keep-mask RMW */
            pthread_jit_write_protect_np(0);
            int len = r128_jit_arm64_generate(code, &d);
            pthread_jit_write_protect_np(1);
            FILE *f = fopen("block_soa_z32.bin", "wb");
            fwrite(code, 1, (size_t) len, f);
            fclose(f);
            printf("dumped SoA z32 case %zu, %d bytes\n", ci, len);
            __builtin___clear_cache((char *) code, (char *) code + len);
        }
        if (dump && ci == 32) {
            pthread_jit_write_protect_np(0);
            int len = r128_jit_arm64_generate(code, &d);
            pthread_jit_write_protect_np(1);
            FILE *f = fopen("block.bin", "wb");
            fwrite(code, 1, (size_t) len, f);
            fclose(f);
            printf("dumped case %zu, %d bytes\n", ci, len);
            __builtin___clear_cache((char *) code, (char *) code + len);
        }
        printf("case %zu (dt=%u z=%d/%d zfn=%u dith=%d ab=%d tex=%d/%d at=%d sp=%d fg=%d): done\n",
               ci, d.dst_dt, d.z_en, d.z_wr, d.zfn, d.dither, d.alpha_en,
               d.tex_en, d.sec_en, d.atest_en, d.spec_en, d.fog_en);
    }

    /* SoA lane-divergence phase: every untextured blendless config
       (the SoA-gated set) re-fuzzed with short rows and wrap rows --
       mixed skip/shade lanes within single groups, every scalar-tail
       length, and the group bail paths. */
    g_soa_short = 1;
    for (size_t ci = 0; ci <= 13; ci++) {
        rage128_draw_state_t d;

        mk_state(&d, &zc[ci]);
        if (run_config(code, &d, dump ? 0 : 3000, 5000 + ci) < 0)
            return 3;
    }
    g_soa_short = 0;
    printf("soa short-row phase: 14 configs done\n");

    /* aux-scissor phase: representative base configs x random rect
       sets. Rects are baked into the block (dedup key), so each set is
       its own compile. Coords straddle the fuzz row's live x [0..264)
       and py [0..1024) ranges, including misses, partial overlaps,
       negative corners, and inverted (never-hit) rects. */
    static const size_t aux_base[] = { 0, 1, 2, 3, 11, 18, 27, 30, 37, 40, 47 };

    for (size_t bi = 0; bi < sizeof(aux_base) / sizeof(aux_base[0]); bi++) {
        rage128_draw_state_t d;

        for (int set = 0; set < 24; set++) {
            mk_state(&d, &zc[aux_base[bi]]);
            d.aux_on = 1;
            /* Enabled rectangles may start above index zero. The first
               rectangle and count select a contiguous range of control bits. */
            int first = (int) (rng() % 3), nen = 1 + (int) (rng() % (3 - first));
            for (int i = first; i < first + nen; i++) {
                d.aux_cntl |= 1u << (i * 2);
                if (rng() & 1)
                    d.aux_cntl |= 2u << (i * 2);
                d.aux_x0[i] = (int32_t) (rng() % 340) - 40;
                d.aux_x1[i] = d.aux_x0[i] + (int32_t) (rng() % 220) - 20;
                d.aux_y0[i] = (int32_t) (rng() % 1140) - 60;
                d.aux_y1[i] = d.aux_y0[i] + (int32_t) (rng() % 560) - 60;
            }
            if (run_config(code, &d, 700, 1000 + bi * 100 + (size_t) set) < 0)
                return 3;
        }
        printf("aux base %zu (zc[%zu]): 24 rect sets done\n", bi, aux_base[bi]);
    }

    /* inline-texture phase: family states compared against the REAL
       replica (rr_*) over fuzzed textures, mip chains, filters, wraps,
       and combine modes, on representative base pixel paths. */
    {
        static const size_t tex_base[] = { 0, 2, 3, 11, 18, 27, 40, 47 };

        for (uint32_t k = 0; k < TEXA_SZ; k += 4)
            *(uint32_t *) &texarena[k] = rng();
        for (int k = 0; k < 256; k++)
            g_texpal[k] = rng();
        g_real_tex = 1;
        for (size_t bi = 0; bi < sizeof(tex_base) / sizeof(tex_base[0]); bi++) {
            int own_w = 0; /* dual configs whose secondary stage got its own W */

            for (int cfg = 0; cfg < 24; cfg++) {
                rage128_draw_state_t d;
                int                  stages = 1 + (int) (rng() % 3);

                mk_state(&d, &zc[tex_base[bi]]);
                d.tex_en   = (stages & 1) != 0;
                d.sec_en   = (stages & 2) != 0;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = (int) (rng() % 2);
                d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                if (d.tex_en)
                    mk_tex_stage(&d, 0);
                if (d.sec_en)
                    mk_tex_stage(&d, 1);
                /* chroma keys (stage 0 only), ~half of textured configs */
                if (d.tex_en && (rng() & 1)) {

                    d.ck3d_on = (int) (rng() & 1);
                    d.ckc_on  = (!d.ck3d_on || (rng() & 3) == 0) ? 1 : 0;
                    d.ckfn    = (rng() & 1) ? 3 : 2;
                    d.ck3d_msk = g_ckmsks[rng() % N_CKMSKS];
                    d.ck3d_clr = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                    d.ckc_msk  = g_ckmsks[rng() % N_CKMSKS];
                    d.ckc_clr  = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                }
                d.need_ck = d.ck3d_on || d.ckc_on;
                /* need_lod / texw mirror r3d_draw_state_derive */
                if (!r128_a64_texinline_can(&d)) {
                    printf("NOT-INLINE cfg base=%zu cfg=%d comb0=%u/%u comba0=%u fmsb0=%u\n",
                           bi, cfg, d.comb[0].comb, d.comb[1].comb, d.comb[0].comba, d.comb[0].fmsb);
                    return 4;
                }
                d.need_lod = d.tex_en
                    && ((!d.sh[0].mipdis && d.sh[0].minb >= 2)
                        || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1)));
                d.need_lod2 = d.sec_en
                    && ((!d.sh[1].mipdis && d.sh[1].minb >= 2)
                        || ((int) (d.sh[1].minb & 1) != (d.sh[1].mag == 1)));
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                if (!r128_a64_texinline_can(&d)) {
                    printf("tex case bi=%zu cfg=%d: gate refused a family state\n", bi, cfg);
                    return 5;
                }
                memset(&g_tctx, 0, sizeof(g_tctx));
                if (d.tex_en)
                    setup_stage_desc(&d, 0, &g_tctx.sd0);
                if (d.sec_en)
                    setup_stage_desc(&d, 1, &g_tctx.sd1);
                /* dual states: two in five follow the primary W as the
                   drivers do, the rest give the secondary stage its own
                   perspective enable and/or W (the second head). The
                   draw comes from the separate stream, so the global
                   stream and every block it shapes stay as they were. */
                g_sec_w = d.sec_en ? (int) (secw_rng() % 5) - 1 : -1;
                own_w += g_sec_w > 0;
                int rc = run_config(code, &d, 600, 3000 + bi * 100 + (size_t) cfg);
                g_sec_w = -1;
                if (rc < 0)
                    return 3;
            }
            printf("tex base %zu (zc[%zu]): 24 inline configs done, %d with the secondary stage's own W\n",
                   bi, tex_base[bi], own_w);
        }
        g_real_tex = 0;
    }

    /* SoA textured phase: single-stage no-LOD family
       states -- the textured vector-loop gate set -- with short rows and
       wrap rows: every tail length, mixed lanes in one group, both bail
       paths, all four wrap modes, nearest and bilinear. */
    {
        static const size_t   stex_base[] = { 0, 1, 2, 3, 6, 8, 11, 12 };
        static const uint32_t sdts[10]    = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 15 };

        g_real_tex  = 1;
        g_soa_short = 1;
        for (size_t bi = 0; bi < sizeof(stex_base) / sizeof(stex_base[0]); bi++) {
            for (int cfg = 0; cfg < 20; cfg++) {
                rage128_draw_state_t d;

                mk_state(&d, &zc[stex_base[bi]]);
                d.tex_en   = 1;
                d.sec_en   = 0;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = 0;
                d.lod_bias = 0.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                mk_tex_stage(&d, 0);
                d.sh[0].dt   = sdts[rng() % 10];
                d.sh[0].s3tc = 0;
                if (rng() & 1) {
                    d.sh[0].minb = 1; /* linear/linear: no LOD consumed */
                    d.sh[0].mag  = 1;
                } else {
                    d.sh[0].minb = 0; /* nearest/nearest */
                    d.sh[0].mag  = 0;
                }
                if (dump && bi == 2 && cfg == 0) {
                    /* disasm block: every new SoA-tex encoding on one
                       path (clamp + mirror wraps, persp, bilinear) */
                    d.do_persp   = 1;
                    d.sh[0].minb = 1;
                    d.sh[0].mag  = 1;
                    d.sh[0].dt   = 4;
                    d.sh[0].clamp_s = 2;
                    d.sh[0].clamp_t = 1;
                }
                d.ck3d_on = d.ckc_on = 0;
                d.need_ck = 0;
                d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                          || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1));
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                if (d.need_lod || !r128_a64_soa_tex_can(&d)
                    || !r128_a64_texinline_can(&d)) {
                    printf("soa-tex case bi=%zu cfg=%d: gate refused\n", bi, cfg);
                    return 6;
                }
                memset(&g_tctx, 0, sizeof(g_tctx));
                setup_stage_desc(&d, 0, &g_tctx.sd0);
                if (run_config(code, &d, dump ? 0 : 500,
                               7000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
                if (dump && bi == 2 && cfg == 0) {
                    pthread_jit_write_protect_np(0);
                    int len = r128_jit_arm64_generate(code, &d);
                    pthread_jit_write_protect_np(1);
                    FILE *f = fopen("block_soa_tex.bin", "wb");
                    fwrite(code, 1, (size_t) len, f);
                    fclose(f);
                    printf("dumped SoA tex case, %d bytes\n", len);
                    __builtin___clear_cache((char *) code, (char *) code + len);
                }
            }
            printf("soa-tex base %zu (zc[%zu]): 20 configs done\n",
                   bi, stex_base[bi]);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
    }

    /* SoA textured mip phase: single-stage mip-nearest
       (minb 2/3, matching magnification) with per-lane LOD -> per-lane slot select.
       Random mip chains (top 0..8), random lod_bias, both persp modes;
       short rows on odd configs for tails/bails, full rows on even ones
       for long vector-loop runs. */
    {
        static const size_t   stex_base[] = { 0, 1, 2, 3, 6, 8, 11, 12 };
        static const uint32_t sdts[10]    = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 15 };

        g_real_tex = 1;
        for (size_t bi = 0; bi < sizeof(stex_base) / sizeof(stex_base[0]); bi++) {
            for (int cfg = 0; cfg < 24; cfg++) {
                rage128_draw_state_t d;

                mk_state(&d, &zc[stex_base[bi]]);
                d.tex_en   = 1;
                d.sec_en   = 0;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = 0;
                d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                mk_tex_stage(&d, 0);
                d.sh[0].dt     = sdts[rng() % 10];
                d.sh[0].s3tc   = 0;
                d.sh[0].mipdis = 0;
                if (rng() & 1) {
                    d.sh[0].minb = 3; /* linear texel + nearest mip */
                    d.sh[0].mag  = 1;
                } else {
                    d.sh[0].minb = 2; /* nearest texel + nearest mip */
                    d.sh[0].mag  = 0;
                }
                if (dump && bi == 2 && cfg == 0) {
                    /* disasm block: LOD + per-lane slots + bilinear on
                       clamp/mirror wraps under persp */
                    d.do_persp   = 1;
                    d.sh[0].minb = 3;
                    d.sh[0].mag  = 1;
                    d.sh[0].dt   = 4;
                    d.sh[0].clamp_s = 2;
                    d.sh[0].clamp_t = 1;
                }
                d.ck3d_on = d.ckc_on = 0;
                d.need_ck = 0;
                d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                          || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1));
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                if (!d.need_lod || !r128_a64_soa_tex_can(&d)
                    || !r128_a64_texinline_can(&d)) {
                    printf("soa-mip case bi=%zu cfg=%d: gate refused\n", bi, cfg);
                    return 6;
                }
                memset(&g_tctx, 0, sizeof(g_tctx));
                setup_stage_desc(&d, 0, &g_tctx.sd0);
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, dump ? 0 : 500,
                               9000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
                if (dump && bi == 2 && cfg == 0) {
                    pthread_jit_write_protect_np(0);
                    int len = r128_jit_arm64_generate(code, &d);
                    pthread_jit_write_protect_np(1);
                    FILE *f = fopen("block_soa_tex_lod.bin", "wb");
                    fwrite(code, 1, (size_t) len, f);
                    fclose(f);
                    printf("dumped SoA mip tex case, %d bytes\n", len);
                    __builtin___clear_cache((char *) code, (char *) code + len);
                }
            }
            printf("soa-mip base %zu (zc[%zu]): 24 configs done\n",
                   bi, stex_base[bi]);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
    }

    /* SoA textured trilinear phase: single-stage
       minb 4/5 (matching magnification) -- per-lane slot PAIR, two coord/gather
       passes, 8.8 mip blend. Same chain/bias/persp/short-row coverage
       as the mip-nearest phase. */
    {
        static const size_t   stex_base[] = { 0, 1, 2, 3, 6, 8, 11, 12 };
        static const uint32_t sdts[10]    = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 15 };

        g_real_tex = 1;
        for (size_t bi = 0; bi < sizeof(stex_base) / sizeof(stex_base[0]); bi++) {
            for (int cfg = 0; cfg < 24; cfg++) {
                rage128_draw_state_t d;

                mk_state(&d, &zc[stex_base[bi]]);
                d.tex_en   = 1;
                d.sec_en   = 0;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = 0;
                d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                mk_tex_stage(&d, 0);
                d.sh[0].dt     = sdts[rng() % 10];
                d.sh[0].s3tc   = 0;
                d.sh[0].mipdis = 0;
                if (rng() & 1) {
                    d.sh[0].minb = 5; /* trilinear */
                    d.sh[0].mag  = 1;
                } else {
                    d.sh[0].minb = 4; /* nearest texel + linear mip */
                    d.sh[0].mag  = 0;
                }
                if (dump && bi == 2 && cfg == 0) {
                    /* disasm block: slot pair + two bilinear passes +
                       mip blend on clamp/mirror wraps under persp */
                    d.do_persp   = 1;
                    d.sh[0].minb = 5;
                    d.sh[0].mag  = 1;
                    d.sh[0].dt   = 4;
                    d.sh[0].clamp_s = 2;
                    d.sh[0].clamp_t = 1;
                }
                d.ck3d_on = d.ckc_on = 0;
                d.need_ck = 0;
                d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                          || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1));
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                if (!d.need_lod || !r128_a64_soa_tex_can(&d)
                    || !r128_a64_texinline_can(&d)) {
                    printf("soa-tri case bi=%zu cfg=%d: gate refused\n", bi, cfg);
                    return 6;
                }
                memset(&g_tctx, 0, sizeof(g_tctx));
                setup_stage_desc(&d, 0, &g_tctx.sd0);
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, dump ? 0 : 500,
                               11000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
                if (dump && bi == 2 && cfg == 0) {
                    pthread_jit_write_protect_np(0);
                    int len = r128_jit_arm64_generate(code, &d);
                    pthread_jit_write_protect_np(1);
                    FILE *f = fopen("block_soa_tex_tri.bin", "wb");
                    fwrite(code, 1, (size_t) len, f);
                    fclose(f);
                    printf("dumped SoA trilinear case, %d bytes\n", len);
                    __builtin___clear_cache((char *) code, (char *) code + len);
                }
            }
            printf("soa-tri base %zu (zc[%zu]): 24 configs done\n",
                   bi, stex_base[bi]);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
    }

    /* SoA textured filter-split phase: minification and magnification
       texel filters differ, so the per-lane LOD sign picks the filter.
       mag = ~(minb & 1) forces the split on every minb; mipdis toggles
       between the base-level split (constant hoist, incl. minb >= 2
       with mip disabled) and the mip'd split (per-lane slots or the
       trilinear pair + the mag rerun's descriptor rebuild). */
    {
        static const size_t   stex_base[] = { 0, 1, 2, 3, 6, 8, 11, 12 };
        static const uint32_t sdts[10]    = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 15 };

        g_real_tex = 1;
        for (size_t bi = 0; bi < sizeof(stex_base) / sizeof(stex_base[0]); bi++) {
            for (int cfg = 0; cfg < 24; cfg++) {
                rage128_draw_state_t d;

                mk_state(&d, &zc[stex_base[bi]]);
                d.tex_en   = 1;
                d.sec_en   = 0;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = 0;
                d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                mk_tex_stage(&d, 0);
                d.sh[0].dt     = sdts[rng() % 10];
                d.sh[0].s3tc   = 0;
                d.sh[0].minb   = rng() % 6;
                d.sh[0].mag    = (d.sh[0].minb & 1) ^ 1;
                d.sh[0].mipdis = (int) (rng() % 2);
                if (dump && bi == 2 && cfg == 0) {
                    /* disasm block: trilinear minification + nearest magnification (slot
                       pair, two passes, mip blend, descriptor rebuild,
                       mag rerun, LOD-sign select) on clamp/mirror
                       wraps under persp */
                    d.do_persp   = 1;
                    d.sh[0].minb = 5;
                    d.sh[0].mag  = 0;
                    d.sh[0].mipdis = 0;
                    d.sh[0].dt   = 4;
                    d.sh[0].clamp_s = 2;
                    d.sh[0].clamp_t = 1;
                }
                d.ck3d_on = d.ckc_on = 0;
                d.need_ck = 0;
                d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                          || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1));
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                if (!d.need_lod || !r128_a64_soa_tex_can(&d)
                    || !r128_a64_texinline_can(&d)) {
                    printf("soa-split case bi=%zu cfg=%d: gate refused\n", bi, cfg);
                    return 6;
                }
                memset(&g_tctx, 0, sizeof(g_tctx));
                setup_stage_desc(&d, 0, &g_tctx.sd0);
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, dump ? 0 : 500,
                               13000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
                if (dump && bi == 2 && cfg == 0) {
                    pthread_jit_write_protect_np(0);
                    int len = r128_jit_arm64_generate(code, &d);
                    pthread_jit_write_protect_np(1);
                    FILE *f = fopen("block_soa_tex_split.bin", "wb");
                    fwrite(code, 1, (size_t) len, f);
                    fclose(f);
                    printf("dumped SoA filter-split case, %d bytes\n", len);
                    __builtin___clear_cache((char *) code, (char *) code + len);
                }
            }
            printf("soa-split base %zu (zc[%zu]): 24 configs done\n",
                   bi, stex_base[bi]);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
    }

    /* Partial write-mask phase: random PLANE_3D_MASK over the base case
       matrix (depth/dither/blend/staging mix). Scalar and both vector store
       paths merge masked bits with the destination. A zero wmask preserves
       color while the returned span and depth still update. */
    {
        static const size_t wm_base[] = { 0, 1, 2, 3, 4, 5, 8, 11, 12, 14 };

        for (size_t bi = 0; bi < sizeof(wm_base) / sizeof(wm_base[0]); bi++) {
            for (int cfg = 0; cfg < 12; cfg++) {
                rage128_draw_state_t d;

                mk_state(&d, &zc[wm_base[bi]]);
                switch (cfg & 3) {
                    case 0: /* random partial */
                        d.wmask = rng();
                        break;
                    case 1: /* single plane / near-full */
                        d.wmask = ~(1u << (rng() % 32));
                        break;
                    case 2: /* zero */
                        d.wmask = 0;
                        break;
                    default: /* full (gate boundary: SoA stays on) */
                        d.wmask = (d.dst_dt == 4) ? 0xffff : 0xffffffffu;
                        break;
                }
                if (run_config(code, &d, 400, 15000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
            printf("wmask base %zu (zc[%zu]): 12 configs done\n",
                   bi, wm_base[bi]);
        }
    }

    /* never-pass phase: atest_fn == 0 or zfn == 0 blocks are
       an immediate empty-row return; the replica walks the row and
       rejects every pixel. Full-memory compare proves the block writes
       nothing. */
    {
        static const size_t np_base[] = { 0, 2, 3, 6, 8, 12 };

        for (size_t bi = 0; bi < sizeof(np_base) / sizeof(np_base[0]); bi++) {
            for (int cfg = 0; cfg < 6; cfg++) {
                rage128_draw_state_t d;

                mk_state(&d, &zc[np_base[bi]]);
                if (cfg & 1) {
                    d.atest_en  = 1;
                    d.atest_fn  = 0;
                    d.atest_ref = rng() & 0xff;
                } else {
                    d.z_en = 1;
                    d.zfn  = 0;
                    d.z_wr = (int) (rng() % 2);
                    if (!d.zbpp) {
                        d.zbpp = 2; d.zmax = 0xffff; d.zshift = 0;
                    }
                }
                if (run_config(code, &d, 400, 16000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
            printf("never-pass base %zu (zc[%zu]): 6 configs done\n",
                   bi, np_base[bi]);
        }
    }

    /* Table fog keeps the same index and fraction with the Z test off.
       Run each texture and color fixture with depth testing, no live Z
       cell, and stencil alone in both byte layouts. Short rows exercise
       scalar tails; random staging checks cell bounds and preservation. */
    {
        static const case_ent ftab[] = {
         /* dt  z zw zfn zb  zmax     zsh d ab bs bd bf tx sc at atfn atrf sp fg ft */
          { 4, 1, 1, 2, 2, 0xffff,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 1, 1 },
          { 6, 1, 1, 2, 4, 0xffffff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 1, 1 },
          { 4, 1, 1, 2, 2, 0xffff,   0, 0, 1, 4, 5, 0, 0, 0, 0, 0,   0, 0, 1, 1 },
          { 4, 1, 0, 7, 2, 0xffff,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 1, 1 },
          { 4, 1, 1, 2, 4, 0xffffff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 1, 1 },
          /* call-path texture (sh.dt=10 from mk_state, not overwritten) */
          { 4, 1, 1, 2, 2, 0xffff,   0, 0, 0, 0, 0, 0, 1, 0, 0, 0,   0, 0, 1, 1 },
          { 4, 1, 1, 2, 2, 0xffff,   0, 0, 1, 4, 5, 0, 1, 0, 0, 0,   0, 1, 1, 1 },
          { 6, 1, 1, 2, 2, 0xffff,   0, 1, 0, 0, 0, 0, 1, 0, 1, 4, 0x40, 0, 1, 1 },
          /* inline-family texture (mk_tex_stage overwrites sh.dt below) */
          { 4, 1, 1, 2, 2, 0xffff,   0, 0, 0, 0, 0, 0, 1, 0, 0, 0,   0, 0, 1, 1 },
          { 4, 1, 1, 2, 2, 0xffff,   0, 0, 1, 4, 5, 0, 1, 0, 0, 0,   0, 0, 1, 1 },
        };
        int nft = (int) (sizeof(ftab) / sizeof(ftab[0]));

        for (int mode = 0; mode < 4; mode++) {
            for (int pass = 0; pass < 2; pass++) {
                g_soa_short = pass;
                for (int ci = 0; ci < nft; ci++) {
                    rage128_draw_state_t d;

                    mk_state(&d, &ftab[ci]);
                    if (d.tex_en && ci >= 8) /* inline-family stage 0 */
                        mk_tex_stage(&d, 0);
                    if (mode != 0) {
                        d.z_en = 0;
                        if (ci == nft - 1) {
                            d.sec_en = 1;
                            mk_tex_stage(&d, 1);
                        }
                        if (mode >= 2) {
                            d.sten_on  = 1;
                            d.zbpp     = 4;
                            d.zmax     = 0xffffff;
                            d.zshift   = mode == 2 ? 0 : 8;
                            d.sshift   = mode == 2 ? 24 : 0;
                            d.sfn      = rng() % 8;
                            d.sfail_op = rng() % 8;
                            d.zpass_op = rng() % 8;
                            d.zfail_op = rng() % 8;
                            d.sref     = rng() & 0xff;
                            d.svmask   = rng() & 0xff;
                            d.swmask   = rng() & 0xff;
                        }
                        if (!r128_jit_arm64_can(&d) || !r128_jit_x64_can(&d)
                            || r128_a64_soa_can(&d) || r128_jit_x64_soa_can(&d)
                            || r128_a64_soa_tex_can(&d) || r128_jit_x64_soa_tex_can(&d)) {
                            printf("table-fog mode=%d ci=%d: scalar gate mismatch\n", mode, ci);
                            return 6;
                        }
                    }
                    if (run_config(code, &d, dump ? 0 : 4000,
                                   (size_t) (18000 + mode * 1000 + pass * 100 + ci)) < 0)
                        return 3;
                }
            }
        }
        g_soa_short = 0;
        printf("table-fog phase: %d configs x4 modes x2 passes done\n", nft);
    }

    /* SoA chroma-key phase: no-LOD nearest ck states take the
       vector loop -- keyed texels must drop out of the store mask AND
       the rx0/rx1 span (edge-lane rejects shrink the reported span).
       Phase order keeps earlier dump inputs fixed. Keys mostly from the arena (pick_key) so
       real rejects occur; short/wrap rows for mixed-lane groups. */
    {
        static const size_t   sck_base[] = { 0, 1, 2, 3, 6, 8, 11, 12 };
        static const uint32_t sdts[10]   = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 15 };

        g_real_tex  = 1;
        g_soa_short = 1;
        for (size_t bi = 0; bi < sizeof(sck_base) / sizeof(sck_base[0]); bi++) {
            for (int cfg = 0; cfg < 16; cfg++) {
                rage128_draw_state_t d;

                mk_state(&d, &zc[sck_base[bi]]);
                d.tex_en   = 1;
                d.sec_en   = 0;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = 0;
                d.lod_bias = 0.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                mk_tex_stage(&d, 0);
                d.sh[0].dt   = sdts[rng() % 10];
                d.sh[0].s3tc = 0;
                d.sh[0].minb = 0; /* the gated ck subset: no-LOD nearest */
                d.sh[0].mag  = 0;
                if (d.sh[0].dt == 1 || d.sh[0].dt == 2)
                    d.sh[0].dt = 4; /* CI stage-0 keys keep the scalar loop (covered by the tex-base sweep) */
                d.ck3d_on  = (int) (rng() & 1);
                d.ckc_on   = (!d.ck3d_on || (rng() & 3) == 0) ? 1 : 0;
                d.ckfn     = (rng() & 1) ? 3 : 2;
                d.ck3d_msk = g_ckmsks[rng() % N_CKMSKS];
                d.ck3d_clr = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                d.ckc_msk  = g_ckmsks[rng() % N_CKMSKS];
                d.ckc_clr  = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                d.need_ck  = 1;
                d.need_lod = 0;
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                if (!r128_a64_texinline_can(&d)) {
                    printf("soa-ck case bi=%zu cfg=%d: inline gate refused\n",
                           bi, cfg);
                    return 6;
                }
                /* A vector-gate refusal remains a diagnostic: run_config verifies
                   the generated scalar block against the same reference. */
                if (!r128_a64_soa_tex_can(&d))
                    printf("soa-ck case bi=%zu cfg=%d: SoA gate refused\n",
                           bi, cfg);
                memset(&g_tctx, 0, sizeof(g_tctx));
                setup_stage_desc(&d, 0, &g_tctx.sd0);
                if (run_config(code, &d, 500,
                               20000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
            printf("soa-ck base %zu (zc[%zu]): 16 configs done\n",
                   bi, sck_base[bi]);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
    }

    /* SoA dual-stage phase: both stages use no-LOD nearest/bilinear or
       mip-nearest minb 2/3. Stage 0's float output supplies stage 1's
       R128_INPUT_FACTOR_PREV_COLOR and R128_INPUT_FACTOR_PREV_ALPHA operands
       and R128_COMB_BLEND_PREV factor. sec_sel chooses between
       R128_SEC_SELECT_PRIM_ST and R128_SEC_SELECT_SEC_ST coordinates.
       This phase omits trilinear, split filters and chroma key, which have
       separate phases. Odd configurations use short/wrap rows to exercise
       mixed lanes. Phase order keeps standalone dump inputs fixed. */
    {
        static const size_t   st2_base[] = { 0, 1, 2, 3, 6, 8, 11, 12 };
        static const uint32_t sdts[10]   = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 15 };

        g_real_tex = 1;
        for (size_t bi = 0; bi < sizeof(st2_base) / sizeof(st2_base[0]); bi++) {
            for (int cfg = 0; cfg < 24; cfg++) {
                rage128_draw_state_t d;

                mk_state(&d, &zc[st2_base[bi]]);
                d.tex_en   = 1;
                d.sec_en   = 1;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = (int) (rng() % 2);
                d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                mk_tex_stage(&d, 0);
                mk_tex_stage(&d, 1);
                for (int st = 0; st < 2; st++) {
                    d.sh[st].dt   = sdts[rng() % 10];
                    d.sh[st].s3tc = 0;
                    switch (rng() % 4) { /* covered classes only */
                        case 0: /* nearest, no LOD */
                            d.sh[st].minb = 0;
                            d.sh[st].mag  = 0;
                            d.sh[st].mipdis = (int) (rng() % 2);
                            break;
                        case 1: /* bilinear, no LOD */
                            d.sh[st].minb = 1;
                            d.sh[st].mag  = 1;
                            d.sh[st].mipdis = (int) (rng() % 2);
                            break;
                        case 2: /* mip-nearest, nearest texel */
                            d.sh[st].minb = 2;
                            d.sh[st].mag  = 0;
                            d.sh[st].mipdis = 0;
                            break;
                        default: /* mip-nearest, linear texel */
                            d.sh[st].minb = 3;
                            d.sh[st].mag  = 1;
                            d.sh[st].mipdis = 0;
                            break;
                    }
                }
                d.ck3d_on = d.ckc_on = 0;
                d.need_ck = 0;
                d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                          || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1));
                d.need_lod2 = (!d.sh[1].mipdis && d.sh[1].minb >= 2)
                           || ((int) (d.sh[1].minb & 1) != (d.sh[1].mag == 1));
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                if (!r128_a64_texinline_can(&d)) {
                    printf("soa-t2 case bi=%zu cfg=%d: inline gate refused\n",
                           bi, cfg);
                    return 6;
                }
                /* A vector-gate refusal remains a diagnostic: run_config verifies
                   the generated scalar block against the same reference. */
                if (!r128_a64_soa_tex_can(&d))
                    printf("soa-t2 case bi=%zu cfg=%d: SoA gate refused\n",
                           bi, cfg);
                memset(&g_tctx, 0, sizeof(g_tctx));
                setup_stage_desc(&d, 0, &g_tctx.sd0);
                setup_stage_desc(&d, 1, &g_tctx.sd1);
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, dump ? 0 : 400,
                               22000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
            printf("soa-t2 base %zu (zc[%zu]): 24 configs done\n",
                   bi, st2_base[bi]);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
    }

    /* SoA blend/alpha-test phase: alpha_en and atest_en states take the
       vector loop -- channel-major dst-read/factor/fcn blend after the
       texstage (or the untextured channel dots), alpha test ANDed into the
       deferred cover mask so failed lanes never widen rx0/rx1. Sweeps every
       factor code (incl. the 0xb/0xc forced pairs and
       source-alpha-saturate), all four fcns, atest fns 1..7, untextured +
       textured single-stage (all covered filter classes incl. tri/ split),
       chroma-key combos and dual-stage (the 16 KB cap check). Phase order
       keeps earlier dump inputs fixed. */
    {
        static const size_t   sab_base[] = { 0, 1, 2, 3, 6, 8, 11, 12 };
        static const uint32_t sdts[10]   = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 15 };
        static const uint32_t bsrcs[13]  = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                             0xa, 0xb, 0xc };
        static const uint32_t bdsts[12]  = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                             0xa, 0xb };

        for (size_t bi = 0; bi < sizeof(sab_base) / sizeof(sab_base[0]); bi++) {
            for (int cfg = 0; cfg < 32; cfg++) {
                rage128_draw_state_t d;
                int tex  = cfg >= 8;
                int dual = cfg >= 26;
                int ck   = tex && !dual && (cfg % 6) == 3;

                mk_state(&d, &zc[sab_base[bi]]);
                /* 0 blend-only, 1 atest-only, 2/3 both */
                d.alpha_en  = (cfg & 3) != 1;
                d.atest_en  = (cfg & 3) >= 1;
                if ((cfg % 5) == 2) /* + partial wmask (SoA RMW) */
                    d.wmask = (rng() & 1)
                                  ? rng()
                                  : ((rng() & 1) ? ~(1u << (rng() % 32)) : 0);
                d.bsrc      = bsrcs[rng() % 13];
                d.bdst      = bdsts[rng() % 12];
                d.bfcn      = rng() % 4;
                d.atest_fn  = 1 + rng() % 7;
                d.atest_ref = rng() & 0xff;
                if (tex) {
                    d.tex_en   = 1;
                    d.sec_en   = dual;
                    d.do_persp = (int) (rng() % 2);
                    d.sec_sel  = dual ? (int) (rng() % 2) : 0;
                    d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                    d.premult  = 0;
                    for (int k = 0; k < 4; k++)
                        d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                    for (int st = 0; st < (dual ? 2 : 1); st++) {
                        mk_tex_stage(&d, st);
                        d.sh[st].dt   = sdts[rng() % 10];
                        d.sh[st].s3tc = 0;
                        if (dual)
                            switch (rng() % 4) { /* covered classes only */
                                case 0:
                                    d.sh[st].minb = 0;
                                    d.sh[st].mag  = 0;
                                    d.sh[st].mipdis = (int) (rng() % 2);
                                    break;
                                case 1:
                                    d.sh[st].minb = 1;
                                    d.sh[st].mag  = 1;
                                    d.sh[st].mipdis = (int) (rng() % 2);
                                    break;
                                case 2:
                                    d.sh[st].minb = 2;
                                    d.sh[st].mag  = 0;
                                    d.sh[st].mipdis = 0;
                                    break;
                                default:
                                    d.sh[st].minb = 3;
                                    d.sh[st].mag  = 1;
                                    d.sh[st].mipdis = 0;
                                    break;
                            }
                    }
                    if (ck) { /* the gated ck subset: no-LOD nearest */
                        d.sh[0].minb = 0;
                        d.sh[0].mag  = 0;
                        if (d.sh[0].dt == 1 || d.sh[0].dt == 2)
                            d.sh[0].dt = 4; /* CI stage-0 keys keep the scalar loop */
                        d.ck3d_on  = (int) (rng() & 1);
                        d.ckc_on   = (!d.ck3d_on || (rng() & 3) == 0) ? 1 : 0;
                        d.ckfn     = (rng() & 1) ? 3 : 2;
                        d.ck3d_msk = g_ckmsks[rng() % N_CKMSKS];
                        d.ck3d_clr = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                        d.ckc_msk  = g_ckmsks[rng() % N_CKMSKS];
                        d.ckc_clr  = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                        d.need_ck  = 1;
                    }
                    d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                              || ((int) (d.sh[0].minb & 1)
                                  != (d.sh[0].mag == 1));
                    if (ck)
                        d.need_lod = 0;
                    d.need_lod2 = dual
                        && ((!d.sh[1].mipdis && d.sh[1].minb >= 2)
                            || ((int) (d.sh[1].minb & 1)
                                != (d.sh[1].mag == 1)));
                    d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                    d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                    if (dual) {
                        d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                        d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                    }
                    if (!r128_a64_texinline_can(&d)) {
                        printf("soa-ab case bi=%zu cfg=%d: inline gate refused\n",
                               bi, cfg);
                        return 6;
                    }
                    /* A vector-gate refusal remains a diagnostic: run_config verifies
                   the generated scalar block against the same reference. */
                    if (!r128_a64_soa_tex_can(&d))
                        printf("soa-ab case bi=%zu cfg=%d: SoA gate refused\n",
                               bi, cfg);
                    memset(&g_tctx, 0, sizeof(g_tctx));
                    setup_stage_desc(&d, 0, &g_tctx.sd0);
                    if (dual)
                        setup_stage_desc(&d, 1, &g_tctx.sd1);
                } else if (!r128_a64_soa_can(&d))
                    printf("soa-ab case bi=%zu cfg=%d: SoA gate refused\n",
                           bi, cfg);
                g_real_tex  = tex;
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, 400,
                               24000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
            printf("soa-ab base %zu (zc[%zu]): 32 configs done\n",
                   bi, sab_base[bi]);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
    }

    /* Dithered RGB565 textured spans with z_en=0 (depth writes without a
       test). Partial coverage and full-buffer comparison
       expose writes beyond a store lane. One/two stages and nearest/bilinear
       filters exercise depth testing disabled and an enabled control. */
    {
        static const case_ent gp3z0 =
            { 4, 0, 1, 2, 2, 0xffff, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        static const case_ent gp3z1 =
            { 4, 1, 1, 2, 2, 0xffff, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

        int soa_n = 0, tot_n = 0;
        g_real_tex = 1;
        for (int zc0 = 0; zc0 < 2; zc0++) {
            for (int cfg = 0; cfg < 48; cfg++) {
                rage128_draw_state_t d;
                int dual  = (cfg & 4) != 0;
                int bilin = (cfg & 1) != 0;

                mk_state(&d, zc0 ? &gp3z1 : &gp3z0);
                d.tex_en   = 1;
                d.sec_en   = dual;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = (int) (rng() % 2);
                d.lod_bias = 0.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                mk_tex_stage(&d, 0);
                d.sh[0].dt   = 4;
                d.sh[0].s3tc = 0;
                d.sh[0].minb = bilin ? 1 : 0;
                d.sh[0].mag  = bilin ? 1 : 0;
                if (dual) {
                    mk_tex_stage(&d, 1);
                    d.sh[1].dt   = 3;
                    d.sh[1].s3tc = 0;
                    d.sh[1].minb = bilin ? 1 : 0;
                    d.sh[1].mag  = bilin ? 1 : 0;
                }
                d.ck3d_on = d.ckc_on = d.need_ck = 0;
                d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                          || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1));
                d.need_lod2 = dual
                    && ((!d.sh[1].mipdis && d.sh[1].minb >= 2)
                        || ((int) (d.sh[1].minb & 1) != (d.sh[1].mag == 1)));
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                if (d.need_lod || d.need_lod2 || !r128_a64_texinline_can(&d))
                    continue; /* not this SoA class; skip */
                tot_n++;
                if (r128_a64_soa_tex_can(&d))
                    soa_n++;
                memset(&g_tctx, 0, sizeof(g_tctx));
                setup_stage_desc(&d, 0, &g_tctx.sd0);
                if (dual)
                    setup_stage_desc(&d, 1, &g_tctx.sd1);
                g_soa_short = (cfg & 2) != 0;
                if (run_config(code, &d, 500,
                               30000 + (size_t) zc0 * 1000 + (size_t) cfg) < 0)
                    return 3;
            }
        }
        g_soa_short = 0;
        g_real_tex  = 0;
        printf("gp3 dither/z_en=0 regression phase: done (%d/%d configs took SoA)\n",
               soa_n, tot_n);
    }

    /* Stencil phase: exercises the deferred fail-op RMW
       and the post-atest discard. Untextured states take the SoA vector
       loop (packed z-block stash + post-pack masked RMW); the
       textured sub-pass below stays on the call path. Covers every sfn,
       random op triples, zfn incl never-pass(0)/always-pass(7), depth on/off, BOTH
       stencil-byte positions (sshift 0 at zwidth 2, sshift 24 at zwidth
       1/3), edge masks (0x00/0xff for svmask + swmask), dst 565/8888,
       dither, alpha blend, alpha test, specular, vertex fog. A textured
       sub-pass drives the CALL path (heavy-clobber synthetic tex stand-in)
       to prove x26 (zcell) and the packed frame stash survive the AAPCS
       call -- the inline path preserves all registers, so it is safe by
       construction. Phase order keeps earlier dump inputs fixed. Full-memory compare proves the byte RMW. */
    {
        static const size_t st_base[] = { 0, 1 };
        int nsb = (int) (sizeof(st_base) / sizeof(st_base[0]));

        for (int bi = 0; bi < nsb; bi++) {
            for (int cfg = 0; cfg < 64; cfg++) {
                rage128_draw_state_t d;

                mk_state(&d, &zc[st_base[bi]]);
                /* stencil needs a 32-bit (zwidth != 0) Z cell */
                d.z_en = cfg & 1;
                d.z_wr = (int) (rng() & 1);
                d.zbpp = 4;
                d.zmax = 0xffffff;
                if (cfg & 2) { d.zshift = 8; d.sshift = 0;  } /* stencil [7:0]   */
                else         { d.zshift = 0; d.sshift = 24; } /* stencil [31:24] */
                d.zfn      = d.z_en ? (rng() % 8) : 0;
                d.sten_on  = 1;
                d.sfn      = rng() % 8;
                d.sfail_op = rng() % 8;
                d.zpass_op = rng() % 8;
                d.zfail_op = rng() % 8;
                d.sref     = rng() & 0xff;
                switch (rng() % 4) {
                    case 0:  d.svmask = 0xff; break;
                    case 1:  d.svmask = 0x00; break;
                    default: d.svmask = rng() & 0xff; break;
                }
                switch (rng() % 4) {
                    case 0:  d.swmask = 0xff; break;
                    case 1:  d.swmask = 0x00; break;
                    default: d.swmask = rng() & 0xff; break;
                }
                if (cfg & 4) {
                    d.alpha_en = 1; d.bsrc = 4; d.bdst = 5; d.bfcn = rng() % 4;
                }
                if (cfg & 8) {
                    d.atest_en  = 1;
                    d.atest_fn  = 1 + rng() % 6; /* 1..6 (never/always tested via zfn) */
                    d.atest_ref = rng() & 0xff;
                }
                if ((cfg & 16) && d.dst_dt == 4)
                    d.dither = 1;
                if (cfg & 32)
                    d.spec_en = 1;
                if ((cfg % 5) == 0)
                    d.fog_en = 1; /* vertex fog (table fog carries its own gate) */
                if (run_config(code, &d, 600, 40000 + (size_t) bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
            printf("stencil base %zu (zc[%zu]): 64 configs done\n",
                   (size_t) bi, st_base[bi]);
        }

        /* textured stencil, CALL path: sh.dt = 10 (CI16, undecoded -> helper
           call) with the synthetic heavy-clobber stand-in. Proves the zcell
           (x26, callee-saved) and the frame stash survive the tex call and
           the caller-saved zptr/z_base reload feeds the next pixel. */
        g_real_tex = 0;
        for (int cfg = 0; cfg < 48; cfg++) {
            rage128_draw_state_t d;

            mk_state(&d, &zc[cfg & 1]);
            d.tex_en = 1;                 /* sh.dt == 10 from mk_state -> call */
            d.z_en   = (cfg >> 1) & 1;
            d.z_wr   = (int) (rng() & 1);
            d.zbpp   = 4;
            d.zmax   = 0xffffff;
            if (cfg & 4) { d.zshift = 8; d.sshift = 0;  }
            else         { d.zshift = 0; d.sshift = 24; }
            d.zfn      = d.z_en ? (rng() % 8) : 0;
            d.sten_on  = 1;
            d.sfn      = rng() % 8;
            d.sfail_op = rng() % 8;
            d.zpass_op = rng() % 8;
            d.zfail_op = rng() % 8;
            d.sref     = rng() & 0xff;
            d.svmask   = (rng() & 1) ? 0xff : (rng() & 0xff);
            d.swmask   = (rng() & 1) ? 0xff : (rng() & 0xff);
            if (cfg & 8) {
                d.atest_en  = 1;
                d.atest_fn  = 1 + rng() % 6;
                d.atest_ref = rng() & 0xff;
            }
            if (run_config(code, &d, 600, 41000 + (size_t) cfg) < 0)
                return 3;
        }
        printf("stencil textured (call path): 48 configs done\n");
    }

    /* SoA S3TC/YUV phase: the BL-subroutine dts (dt 0, all four s3tc block
       classes; dt 11/12 packed 422; dt 14 aYUV444) take the vector loop in
       the covered filter classes (no-LOD nearest/bilinear and mip-nearest).
       The per-stage decode sub is emitted once ahead of both loops and
       shared with the scalar tail. Sweeps: single-stage (all sub dts x all
       covered classes), chroma key (no-LOD nearest, keys from decoded arena
       texels), mip-nearest + alpha test (the deferred cover stash above the
       frame, per-lane descriptors + sub), dual stage (sub+GPR / GPR+sub /
       sub+sub incl. both-DXT, the 16 KB cap check), and
       blend/atest/partial-wmask riders. Short/wrap rows on odd cfgs. Phase
       order keeps earlier dump inputs fixed. */
    {
        static const size_t   ssub_base[] = { 0, 1, 2, 3, 6, 8, 11, 12 };
        static const uint32_t subdts[8][2] = {
            { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 },
            { 11, 0 }, { 12, 0 }, { 14, 0 }, { 0, 3 },
        };
        static const uint32_t gdts[10]   = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 15 };
        static const uint32_t bsrcs[13]  = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                             0xa, 0xb, 0xc };
        static const uint32_t bdsts[12]  = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                             0xa, 0xb };

        g_real_tex = 1;
        for (size_t bi = 0; bi < sizeof(ssub_base) / sizeof(ssub_base[0]); bi++) {
            for (int cfg = 0; cfg < 32; cfg++) {
                rage128_draw_state_t d;
                int dual = cfg >= 20 && cfg < 28;
                int ck   = cfg >= 12 && cfg < 16;
                int matest = cfg >= 16 && cfg < 20;

                mk_state(&d, &zc[ssub_base[bi]]);
                d.tex_en   = 1;
                d.sec_en   = dual;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = dual ? (int) (rng() % 2) : 0;
                d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                for (int st = 0; st < (dual ? 2 : 1); st++) {
                    /* dual dt mixing per (cfg-20)%4: 0 sub+GPR, 1 GPR+sub,
                       2 sub+sub, 3 both-DXT (the size ceiling) */
                    int sub_st = !dual
                        || ((cfg - 20) % 4 == 0 && st == 0)
                        || ((cfg - 20) % 4 == 1 && st == 1)
                        || ((cfg - 20) % 4 >= 2);

                    mk_tex_stage(&d, st);
                    if (!sub_st) {
                        d.sh[st].dt   = gdts[rng() % 10];
                        d.sh[st].s3tc = 0;
                    } else if (dual && (cfg - 20) % 4 == 3) {
                        d.sh[st].dt   = 0; /* both-DXT worst case */
                        d.sh[st].s3tc = rng() % 4;
                    } else {
                        const uint32_t *sd =
                            subdts[(cfg < 12) ? (cfg % 8) : (rng() % 8)];

                        d.sh[st].dt   = sd[0];
                        d.sh[st].s3tc = sd[1];
                    }
                    switch (matest ? (2 + (cfg & 1)) : (rng() % 4)) {
                        case 0: /* nearest, no LOD */
                            d.sh[st].minb = 0;
                            d.sh[st].mag  = 0;
                            d.sh[st].mipdis = (int) (rng() % 2);
                            break;
                        case 1: /* bilinear, no LOD */
                            d.sh[st].minb = 1;
                            d.sh[st].mag  = 1;
                            d.sh[st].mipdis = (int) (rng() % 2);
                            break;
                        case 2: /* mip-nearest, nearest texel */
                            d.sh[st].minb = 2;
                            d.sh[st].mag  = 0;
                            d.sh[st].mipdis = 0;
                            break;
                        default: /* mip-nearest, linear texel */
                            d.sh[st].minb = 3;
                            d.sh[st].mag  = 1;
                            d.sh[st].mipdis = 0;
                            break;
                    }
                }
                if (ck) { /* the gated ck subset: no-LOD nearest */
                    d.sh[0].minb = 0;
                    d.sh[0].mag  = 0;
                    d.ck3d_on  = (int) (rng() & 1);
                    d.ckc_on   = (!d.ck3d_on || (rng() & 3) == 0) ? 1 : 0;
                    d.ckfn     = (rng() & 1) ? 3 : 2;
                    d.ck3d_msk = g_ckmsks[rng() % N_CKMSKS];
                    d.ck3d_clr = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                    d.ckc_msk  = g_ckmsks[rng() % N_CKMSKS];
                    d.ckc_clr  = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                    d.need_ck  = 1;
                }
                if (matest || cfg >= 29) {
                    d.atest_en  = 1;
                    d.atest_fn  = 1 + rng() % 7;
                    d.atest_ref = rng() & 0xff;
                }
                if (cfg == 28 || cfg >= 30) {
                    d.alpha_en = 1;
                    d.bsrc     = bsrcs[rng() % 13];
                    d.bdst     = bdsts[rng() % 12];
                    d.bfcn     = rng() % 4;
                }
                if (cfg == 31) /* + partial wmask (SoA RMW store) */
                    d.wmask = (rng() & 1)
                                  ? rng()
                                  : ((rng() & 1) ? ~(1u << (rng() % 32)) : 0);
                d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                          || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1));
                d.need_lod2 = dual
                    && ((!d.sh[1].mipdis && d.sh[1].minb >= 2)
                        || ((int) (d.sh[1].minb & 1) != (d.sh[1].mag == 1)));
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                if (dual) {
                    d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                    d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                }
                if (!r128_a64_texinline_can(&d)) {
                    printf("soa-sub case bi=%zu cfg=%d: inline gate refused\n",
                           bi, cfg);
                    return 6;
                }
                /* hard error: every cfg here is built inside the covered
                   classes, so the SoA gate must take it */
                if (!r128_a64_soa_tex_can(&d)) {
                    printf("soa-sub case bi=%zu cfg=%d: SoA gate refused\n",
                           bi, cfg);
                    return 6;
                }
                memset(&g_tctx, 0, sizeof(g_tctx));
                setup_stage_desc(&d, 0, &g_tctx.sd0);
                if (dual)
                    setup_stage_desc(&d, 1, &g_tctx.sd1);
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, 400,
                               42000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
            printf("soa-sub base %zu (zc[%zu]): 32 configs done\n",
                   bi, ssub_base[bi]);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
    }

    /* SoA S3TC/YUV tri/split phase: trilinear (minb 4/5) and the
       minification/magnification filter split for the BL-subroutine dts,
       single stage. Dual combinations have their own phase. The third decode
       scratch word uses v31.s[2], keeping frame word 184 for the modeled
       trilinear mip weight, which must survive both decode calls. Sweeps:
       tri on every sub dt (both minb parities), split on every sub dt (all
       minb, mipdis toggling between the base-level and mip'd split incl. the
       tri-pair mag rerun), and riders mirrored into BOTH halves -- alpha
       test (tri's deferred cover stash above the 512 frame takes the sub-sp
       prologue), blend, partial wmask. cfgs 0..11 tri / 12..23 split; within
       each half 0..7 sweep the sub-dt table plain, 8..11 are the riders on
       random sub dts. Short/wrap rows on odd cfgs. Follows the S3TC/YUV
       phase, preserving earlier dump inputs. */
    {
        static const size_t   ssub_base[] = { 0, 1, 2, 3, 6, 8, 11, 12 };
        static const uint32_t subdts[8][2] = {
            { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 },
            { 11, 0 }, { 12, 0 }, { 14, 0 }, { 0, 3 },
        };
        static const uint32_t bsrcs[13]  = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                             0xa, 0xb, 0xc };
        static const uint32_t bdsts[12]  = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                             0xa, 0xb };

        g_real_tex = 1;
        for (size_t bi = 0; bi < sizeof(ssub_base) / sizeof(ssub_base[0]); bi++) {
            for (int cfg = 0; cfg < 24; cfg++) {
                rage128_draw_state_t d;
                int split = cfg >= 12;
                int sub12 = cfg % 12;
                int dti   = (sub12 < 8) ? sub12 : (int) (rng() % 8);

                mk_state(&d, &zc[ssub_base[bi]]);
                d.tex_en   = 1;
                d.sec_en   = 0;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = 0;
                d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                mk_tex_stage(&d, 0);
                d.sh[0].dt   = subdts[dti][0];
                d.sh[0].s3tc = subdts[dti][1];
                if (!split) { /* trilinear, matching magnification */
                    d.sh[0].minb   = 4 + (int) (rng() % 2);
                    d.sh[0].mag    = d.sh[0].minb & 1;
                    d.sh[0].mipdis = 0;
                } else {      /* minification/magnification split on every minb */
                    d.sh[0].minb   = rng() % 6;
                    d.sh[0].mag    = (d.sh[0].minb & 1) ^ 1;
                    d.sh[0].mipdis = (int) (rng() % 2);
                }
                if (dump && bi == 2 && cfg == 3) {
                    /* disasm block: DXT5 trilinear -- interpolated-alpha
                       sub + slot pair + two gather passes + mip-weight blend --
                       on clamp/mirror wraps under persp */
                    d.do_persp      = 1;
                    d.sh[0].minb    = 5;
                    d.sh[0].mag     = 1;
                    d.sh[0].mipdis  = 0;
                    d.sh[0].clamp_s = 2;
                    d.sh[0].clamp_t = 1;
                }
                d.ck3d_on = d.ckc_on = 0;
                d.need_ck = 0;
                if (sub12 == 8 || sub12 == 9) { /* atest rider (tri: 528
                                                   sub-sp frame) */
                    d.atest_en  = 1;
                    d.atest_fn  = 1 + rng() % 7;
                    d.atest_ref = rng() & 0xff;
                }
                if (sub12 == 10) { /* blend rider */
                    d.alpha_en = 1;
                    d.bsrc     = bsrcs[rng() % 13];
                    d.bdst     = bdsts[rng() % 12];
                    d.bfcn     = rng() % 4;
                }
                if (sub12 == 11) /* partial wmask rider (SoA RMW store) */
                    d.wmask = (rng() & 1)
                                  ? rng()
                                  : ((rng() & 1) ? ~(1u << (rng() % 32)) : 0);
                d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                          || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1));
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                if (!d.need_lod || !r128_a64_soa_tex_can(&d)
                    || !r128_a64_texinline_can(&d)) {
                    printf("soa-sub-tri case bi=%zu cfg=%d: gate refused\n",
                           bi, cfg);
                    return 6;
                }
                memset(&g_tctx, 0, sizeof(g_tctx));
                setup_stage_desc(&d, 0, &g_tctx.sd0);
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, dump ? 0 : 400,
                               44000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
                if (dump && bi == 2 && cfg == 3) {
                    pthread_jit_write_protect_np(0);
                    int len = r128_jit_arm64_generate(code, &d);
                    pthread_jit_write_protect_np(1);
                    FILE *f = fopen("block_soa_sub_tri.bin", "wb");
                    fwrite(code, 1, (size_t) len, f);
                    fclose(f);
                    printf("dumped SoA sub-dt trilinear case, %d bytes\n", len);
                    __builtin___clear_cache((char *) code, (char *) code + len);
                }
            }
            printf("soa-sub-tri base %zu (zc[%zu]): 24 configs done\n",
                   bi, ssub_base[bi]);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
    }

    /* SoA dual-stage tri/split phase: sec_en states with a trilinear or
       minification/magnification-split stage take the vector loop when the
       stage-pair weight clears the 16 KB cap gate
       (r128_a64_soa_stage_weight); heavier pairs keep the scalar loop and
       verify through it here too. Every cfg forces tri or split on one stage
       (the other draws any class, so tri x tri / split x split land too);
       dts sweep the full family incl the BL-subroutine dts -- the shared
       decode sub runs with v31.s[2] scratch because the modeled mip weight
       stays live across both fetches of a dual stage. Riders mirror the
       single-stage tri phase: alpha test (cover stash above the 656 sub-sp
       frame), blend, partial wmask. Short/wrap rows on odd cfgs. Phase order
       keeps earlier dump inputs fixed. */
    {
        static const size_t   t2t_base[]   = { 0, 1, 2, 3, 6, 8, 11, 12 };
        static const uint32_t t2dts[12][2] = {
            { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 11, 0 }, { 12, 0 },
            { 14, 0 }, { 4, 0 }, { 6, 0 }, { 1, 0 }, { 2, 0 }, { 15, 0 },
        };
        static const uint32_t bsrcs[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb, 0xc };
        static const uint32_t bdsts[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb };

        g_real_tex = 1;
        for (size_t bi = 0; bi < sizeof(t2t_base) / sizeof(t2t_base[0]); bi++) {
            int nsoa = 0;

            for (int cfg = 0; cfg < 24; cfg++) {
                rage128_draw_state_t d;
                int sub12 = cfg % 12;

                mk_state(&d, &zc[t2t_base[bi]]);
                d.tex_en   = 1;
                d.sec_en   = 1;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = (int) (rng() % 2);
                d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                mk_tex_stage(&d, 0);
                mk_tex_stage(&d, 1);
                for (int st = 0; st < 2; st++) {
                    uint32_t di = rng() % 12;

                    d.sh[st].dt   = t2dts[di][0];
                    d.sh[st].s3tc = t2dts[di][1];
                    if (st == (cfg & 1)) {
                        /* forced stage: tri (mag both parities, so the
                           tri+split combined class lands) or split */
                        if ((cfg >> 1) & 1) {
                            d.sh[st].minb   = rng() % 6;
                            d.sh[st].mag    = (d.sh[st].minb & 1) ^ 1;
                            d.sh[st].mipdis = (int) (rng() % 2);
                        } else {
                            d.sh[st].minb   = 4 + (int) (rng() % 2);
                            d.sh[st].mag    = rng() % 2;
                            d.sh[st].mipdis = 0;
                        }
                    } else {
                        switch (rng() % 6) { /* any class */
                            case 0:
                                d.sh[st].minb   = 0;
                                d.sh[st].mag    = 0;
                                d.sh[st].mipdis = (int) (rng() % 2);
                                break;
                            case 1:
                                d.sh[st].minb   = 1;
                                d.sh[st].mag    = 1;
                                d.sh[st].mipdis = (int) (rng() % 2);
                                break;
                            case 2:
                                d.sh[st].minb   = 2;
                                d.sh[st].mag    = 0;
                                d.sh[st].mipdis = 0;
                                break;
                            case 3:
                                d.sh[st].minb   = 3;
                                d.sh[st].mag    = 1;
                                d.sh[st].mipdis = 0;
                                break;
                            case 4:
                                d.sh[st].minb   = 4 + (int) (rng() % 2);
                                d.sh[st].mag    = d.sh[st].minb & 1;
                                d.sh[st].mipdis = 0;
                                break;
                            default:
                                d.sh[st].minb   = rng() % 6;
                                d.sh[st].mag    = (d.sh[st].minb & 1) ^ 1;
                                d.sh[st].mipdis = (int) (rng() % 2);
                                break;
                        }
                    }
                }
                d.ck3d_on = d.ckc_on = 0;
                d.need_ck = 0;
                if (sub12 == 8 || sub12 == 9) { /* atest rider */
                    d.atest_en  = 1;
                    d.atest_fn  = 1 + rng() % 7;
                    d.atest_ref = rng() & 0xff;
                }
                if (sub12 == 10) { /* blend rider */
                    d.alpha_en = 1;
                    d.bsrc     = bsrcs[rng() % 13];
                    d.bdst     = bdsts[rng() % 12];
                    d.bfcn     = rng() % 4;
                }
                if (sub12 == 11) /* partial wmask rider */
                    d.wmask = (rng() & 1)
                                  ? rng()
                                  : ((rng() & 1) ? ~(1u << (rng() % 32)) : 0);
                if (dump && bi == 2 && cfg == 2) {
                    /* disasm block: DXT1 trilinear stage 0 feeding a
                       565 nearest stage 1 -- shared sub under a live
                       mip weight, stage-0 output handoff, clamp/mirror wraps */
                    d.do_persp      = 1;
                    d.sh[0].dt      = 0;
                    d.sh[0].s3tc    = 0;
                    d.sh[0].minb    = 5;
                    d.sh[0].mag     = 1;
                    d.sh[0].mipdis  = 0;
                    d.sh[0].clamp_s = 2;
                    d.sh[0].clamp_t = 1;
                    d.sh[1].dt      = 4;
                    d.sh[1].minb    = 0;
                    d.sh[1].mag     = 0;
                    d.sh[1].mipdis  = 1;
                }
                d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                          || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1));
                d.need_lod2 = (!d.sh[1].mipdis && d.sh[1].minb >= 2)
                           || ((int) (d.sh[1].minb & 1) != (d.sh[1].mag == 1));
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                if (!r128_a64_texinline_can(&d)) {
                    printf("soa-t2-tri case bi=%zu cfg=%d: inline gate refused\n",
                           bi, cfg);
                    return 6;
                }
                nsoa += r128_a64_soa_tex_can(&d); /* heavy pairs: scalar */
                memset(&g_tctx, 0, sizeof(g_tctx));
                setup_stage_desc(&d, 0, &g_tctx.sd0);
                setup_stage_desc(&d, 1, &g_tctx.sd1);
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, dump ? 0 : 400,
                               46000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
                if (dump && bi == 2 && cfg == 2) {
                    pthread_jit_write_protect_np(0);
                    int len = r128_jit_arm64_generate(code, &d);
                    pthread_jit_write_protect_np(1);
                    FILE *f = fopen("block_soa_t2_tri.bin", "wb");
                    fwrite(code, 1, (size_t) len, f);
                    fclose(f);
                    printf("dumped SoA dual trilinear case, %d bytes\n", len);
                    __builtin___clear_cache((char *) code, (char *) code + len);
                }
            }
            printf("soa-t2-tri base %zu (zc[%zu]): 24 configs done, %d SoA\n",
                   bi, t2t_base[bi], nsoa);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
    }

    /* SoA dual-stage chroma-key phase: sec_en +
       need_ck states take the vector loop -- the class gate pins stage
       0 to no-LOD nearest (where the gathered c00 IS tnear) and the
       cover stash moves to the dedicated slot above the 656 frame (672
       sub-sp; R128_A64_SP_SOA_VLW at 96 is per-stage working storage
       in dual blocks). Stage 1
       draws any covered class incl tri/split and the BL-sub dts. Keyed
       texels must drop out of the store mask AND the rx0/rx1 span.
       Riders mirror the dual tri phase -- alpha test (ck and atest AND
       into the SAME stash), blend, partial wmask. Keys mostly from the
       arena (pick_key) so real rejects occur; short/wrap rows on odd
       cfgs. Phase order keeps earlier dump inputs fixed. */
    {
        static const size_t   ck2_base[]   = { 0, 1, 2, 3, 6, 8, 11, 12 };
        static const uint32_t ck2dts[12][2] = {
            { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 11, 0 }, { 12, 0 },
            { 14, 0 }, { 4, 0 }, { 6, 0 }, { 1, 0 }, { 2, 0 }, { 15, 0 },
        };
        static const uint32_t bsrcs[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb, 0xc };
        static const uint32_t bdsts[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb };

        g_real_tex = 1;
        for (size_t bi = 0; bi < sizeof(ck2_base) / sizeof(ck2_base[0]); bi++) {
            for (int cfg = 0; cfg < 24; cfg++) {
                rage128_draw_state_t d;
                int sub12 = cfg % 12;

                mk_state(&d, &zc[ck2_base[bi]]);
                d.tex_en   = 1;
                d.sec_en   = 1;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = (int) (rng() % 2);
                d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                mk_tex_stage(&d, 0);
                mk_tex_stage(&d, 1);
                /* stage 0: the gated ck class -- no-LOD nearest */
                d.sh[0].dt     = ck2dts[rng() % 12][0];
                d.sh[0].s3tc   = d.sh[0].dt ? 0 : ck2dts[rng() % 4][1];
                d.sh[0].minb   = 0;
                d.sh[0].mag    = 0;
                d.sh[0].mipdis = (int) (rng() % 2);
                /* stage 1: any covered class */
                {
                    uint32_t di = rng() % 12;

                    d.sh[1].dt   = ck2dts[di][0];
                    d.sh[1].s3tc = ck2dts[di][1];
                    switch (rng() % 6) {
                        case 0:
                            d.sh[1].minb   = 0;
                            d.sh[1].mag    = 0;
                            d.sh[1].mipdis = (int) (rng() % 2);
                            break;
                        case 1:
                            d.sh[1].minb   = 1;
                            d.sh[1].mag    = 1;
                            d.sh[1].mipdis = (int) (rng() % 2);
                            break;
                        case 2:
                            d.sh[1].minb   = 2;
                            d.sh[1].mag    = 0;
                            d.sh[1].mipdis = 0;
                            break;
                        case 3:
                            d.sh[1].minb   = 3;
                            d.sh[1].mag    = 1;
                            d.sh[1].mipdis = 0;
                            break;
                        case 4:
                            d.sh[1].minb   = 4 + (int) (rng() % 2);
                            d.sh[1].mag    = d.sh[1].minb & 1;
                            d.sh[1].mipdis = 0;
                            break;
                        default:
                            d.sh[1].minb   = rng() % 6;
                            d.sh[1].mag    = (d.sh[1].minb & 1) ^ 1;
                            d.sh[1].mipdis = (int) (rng() % 2);
                            break;
                    }
                }
                if (d.sh[0].dt == 1 || d.sh[0].dt == 2)
                    d.sh[0].dt = 4; /* CI stage-0 keys keep the scalar loop (covered by the tex-base sweep) */
                d.ck3d_on  = (int) (rng() & 1);
                d.ckc_on   = (!d.ck3d_on || (rng() & 3) == 0) ? 1 : 0;
                d.ckfn     = (rng() & 1) ? 3 : 2;
                d.ck3d_msk = g_ckmsks[rng() % N_CKMSKS];
                d.ck3d_clr = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                d.ckc_msk  = g_ckmsks[rng() % N_CKMSKS];
                d.ckc_clr  = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                d.need_ck  = 1;
                if (sub12 == 8 || sub12 == 9) { /* atest rider: shared stash */
                    d.atest_en  = 1;
                    d.atest_fn  = 1 + rng() % 7;
                    d.atest_ref = rng() & 0xff;
                }
                if (sub12 == 10) { /* blend rider */
                    d.alpha_en = 1;
                    d.bsrc     = bsrcs[rng() % 13];
                    d.bdst     = bdsts[rng() % 12];
                    d.bfcn     = rng() % 4;
                }
                if (sub12 == 11) /* partial wmask rider */
                    d.wmask = (rng() & 1)
                                  ? rng()
                                  : ((rng() & 1) ? ~(1u << (rng() % 32)) : 0);
                d.need_lod = 0; /* stage 0 pinned no-LOD nearest */
                d.need_lod2 = (!d.sh[1].mipdis && d.sh[1].minb >= 2)
                           || ((int) (d.sh[1].minb & 1) != (d.sh[1].mag == 1));
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                if (!r128_a64_texinline_can(&d)) {
                    printf("soa-t2-ck case bi=%zu cfg=%d: inline gate refused\n",
                           bi, cfg);
                    return 6;
                }
                /* stage 0 weighs 0 (no-LOD nearest), so every pair
                   clears the 16 KB gate: SoA is mandatory here */
                if (!r128_a64_soa_tex_can(&d)) {
                    printf("soa-t2-ck case bi=%zu cfg=%d: SoA gate refused\n",
                           bi, cfg);
                    return 6;
                }
                memset(&g_tctx, 0, sizeof(g_tctx));
                setup_stage_desc(&d, 0, &g_tctx.sd0);
                setup_stage_desc(&d, 1, &g_tctx.sd1);
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, 400,
                               48000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
            printf("soa-t2-ck base %zu (zc[%zu]): 24 configs done\n",
                   bi, ck2_base[bi]);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
    }

    /* SoA filtered chroma-key phase: bilinear, mip-nearest/linear,
       trilinear and split filters fetch tnear separately, before filtering.
       The modeled sampler uses unbiased nearest coordinates at the primary
       level; magnified lanes select top, matching r3d_tex_level. Mip and
       split working sets need a dedicated coverage stash above the frame.
       Single-stage cases require vector admission; dual cases include any
       stage-1 class and count heavy pairs verified by scalar blocks.
       Alpha test shares the stash; blend and partial masks exercise stores.
       Keys come mostly from decoded texels so rejection occurs. Short/wrap
       rows exercise mixed lanes; phase order keeps dump inputs fixed. */
    {
        static const size_t   lck_base[]    = { 0, 1, 2, 3, 6, 8, 11, 12 };
        static const uint32_t lckdts[12][2] = {
            { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 11, 0 }, { 12, 0 },
            { 14, 0 }, { 4, 0 }, { 6, 0 }, { 1, 0 }, { 2, 0 }, { 15, 0 },
        };
        /* the filtered stage-0 classes: minb, mag, mipdis (-1 = random) */
        static const int lckcls[11][3] = {
            { 1, 1, -1 },           /* bilinear, no LOD             */
            { 2, 0, 0 },            /* mip-nearest, nearest texel   */
            { 3, 1, 0 },            /* mip-nearest, linear texel    */
            { 4, 0, 0 },            /* trilinear, nearest texel     */
            { 5, 1, 0 },            /* trilinear, linear texel      */
            { 0, 1, -1 },           /* split: nearest minification, lin magnification  */
            { 1, 0, -1 },           /* split: linear minification, near magnification  */
            { 2, 1, 0 },            /* split + mip-nearest          */
            { 3, 0, 0 },            /* split + mip-linear           */
            { 4, 1, 0 },            /* split + trilinear-nearest    */
            { 5, 0, 0 },            /* split + trilinear-linear     */
        };
        static const uint32_t bsrcs[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb, 0xc };
        static const uint32_t bdsts[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb };
        int scalar_n = 0;

        g_real_tex = 1;
        for (size_t bi = 0; bi < sizeof(lck_base) / sizeof(lck_base[0]); bi++) {
            for (int cfg = 0; cfg < 24; cfg++) {
                rage128_draw_state_t d;
                int        dual  = cfg >= 16;
                int        sub8  = cfg % 8;
                const int *cl    = lckcls[dual ? (rng() % 11) : (cfg % 11)];

                mk_state(&d, &zc[lck_base[bi]]);
                d.tex_en   = 1;
                d.sec_en   = dual;
                d.do_persp = (int) (rng() % 2);
                d.sec_sel  = dual ? (int) (rng() % 2) : 0;
                d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                d.premult  = 0;
                for (int k = 0; k < 4; k++)
                    d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                mk_tex_stage(&d, 0);
                {
                    uint32_t di = rng() % 12;

                    d.sh[0].dt   = lckdts[di][0];
                    d.sh[0].s3tc = lckdts[di][1];
                }
                d.sh[0].minb   = (uint32_t) cl[0];
                d.sh[0].mag    = (uint32_t) cl[1];
                d.sh[0].mipdis = cl[2] < 0 ? (int) (rng() % 2) : cl[2];
                if (dual) {
                    /* stage 1: any covered class (heavy ones included --
                       the weight gate decides) */
                    uint32_t di = rng() % 12;

                    mk_tex_stage(&d, 1);
                    d.sh[1].dt   = lckdts[di][0];
                    d.sh[1].s3tc = lckdts[di][1];
                    switch (rng() % 6) {
                        case 0:
                            d.sh[1].minb   = 0;
                            d.sh[1].mag    = 0;
                            d.sh[1].mipdis = (int) (rng() % 2);
                            break;
                        case 1:
                            d.sh[1].minb   = 1;
                            d.sh[1].mag    = 1;
                            d.sh[1].mipdis = (int) (rng() % 2);
                            break;
                        case 2:
                            d.sh[1].minb   = 2;
                            d.sh[1].mag    = 0;
                            d.sh[1].mipdis = 0;
                            break;
                        case 3:
                            d.sh[1].minb   = 3;
                            d.sh[1].mag    = 1;
                            d.sh[1].mipdis = 0;
                            break;
                        case 4:
                            d.sh[1].minb   = 4 + (int) (rng() % 2);
                            d.sh[1].mag    = d.sh[1].minb & 1;
                            d.sh[1].mipdis = 0;
                            break;
                        default:
                            d.sh[1].minb   = rng() % 6;
                            d.sh[1].mag    = (d.sh[1].minb & 1) ^ 1;
                            d.sh[1].mipdis = (int) (rng() % 2);
                            break;
                    }
                }
                if (d.sh[0].dt == 1 || d.sh[0].dt == 2)
                    d.sh[0].dt = 4; /* CI stage-0 keys keep the scalar loop (covered by the tex-base sweep) */
                d.ck3d_on  = (int) (rng() & 1);
                d.ckc_on   = (!d.ck3d_on || (rng() & 3) == 0) ? 1 : 0;
                d.ckfn     = (rng() & 1) ? 3 : 2;
                d.ck3d_msk = g_ckmsks[rng() % N_CKMSKS];
                d.ck3d_clr = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                d.ckc_msk  = g_ckmsks[rng() % N_CKMSKS];
                d.ckc_clr  = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                d.need_ck  = 1;
                if (sub8 == 4 || sub8 == 5) { /* atest rider: shared stash */
                    d.atest_en  = 1;
                    d.atest_fn  = 1 + rng() % 7;
                    d.atest_ref = rng() & 0xff;
                }
                if (sub8 == 6) { /* blend rider */
                    d.alpha_en = 1;
                    d.bsrc     = bsrcs[rng() % 13];
                    d.bdst     = bdsts[rng() % 12];
                    d.bfcn     = rng() % 4;
                }
                if (sub8 == 7) /* partial wmask rider */
                    d.wmask = (rng() & 1)
                                  ? rng()
                                  : ((rng() & 1) ? ~(1u << (rng() % 32)) : 0);
                d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                          || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1));
                d.need_lod2 = dual
                    && ((!d.sh[1].mipdis && d.sh[1].minb >= 2)
                        || ((int) (d.sh[1].minb & 1) != (d.sh[1].mag == 1)));
                d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                if (!r128_a64_texinline_can(&d)) {
                    printf("soa-lck case bi=%zu cfg=%d: inline gate refused\n",
                           bi, cfg);
                    return 6;
                }
                if (!r128_a64_soa_tex_can(&d)) {
                    if (!dual) { /* single-stage filtered chroma key must be admitted */
                        printf("soa-lck case bi=%zu cfg=%d: SoA gate refused\n",
                               bi, cfg);
                        return 6;
                    }
                    scalar_n++; /* heavy dual pair: weight gate said no */
                }
                memset(&g_tctx, 0, sizeof(g_tctx));
                setup_stage_desc(&d, 0, &g_tctx.sd0);
                if (dual)
                    setup_stage_desc(&d, 1, &g_tctx.sd1);
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, 400,
                               50000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
            printf("soa-lck base %zu (zc[%zu]): 24 configs done\n",
                   bi, lck_base[bi]);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
        printf("soa-lck phase: done (%d dual pairs weight-gated to scalar)\n",
               scalar_n);
    }

    /* SoA specular/fog/auxiliary phase: specular adds a second weight dot
       to each RGB channel and clamps above one; vertex fog interpolates
       toward fog color with a clamped factor. Textured blocks stash vertex
       weights in the dual-stage frame. Auxiliary scissors combine per-lane
       x masks with the row's y-active mask at R128_A64_SP_AUX or
       R128_X64_SP_AUX, removing rejected lanes from stores and the span.
       Untextured depth-indexed fog uses the vector loop; textured table fog
       uses helper calls. Single/dual texture stages and key, alpha-test,
       blend and partial-mask riders exercise the size gate; heavy pairs
       verify scalar blocks. Odd configurations use short/wrap rows. */
    {
        static const size_t   sfa_base[]  = { 0, 1, 2, 3, 6, 8, 11, 12 };
        static const uint32_t sdts[12][2] = {
            { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 11, 0 }, { 12, 0 },
            { 14, 0 }, { 4, 0 }, { 6, 0 }, { 1, 0 }, { 2, 0 }, { 15, 0 },
        };
        static const uint32_t bsrcs[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb, 0xc };
        static const uint32_t bdsts[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb };
        int scalar_n = 0;

        for (size_t bi = 0; bi < sizeof(sfa_base) / sizeof(sfa_base[0]); bi++) {
            for (int cfg = 0; cfg < 32; cfg++) {
                rage128_draw_state_t d;
                int tex   = cfg >= 8;
                int dual  = cfg >= 24;
                int sub8  = cfg % 8;
                int sf    = (cfg % 4) != 3;          /* spec and/or fog  */
                int aux   = (cfg % 4) >= 2 || cfg < 8;
                /* table-fog gate control: z-carrying bases only (the
                   forced z_en needs the base's real z config) */
                int ftab  = !tex && cfg == 6 && zc[sfa_base[bi]].z_en;

                mk_state(&d, &zc[sfa_base[bi]]);
                if (sf) {
                    d.spec_en = (int) (rng() & 1);
                    d.fog_en  = d.spec_en ? (int) (rng() & 1) : 1;
                    d.fogr    = (float) (rng() & 0xff) / 255.0f;
                    d.fogg    = (float) (rng() & 0xff) / 255.0f;
                    d.fogb    = (float) (rng() & 0xff) / 255.0f;
                }
                if (ftab) {
                    d.z_en         = 1;
                    d.fog_en       = 1;
                    d.fog_table_en = 1;
                }
                if (aux) {
                    int first = (int) (rng() % 3), nen = 1 + (int) (rng() % (3 - first));

                    d.aux_on = 1;
                    for (int i = first; i < first + nen; i++) {
                        d.aux_cntl |= 1u << (i * 2);
                        if (rng() & 1)
                            d.aux_cntl |= 2u << (i * 2);
                        d.aux_x0[i] = (int32_t) (rng() % 340) - 40;
                        d.aux_x1[i] = d.aux_x0[i] + (int32_t) (rng() % 220) - 20;
                        d.aux_y0[i] = (int32_t) (rng() % 1140) - 60;
                        d.aux_y1[i] = d.aux_y0[i] + (int32_t) (rng() % 560) - 60;
                    }
                }
                if (sub8 == 4) { /* blend rider */
                    d.alpha_en = 1;
                    d.bsrc     = bsrcs[rng() % 13];
                    d.bdst     = bdsts[rng() % 12];
                    d.bfcn     = rng() % 4;
                }
                if (sub8 == 5) { /* atest rider */
                    d.atest_en  = 1;
                    d.atest_fn  = 1 + rng() % 7;
                    d.atest_ref = rng() & 0xff;
                }
                if (sub8 == 6 && tex) /* partial wmask rider */
                    d.wmask = (rng() & 1)
                                  ? rng()
                                  : ((rng() & 1) ? ~(1u << (rng() % 32)) : 0);
                if (tex) {
                    d.tex_en   = 1;
                    d.sec_en   = dual;
                    d.do_persp = (int) (rng() % 2);
                    d.sec_sel  = dual ? (int) (rng() % 2) : 0;
                    d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                    d.premult  = 0;
                    for (int k = 0; k < 4; k++)
                        d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                    for (int st = 0; st < (dual ? 2 : 1); st++) {
                        uint32_t di = rng() % 12;

                        mk_tex_stage(&d, st);
                        d.sh[st].dt   = sdts[di][0];
                        d.sh[st].s3tc = sdts[di][1];
                        switch (rng() % 6) { /* all covered classes */
                            case 0:
                                d.sh[st].minb   = 0;
                                d.sh[st].mag    = 0;
                                d.sh[st].mipdis = (int) (rng() % 2);
                                break;
                            case 1:
                                d.sh[st].minb   = 1;
                                d.sh[st].mag    = 1;
                                d.sh[st].mipdis = (int) (rng() % 2);
                                break;
                            case 2:
                                d.sh[st].minb   = 2 + (int) (rng() % 2);
                                d.sh[st].mag    = d.sh[st].minb & 1;
                                d.sh[st].mipdis = 0;
                                break;
                            case 3:
                                d.sh[st].minb   = 4 + (int) (rng() % 2);
                                d.sh[st].mag    = d.sh[st].minb & 1;
                                d.sh[st].mipdis = 0;
                                break;
                            default:
                                d.sh[st].minb   = rng() % 6;
                                d.sh[st].mag    = (d.sh[st].minb & 1) ^ 1;
                                d.sh[st].mipdis = (int) (rng() % 2);
                                break;
                        }
                    }
                    if (sub8 == 7) { /* chroma-key rider, any class */
                        if (d.sh[0].dt == 1 || d.sh[0].dt == 2)
                            d.sh[0].dt = 4; /* CI stage-0 keys keep the scalar loop */
                        d.ck3d_on  = (int) (rng() & 1);
                        d.ckc_on   = (!d.ck3d_on || (rng() & 3) == 0) ? 1 : 0;
                        d.ckfn     = (rng() & 1) ? 3 : 2;
                        d.ck3d_msk = g_ckmsks[rng() % N_CKMSKS];
                        d.ck3d_clr = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                        d.ckc_msk  = g_ckmsks[rng() % N_CKMSKS];
                        d.ckc_clr  = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                        d.need_ck  = 1;
                    }
                    d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                              || ((int) (d.sh[0].minb & 1)
                                  != (d.sh[0].mag == 1));
                    d.need_lod2 = dual
                        && ((!d.sh[1].mipdis && d.sh[1].minb >= 2)
                            || ((int) (d.sh[1].minb & 1)
                                != (d.sh[1].mag == 1)));
                    d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                    d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                    d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                    d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                    if (!r128_a64_texinline_can(&d)) {
                        printf("soa-sfa case bi=%zu cfg=%d: inline gate refused\n",
                               bi, cfg);
                        return 6;
                    }
                    if (!r128_a64_soa_tex_can(&d)) {
                        if (!dual) {
                            printf("soa-sfa case bi=%zu cfg=%d: SoA gate refused\n",
                                   bi, cfg);
                            return 6;
                        }
                        scalar_n++; /* heavy pair + 516 rider: scalar */
                    }
                    memset(&g_tctx, 0, sizeof(g_tctx));
                    setup_stage_desc(&d, 0, &g_tctx.sd0);
                    if (dual)
                        setup_stage_desc(&d, 1, &g_tctx.sd1);
                } else if (ftab) {
                    if (!r128_a64_soa_can(&d)) {
                        printf("soa-sfa case bi=%zu cfg=%d: table fog refused\n",
                               bi, cfg);
                        return 6; /* untextured z_en table fog is SoA */
                    }
                } else if (!r128_a64_soa_can(&d)) {
                    printf("soa-sfa case bi=%zu cfg=%d: SoA gate refused\n",
                           bi, cfg);
                    return 6;
                }
                g_real_tex  = tex;
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, 400,
                               52000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
            printf("soa-sfa base %zu (zc[%zu]): 32 configs done\n",
                   bi, sfa_base[bi]);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
        printf("soa-sfa phase: done (%d dual pairs weight-gated to scalar)\n",
               scalar_n);
    }

    /* SoA heavy-pair phase: modeled inline weights above 8600 use an
       intra-block coordinate/gather subroutine per trilinear stage when
       sub-mode weights fit the 8500 budget. Its return address has a frame
       slot above coverage because nested S3TC/YUV decoding uses v31.d[0].
       Exercise subroutines on stage 0, stage 1 and both, shared tnear calls
       for nearest filters, and specular/fog (+516) and auxiliary (+360)
       riders. Count pairs sent to scalar blocks and verify them there. */
    {
        typedef struct dsub_cls { uint32_t minb, mag; int mipdis; } dsub_cls;
        static const dsub_cls dc[8] = {
            { 1, 1, 1 },            /* 0 n-bl                            */
            { 2, 0, 0 },            /* 1 m-nr                            */
            { 3, 1, 0 },            /* 2 m-bl                            */
            { 0, 1, 1 },            /* 3 sb-nb                           */
            { 4, 0, 0 },            /* 4 t-nr                            */
            { 5, 1, 0 },            /* 5 t-bl                            */
            { 4, 1, 0 },            /* 6 ts-nb                           */
            { 5, 0, 0 },            /* 7 ts-bn                           */
        };
        /* the sub-mode pair list: stage-0 class x stage-1 class +
           stage-0 chroma key (sums per soa_stage_weight_sub) */
        static const struct { uint8_t c0, c1, ck; } dpairs[] = {
            { 5, 0, 0 }, { 5, 1, 0 }, { 5, 4, 0 }, /* t-bl  x n-bl/m-nr/t-nr */
            { 7, 0, 0 }, { 7, 1, 0 }, { 7, 4, 0 }, /* ts-bn x same           */
            { 6, 2, 0 }, { 6, 3, 0 },              /* ts-nb x m-bl/sb-nb     */
            { 4, 5, 0 },                           /* both stages sub'd      */
            { 0, 7, 0 }, { 2, 6, 0 },              /* sub on stage 1 only    */
            { 4, 5, 1 }, { 4, 6, 1 }, { 4, 7, 1 }, /* ck+t-nr (shared tnear) */
            { 5, 1, 1 },                           /* ck+t-bl (inline tnear) */
            { 6, 3, 1 },                           /* ck+ts-nb (shared)      */
            { 7, 0, 1 }, { 7, 1, 1 }, { 7, 4, 1 }, /* ck+ts-bn (inline)      */
        };
        static const size_t   dsb_base[]  = { 0, 2, 6, 12 };
        static const uint32_t ddts[12][2] = {
            { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 11, 0 }, { 12, 0 },
            { 14, 0 }, { 4, 0 }, { 6, 0 }, { 1, 0 }, { 2, 0 }, { 15, 0 },
        };
        static const uint32_t bsrcs[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb, 0xc };
        static const uint32_t bdsts[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb };
        int scalar_n = 0;

        g_real_tex = 1;
        for (size_t bi = 0; bi < sizeof(dsb_base) / sizeof(dsb_base[0]); bi++) {
            for (size_t pi = 0; pi < sizeof(dpairs) / sizeof(dpairs[0]); pi++) {
                for (int cfg = 0; cfg < 5; cfg++) {
                    /* cfg: 0 bare, 1 atest, 2 blend+wmask, 3 sf, 4 aux */
                    rage128_draw_state_t d;

                    mk_state(&d, &zc[dsb_base[bi]]);
                    d.tex_en   = 1;
                    d.sec_en   = 1;
                    d.do_persp = (int) (rng() % 2);
                    d.sec_sel  = (int) (rng() % 2);
                    d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                    d.premult  = 0;
                    for (int k = 0; k < 4; k++)
                        d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                    for (int st = 0; st < 2; st++) {
                        const dsub_cls *c = &dc[st ? dpairs[pi].c1
                                                   : dpairs[pi].c0];
                        uint32_t        di = rng() % 12;

                        mk_tex_stage(&d, st);
                        d.sh[st].dt     = ddts[di][0];
                        d.sh[st].s3tc   = ddts[di][1];
                        d.sh[st].minb   = c->minb;
                        d.sh[st].mag    = c->mag;
                        d.sh[st].mipdis = c->mipdis;
                    }
                    if (cfg == 1) {
                        d.atest_en  = 1;
                        d.atest_fn  = 1 + rng() % 7;
                        d.atest_ref = rng() & 0xff;
                    }
                    if (cfg == 2) {
                        d.alpha_en = 1;
                        d.bsrc     = bsrcs[rng() % 13];
                        d.bdst     = bdsts[rng() % 12];
                        d.bfcn     = rng() % 4;
                        d.wmask    = (rng() & 1)
                                         ? rng()
                                         : ~(1u << (rng() % 32));
                    }
                    if (cfg == 3) {
                        d.spec_en = (int) (rng() & 1);
                        d.fog_en  = d.spec_en ? (int) (rng() & 1) : 1;
                        d.fogr    = (float) (rng() & 0xff) / 255.0f;
                        d.fogg    = (float) (rng() & 0xff) / 255.0f;
                        d.fogb    = (float) (rng() & 0xff) / 255.0f;
                    }
                    if (cfg == 4) {
                        int first = (int) (rng() % 3), nen = 1 + (int) (rng() % (3 - first));

                        d.aux_on = 1;
                        for (int i = first; i < first + nen; i++) {
                            d.aux_cntl |= 1u << (i * 2);
                            if (rng() & 1)
                                d.aux_cntl |= 2u << (i * 2);
                            d.aux_x0[i] = (int32_t) (rng() % 340) - 40;
                            d.aux_x1[i] = d.aux_x0[i]
                                        + (int32_t) (rng() % 220) - 20;
                            d.aux_y0[i] = (int32_t) (rng() % 1140) - 60;
                            d.aux_y1[i] = d.aux_y0[i]
                                        + (int32_t) (rng() % 560) - 60;
                        }
                    }
                    if (dpairs[pi].ck) {
                        if (d.sh[0].dt == 1 || d.sh[0].dt == 2)
                            d.sh[0].dt = 4; /* CI stage-0 keys keep the scalar loop */
                        d.ck3d_on  = (int) (rng() & 1);
                        d.ckc_on   = (!d.ck3d_on || (rng() & 3) == 0) ? 1 : 0;
                        d.ckfn     = (rng() & 1) ? 3 : 2;
                        d.ck3d_msk = g_ckmsks[rng() % N_CKMSKS];
                        d.ck3d_clr = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                        d.ckc_msk  = g_ckmsks[rng() % N_CKMSKS];
                        d.ckc_clr  = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                        d.need_ck  = 1;
                    }
                    if (dump && bi == 0 && pi == 0 && cfg == 0) {
                        /* disasm block: DXT1 t-bl stage 0 through the
                           coord sub (BL nesting into the decode sub)
                           feeding a 565 no-LOD bilinear stage 1 */
                        d.do_persp      = 1;
                        d.sh[0].dt      = 0;
                        d.sh[0].s3tc    = 0;
                        d.sh[0].clamp_s = 2;
                        d.sh[0].clamp_t = 1;
                        d.sh[1].dt      = 4;
                    }
                    d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                              || ((int) (d.sh[0].minb & 1)
                                  != (d.sh[0].mag == 1));
                    d.need_lod2 = (!d.sh[1].mipdis && d.sh[1].minb >= 2)
                               || ((int) (d.sh[1].minb & 1)
                                   != (d.sh[1].mag == 1));
                    d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                    d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                    d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                    d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                    if (!r128_a64_texinline_can(&d)) {
                        printf("soa-dsub bi=%zu pi=%zu cfg=%d: inline gate refused\n",
                               bi, pi, cfg);
                        return 6;
                    }
                    if (!r128_a64_soa_tex_can(&d)) {
                        if (cfg < 3) { /* only sf/aux may re-gate */
                            printf("soa-dsub bi=%zu pi=%zu cfg=%d: SoA gate refused\n",
                                   bi, pi, cfg);
                            return 6;
                        }
                        scalar_n++;
                    } else if (r128_a64_soa_stage_weight(&d, 0)
                                   + r128_a64_soa_stage_weight(&d, 1)
                                   + ((d.spec_en || d.fog_en) ? 516 : 0)
                               <= 8600) {
                        /* An admitted pair within the inline budget uses the inline
                           path. This sum matches r128_a64_soa_dual_sub and
                           must exceed the budget to exercise sub mode. */
                        printf("soa-dsub bi=%zu pi=%zu cfg=%d: pair fit INLINE\n",
                               bi, pi, cfg);
                        return 6; /* pair list must exercise sub mode */
                    }
                    memset(&g_tctx, 0, sizeof(g_tctx));
                    setup_stage_desc(&d, 0, &g_tctx.sd0);
                    setup_stage_desc(&d, 1, &g_tctx.sd1);
                    g_soa_short = (cfg & 1);
                    if (run_config(code, &d, dump ? 0 : 400,
                                   54000 + pi * 100 + bi * 25 + (size_t) cfg) < 0)
                        return 3;
                    if (dump && bi == 0 && pi == 0 && cfg == 0) {
                        pthread_jit_write_protect_np(0);
                        int len = r128_jit_arm64_generate(code, &d);
                        pthread_jit_write_protect_np(1);
                        FILE *f = fopen("block_soa_dsub.bin", "wb");
                        fwrite(code, 1, (size_t) len, f);
                        fclose(f);
                        printf("dumped SoA dual sub-mode case, %d bytes\n", len);
                        __builtin___clear_cache((char *) code,
                                                (char *) code + len);
                    }
                }
            }
            printf("soa-dsub base %zu (zc[%zu]): %zu pairs x 5 configs done\n",
                   bi, dsb_base[bi],
                   sizeof(dpairs) / sizeof(dpairs[0]));
        }
        g_soa_short = 0;
        g_real_tex  = 0;
        printf("soa-dsub phase: done (%d rider-gated to scalar)\n", scalar_n);
    }

    /* Untextured table fog with the Z test uses the vector stage's raw
       depth stash; Z-off states use the scalar DDA. Textured table fog
       uses the helper call so its depth operands survive sampling.
       Riders exercise specular, blending, alpha tests and scissors. */
    {
        static const size_t   tf_base[]   = { 2, 3, 4, 5, 6, 7, 8, 9,
                                              11, 12, 13, 17, 18 };
        static const uint32_t sdts[12][2] = {
            { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 11, 0 }, { 12, 0 },
            { 14, 0 }, { 4, 0 }, { 6, 0 }, { 1, 0 }, { 2, 0 }, { 15, 0 },
        };
        static const uint32_t bsrcs[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb, 0xc };
        static const uint32_t bdsts[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb };

        /* Z-off table fog compiles only as a scalar block because the
           vector fog stage requires the vector Z stage's depth stash. */
        {
            rage128_draw_state_t d;

            mk_state(&d, &zc[0]);
            d.fog_en       = 1;
            d.fog_table_en = 1;
            if (!r128_jit_arm64_can(&d) || !r128_jit_x64_can(&d)
                || r128_a64_soa_can(&d) || r128_jit_x64_soa_can(&d)) {
                printf("soa-tfog: z-off table fog scalar gate mismatch\n");
                return 6;
            }
        }

        for (size_t bi = 0; bi < sizeof(tf_base) / sizeof(tf_base[0]); bi++) {
            for (int cfg = 0; cfg < 8; cfg++) {
                rage128_draw_state_t d;
                int tex = cfg >= 6;

                mk_state(&d, &zc[tf_base[bi]]);
                d.fog_en       = 1;
                d.fog_table_en = 1;
                d.fogr         = (float) (rng() & 0xff) / 255.0f;
                d.fogg         = (float) (rng() & 0xff) / 255.0f;
                d.fogb         = (float) (rng() & 0xff) / 255.0f;
                switch (cfg) {
                    case 1: /* specular rides the same channel stage */
                        d.spec_en = 1;
                        break;
                    case 2: /* blend (+ partial wmask half the time) */
                        d.alpha_en = 1;
                        d.bsrc     = bsrcs[rng() % 13];
                        d.bdst     = bdsts[rng() % 12];
                        d.bfcn     = rng() % 4;
                        if (rng() & 1)
                            d.wmask = (rng() & 1) ? rng()
                                                  : ~(1u << (rng() % 32));
                        break;
                    case 3: /* alpha test (cover-mask AND, untextured) */
                        d.atest_en  = 1;
                        d.atest_fn  = 1 + rng() % 7;
                        d.atest_ref = rng() & 0xff;
                        break;
                    case 4: { /* aux scissors ahead of the z block */
                        int first = (int) (rng() % 3), nen = 1 + (int) (rng() % (3 - first));

                        d.aux_on = 1;
                        for (int i = first; i < first + nen; i++) {
                            d.aux_cntl |= 1u << (i * 2);
                            if (rng() & 1)
                                d.aux_cntl |= 2u << (i * 2);
                            d.aux_x0[i] = (int32_t) (rng() % 340) - 40;
                            d.aux_x1[i] = d.aux_x0[i]
                                        + (int32_t) (rng() % 220) - 20;
                            d.aux_y0[i] = (int32_t) (rng() % 1140) - 60;
                            d.aux_y1[i] = d.aux_y0[i]
                                        + (int32_t) (rng() % 560) - 60;
                        }
                        break;
                    }
                    case 5: /* always-pass + no write: the z block emits only
                               the zline chain + the fog stash */
                        d.zfn  = 7;
                        d.z_wr = 0;
                        break;
                    default:
                        break;
                }
                if (tex) {
                    /* textured table fog rides the CALL path (both jit
                       and ref use the synthetic seed helper: g_real_tex
                       stays 0, like the 18000 table-fog phase) */
                    d.tex_en   = 1;
                    d.do_persp = (int) (rng() % 2);
                    d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                    for (int k = 0; k < 4; k++)
                        d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                    if (cfg == 7) {
                        /* inline-family stage: generate still forces the
                           call path (table fog needs the z constants the
                           inline stage clobbers) */
                        uint32_t di = rng() % 12;

                        mk_tex_stage(&d, 0);
                        d.sh[0].dt   = sdts[di][0];
                        d.sh[0].s3tc = sdts[di][1];
                        d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                                  || ((int) (d.sh[0].minb & 1)
                                      != (d.sh[0].mag == 1));
                        d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                        d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                        d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                        d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                        if (r128_a64_soa_tex_can(&d)) {
                            printf("soa-tfog bi=%zu cfg=%d: textured table fog ADMITTED\n",
                                   bi, cfg);
                            return 6; /* must keep the texcall path */
                        }
                    }
                    /* cfg 6 keeps mk_state's dt=10 helper-call glue */
                } else if (!r128_a64_soa_can(&d)) {
                    printf("soa-tfog bi=%zu cfg=%d: SoA gate refused\n",
                           bi, cfg);
                    return 6;
                }
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, 400,
                               56000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
            printf("soa-tfog base %zu (zc[%zu]): 8 configs done\n",
                   bi, tf_base[bi]);
        }
        g_soa_short = 0;
        printf("soa-tfog phase: done\n");
    }

    /* SoA stencil phase: sten_on states take the vector loop -- the sten z
       block packs {sbuf | sres<<8 | zres<<9} per lane (a z fail must NOT
       drop the lane: it still runs its fail op), the post-pack stage
       computes the op results vectorially (nested BSL selects), RMWs the
       stencil byte per lane under the operation mask, and only then derives
       the writemask for the store and the rx span. Untextured, textured
       single (all filter classes) and dual; both sshift positions; sten-only
       and depth+stencil (zfn incl never-pass/always-pass);
       ck/atest/blend+wmask/spec-fog/aux and untextured table-fog riders;
       dual pairs re-gated by the measured sten budget rider verify through
       the scalar loop (counted). Phase order keeps earlier dump inputs
       fixed; configurations 58000+. */
    {
        static const size_t   stn_base[]  = { 0, 1, 10, 15 };
        static const uint32_t sdts[12][2] = {
            { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 11, 0 }, { 12, 0 },
            { 14, 0 }, { 4, 0 }, { 6, 0 }, { 1, 0 }, { 2, 0 }, { 15, 0 },
        };
        static const uint32_t bsrcs[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb, 0xc };
        static const uint32_t bdsts[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb };
        int scalar_n = 0;

        for (size_t bi = 0; bi < sizeof(stn_base) / sizeof(stn_base[0]); bi++) {
            for (int cfg = 0; cfg < 24; cfg++) {
                rage128_draw_state_t d;
                int tex  = cfg >= 8;
                int dual = cfg >= 16;
                int rid  = cfg % 8;

                mk_state(&d, &zc[stn_base[bi]]);
                d.z_en = (cfg & 4) ? 1 : 0;
                d.z_wr = (int) (rng() & 1);
                d.zbpp = 4;
                d.zmax = 0xffffff;
                if (cfg & 2) { d.zshift = 0; d.sshift = 24; }
                else         { d.zshift = 8; d.sshift = 0;  }
                d.zfn      = d.z_en ? (rng() % 8) : 0;
                d.sten_on  = 1;
                d.sfn      = rng() % 8;
                d.sfail_op = rng() % 8;
                d.zpass_op = rng() % 8;
                d.zfail_op = rng() % 8;
                d.sref     = rng() & 0xff;
                switch (rng() % 4) {
                    case 0:  d.svmask = 0xff; break;
                    case 1:  d.svmask = 0x00; break;
                    default: d.svmask = rng() & 0xff; break;
                }
                switch (rng() % 4) {
                    case 0:  d.swmask = 0xff; break;
                    case 1:  d.swmask = 0x00; break;
                    default: d.swmask = rng() & 0xff; break;
                }
                if (rid == 1 || rid == 7) {
                    d.atest_en  = 1;
                    d.atest_fn  = 1 + rng() % 7;
                    d.atest_ref = rng() & 0xff;
                }
                if (rid == 2 || rid == 7) {
                    d.alpha_en = 1;
                    d.bsrc     = bsrcs[rng() % 13];
                    d.bdst     = bdsts[rng() % 12];
                    d.bfcn     = rng() % 4;
                    if (rid == 2)
                        d.wmask = (rng() & 1) ? rng()
                                              : ~(1u << (rng() % 32));
                }
                if (rid == 3) {
                    d.spec_en = (int) (rng() & 1);
                    d.fog_en  = d.spec_en ? (int) (rng() & 1) : 1;
                    d.fogr    = (float) (rng() & 0xff) / 255.0f;
                    d.fogg    = (float) (rng() & 0xff) / 255.0f;
                    d.fogb    = (float) (rng() & 0xff) / 255.0f;
                    if (!tex && d.fog_en && d.z_en && (rng() & 1))
                        d.fog_table_en = 1; /* sten + table fog */
                }
                if (rid == 4) {
                    int first = (int) (rng() % 3), nen = 1 + (int) (rng() % (3 - first));

                    d.aux_on = 1;
                    for (int i = first; i < first + nen; i++) {
                        d.aux_cntl |= 1u << (i * 2);
                        if (rng() & 1)
                            d.aux_cntl |= 2u << (i * 2);
                        d.aux_x0[i] = (int32_t) (rng() % 340) - 40;
                        d.aux_x1[i] = d.aux_x0[i]
                                    + (int32_t) (rng() % 220) - 20;
                        d.aux_y0[i] = (int32_t) (rng() % 1140) - 60;
                        d.aux_y1[i] = d.aux_y0[i]
                                    + (int32_t) (rng() % 560) - 60;
                    }
                }
                if (rid == 6)
                    d.wmask = (rng() & 1) ? rng() : ~(1u << (rng() % 32));
                if (tex) {
                    d.tex_en   = 1;
                    d.sec_en   = dual;
                    d.do_persp = (int) (rng() % 2);
                    d.sec_sel  = dual ? (int) (rng() % 2) : 0;
                    d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                    d.premult  = 0;
                    for (int k = 0; k < 4; k++)
                        d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                    for (int st = 0; st < (dual ? 2 : 1); st++) {
                        uint32_t di = rng() % 12;

                        mk_tex_stage(&d, st);
                        d.sh[st].dt   = sdts[di][0];
                        d.sh[st].s3tc = sdts[di][1];
                        switch (rng() % 6) { /* all covered classes */
                            case 0:
                                d.sh[st].minb   = 0;
                                d.sh[st].mag    = 0;
                                d.sh[st].mipdis = (int) (rng() % 2);
                                break;
                            case 1:
                                d.sh[st].minb   = 1;
                                d.sh[st].mag    = 1;
                                d.sh[st].mipdis = (int) (rng() % 2);
                                break;
                            case 2:
                                d.sh[st].minb   = 2 + (int) (rng() % 2);
                                d.sh[st].mag    = d.sh[st].minb & 1;
                                d.sh[st].mipdis = 0;
                                break;
                            case 3:
                                d.sh[st].minb   = 4 + (int) (rng() % 2);
                                d.sh[st].mag    = d.sh[st].minb & 1;
                                d.sh[st].mipdis = 0;
                                break;
                            default:
                                d.sh[st].minb   = rng() % 6;
                                d.sh[st].mag    = (d.sh[st].minb & 1) ^ 1;
                                d.sh[st].mipdis = (int) (rng() % 2);
                                break;
                        }
                    }
                    if (rid == 5) { /* chroma-key rider, any class */
                        if (d.sh[0].dt == 1 || d.sh[0].dt == 2)
                            d.sh[0].dt = 4; /* CI stage-0 keys keep the scalar loop */
                        d.ck3d_on  = (int) (rng() & 1);
                        d.ckc_on   = (!d.ck3d_on || (rng() & 3) == 0) ? 1 : 0;
                        d.ckfn     = (rng() & 1) ? 3 : 2;
                        d.ck3d_msk = g_ckmsks[rng() % N_CKMSKS];
                        d.ck3d_clr = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                        d.ckc_msk  = g_ckmsks[rng() % N_CKMSKS];
                        d.ckc_clr  = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                        d.need_ck  = 1;
                    }
                    d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                              || ((int) (d.sh[0].minb & 1)
                                  != (d.sh[0].mag == 1));
                    d.need_lod2 = dual
                        && ((!d.sh[1].mipdis && d.sh[1].minb >= 2)
                            || ((int) (d.sh[1].minb & 1)
                                != (d.sh[1].mag == 1)));
                    d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                    d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                    d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                    d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                    if (!r128_a64_texinline_can(&d)) {
                        printf("soa-sten bi=%zu cfg=%d: inline gate refused\n",
                               bi, cfg);
                        return 6;
                    }
                    if (!r128_a64_soa_tex_can(&d)) {
                        if (!dual) {
                            printf("soa-sten bi=%zu cfg=%d: SoA gate refused\n",
                                   bi, cfg);
                            return 6;
                        }
                        scalar_n++; /* sten-rider re-gated pair: scalar */
                    }
                    memset(&g_tctx, 0, sizeof(g_tctx));
                    setup_stage_desc(&d, 0, &g_tctx.sd0);
                    if (dual)
                        setup_stage_desc(&d, 1, &g_tctx.sd1);
                } else if (!r128_a64_soa_can(&d)) {
                    printf("soa-sten bi=%zu cfg=%d: SoA gate refused\n",
                           bi, cfg);
                    return 6;
                }
                g_real_tex  = tex;
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, 500,
                               58000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
            printf("soa-sten base %zu (zc[%zu]): 24 configs done\n",
                   bi, stn_base[bi]);
        }
        g_soa_short = 0;
        g_real_tex  = 0;
        printf("soa-sten phase: done (%d dual pairs sten-gated to scalar)\n",
               scalar_n);
    }

    /* Color/depth row-aliasing phase: the interpreter interleaves depth
       and stencil access with each color write. Vector groups must use the
       modeled masked-distance guard to fall back to scalar pixel order
       when rows overlap. g_zov places unstaged depth within +/-1KB of color
       to exercise overlaps directly. Cover 16/32-bit depth, depth writes
       on/off, one/two texture stages, stencil and the other pixel riders.
       This phase follows soa-sten and uses configurations numbered 60000+. */
    {
        static const size_t   zov_base[]  = { 2, 3, 4, 5, 7, 8, 9, 12 };
        static const uint32_t zdts[7]     = { 4, 6, 1, 2, 15, 0, 11 };
        static const uint32_t bsrcs[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb, 0xc };
        static const uint32_t bdsts[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                            0xa, 0xb };
        int scalar_n = 0;

        g_zov = 1;
        for (size_t bi = 0; bi < sizeof(zov_base) / sizeof(zov_base[0]); bi++) {
            for (int cfg = 0; cfg < 18; cfg++) {
                rage128_draw_state_t d;
                int tex  = cfg >= 6;
                int dual = cfg >= 12;
                int rid  = cfg % 6;

                mk_state(&d, &zc[zov_base[bi]]);
                if (rid == 1) {
                    d.atest_en  = 1;
                    d.atest_fn  = 1 + rng() % 7;
                    d.atest_ref = rng() & 0xff;
                }
                if (rid == 2) {
                    d.alpha_en = 1;
                    d.bsrc     = bsrcs[rng() % 13];
                    d.bdst     = bdsts[rng() % 12];
                    d.bfcn     = rng() % 4;
                    if (rng() & 1)
                        d.wmask = (rng() & 1) ? rng()
                                              : ~(1u << (rng() % 32));
                }
                if (rid == 3) {
                    d.spec_en = (int) (rng() & 1);
                    d.fog_en  = d.spec_en ? (int) (rng() & 1) : 1;
                    d.fogr    = (float) (rng() & 0xff) / 255.0f;
                    d.fogg    = (float) (rng() & 0xff) / 255.0f;
                    d.fogb    = (float) (rng() & 0xff) / 255.0f;
                    if (!tex && d.fog_en && (rng() & 1))
                        d.fog_table_en = 1; /* z_en=1 on every base */
                }
                if (rid == 4) {
                    int first = (int) (rng() % 3), nen = 1 + (int) (rng() % (3 - first));

                    d.aux_on = 1;
                    for (int i = first; i < first + nen; i++) {
                        d.aux_cntl |= 1u << (i * 2);
                        if (rng() & 1)
                            d.aux_cntl |= 2u << (i * 2);
                        d.aux_x0[i] = (int32_t) (rng() % 340) - 40;
                        d.aux_x1[i] = d.aux_x0[i]
                                    + (int32_t) (rng() % 220) - 20;
                        d.aux_y0[i] = (int32_t) (rng() % 1140) - 60;
                        d.aux_y1[i] = d.aux_y0[i]
                                    + (int32_t) (rng() % 560) - 60;
                    }
                }
                if (rid == 5) { /* sten rider: D24S8 overrides the base z */
                    d.zbpp     = 4;
                    d.zmax     = 0xffffff;
                    if (rng() & 1) { d.zshift = 0; d.sshift = 24; }
                    else           { d.zshift = 8; d.sshift = 0;  }
                    d.sten_on  = 1;
                    d.sfn      = rng() % 8;
                    d.sfail_op = rng() % 8;
                    d.zpass_op = rng() % 8;
                    d.zfail_op = rng() % 8;
                    d.sref     = rng() & 0xff;
                    d.svmask   = rng() & 0xff;
                    d.swmask   = rng() & 0xff;
                }
                if (tex) {
                    d.tex_en   = 1;
                    d.sec_en   = dual;
                    d.do_persp = (int) (rng() % 2);
                    d.sec_sel  = dual ? (int) (rng() % 2) : 0;
                    d.lod_bias = -(float) (int8_t) (rng() & 0xff) / 128.0f;
                    d.premult  = 0;
                    for (int k = 0; k < 4; k++)
                        d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                    for (int st = 0; st < (dual ? 2 : 1); st++) {
                        mk_tex_stage(&d, st);
                        d.sh[st].dt   = zdts[rng() % 7];
                        d.sh[st].s3tc = (d.sh[st].dt == 0) ? rng() % 4 : 0;
                        switch (rng() % 5) { /* all covered classes */
                            case 0:
                                d.sh[st].minb   = 0;
                                d.sh[st].mag    = 0;
                                d.sh[st].mipdis = (int) (rng() % 2);
                                break;
                            case 1:
                                d.sh[st].minb   = 1;
                                d.sh[st].mag    = 1;
                                d.sh[st].mipdis = (int) (rng() % 2);
                                break;
                            case 2:
                                d.sh[st].minb   = 2 + (int) (rng() % 2);
                                d.sh[st].mag    = d.sh[st].minb & 1;
                                d.sh[st].mipdis = 0;
                                break;
                            case 3:
                                d.sh[st].minb   = 4 + (int) (rng() % 2);
                                d.sh[st].mag    = d.sh[st].minb & 1;
                                d.sh[st].mipdis = 0;
                                break;
                            default:
                                d.sh[st].minb   = rng() % 6;
                                d.sh[st].mag    = (d.sh[st].minb & 1) ^ 1;
                                d.sh[st].mipdis = (int) (rng() % 2);
                                break;
                        }
                    }
                    d.need_lod = (!d.sh[0].mipdis && d.sh[0].minb >= 2)
                              || ((int) (d.sh[0].minb & 1)
                                  != (d.sh[0].mag == 1));
                    d.need_lod2 = dual
                        && ((!d.sh[1].mipdis && d.sh[1].minb >= 2)
                            || ((int) (d.sh[1].minb & 1)
                                != (d.sh[1].mag == 1)));
                    d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                    d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                    d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                    d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                    if (!r128_a64_texinline_can(&d)) {
                        printf("soa-zov bi=%zu cfg=%d: inline gate refused\n",
                               bi, cfg);
                        return 6;
                    }
                    if (!r128_a64_soa_tex_can(&d)) {
                        if (!dual) {
                            printf("soa-zov bi=%zu cfg=%d: SoA gate refused\n",
                                   bi, cfg);
                            return 6;
                        }
                        scalar_n++; /* weight-gated pair: scalar loop */
                    }
                    memset(&g_tctx, 0, sizeof(g_tctx));
                    setup_stage_desc(&d, 0, &g_tctx.sd0);
                    if (dual)
                        setup_stage_desc(&d, 1, &g_tctx.sd1);
                } else if (!r128_a64_soa_can(&d)) {
                    printf("soa-zov bi=%zu cfg=%d: SoA gate refused\n",
                           bi, cfg);
                    return 6;
                }
                g_real_tex  = tex;
                g_soa_short = (cfg & 1);
                if (run_config(code, &d, 500,
                               60000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
            printf("soa-zov base %zu (zc[%zu]): 18 configs done\n",
                   bi, zov_base[bi]);
        }
        g_zov       = 0;
        g_soa_short = 0;
        g_real_tex  = 0;
        printf("soa-zov phase: done (%d dual pairs weight-gated to scalar)\n",
               scalar_n);
    }

    /* Destination formats cross the full base state matrix so Z,
       helper and inline textures, alpha tests, blend and fog keep their
       reference coverage at 16 bpp. Stencil crosses both byte positions
       using a 32-bit Z cell. Short rows exercise partial groups,
       while masks select alpha, RGB, zero and arbitrary packed planes. */
    {
        static const uint32_t dst_dt[] = { 3, 15 };
        static const uint32_t masks[] = {
            0xffff, 0, 0x8000, 0x7fff, 0xf000, 0x0fff, 0x1357, 0xa5a55a5a
        };

        for (size_t di = 0; di < sizeof(dst_dt) / sizeof(dst_dt[0]); di++)
            for (size_t bi = 0; bi < sizeof(zc) / sizeof(zc[0]); bi++)
                for (int cfg = 0; cfg < 16; cfg++) {
                    rage128_draw_state_t d;
                    case_ent ce = zc[bi];

                    ce.dt = dst_dt[di];
                    ce.dith = cfg & 1;
                    mk_state(&d, &ce);
                    d.wmask = masks[cfg & 7];
                    /* Cross alpha-bearing packing with the scalar fog DDA,
                       including stencil-only cells selected below. */
                    if (d.fog_en && (cfg & 2)) {
                        d.fog_table_en = 1;
                        d.z_en = 0;
                        d.z_wr = 0;
                    }
                    if (d.alpha_en && cfg >= 4) {
                        d.bsrc = rng() & 15;
                        d.bdst = rng() & 15;
                        d.bfcn = cfg & 3;
                    }
                    g_real_tex = !!(cfg & 2);
                    g_soa_short = !!(cfg & 4);
                    if (cfg & 8) {
                        d.sten_on = 1;
                        d.zbpp = 4;
                        d.zmax = 0xffffff;
                        d.zshift = (cfg & 2) ? 8 : 0;
                        d.sshift = (cfg & 2) ? 0 : 24;
                        d.sfn = rng() & 7;
                        d.sfail_op = rng() & 7;
                        d.zfail_op = rng() & 7;
                        d.zpass_op = rng() & 7;
                        d.sref = rng() & 0xff;
                        d.svmask = rng() & 0xff;
                        d.swmask = rng() & 0xff;
                    }
                    if (g_real_tex) {
                        d.do_persp = cfg & 1;
                        d.sec_sel = !!(cfg & 4);
                        d.lod_bias = frand(-1.0f, 1.0f);
                        for (int k = 0; k < 4; k++)
                            d.cc[k] = (float) (rng() & 0xff) / 255.0f;
                        if (d.tex_en)
                            mk_tex_stage(&d, 0);
                        if (d.sec_en)
                            mk_tex_stage(&d, 1);
                        d.need_lod = d.tex_en
                            && ((!d.sh[0].mipdis && d.sh[0].minb >= 2)
                                || ((int) (d.sh[0].minb & 1) != (d.sh[0].mag == 1)));
                        d.need_lod2 = d.sec_en
                            && ((!d.sh[1].mipdis && d.sh[1].minb >= 2)
                                || ((int) (d.sh[1].minb & 1) != (d.sh[1].mag == 1)));
                        d.texw0 = (float) (1u << (d.sh[0].tsp & 0xf));
                        d.texh0 = (float) (1u << ((d.sh[0].tsp >> 8) & 0xf));
                        d.texw1 = (float) (1u << (d.sh[1].tsp & 0xf));
                        d.texh1 = (float) (1u << ((d.sh[1].tsp >> 8) & 0xf));
                        if (d.tex_en && (cfg & 4)) {
                            d.need_ck = d.ck3d_on = 1;
                            d.ckfn = 3;
                            d.ck3d_msk = 0xffffffffu;
                            d.ck3d_clr = pick_key(d.sh[0].dt, d.sh[0].s3tc);
                        }
                        memset(&g_tctx, 0, sizeof(g_tctx));
                        if (d.tex_en)
                            setup_stage_desc(&d, 0, &g_tctx.sd0);
                        if (d.sec_en)
                            setup_stage_desc(&d, 1, &g_tctx.sd1);
                    }
                    int want_soa = !d.tex_en && !d.sec_en && !d.alpha_en
                        && !d.spec_en && !d.fog_en;
                    if (!r128_jit_arm64_can(&d) || !r128_jit_x64_can(&d)
                        || r128_a64_soa_can(&d) != want_soa
                        || r128_jit_x64_soa_can(&d) != want_soa
                        || r128_a64_soa_tex_can(&d) || r128_jit_x64_soa_tex_can(&d))
                        return 4;
                    if (run_config(code, &d, dump ? 0 : 400,
                                   80000 + di * 1000 + bi * 16 + (size_t) cfg) < 0)
                        return 3;
                }
        g_real_tex = 0;
        g_soa_short = 0;
        printf("alpha destination matrix: done\n");
    }

    /* Stipple shares a compiled state across changing triangle patterns.
       Real texture contexts expose the captured rows to both backends;
       aux, dither, depth, stencil and helper-call riders test discard order
       and preservation of the row word across pixel shading. */
    {
        static const size_t stip_base[] = { 0, 2, 3, 11, 18, 27, 40, 47 };

        g_real_tex = 1;
        for (uint32_t k = 0; k < TEXA_SZ; k += 4)
            *(uint32_t *) &texarena[k] = rng();
        for (size_t bi = 0; bi < sizeof(stip_base) / sizeof(stip_base[0]); bi++) {
            for (int cfg = 0; cfg < 27; cfg++) {
                rage128_draw_state_t d;
                int mode = cfg % 9;

                mk_state(&d, &zc[stip_base[bi]]);
                d.stip_en = 1;
                if (cfg >= 9) {
                    d.dst_dt = cfg < 18 ? 3 : 15;
                    d.bpp = 2;
                    d.dither = cfg & 1;
                }
                d.tex_en = mode == 1 || mode == 2 || mode == 3 || mode == 5;
                d.sec_en = mode == 2;
                d.do_persp = rng() & 1;
                d.aux_on = 1;
                d.aux_cntl = 1u | (rng() & 1 ? 2u : 0u);
                d.aux_x0[0] = (int32_t) (rng() % 128);
                d.aux_x1[0] = d.aux_x0[0] + 96;
                d.aux_y0[0] = 0;
                d.aux_y1[0] = 1023;
                if (mode >= 3) {
                    d.z_en = mode != 4;
                    d.z_wr = d.z_en;
                    d.zbpp = 4;
                    d.zmax = 0xffffff;
                    d.zshift = 0;
                    if (mode == 3) {
                        d.fog_en = 1;
                        d.fog_table_en = 1;
                    } else {
                        d.sten_on = 1;
                        d.sshift = 24;
                        d.sfn = rng() % 8;
                        d.sfail_op = rng() % 8;
                        d.zfail_op = rng() % 8;
                        d.zpass_op = rng() % 8;
                        d.sref = rng() & 0xff;
                        d.svmask = rng() & 0xff;
                        d.swmask = rng() & 0xff;
                    }
                }
                /* Z-off fog advances through stipple drops, including a
                   secondary-only helper and a stencil-only depth cell. */
                if (mode >= 6) {
                    d.z_en = d.z_wr = 0;
                    d.fog_en = d.fog_table_en = 1;
                    d.sten_on = mode == 8;
                    d.tex_en = 0;
                    d.sec_en = mode == 7;
                }
                g_soa_short = cfg & 1;
                if (!r128_jit_arm64_can(&d) || !r128_jit_x64_can(&d)
                    || r128_a64_soa_can(&d) || r128_jit_x64_soa_can(&d)
                    || r128_a64_soa_tex_can(&d) || r128_jit_x64_soa_tex_can(&d))
                    return 6;
                memset(&g_tctx, 0, sizeof(g_tctx));
                for (int st = 0; st < 2; st++) {
                    d.sh[st].dt = st ? 6 : 4;
                    d.sh[st].tsp = 4u | (4u << 8);
                    d.sh[st].minb = 1;
                    d.sh[st].mag = 1;
                    d.sh[st].mipdis = 1;
                    setup_stage_desc(&d, st, st ? &g_tctx.sd1 : &g_tctx.sd0);
                }
                d.texw0 = d.texh0 = d.texw1 = d.texh1 = 16.0f;
                if (run_config(code, &d, dump ? 0 : 400,
                               70000 + bi * 100 + (size_t) cfg) < 0)
                    return 3;
            }
        }
        g_real_tex = 0;
        g_soa_short = 0;
        printf("stipple phase: 216 configs done\n");
    }

#if !JHT_EXEC_A64
    printf("REF-ONLY rows=%llu ck=%016llx (uncovered states; exec-compared "
           "rows=%llu)\n",
           (unsigned long long) rows_refonly, (unsigned long long) ref_ck,
           (unsigned long long) (total_rows - rows_refonly));
#endif
    printf("rows=%llu fail=%llu\n", (unsigned long long) total_rows,
           (unsigned long long) total_fail);
    return total_fail ? 1 : 0;
}
