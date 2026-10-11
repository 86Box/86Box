/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- GPU backend test: the production span
 *          kernels against the C reference pixel loop.
 *
 *          The tool drives the production SPIR-V span-shading kernels
 *          (the twenty variants, each called a kernel here as in the
 *          device's kernel ids) and the depth pre-pass, the bytes in
 *          src/include/86box/vid_ati_rage128_gpu_spv.h that the emulator
 *          ships, through the device's row, order, span and triangle
 *          records, and compares their output byte for byte against the
 *          C reference from the JIT harness. The header's 2D
 *          read-modify-write and overlay kernels are not run here; the
 *          register harness's GPU verify lane compares them with the CPU
 *          path.
 *
 *          The reference pixel loop and texture sampler chain mirror
 *          ../jit-harness/jit_host_test.c: blend, auxiliary scissors,
 *          clamp modes, every texel datatype the interpreter decodes,
 *          both texture stages and the combine modes. States in the
 *          inline texture family call the reference texture stage
 *          directly. Compile with -ffp-contract=off so each multiply and
 *          add rounds separately. On macOS the program turns MoltenVK
 *          fast math off unless the environment supplies a value; fast
 *          math must stay off to preserve those rounding steps.
 *
 *          The default gate runs the software binary64 self-test,
 *          per-state fuzz for every production kernel, and mixed-kernel
 *          segment fuzz. A per-state segment holds 1 to 3 draws with
 *          randomized run-time selectors and spans chunked at 256
 *          pixels. Fault injection corrupts the spans' destination row
 *          addresses and must produce a byte mismatch on every kernel.
 *          Mixed segments exercise run splits, barrier elision, the row
 *          counting sort, draw order, and the modeled alias and hazard
 *          acceptance rules against a sequential reference replay. A draw
 *          that aliases itself runs serially; a draw with a color or
 *          depth hazard against the draws already in the segment is
 *          rejected from it.
 *
 *          Normal textures occupy [0, 512K), color rows start at 1M and
 *          depth rows at 2M. Mask-edge reads run from the last VRAM byte
 *          into four guard bytes past the end: a cell's base address
 *          wraps, the bytes after it stay linear. The whole VRAM image
 *          and the guard bytes are randomized and compared.
 *
 *          Usage: gputri [iters-per-state] [seed]. The default is 150
 *          iterations; an omitted or zero seed runs the three standard
 *          seeds in sequence.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

/* Windows has no sysconf. Query the processor count through the system
   API so pipeline priming uses the same worker-count rule on every host. */
#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
/* windows.h defines near and far as pointer qualifiers; undefine them
   so local variables such as rr_tex_level's near remain valid. */
#    undef near
#    undef far
#    define _SC_NPROCESSORS_ONLN 0
static long
sysconf(int name)
{
    SYSTEM_INFO si;

    (void) name;
    GetSystemInfo(&si);
    return (long) si.dwNumberOfProcessors;
}
#endif

/* A positive GPUTRI_THREADS value sets the pipeline-priming worker
   limit; otherwise use the processor count minus two. Limiting parallel
   compilation bounds the driver's concurrent scratch-memory demand. */
static long
gputri_prime_threads(void)
{
    const char *e = getenv("GPUTRI_THREADS");
    long        n;

    if (e && e[0] && (n = strtol(e, NULL, 10)) > 0)
        return n;
    return sysconf(_SC_NPROCESSORS_ONLN) - 2;
}

#define VK_CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { \
    fprintf(stderr, "VK_CHECK failed %s:%d: %s = %d\n", __FILE__, __LINE__, #x, r_); \
    exit(1); } } while (0)

/* Exit status for a host with no Vulkan driver or device: nothing was
   tested, which is not a failure. CTest reports it as a skip. */
#define GPUTRI_EXIT_SKIP 77

#define VRAM_SZ   (4u << 20)
#define VRAM_MASK (VRAM_SZ - 1)
#define STAGE_SZ  (1u << 20)     /* staged-texture arena (binding 7)   */
#define CZSTG_SZ  (1u << 19)     /* staged c/z arenas (bindings 9-12); covers
                                    256 rows x ROW_PITCH */
#define TEX_ZONE  (512u << 10)   /* texture bases (normal placement)   */
#define C_BASE    (1u << 20)     /* color rows                         */
#define Z_BASE    (2u << 20)     /* z rows                             */
#define ROW_PITCH 2048u          /* bytes per row, both surfaces       */
/* MAX_PY gives timing legs enough rows to vary workgroup occupancy.
   FUZZ_PY keeps fuzz draws dense enough to exercise multiple spans per row. */
#define MAX_PY    480
#define FUZZ_PY   48

/* ---- structs from the tree (types only; layouts must match) ---- */
typedef struct r3d_comb_desc_t {
    uint32_t comb, fmsb, cfac, ifac;
    uint32_t comba, afac, ifaca;
} r3d_comb_desc_t;

typedef struct r3d_stage_hdr_t {
    uint32_t tsp;
    uint32_t clamp_s, clamp_t;
    uint32_t dt, s3tc;
    uint32_t aone; /* texel alpha reads as 0xff (TEX_MAP_AEN clear) */
    uint32_t border;
    uint32_t minb, mag;
    int      mipdis, top;
} r3d_stage_hdr_t;

typedef struct rage128_draw_state_t {
    int      draw_ok;
    uint32_t dst_dt;
    int      bpp;
    uint32_t wmask;
    int      dither;
    int      stip_en;
    int      aux_on;
    uint32_t aux_cntl;
    int32_t  aux_x0[3], aux_x1[3], aux_y0[3], aux_y1[3];
    int      sx0, sy0, sx1, sy1;
    int      sub;
    float    subf;
    int      rnd;
    int32_t  slim;
    int32_t  woxi, woyi;
    int      z_en, z_wr;
    uint32_t zfn;
    int      zbpp;
    uint32_t zmax;
    int      zshift;
    uint32_t zrowpx;
    int      sten_on;
    uint32_t sfn, sfail_op, zpass_op, zfail_op;
    uint32_t sref, svmask, swmask;
    int      sshift;
    int      flat_on;
    int      flat_src;
    int      tex_en, sec_en;
    int      premult, do_persp;
    int      need_lod, need_lod2;
    float    lod_bias;
    float    texw0, texh0, texw1, texh1;
    int      sec_sel;
    r3d_stage_hdr_t sh[2];
    r3d_comb_desc_t comb[2];
    int      need_ck, ck3d_on, ckc_on;
    uint32_t ckfn;
    uint32_t ck3d_clr, ck3d_msk, ckc_clr, ckc_msk;
    float    cc[4];
    int      spec_en;
    int      fog_en, fog_table_en;
    float    fogr, fogg, fogb;
    int      atest_en;
    uint32_t atest_fn, atest_ref;
    int      alpha_en;
    uint32_t bsrc, bdst, bfcn;
    /* texture lighting: a third combine pass after the stages, its texel
       the stage output and its input the iterated color */
    r3d_comb_desc_t lcomb;
    uint8_t         light_on;
} rage128_draw_state_t;

typedef struct r128_jit_tri_t {
    uint8_t *vram;
    uint8_t *zptr;
    uint8_t *cptr;
    uint32_t vram_mask;
    uint32_t z_base, z_lim;
    uint32_t c_base, c_lim;
    int32_t  x0, x1;
    int64_t  e0dxi, e1dxi, e2dxi;
    float    invs;
    double   dZdx;
    float    vca[4], vcb[4], vcc[4];
    void    *texctx;
    float    fog[4];
    float    spa[4], spb[4], spc[4];
    const uint8_t *fog_table;
} r128_jit_tri_t;

typedef struct r3d_stage_desc_t {
    uint32_t tsp;
    uint32_t clamp_s, clamp_t, dt, s3tc;
    uint32_t amask; /* 0xff000000 when the stage's texel alpha reads as 1 */
    uint32_t border;
    uint32_t minb, mag;
    int      mipdis, top;
    const uint32_t *pal;
    uint16_t slot_valid;
    struct r3d_slot_desc_t {
        uint32_t       lw, lh;
        const uint8_t *texbase;
        uint32_t       base, mask;
    } slot[11];
} r3d_stage_desc_t;

typedef struct r3d_texctx_t {
    float sta, stb, stc, tta, ttb, ttc;
    float s2a, s2b, s2c, t2a, t2b, t2c;
    float arhw, brhw, crhw;
    float dSdx, dSdy, dTdx, dTdy;
    float dWdx, dWdy;
    float dS2dx, dS2dy, dT2dx, dT2dy;
    r3d_stage_desc_t sd0, sd1;
} r3d_texctx_t;

/* ---- GPU-side records: must match vid_ati_rage128_gpu.c and the
   std430 blocks in vid_ati_rage128_gpu_seg.comp ---- */
typedef struct seg_span_t {
    int64_t  e0, e1, e2;
    uint64_t zline;
    int32_t  x0, x1, py, tri;
    uint32_t drow, zrow, px_base, pad1;
} seg_span_t;

typedef struct seg_tri_t {
    int64_t  e0dxi, e1dxi, e2dxi;
    uint64_t dZdx;
    float    invs, lod_bias;
    float    texw0, texh0, texw1, texh1;
    uint32_t zfn, z_wr;
    uint32_t atest_en, atest_fn, atest_ref;
    uint32_t bsrc, bdst, bfcn;
    uint32_t aux_cntl;
    int32_t  aux_x0[3], aux_x1[3], aux_y0[3], aux_y1[3];
    float    cc[4];
    float    vca[4], vcb[4], vcc[4];
    float    sta, stb, stc, tta, ttb, ttc;
    float    s2a, s2b, s2c, t2a, t2b, t2c;
    float    arhw, brhw, crhw;
    float    dSdx, dSdy, dTdx, dTdy, dWdx, dWdy;
    float    dS2dx, dS2dy, dT2dx, dT2dy;
    /* the secondary stage's own W per vertex and its screen gradients,
       its perspective enable, and whether that W is the vertex rhw2
       (SEC_SRC_SEL_W) */
    float    a2rhw, b2rhw, c2rhw, dW2dx, dW2dy;
    uint32_t persp2, sel_w, stip_en;
    uint32_t sec_sel, need_lod2;
    int32_t  top0, top1;
    uint32_t st_cfg[16];
    uint32_t comb_cfg[16];
    uint32_t slot0[44];
    uint32_t slot1[44];
    uint32_t dst_dt, wmask, dither, persp;
    uint32_t zbpp, zshift, sten_ctl, sten_rm;
    float    spa[3], spb[3], spc[3];
    uint32_t spec_en;
    float    fga, fgb, fgc;
    float    fogr, fogg, fogb;
    uint32_t fog_en, ftab_en;
    uint32_t ck3d_clr, ck3d_msk, ckc_clr, ckc_msk;
    uint32_t ck_ctl, pal_base;
    uint32_t fog_table[64];
    uint32_t stipple[32];
} seg_tri_t;

_Static_assert(sizeof(seg_span_t) == 64, "span layout drift");
_Static_assert(sizeof(seg_tri_t) == 1344, "tri layout drift");
_Static_assert(offsetof(seg_tri_t, stip_en) == 332, "tri layout drift");
_Static_assert(offsetof(seg_tri_t, stipple) == 1216, "tri layout drift");
_Static_assert(offsetof(seg_tri_t, cc) == 140, "tri layout drift");
_Static_assert(offsetof(seg_tri_t, st_cfg) == 352, "tri layout drift");
_Static_assert(offsetof(seg_tri_t, slot0) == 480, "tri layout drift");

#include <86box/vid_ati_rage128_gpu_spv.h>
/* the device's own combine-tuple enumeration, so plbench builds exactly
   the boot precompile's job list */
#include <86box/vid_ati_rage128_gpu_comb.h>

#define N_KERNELS (int) (sizeof(r128_gpu_variants) / sizeof(r128_gpu_variants[0]))

/* ------------------------------------------------------------------------
 * C reference -- mirrors ../jit-harness/jit_host_test.c
 * (rr_* sampler chain + ref_* pixel loop).
 * ---------------------------------------------------------------------- */

static uint32_t g_texpals[4][256]; /* CI4/CI8 palette snapshots; each state
   picks one, mirrored verbatim in the binding-8 arena at idx*256 words */

/* GPUTRI_PIXDUMP=<vram byte address>: trace the reference's sampler and
   pre-quantization color at that one destination pixel. For diagnosing a
   1-LSB divergence against the GPU lane -- how close the reference lands to
   each quantization step says whether a legal ULP difference could flip it.
   Diagnostic only; unset (the default) is a no-op on every host. */
static uint32_t     g_pixdump[8]; /* comma-separated list; matched against the
   whole destination cell, so a differing byte address can be pasted straight
   from a mismatch dump without working out the cell base or bpp */
static int          g_pixdump_n;
static __thread int g_pd_active; /* this pixel is a traced one */

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

/* the saturated coordinate's successor, as r3d_tex_wrap_next: a
   bounded representative keeps its low bits for wrap and mirror and
   its upper-edge result for clamp and border without signed overflow */
static int
rr_tex_wrap_next(int c, int n, uint32_t mode)
{
    int next = c == INT_MAX ? ((mode & 2) ? n : 0) : c + 1;

    return rr_tex_wrap(next, n, mode);
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

/* verbatim mirror of r3d_f2i / r3d_f2u: truncate, saturate, NaN gives 0
   (the arm64 conversion; a plain cast is undefined out of range and
   differs on x86-64) */
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
        case 0: { /* S3TC decode mirrors r3d_texel. */
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
                    return amask; /* DXT1 3-color mode: black, with selected alpha */
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
                 | ((texel & 0x7c00) << 9) | ((texel & 0x03e0) << 6) | ((texel & 0x001f) << 3)
                 | amask;
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
                                  (int) ((texel >> 8) & 0xff), (int) (texel & 0xff))
                 | amask;
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
        if (g_pd_active)
            printf("  TEX slot=%d lw=%u lh=%u dt=%u linear=%d s=%a t=%a\n"
                   "    nearest: su=%a tv=%a u=%d v=%d texel=%08x\n",
                   cslot, lw, lh, d->dt, linear, s, t,
                   (double) (s * (float) lw), (double) (t * (float) lh),
                   u, v, near);
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
        {
            uint32_t bl = rr_lerp_packed(rr_lerp_packed(c[0], c[1], wu),
                                         rr_lerp_packed(c[2], c[3], wu), wv);

            /* wu/wv are the amplifier to watch: they quantize the fractional
               texcoord to 1/256, so a ULP-level difference in fu/fv that
               straddles a step flips the weight by one and moves the result
               by (texel delta)/256 -- far more than a ULP of color. */
            if (g_pd_active) {
                /* the first quantizer: texcoord_fx roundEven()s this to an
                   integer. How far it sits from .5 is the ULP headroom the
                   perspective divide has before the whole chain steps. */
                float qs = s * (float) lw * 4096.0f;
                float qt = t * (float) lh * 4096.0f;

                printf("    bilin: s=%a t=%a lw=%u lh=%u\n"
                       "      q: s*lw*4096=%.9g (%.9g from .5)"
                       "  t*lh*4096=%.9g (%.9g from .5)\n"
                       "      fu=%a fv=%a u0=%d v0=%d wu=%u wv=%u\n"
                       "      frac_u=%.9g frac_v=%.9g (x256 pre-round"
                       " %.9g %.9g)\n"
                       "      c00=%08x c10=%08x c01=%08x c11=%08x -> %08x\n",
                       s, t, lw, lh,
                       (double) qs,
                       (double) fabsf(qs - floorf(qs) - 0.5f),
                       (double) qt,
                       (double) fabsf(qt - floorf(qt) - 0.5f),
                       fu, fv, u0, v0, wu, wv,
                       (double) (fu - (float) u0), (double) (fv - (float) v0),
                       (double) ((fu - (float) u0) * 256.0f + 0.5f),
                       (double) ((fv - (float) v0) * 256.0f + 0.5f),
                       c[0], c[1], c[2], c[3], bl);
            }
            return bl;
        }
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

/* combine -- verbatim mirror of r3d_tex_combine (full code book), which
   takes the texel as floats: a stage passes its sampled texel scaled from
   8 bits (rr_tex_combine below), the lighting pass the unquantized stage
   output */
static void
rr_tex_combine_f(const r3d_comb_desc_t *cd, float col[4], float tr, float tg,
                 float tb, float ta, const float int_color[4], const float cc[4],
                 int first)
{
    uint32_t   comb  = cd->comb;
    uint32_t   fmsb  = cd->fmsb;
    uint32_t   cfac  = cd->cfac;
    uint32_t   ifac  = cd->ifac;
    uint32_t   comba = cd->comba;
    uint32_t   afac  = cd->afac;
    uint32_t   ifaca = cd->ifaca;
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
        case 0: /* disable outputs the texture color itself; copy (1)
                   outputs the COLOR_FACTOR operand, which is the texture
                   color only while cfac selects it */
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

/* a stage's combine, with its sampled ARGB8888 texel */
static void
rr_tex_combine(const r3d_comb_desc_t *cd, float col[4], uint32_t tx,
               const float int_color[4], const float cc[4], int first)
{
    rr_tex_combine_f(cd, col, ((tx >> 16) & 0xff) / 255.0f, ((tx >> 8) & 0xff) / 255.0f,
                     (tx & 0xff) / 255.0f, (tx >> 24) / 255.0f, int_color, cc, first);
}

/* Stage-0 chroma key coverage: color-compare evaluations by ckfn
   polarity, kills per key path, and fragments that pass both paths. */
static uint64_t ck_fn_px[2], ck3d_kill_px, ckc_kill_px, ck_pass_px;

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

            ck_fn_px[ds->ckfn == 3 ? 1 : 0]++;
            if (ds->ckfn == 3 ? eq : !eq) {
                ck3d_kill_px++;
                return 0;
            }
        }
        if (ds->ckc_on) {
            if ((tnear & ds->ckc_msk) == (ds->ckc_clr & ds->ckc_msk)) {
                ckc_kill_px++;
                return 0;
            }
        }
        if (ds->need_ck)
            ck_pass_px++;
        rr_tex_combine(&ds->comb[0], col, tx, int_color, ds->cc, 1);
    }
    if (ds->sec_en) {
        int      sel  = ds->sec_sel;
        float    sp2  = sel ? (w0 * tc->s2a + w1 * tc->s2b + w2 * tc->s2c)
                            : (w0 * tc->sta + w1 * tc->stb + w2 * tc->stc);
        float    tp2  = sel ? (w0 * tc->t2a + w1 * tc->t2b + w2 * tc->t2c)
                            : (w0 * tc->tta + w1 * tc->ttb + w2 * tc->ttc);
        float    s    = sp2 * ir;
        float    t    = tp2 * ir;
        float    lod2 = 0.0f;
        uint32_t tx;

        if (ds->need_lod2) {
            float gsx = sel ? tc->dS2dx : tc->dSdx, gsy = sel ? tc->dS2dy : tc->dSdy;
            float gtx = sel ? tc->dT2dx : tc->dTdx, gty = sel ? tc->dT2dy : tc->dTdy;
            float dsx, dsy, dtx, dty, ax2, ay2, rho2;

            if (ds->do_persp) {
                float wp  = rhw;
                float iw2 = (wp != 0.0f) ? 1.0f / (wp * wp) : 0.0f;

                dsx = (gsx * wp - sp2 * tc->dWdx) * iw2;
                dsy = (gsy * wp - sp2 * tc->dWdy) * iw2;
                dtx = (gtx * wp - tp2 * tc->dWdx) * iw2;
                dty = (gty * wp - tp2 * tc->dWdy) * iw2;
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
    /* texture lighting, as the interpreter runs it: the stage chain's
       output is this pass's texel and the iterated color its input */
    if (ds->light_on)
        rr_tex_combine_f(&ds->lcomb, col, col[0], col[1], col[2], col[3],
                         int_color, ds->cc, 0);
    return 1;
}

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

    switch (dt) {
        case 3: /* ARGB1555 */
            v = *(const uint16_t *) p;
            return ((v & 0x8000) ? 0xff000000 : 0)
                 | ((v & 0x7c00) << 9) | ((v & 0x03e0) << 6) | ((v & 0x001f) << 3);
        case 4: /* RGB565 */
            v = *(const uint16_t *) p;
            return 0xff000000 | ((v & 0xf800) << 8) | ((v & 0x07e0) << 5)
                 | ((v & 0x001f) << 3);
        case 15: /* ARGB4444 */
            v = *(const uint16_t *) p;
            return ((v & 0xf000) << 16) | ((v & 0x0f00) << 12)
                 | ((v & 0x00f0) << 8) | ((v & 0x000f) << 4);
        default: /* 6 = ARGB8888 */
            return *(const uint32_t *) p;
    }
}

/* Destination-surface coverage, counted at the write, not at
   state construction: a format whose draws all got scissored or
   alpha-killed would otherwise look covered. */
static uint64_t dst_fmt_px[16], dst_undith_px, dst_masked_px;
static uint64_t tex_fmt_use[16]; /* accepted draws per stage texture dt */
static uint64_t tex_persp_use[2]; /* accepted textured draws: affine, perspective */
static uint64_t tex_seconly_use;
static uint64_t tex_aone_use;  /* accepted stage uses with the texel alpha forced to 1 */
static uint64_t tex_light_use; /* accepted draws with texture lighting on */
static uint64_t dst_cdead_px; /* wmask 0 + atest off: kernel dead-color path */
/* depth cell coverage: [0] u16, [1] 4-byte depth-low (zshift 0),
   [2] 4-byte depth-high (zshift 8). A green gate that never ran a wide
   cell would prove nothing about it. */
static uint64_t z_cell_px[3];
/* straddling-cell coverage (odd dst/z offset), counted at the resident
   cell resolve, attributed per axis: [0] 16bpp, [1] 32bpp misaligned;
   both = color and z of one pixel misaligned; edge = the cell's base
   is within bpp-1 bytes of the vram mask, so its tail spills past it */
static uint64_t odd_c_px[2], odd_z_px[2], odd_both_px;
static uint64_t edge_c_px, edge_z_px, edge_faults;
/* stencil coverage, counted where the reference actually runs
   the path: byte position tested, update op applied, which outcome
   picked the op (sfail/zfail/zpass), stencil-only draws (Z test off),
   and alpha-killed fragments under STENCIL_EN (the ordering trap: those
   must not touch the byte -- the byte-diff proves they did not, this
   counter proves the scenario occurred). */
static uint64_t sten_px[2];      /* [0] low byte ([7:0]), [1] high ([31:24]) */
static uint64_t sten_op_px[8];
static uint64_t sten_path_px[3]; /* [0] sfail, [1] zfail, [2] zpass */
static uint64_t sten_zoff_px, sten_akill_px;
/* specular coverage: pixels through the add, and pixels the saturate
   actually clamped (the clamp must be exercised, not just reachable) */
static uint64_t spec_px, spec_clamp_px;
/* vertex-fog coverage: pixels fogged, and pixels whose interpolated
   factor left [0,1] on either side (both clamp arms must fire) */
static uint64_t fogv_px, fogv_clamp_lo_px, fogv_clamp_hi_px, fogt_px;

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
        case 0xa:
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
         uint32_t drow, uint32_t zrow, int32_t py, const uint32_t stipple[32])
{
    int32_t rx0 = -1, rx1 = -1;

    for (int32_t px = t->x0; px <= t->x1;
         px++, e0 += t->e0dxi, e1 += t->e1dxi, e2 += t->e2dxi, zline += t->dZdx) {
        if (e0 < 0 || e1 < 0 || e2 < 0)
            continue;
        if (d->aux_on && !ref_aux_pass(d, px, py))
            continue;
        if (d->stip_en && !((stipple[py & 31] >> (31 - (px & 31))) & 1))
            continue;

        float w0 = (float) e0 * t->invs;
        float w1 = (float) e1 * t->invs;
        float w2 = (float) e2 * t->invs;

        double zc = zline;
        if (!(zc > 0.0)) zc = 0.0;
        if (zc > 1.0) zc = 1.0;

        uint32_t zi = 0;
        uint8_t *zcell = NULL;
        int      zres = 1, sres = 1, z_mis = 0;
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
            } else {
                zcell = &t->vram[zaddr & t->vram_mask];
                if (zaddr & (uint32_t) (d->zbpp - 1)) {
                    z_mis = 1;
                    odd_z_px[d->zbpp == 4]++;
                }
                if ((zaddr & t->vram_mask)
                    > t->vram_mask + 1u - (uint32_t) d->zbpp)
                    edge_z_px++;
            }
            uint32_t zbuf;
            if (d->zbpp == 2) {
                zbuf = *(uint16_t *) zcell & d->zmax;
                z_cell_px[0]++;
            } else {
                zbuf = (*(uint32_t *) zcell >> d->zshift) & d->zmax;
                z_cell_px[d->zshift ? 2 : 1]++;
            }
            if (d->z_en)
                zres = ref_cmp(d->zfn, zi, zbuf);
            if (d->sten_on) {
                sbuf = (*(uint32_t *) zcell >> d->sshift) & 0xff;
                sres = ref_cmp(d->sfn, d->sref & d->svmask, sbuf & d->svmask);
                sten_px[d->sshift ? 1 : 0]++;
                if (!d->z_en)
                    sten_zoff_px++;
            } else if (!zres)
                continue;
        }

        g_pd_active = 0;
        for (int i = 0; i < g_pixdump_n; i++) {
            uint32_t cell = drow + (uint32_t) px * d->bpp;

            if (g_pixdump[i] >= cell && g_pixdump[i] < cell + (uint32_t) d->bpp) {
                g_pd_active = 1;
                break;
            }
        }

        float col[4];
        col[0] = w0 * t->vca[0] + w1 * t->vcb[0] + w2 * t->vcc[0];
        col[1] = w0 * t->vca[1] + w1 * t->vcb[1] + w2 * t->vcc[1];
        col[2] = w0 * t->vca[2] + w1 * t->vcb[2] + w2 * t->vcc[2];
        col[3] = w0 * t->vca[3] + w1 * t->vcb[3] + w2 * t->vcc[3];

        if (d->tex_en || d->sec_en) {
            if (!rr_texstage_run((const r3d_texctx_t *) t->texctx, d, w0, w1, w2, col))
                continue;
        }

        if (d->spec_en) {
            col[0] += w0 * t->spa[0] + w1 * t->spb[0] + w2 * t->spc[0];
            col[1] += w0 * t->spa[1] + w1 * t->spb[1] + w2 * t->spc[1];
            col[2] += w0 * t->spa[2] + w1 * t->spb[2] + w2 * t->spc[2];
            spec_px++;
            if (col[0] > 1.0f || col[1] > 1.0f || col[2] > 1.0f)
                spec_clamp_px++;
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
                fogt_px++;
            } else
                f = w0 * t->fog[0] + w1 * t->fog[1] + w2 * t->fog[2];
            fogv_px++;
            if (f < 0.0f) fogv_clamp_lo_px++;
            if (f > 1.0f) fogv_clamp_hi_px++;
            if (f < 0.0f) f = 0.0f;
            if (f > 1.0f) f = 1.0f;
            col[0] = col[0] * f + d->fogr * (1.0f - f);
            col[1] = col[1] * f + d->fogg * (1.0f - f);
            col[2] = col[2] * f + d->fogb * (1.0f - f);
        }

        if (d->atest_en
            && !ref_cmp(d->atest_fn, (uint32_t) (col[3] * 255.0f + 0.5f), d->atest_ref)) {
            if (d->sten_on)
                sten_akill_px++;
            continue;
        }

        if (d->sten_on) {
            uint32_t sop  = !sres ? d->sfail_op : (zres ? d->zpass_op : d->zfail_op);

            sten_path_px[!sres ? 0 : (zres ? 2 : 1)]++;
            sten_op_px[sop & 7]++;
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

        uint32_t daddr = drow + (uint32_t) px * d->bpp;
        uint8_t *dcell;
        if (t->cptr) {
            uint32_t coff = daddr - t->c_base;
            if (coff + (uint32_t) d->bpp > t->c_lim)
                continue;
            dcell = t->cptr + coff;
        } else {
            dcell = &t->vram[daddr & t->vram_mask];
            if (daddr & (uint32_t) (d->bpp - 1)) {
                odd_c_px[d->bpp == 4]++;
                if (z_mis)
                    odd_both_px++;
            }
            if ((daddr & t->vram_mask) > t->vram_mask + 1u - (uint32_t) d->bpp)
                edge_c_px++;
        }

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

        if (g_pd_active)
            printf("PIXDUMP addr=%06x py=%d px=%d dt=%u dith=%u wm=%08x "
                   "tex=%u blend=%u out=%08x\n"
                   "  col  = [%a %a %a %a]\n"
                   "  x255 = [%.9g %.9g %.9g %.9g]  (pre-floor, +0.5 applied)\n",
                   daddr, py, px, d->dst_dt, d->dither, d->wmask, d->tex_en,
                   d->alpha_en, out, col[0], col[1], col[2], col[3],
                   (double) (col[0] * 255.0f + 0.5f),
                   (double) (col[1] * 255.0f + 0.5f),
                   (double) (col[2] * 255.0f + 0.5f),
                   (double) (col[3] * 255.0f + 0.5f));

        if (d->dst_dt == 6) {
            /* 8888 never dithers (no truncation), so it feeds the format
               and plane-mask tallies only */
            if (d->wmask != 0xffffffffu) {
                out = (out & d->wmask) | (*(uint32_t *) dcell & ~d->wmask);
                dst_masked_px++;
            }
            if (d->wmask == 0 && !d->atest_en)
                dst_cdead_px++;
            *(uint32_t *) dcell = out;
            dst_fmt_px[6]++;
        } else {
            uint32_t a   = (out >> 24) & 0xff;
            uint32_t r   = (out >> 16) & 0xff;
            uint32_t g   = (out >> 8) & 0xff;
            uint32_t b   = out & 0xff;
            int      bay = d->dither ? ref_bayer4[py & 3][px & 3] : -1;
            uint32_t raw, m;

            switch (d->dst_dt) {
                case 3: /* ARGB1555 */
                    if (bay >= 0) {
                        r = ref_dq(r, (uint32_t) bay >> 1);
                        g = ref_dq(g, (uint32_t) bay >> 1);
                        b = ref_dq(b, (uint32_t) bay >> 1);
                    }
                    raw = ((a << 8) & 0x8000) | ((r << 7) & 0x7c00)
                        | ((g << 2) & 0x03e0) | (b >> 3);
                    break;
                case 15: /* ARGB4444 */
                    if (bay >= 0) {
                        a = ref_dq(a, (uint32_t) bay);
                        r = ref_dq(r, (uint32_t) bay);
                        g = ref_dq(g, (uint32_t) bay);
                        b = ref_dq(b, (uint32_t) bay);
                    }
                    raw = ((a << 8) & 0xf000) | ((r << 4) & 0x0f00)
                        | (g & 0x00f0) | (b >> 4);
                    break;
                default: /* 4 = RGB565 */
                    if (bay >= 0) {
                        r = ref_dq(r, (uint32_t) bay >> 1);
                        g = ref_dq(g, (uint32_t) bay >> 2);
                        b = ref_dq(b, (uint32_t) bay >> 1);
                    }
                    raw = ((r << 8) & 0xf800) | ((g << 3) & 0x07e0) | (b >> 3);
                    break;
            }
            m = d->wmask & 0xffff;
            if (m != 0xffff)
                raw = (raw & m) | (*(uint16_t *) dcell & ~m);
            *(uint16_t *) dcell = (uint16_t) raw;
            dst_fmt_px[d->dst_dt & 15]++;
            if (bay < 0)
                dst_undith_px++;
            if (m != 0xffff)
                dst_masked_px++;
            if (d->wmask == 0 && !d->atest_en)
                dst_cdead_px++;
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

/* ---- rng: same LCG family as the harness ---- */
static uint32_t rngs = 0x12345678;
static uint32_t rng(void) { rngs = rngs * 1664525u + 1013904223u; return rngs; }
/* Small-range draws use the high LCG bits. Bit zero alternates on
   every call, so consecutive low-bit draws correlate and can prevent
   the need_lod retry condition from being satisfied. */
static uint32_t
rhi(uint32_t n)
{
    return (rng() >> 16) % n;
}
static float
frand(float lo, float hi)
{
    return lo + (hi - lo) * (float) (rng() & 0xffffff) / 16777216.0f;
}

/* A separate stream supplies texture alpha-one and lighting selectors
   without shifting the main stream's draw values. Both streams reset
   for each seed. */
static uint32_t xrngs;
static uint32_t
xrhi(uint32_t n)
{
    xrngs = xrngs * 1664525u + 1013904223u;
    return (xrngs >> 16) % n;
}

/* Texture-lighting state from the light word the device packs into the
   tri record and the folded tuple: TEX_LIGHT_FN in [3:0], the subtract
   bit in [4], ALPHA_LIGHT_FN in [7:5]. The factors are fixed (texel
   color, interpolated color, texel alpha, interpolated alpha), as the
   interpreter's draw-state setup gives them; word 0 is lighting off. */
static void
set_light(rage128_draw_state_t *d, uint32_t lw)
{
    memset(&d->lcomb, 0, sizeof(d->lcomb));
    d->light_on = lw != 0;
    if (!d->light_on)
        return;
    d->lcomb.comb  = lw & 0xfu;
    d->lcomb.fmsb  = (lw >> 4) & 1u;
    d->lcomb.cfac  = 4;
    d->lcomb.ifac  = 4;
    d->lcomb.comba = (lw >> 5) & 7u;
    d->lcomb.afac  = 6;
    d->lcomb.ifaca = 2;
}

static uint32_t
light_word(const rage128_draw_state_t *d)
{
    return d->light_on
        ? (d->lcomb.comb | (d->lcomb.fmsb << 4) | (d->lcomb.comba << 5))
        : 0u;
}

/* Datatypes whose texel carries alpha, the ones the alpha-one bit
   applies to (the interpreter's stage setup and the kernel's decode
   both limit it to these six). */
static int
dt_has_alpha(uint32_t dt)
{
    return dt == 0 || dt == 3 || dt == 6 || dt == 9 || dt == 14 || dt == 15;
}

/* ---- Vulkan plumbing ---- */
static VkInstance       inst;
static VkPhysicalDevice phys;
static VkDevice         dev;
static VkQueue          queue;
static uint32_t         qfam;
static VkCommandPool    pool;
static VkCommandBuffer  cb;
static VkFence          fence;
static VkDescriptorPool dpool;

typedef struct gbuf {
    VkBuffer       b;
    VkDeviceMemory m;
    void          *map;
    VkDeviceSize   sz;
} gbuf;

/* caps sized for bench-mode workloads; fuzz cases stay far below them */
/* must match GPU_SLOT_W in vid_ati_rage128_gpu_abi.glsl -- the kernel
   cuts its fixed 256-thread workgroup into slots of this width */
#define SLOT_W      16u
#define SLOT_SHIFT  4u
#define BATCH_SLOTS (256u / SLOT_W)
/* above this many spans in one row, skip the O(n^2) leveling */
#define BATCH_LEVEL_MAX 1024u

#define FZ_SPAN_CAP 8192u
#define FZ_TRI_CAP  2048u
#define FZ_RUN_CAP  64u
#define FZ_PX_CAP   (FZ_SPAN_CAP * 512u) /* ladder words: 2 per px when every span is table-fogged */
_Static_assert(FZ_SPAN_CAP <= (1u << 24), "span index overflows batch slot");
_Static_assert(BATCH_SLOTS == 256u / SLOT_W, "slot table mis-sized");
_Static_assert(SLOT_W == (1u << SLOT_SHIFT), "slot shift mismatch");

static gbuf b_cstage, b_zstage;
static gbuf b_vram, b_rows, b_order, b_spans, b_tris, b_lad, b_stage,
            b_pal, b_fin, b_fout;

/* GPUTRI_IMPORT=1 imports host allocations for the VRAM, texture-stage,
   and color/depth-stage arenas through Vulkan's external host-memory
   extension, mirroring the emulator's zero-copy path. The reference uses
   the same case content with either allocator, isolating import behavior. */
static int g_import;
static PFN_vkGetMemoryHostPointerPropertiesEXT p_gmhpp;
#define IMPORT_ALIGN 16384u

static void
mk_buf_import(gbuf *g, VkDeviceSize sz)
{
    VkDeviceSize asz = (sz + IMPORT_ALIGN - 1) & ~(VkDeviceSize) (IMPORT_ALIGN - 1);
    uint8_t     *raw = malloc((size_t) asz + IMPORT_ALIGN);
    if (!raw) { fprintf(stderr, "import arena malloc\n"); exit(1); }
    uint8_t *ptr = (uint8_t *) (((uintptr_t) raw + IMPORT_ALIGN - 1)
                                & ~(uintptr_t) (IMPORT_ALIGN - 1));
    memset(ptr, 0, (size_t) asz);

    VkMemoryHostPointerPropertiesEXT hpp = {
        VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT
    };
    VK_CHECK(p_gmhpp(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                     ptr, &hpp));

    VkExternalMemoryBufferCreateInfo embci = {
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO
    };
    embci.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.pNext = &embci;
    bci.size  = sz;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
              | VK_BUFFER_USAGE_TRANSFER_DST_BIT
              | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VK_CHECK(vkCreateBuffer(dev, &bci, NULL, &g->b));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, g->b, &mr);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    uint32_t mti = ~0u;
    VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((hpp.memoryTypeBits & mr.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & want) == want) { mti = i; break; }
    if (mti == ~0u) { fprintf(stderr, "no coherent import memory type\n"); exit(1); }
    VkImportMemoryHostPointerInfoEXT imp = {
        VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT
    };
    imp.handleType   = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    imp.pHostPointer = ptr;
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.pNext           = &imp;
    mai.allocationSize  = asz;
    mai.memoryTypeIndex = mti;
    VK_CHECK(vkAllocateMemory(dev, &mai, NULL, &g->m));
    VK_CHECK(vkBindBufferMemory(dev, g->b, g->m, 0));
    g->map = ptr;
    g->sz  = sz;
}

static void
mk_buf(gbuf *g, VkDeviceSize sz)
{
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size  = sz;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VK_CHECK(vkCreateBuffer(dev, &bci, NULL, &g->b));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, g->b, &mr);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    uint32_t mti = ~0u;
    VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & want) == want) { mti = i; break; }
    if (mti == ~0u) { fprintf(stderr, "no host memory type\n"); exit(1); }
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = mti;
    VK_CHECK(vkAllocateMemory(dev, &mai, NULL, &g->m));
    VK_CHECK(vkBindBufferMemory(dev, g->b, g->m, 0));
    VK_CHECK(vkMapMemory(dev, g->m, 0, VK_WHOLE_SIZE, 0, &g->map));
    g->sz = sz;
}

static VkShaderModule
load_spv_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "%s not found in the working directory (build.sh compiles it)\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t *spv = malloc(len);
    if (fread(spv, 1, len, f) != (size_t) len) { fprintf(stderr, "spv read\n"); exit(1); }
    fclose(f);
    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smci.codeSize = len;
    smci.pCode    = spv;
    VkShaderModule sm;
    VK_CHECK(vkCreateShaderModule(dev, &smci, NULL, &sm));
    free(spv);
    return sm;
}

static VkDescriptorSetLayout r_dsl, f_dsl;
static VkPipelineLayout      r_plyt, f_plyt;
static VkPipeline            r_pipes[N_KERNELS], r_pipes_serial[N_KERNELS],
                             r_lad_pipe, f_pipe;
static VkDescriptorSet       r_dset, f_dset;
static VkPipelineCache       plcache; /* file-persisted: repeat fuzz runs warm */

/* ---- folded-pipeline pool (device combine-tuple mirror) ----
   A pool tuple pins both stages' combine selectors as spec constants
   (C_FOLD, ids 7..21), exactly like the device's interned tuples; fuzz
   cases that use one override the draw state to match, so the C
   reference and the folded pipeline compute the same book entry and the
   compare stays bit-exact. Pool comb/comba codes are stratified across
   pool slots and seeds so every code in both books gets folded-form
   coverage over a default 3-seed run. */
#define TP_MAX   16
#define TP_SLOTS 6
typedef struct fz_tuple_t {
    /* [0..13] {comb,fmsb,cfac,ifac,comba,afac,ifaca} x 2;
       [14..17] {dt, s3tc} x 2 -- the production format fold
       pins these as spec constants too, stratified across slots and
       seeds like the comb codes */
    uint32_t   sel[19]; /* [18] the texture-lighting word, pinned as C_LIGHT */
    VkPipeline pipes[N_KERNELS][2][2]; /* [sten][dead] tuple folds */
} fz_tuple_t;
static fz_tuple_t tpool[TP_MAX];
static int        ntpool;
static int        g_seedidx;
static uint64_t   fold_cases, fold_faults, fold_splits;

/* One seg-module pipeline for the given spec constants:
   stock_sm + VkSpecializationInfo, the shape the device backend uses. */
static VkPipeline
mk_seg_pipe(const VkSpecializationMapEntry *sme, const int32_t *sd,
            uint32_t n, VkShaderModule stock_sm)
{
    VkComputePipelineCreateInfo cpi = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    VkSpecializationInfo        si  = { n, sme, (size_t) n * 4, sd };
    VkPipeline                  p;

    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.pName = "main";
    cpi.layout      = r_plyt;
    cpi.stage.module              = stock_sm;
    cpi.stage.pSpecializationInfo = &si;
    VK_CHECK(vkCreateComputePipelines(dev, plcache, 1, &cpi, NULL, &p));
    return p;
}

static VkPipeline
mk_fold_pipe(int kern, const uint32_t sel[19], int sten, int dead)
{
    VkSpecializationMapEntry sme[26];
    int32_t                  sd[26];
    for (int i = 0; i < 4; i++)
        sme[i] = (VkSpecializationMapEntry) { (uint32_t) i, (uint32_t) i * 4, 4 };
    sme[4] = (VkSpecializationMapEntry) { 7, 16, 4 };
    for (int i = 0; i < 14; i++)
        sme[5 + i] = (VkSpecializationMapEntry) { (uint32_t) (8 + i),
                                                  (uint32_t) (20 + i * 4), 4 };
    sme[19] = (VkSpecializationMapEntry) { 22, 76, 4 };
    sme[20] = (VkSpecializationMapEntry) { 23, 80, 4 };
    for (int i = 0; i < 4; i++)
        sme[21 + i] = (VkSpecializationMapEntry) { (uint32_t) (25 + i),
                                                   (uint32_t) (84 + i * 4), 4 };
    sme[25] = (VkSpecializationMapEntry) { 29, 100, 4 };
    sd[0] = r128_gpu_variants[kern].tex;
    sd[1] = r128_gpu_variants[kern].lod;
    sd[2] = r128_gpu_variants[kern].blend;
    sd[3] = r128_gpu_variants[kern].z;
    sd[4] = 1; /* C_FOLD */
    for (int i = 0; i < 14; i++)
        sd[5 + i] = (int32_t) sel[i];
    sd[19] = sten; /* S_STEN */
    sd[20] = dead; /* C_DEAD */
    for (int i = 0; i < 4; i++)
        sd[21 + i] = (int32_t) sel[14 + i]; /* F_DT0/F_S3TC0/F_DT1/F_S3TC1 */
    /* C_LIGHT: a dead-color draw pins 0, as the device's tuple does */
    sd[25] = dead ? 0 : (int32_t) sel[18];
    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smci.codeSize = sizeof(r128_gpu_seg_spv);
    smci.pCode    = (const uint32_t *) r128_gpu_seg_spv;
    VkShaderModule sm;
    VK_CHECK(vkCreateShaderModule(dev, &smci, NULL, &sm));
    VkPipeline p = mk_seg_pipe(sme, sd, 26, sm);
    vkDestroyShaderModule(dev, sm, NULL);
    return p;
}

static int fold_pipes_built;

static VkPipeline
tpool_pipe(int tid, int kern, int sten, int dead)
{
    if (!tpool[tid].pipes[kern][sten][dead]) {
        tpool[tid].pipes[kern][sten][dead] =
            mk_fold_pipe(kern, tpool[tid].sel, sten, dead);
        fold_pipes_built++;
    }
    return tpool[tid].pipes[kern][sten][dead];
}

static void tpool_reset(void)
{
    for (int i = 0; i < TP_SLOTS * 2; i++)
        for (int k = 0; k < N_KERNELS; k++)
            for (int s = 0; s < 2; s++)
                for (int d = 0; d < 2; d++)
                    if (tpool[i].pipes[k][s][d]) {
                        vkDestroyPipeline(dev, tpool[i].pipes[k][s][d], NULL);
                        tpool[i].pipes[k][s][d] = VK_NULL_HANDLE;
                    }
    ntpool = 0;
}

/* Seed-start parallel precompile enumerates the folded tuple matrix.
   Combine codes are stratified by seed and slot; other selectors come
   from a separate per-slot LCG so workers can build the pipelines before
   fuzzing begins. tpool_take still consumes and discards mk_comb draws
   at each textured draw, keeping the main stream independent of pipeline
   creation. tpool_pipe creates any missing pipeline on demand. */
#define PRIME_THR_MAX 16

static uint32_t prng_s;
static uint32_t prng(void) { prng_s = prng_s * 1664525u + 1013904223u; return prng_s; }
/* Use high LCG bits to avoid short-period small-range draws. */
static uint32_t phi(uint32_t n) { return (prng() >> 16) % n; }

typedef struct prime_job_t {
    int tid, kern, sten, dead;
} prime_job_t;
static prime_job_t prime_jobs[TP_SLOTS * 2 * N_KERNELS * 4];
static int         prime_njobs;
static _Atomic int prime_next;

static void *
prime_worker(void *arg)
{
    (void) arg;
    for (;;) {
        int i = atomic_fetch_add(&prime_next, 1);

        if (i >= prime_njobs)
            return NULL;
        prime_job_t *jb = &prime_jobs[i];

        tpool[jb->tid].pipes[jb->kern][jb->sten][jb->dead] =
            mk_fold_pipe(jb->kern, tpool[jb->tid].sel, jb->sten, jb->dead);
    }
}

static void
tpool_prime(void)
{
    pthread_t       thr[PRIME_THR_MAX];
    struct timespec ts0, ts1;
    long            nthr = gputri_prime_threads();
    int             live = 0;

    /* Fill selectors deterministically: comb/comba are stratified by seed
       and slot; remaining selectors use the separate per-slot LCG. */
    for (int idx = 0; idx < TP_SLOTS * 2; idx++) {
        int       tj  = idx % TP_SLOTS;
        int       tcl = idx / TP_SLOTS;
        int       s   = g_seedidx * TP_SLOTS + tj;
        uint32_t *sel = tpool[idx].sel;

        prng_s = 0x9e3779b9u ^ (uint32_t) (g_seedidx * 131 + idx);
        memset(sel, 0, sizeof(tpool[idx].sel));
        sel[0] = (uint32_t) (s % 16);
        sel[1] = phi(2);
        sel[2] = phi(9);
        sel[3] = phi(10);
        sel[4] = (uint32_t) ((s + 8) % 16);
        sel[5] = phi(8);
        sel[6] = phi(4);
        if (tcl == 1) {
            sel[7]  = (uint32_t) ((s + 4) % 16);
            sel[8]  = phi(2);
            sel[9]  = phi(9);
            sel[10] = phi(10);
            sel[11] = (uint32_t) ((s + 12) % 16);
            sel[12] = phi(8);
            sel[13] = phi(4);
        }
        /* format fold: dt classes stride across (seed, slot) exactly
           like the comb codes -- mk_stagehdr's 14-way class list, so a
           3-seed run folds every decodable format at least once */
        {
            static const uint32_t dt_cls[14] = { 4, 15, 6, 1, 2, 3, 5,
                                                 7, 8, 9, 11, 12, 14, 0 };
            uint32_t c0 = (uint32_t) ((idx + g_seedidx * 12) % 14);
            uint32_t c1 = (uint32_t) ((idx + 7 + g_seedidx * 12) % 14);

            sel[14] = dt_cls[c0];
            sel[15] = (sel[14] == 0u) ? phi(4) : 0u;
            sel[16] = dt_cls[c1];
            sel[17] = (sel[16] == 0u) ? phi(4) : 0u;
            /* the alpha-one bit rides the format selector (bit 4) on
               half the alpha-bearing slots; drawn last, so the values
               above are unchanged */
            if (dt_has_alpha(sel[14]) && phi(2))
                sel[14] |= 0x10u;
            if (dt_has_alpha(sel[16]) && phi(2))
                sel[16] |= 0x10u;
            /* the texture-lighting word on a third of the slots */
            if (phi(3) == 0)
                sel[18] = phi(16) | (phi(2) << 4) | (phi(8) << 5);
        }
    }

    prime_njobs = 0;
    for (int idx = 0; idx < TP_SLOTS * 2; idx++) {
        int cls = idx / TP_SLOTS;

        for (int k = 0; k < N_KERNELS; k++) {
            const struct r128_gpu_variant_t *a = &r128_gpu_variants[k];

            if (a->tex == 0 || (a->tex == 2 ? 1 : 0) != cls)
                continue;
            for (int st = 0; st <= (a->z ? 1 : 0); st++)
                for (int dd = 0; dd < 2; dd++)
                    prime_jobs[prime_njobs++] =
                        (prime_job_t) { idx, k, st, dd };
        }
    }

    if (nthr < 1)
        nthr = 1;
    if (nthr > PRIME_THR_MAX)
        nthr = PRIME_THR_MAX;
    atomic_store(&prime_next, 0);
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    for (long i = 1; i < nthr; i++)
        if (pthread_create(&thr[live], NULL, prime_worker, NULL) == 0)
            live++;
    prime_worker(NULL);
    for (int i = 0; i < live; i++)
        pthread_join(thr[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    printf("tpool prime: %d fold pipes in %.1f s (%d threads)\n",
           prime_njobs,
           (double) (ts1.tv_sec - ts0.tv_sec)
               + (double) (ts1.tv_nsec - ts0.tv_nsec) / 1e9,
           live + 1);
}

/* base (unfolded) pipeline creation worker: one job per kernel id */
static VkShaderModule base_sm;
static _Atomic int    base_next;

static void *
base_worker(void *arg)
{
    (void) arg;
    for (;;) {
        int v = atomic_fetch_add(&base_next, 1);

        if (v >= N_KERNELS)
            return NULL;
        static const VkSpecializationMapEntry sme[4] = {
            { 0, 0, 4 }, { 1, 4, 4 }, { 2, 8, 4 }, { 3, 12, 4 }
        };
        int32_t sd[4] = { r128_gpu_variants[v].tex,
                          r128_gpu_variants[v].lod,
                          r128_gpu_variants[v].blend,
                          r128_gpu_variants[v].z };

        r_pipes[v] = mk_seg_pipe(sme, sd, 4, base_sm);

        /* V_SERIAL=1 twin (device gpu_serial_build mirror): one
           workgroup, lane 0, spans in capture order */
        static const VkSpecializationMapEntry sme_s[5] = {
            { 0, 0, 4 }, { 1, 4, 4 }, { 2, 8, 4 }, { 3, 12, 4 }, { 24, 16, 4 }
        };
        int32_t sd_s[5] = { r128_gpu_variants[v].tex,
                            r128_gpu_variants[v].lod,
                            r128_gpu_variants[v].blend,
                            r128_gpu_variants[v].z, 1 };

        r_pipes_serial[v] = mk_seg_pipe(sme_s, sd_s, 5, base_sm);
    }
}

static void
init_vulkan(void)
{
    const char *iexts[] = {
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    ai.pApplicationName = "r128-gputri";
    ai.apiVersion       = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo        = &ai;
    ici.flags                   = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    ici.enabledExtensionCount   = 2;
    ici.ppEnabledExtensionNames = iexts;
#ifdef __APPLE__
    /* MoltenVK compiles with Metal's fast math unless told otherwise, and
       fast math lets the compiler fuse and reorder float operations that
       the C reference rounds one at a time. The device sets the same. */
    setenv("MVK_CONFIG_FAST_MATH_ENABLED", "0", 0);
#endif
    VkResult ir = vkCreateInstance(&ici, NULL, &inst);
    if (ir == VK_ERROR_INCOMPATIBLE_DRIVER || ir == VK_ERROR_INITIALIZATION_FAILED) {
        fprintf(stderr, "no Vulkan driver (vkCreateInstance = %d): skipped\n", ir);
        exit(GPUTRI_EXIT_SKIP);
    }
    VK_CHECK(ir);

    uint32_t np = 1;
    VkResult er = vkEnumeratePhysicalDevices(inst, &np, &phys);
    if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || np == 0) {
        fprintf(stderr, "no physical device: skipped\n");
        exit(GPUTRI_EXIT_SKIP);
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(phys, &props);
    printf("device: %s, %d production kernels\n", props.deviceName, N_KERNELS);

    uint32_t nqf = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nqf, NULL);
    VkQueueFamilyProperties *qfp = malloc(nqf * sizeof(*qfp));
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nqf, qfp);
    qfam = ~0u;
    for (uint32_t i = 0; i < nqf; i++)
        if (qfp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { qfam = i; break; }
    free(qfp);

    uint32_t nde = 0;
    vkEnumerateDeviceExtensionProperties(phys, NULL, &nde, NULL);
    VkExtensionProperties *de = malloc(nde * sizeof(*de));
    vkEnumerateDeviceExtensionProperties(phys, NULL, &nde, de);
    const char *dexts[2];
    uint32_t ndext = 0;
    int has_emh = 0;
    for (uint32_t i = 0; i < nde; i++) {
        if (!strcmp(de[i].extensionName, "VK_KHR_portability_subset"))
            dexts[ndext++] = "VK_KHR_portability_subset";
        if (!strcmp(de[i].extensionName, "VK_EXT_external_memory_host"))
            has_emh = 1;
    }
    free(de);
    {
        const char *ie = getenv("GPUTRI_IMPORT");
        g_import = ie && !strcmp(ie, "1");
    }
    if (g_import) {
        if (!has_emh) {
            fprintf(stderr, "GPUTRI_IMPORT=1 but VK_EXT_external_memory_host unsupported\n");
            exit(1);
        }
        dexts[ndext++] = "VK_EXT_external_memory_host";
        VkPhysicalDeviceExternalMemoryHostPropertiesEXT ehp = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT
        };
        VkPhysicalDeviceProperties2 pp2 = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
        };
        pp2.pNext = &ehp;
        vkGetPhysicalDeviceProperties2(phys, &pp2);
        if (ehp.minImportedHostPointerAlignment > IMPORT_ALIGN) {
            fprintf(stderr, "import alignment %llu > %u\n",
                    (unsigned long long) ehp.minImportedHostPointerAlignment,
                    IMPORT_ALIGN);
            exit(1);
        }
        printf("host-pointer import: ON (min align %llu)\n",
               (unsigned long long) ehp.minImportedHostPointerAlignment);
    }

    VkPhysicalDeviceVulkan11Features f11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    f11.storageBuffer16BitAccess = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    f2.pNext                 = &f11;
    f2.features.shaderInt64  = VK_TRUE;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = qfam;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.pNext                   = &f2;
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = ndext;
    dci.ppEnabledExtensionNames = dexts;
    VK_CHECK(vkCreateDevice(phys, &dci, NULL, &dev));
    vkGetDeviceQueue(dev, qfam, 0, &queue);

    VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = qfam;
    VK_CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool        = pool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(dev, &cbai, &cb));
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VK_CHECK(vkCreateFence(dev, &fci, NULL, &fence));

    if (g_import) {
        p_gmhpp = (PFN_vkGetMemoryHostPointerPropertiesEXT)
            vkGetDeviceProcAddr(dev, "vkGetMemoryHostPointerPropertiesEXT");
        if (!p_gmhpp) { fprintf(stderr, "no vkGetMemoryHostPointerPropertiesEXT\n"); exit(1); }
    }
    void (*mkb)(gbuf *, VkDeviceSize) = g_import ? mk_buf_import : mk_buf;
    mkb(&b_vram, VRAM_SZ + 4); /* +4: guard word, production contract */
    mk_buf(&b_rows, FZ_SPAN_CAP * 2 * sizeof(uint32_t));
    mk_buf(&b_order, FZ_SPAN_CAP * BATCH_SLOTS * sizeof(uint32_t));
    mk_buf(&b_spans, FZ_SPAN_CAP * sizeof(seg_span_t));
    mk_buf(&b_lad, FZ_PX_CAP * sizeof(uint32_t));
    mk_buf(&b_tris, FZ_TRI_CAP * sizeof(seg_tri_t));
    mkb(&b_stage, STAGE_SZ + 4); /* +4: same guard contract as vram */
    mkb(&b_cstage, CZSTG_SZ + 4);
    mkb(&b_zstage, CZSTG_SZ + 4);
    mk_buf(&b_pal, 4 * 256 * sizeof(uint32_t));
    mk_buf(&b_fin, 4096 * 2 * 8);
    mk_buf(&b_fout, 4096 * 3 * 8);

    /* raster: the production binding layout + 8-byte push constants */
    VkDescriptorSetLayoutBinding rb[13];
    for (int i = 0; i < 13; i++) {
        rb[i].binding            = i;
        rb[i].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        rb[i].descriptorCount    = 1;
        rb[i].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
        rb[i].pImmutableSamplers = NULL;
    }
    VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dslci.bindingCount = 13;
    dslci.pBindings    = rb;
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &r_dsl));
    dslci.bindingCount = 2;
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &f_dsl));

    VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 8 };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount          = 1;
    plci.pSetLayouts             = &r_dsl;
    plci.pushConstantRangeCount  = 1;
    plci.pPushConstantRanges     = &pcr;
    VK_CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &r_plyt));
    VkPushConstantRange fpcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 };
    plci.pSetLayouts         = &f_dsl;
    plci.pPushConstantRanges = &fpcr;
    VK_CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &f_plyt));

    /* file-persisted pipeline cache: with the fixed fuzz seeds the
       folded-tuple set is deterministic, so repeat runs compile warm */
    {
        VkPipelineCacheCreateInfo pcci = {
            VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO
        };
        FILE *pf   = fopen("gputri.plcache", "rb");
        void *blob = NULL;

        if (pf) {
            long bsz;

            fseek(pf, 0, SEEK_END);
            bsz = ftell(pf);
            fseek(pf, 0, SEEK_SET);
            if (bsz > 0 && (blob = malloc((size_t) bsz))
                && fread(blob, 1, (size_t) bsz, pf) == (size_t) bsz) {
                pcci.initialDataSize = (size_t) bsz;
                pcci.pInitialData    = blob;
            }
            fclose(pf);
        }
        if (vkCreatePipelineCache(dev, &pcci, NULL, &plcache) != VK_SUCCESS) {
            pcci.initialDataSize = 0;
            pcci.pInitialData    = NULL;
            VK_CHECK(vkCreatePipelineCache(dev, &pcci, NULL, &plcache));
        }
        free(blob);
    }

    VkComputePipelineCreateInfo cpi = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.pName  = "main";
    /* one module, per-kernel specialization -- same shape as the device
       backend so what is fuzzed is what ships */
    {
        VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        smci.codeSize = sizeof(r128_gpu_seg_spv);
        smci.pCode    = (const uint32_t *) r128_gpu_seg_spv;
        VkShaderModule sm;
        VK_CHECK(vkCreateShaderModule(dev, &smci, NULL, &sm));
        /* Independent kernel jobs share the shader module and internally
           synchronized pipeline cache, as in tpool_prime. */
        base_sm = sm;
        {
            pthread_t thr[PRIME_THR_MAX];
            long      nthr = gputri_prime_threads();
            int       live = 0;

            if (nthr < 1)
                nthr = 1;
            if (nthr > PRIME_THR_MAX)
                nthr = PRIME_THR_MAX;
            atomic_store(&base_next, 0);
            for (long i = 1; i < nthr; i++)
                if (pthread_create(&thr[live], NULL, base_worker, NULL) == 0)
                    live++;
            base_worker(NULL);
            for (int i = 0; i < live; i++)
                pthread_join(thr[i], NULL);
        }
        vkDestroyShaderModule(dev, sm, NULL);
    }
    {
        VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        smci.codeSize = sizeof(r128_gpu_lad_spv);
        smci.pCode    = (const uint32_t *) r128_gpu_lad_spv;
        VkShaderModule sm;
        VK_CHECK(vkCreateShaderModule(dev, &smci, NULL, &sm));
        cpi.stage.module = sm;
        cpi.layout       = r_plyt;
        VK_CHECK(vkCreateComputePipelines(dev, plcache, 1, &cpi, NULL, &r_lad_pipe));
        vkDestroyShaderModule(dev, sm, NULL);
    }
    VkShaderModule fsm = load_spv_file("f64test.spv");
    cpi.stage.module = fsm;
    cpi.layout       = f_plyt;
    VK_CHECK(vkCreateComputePipelines(dev, plcache, 1, &cpi, NULL, &f_pipe));
    vkDestroyShaderModule(dev, fsm, NULL);

    VkDescriptorPoolSize dps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 15 };
    VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpci.maxSets       = 2;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &dps;
    VK_CHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool));
    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool     = dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &r_dsl;
    VK_CHECK(vkAllocateDescriptorSets(dev, &dsai, &r_dset));
    dsai.pSetLayouts = &f_dsl;
    VK_CHECK(vkAllocateDescriptorSets(dev, &dsai, &f_dset));

    /* bindings 0/1 are both the vram buffer: u16 cell view and word view
       (texture fetch plus the 4-byte color/Z cells); 7 is the staged
       arena word view; 9-12 the staged c/z arenas (u16 + word view each) */
    gbuf *rbufs[13] = { &b_vram, &b_vram, &b_rows, &b_order, &b_spans, &b_tris,
                        &b_lad, &b_stage, &b_pal,
                        &b_cstage, &b_cstage, &b_zstage, &b_zstage };
    VkDescriptorBufferInfo dbi[15];
    VkWriteDescriptorSet   wds[15];
    for (int i = 0; i < 13; i++) {
        dbi[i] = (VkDescriptorBufferInfo) { rbufs[i]->b, 0, VK_WHOLE_SIZE };
        wds[i] = (VkWriteDescriptorSet) { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        wds[i].dstSet          = r_dset;
        wds[i].dstBinding      = i;
        wds[i].descriptorCount = 1;
        wds[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wds[i].pBufferInfo     = &dbi[i];
    }
    dbi[13] = (VkDescriptorBufferInfo) { b_fin.b, 0, VK_WHOLE_SIZE };
    dbi[14] = (VkDescriptorBufferInfo) { b_fout.b, 0, VK_WHOLE_SIZE };
    for (int i = 13; i < 15; i++) {
        wds[i] = (VkWriteDescriptorSet) { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        wds[i].dstSet          = f_dset;
        wds[i].dstBinding      = i - 13;
        wds[i].descriptorCount = 1;
        wds[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wds[i].pBufferInfo     = &dbi[i];
    }
    vkUpdateDescriptorSets(dev, 15, wds, 0, NULL);
}

static void
submit_wait(void)
{
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    VK_CHECK(vkQueueSubmit(queue, 1, &si, fence));
    VK_CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(dev, 1, &fence));
}

/* Run the f64test kernel over the first n pairs in b_fin; the results
   land in b_fout as add, mul, zq16 per pair. */
static void
f64test_dispatch(uint32_t n)
{
    VK_CHECK(vkResetCommandBuffer(cb, 0));
    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cb, &cbbi));
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, f_pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, f_plyt, 0, 1, &f_dset, 0, NULL);
    vkCmdPushConstants(cb, f_plyt, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &n);
    vkCmdDispatch(cb, (n + 63) / 64, 1, 1);
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    VK_CHECK(vkEndCommandBuffer(cb));
    submit_wait();
}

/* ---- soft-f64 add on the exceptional classes: zeros of both signs,
   subnormal and normal boundaries, cancellation into a subnormal,
   signed infinities, NaN operands and generated NaNs, overflow. Only
   the add result is compared (f64_mul keeps its finite-normal scope):
   bit-exact for every non-NaN sum, by class for a NaN. ---- */
static int
f64_except_check(void)
{
    static const struct {
        const char *name;
        uint64_t    a, b;
    } vec[] = {
        { "+0 + -0",            0x0000000000000000ull, 0x8000000000000000ull },
        { "-0 + -0",            0x8000000000000000ull, 0x8000000000000000ull },
        { "-0 + +0",            0x8000000000000000ull, 0x0000000000000000ull },
        { "minsub + minsub",    0x0000000000000001ull, 0x0000000000000001ull },
        { "sub + sub carry",    0x0008000000000000ull, 0x0008000000000000ull },
        { "minnorm - half",     0x0010000000000000ull, 0x8008000000000000ull },
        { "norm cancel to sub", 0x0010000000000001ull, 0x8010000000000000ull },
        { "sub exact cancel",   0x0000000000000005ull, 0x8000000000000005ull },
        { "sub - smaller sub",  0x0000000000000005ull, 0x8000000000000003ull },
        { "minnorm + minsub",   0x0010000000000000ull, 0x0000000000000001ull },
        { "one + minsub",       0x3ff0000000000000ull, 0x0000000000000001ull },
        { "minsub + one",       0x0000000000000001ull, 0x3ff0000000000000ull },
        { "-minsub + -minsub",  0x8000000000000001ull, 0x8000000000000001ull },
        { "one + tie",          0x3ff0000000000000ull, 0x3ca0000000000000ull },
        { "one + tie + sticky", 0x3ff0000000000000ull, 0x3ca0000000000001ull },
        { "max + max",          0x7fefffffffffffffull, 0x7fefffffffffffffull },
        { "-max + -max",        0xffefffffffffffffull, 0xffefffffffffffffull },
        { "max + half ulp",     0x7fefffffffffffffull, 0x7c90000000000000ull },
        { "max + below half",   0x7fefffffffffffffull, 0x7c8fffffffffffffull },
        { "+inf + +inf",        0x7ff0000000000000ull, 0x7ff0000000000000ull },
        { "-inf + -inf",        0xfff0000000000000ull, 0xfff0000000000000ull },
        { "+inf + -inf",        0x7ff0000000000000ull, 0xfff0000000000000ull },
        { "-inf + +inf",        0xfff0000000000000ull, 0x7ff0000000000000ull },
        { "+inf + one",         0x7ff0000000000000ull, 0x3ff0000000000000ull },
        { "one + +inf",         0x3ff0000000000000ull, 0x7ff0000000000000ull },
        { "-inf + one",         0xfff0000000000000ull, 0x3ff0000000000000ull },
        { "+inf + -0",          0x7ff0000000000000ull, 0x8000000000000000ull },
        { "+0 + -inf",          0x0000000000000000ull, 0xfff0000000000000ull },
        { "+inf + minsub",      0x7ff0000000000000ull, 0x0000000000000001ull },
        { "qnan + one",         0x7ff8000000000000ull, 0x3ff0000000000000ull },
        { "one + qnan",         0x3ff0000000000000ull, 0x7ff8000000000000ull },
        { "snan + one",         0x7ff0000000000001ull, 0x3ff0000000000000ull },
        { "qnan + qnan",        0x7ff8000000000000ull, 0xfff8000000000000ull },
        { "qnan + +inf",        0x7ff8000000000000ull, 0x7ff0000000000000ull },
        { "qnan + +0",          0x7ff8000000000000ull, 0x0000000000000000ull },
        { "-0 + qnan",          0x8000000000000000ull, 0x7ff8000000000000ull },
    };
    enum { NV = (int) (sizeof(vec) / sizeof(vec[0])) };
    uint64_t *in  = b_fin.map;
    uint64_t *out = b_fout.map;
    int       bad = 0;

    for (int i = 0; i < NV; i++) {
        in[i * 2]     = vec[i].a;
        in[i * 2 + 1] = vec[i].b;
    }
    f64test_dispatch(NV);
    for (int i = 0; i < NV; i++) {
        double   a, b, r;
        uint64_t want, got = out[i * 3];
        int      ok;

        memcpy(&a, &vec[i].a, 8);
        memcpy(&b, &vec[i].b, 8);
        r = a + b;
        memcpy(&want, &r, 8);
        ok = isnan(r) ? ((got & 0x7fffffffffffffffull) > 0x7ff0000000000000ull)
                      : got == want;
        if (!ok)
            printf("f64 add %-20s a=%016llx b=%016llx ref=%016llx got=%016llx\n",
                   vec[i].name, (unsigned long long) vec[i].a,
                   (unsigned long long) vec[i].b, (unsigned long long) want,
                   (unsigned long long) got);
        bad += !ok;
    }
    printf("f64 exceptional add: %d/%d mismatches\n", bad, NV);
    return bad;
}

/* ---- soft-f64 selftest: GPU add/mul/zq16 vs host doubles ---- */
static int
f64_selftest(void)
{
    enum { N = 4096 };
    double   *in  = b_fin.map;
    uint64_t *out = b_fout.map;

    for (int i = 0; i < N; i++) {
        double a, b;

        switch (i & 3) {
            case 0:
                a = (double) (int32_t) (rng() % 3000) / 2000.0 - 0.2;
                b = ((double) (int32_t) rng() / 4e12);
                break;
            case 1:
                a = ldexp((double) (rng() & 0xfffff) + 1.0, (int) (rng() % 45) - 41)
                    * ((rng() & 1) ? -1.0 : 1.0);
                b = ldexp((double) (rng() & 0xfffff) + 1.0, (int) (rng() % 45) - 41)
                    * ((rng() & 1) ? -1.0 : 1.0);
                break;
            case 2:
                a = (double) (int32_t) rng() / 1e6;
                b = -a * (1.0 + ldexp((double) (rng() & 0xff), -52));
                break;
            default:
                a = (rng() & 1) ? 0.0 : (double) (rng() & 0xffff) + 0.5;
                b = (rng() & 1) ? -0.0 : ldexp(1.0, -(int) (rng() % 30));
                break;
        }
        in[i * 2]     = a;
        in[i * 2 + 1] = b;
    }
    f64test_dispatch(N);

    int bad = 0;
    for (int i = 0; i < N; i++) {
        double a = in[i * 2], b = in[i * 2 + 1];
        double radd = a + b, rmul = a * b;
        double zc = a;
        if (!(zc > 0.0)) zc = 0.0;
        if (zc > 1.0) zc = 1.0;
        double zq = zc * 65535.0 + 0.5;
        if (zq > 65535.0) zq = 65535.0;
        uint64_t ezq = (uint32_t) zq;
        uint64_t eadd, emul;

        memcpy(&eadd, &radd, 8);
        memcpy(&emul, &rmul, 8);
        if (out[i * 3] != eadd || out[i * 3 + 1] != emul || out[i * 3 + 2] != ezq)
            bad++;
    }
    printf("f64 selftest: %d/%d mismatches\n", bad, N);
    return bad;
}

/* ------------------------------------------------------------------------
 * Draw / segment generation
 * ---------------------------------------------------------------------- */

static uint8_t vram_ref[VRAM_SZ + 4];
/* pre-image of the case, so a mismatch can name which side wrote the byte
   instead of only that the two differ */
static uint8_t vram_orig[VRAM_SZ + 4];
/* stage arena mirror: staged slots sample here (bit-31 base on the GPU
   side); read-only for both sides */
static uint8_t stage_ref[STAGE_SZ + 4];

#define MAXDRAWS 16
#define MAXROWS  5

typedef struct fz_row_t {
    int     py;
    int64_t e0, e1, e2;
    double  zline;
} fz_row_t;

typedef struct fz_draw_t {
    int                  kern;
    int                  tuple; /* tpool index, -1 = unfolded pipeline */
    int                  serial; /* self-aliasing: serial-run execution */
    int                  probe_cinit; /* seed covered color cells with 0x55 */
    rage128_draw_state_t d;
    r3d_texctx_t         tc;
    int32_t              x0, x1;
    int64_t              e0dxi, e1dxi, e2dxi;
    float                invs;
    double               dZdx;
    float                vca[4], vcb[4], vcc[4];
    float                spa[4], spb[4], spc[4];
    float                fog[4];
    uint8_t              fog_table[256];
    uint32_t             stipple[32];
    uint32_t             pal_idx;
    int                  nrows;
    fz_row_t             row[MAXROWS];
} fz_draw_t;

static uint32_t g_c_off, g_z_off;
static int g_stipple_fuzz;
/* staged c/z case flags (production bit-31 drow/zrow contract): the case's
   color / z surface lives in the b_cstage / b_zstage arena instead of
   vram, arena-relative at py * ROW_PITCH (base = the surface base, the
   interpreter's staged index). Host mirrors below are the reference. */
static int      g_c_stg, g_z_stg;
static uint64_t cstg_cases, zstg_cases;
static uint8_t  cstage_ref[CZSTG_SZ + 4];
static uint8_t  zstage_ref[CZSTG_SZ + 4];

static uint32_t
level_bytes(uint32_t dt, uint32_t s3tc, uint32_t w, uint32_t h)
{
    if (dt == 0)
        return ((w + 3) / 4) * ((h + 3) / 4) * (s3tc >= 2 ? 16 : 8);
    if (dt == 1 || dt == 2 || dt == 7 || dt == 8 || dt == 9)
        return w * h; /* r3d_level_bytes: 1 byte/texel classes */
    return w * h * ((dt == 6 || dt == 5 || dt == 14) ? 4 : 2);
}

/* mirror of the production gpu_slot_base_pack: a staged slot (texbase in
   the stage arena, not vram) packs its arena offset with bit 31 */
static uint32_t
slot_base_pack(const struct r3d_slot_desc_t *sl)
{
    if (sl->texbase == vram_ref)
        return sl->base;
    return (uint32_t) (sl->texbase - stage_ref) | 0x80000000u;
}

/* mirror of the harness setup_stage_desc, with zone-aware base placement:
   normal bases keep every read in [0, TEX_ZONE); mask-edge bases run the
   reads up to VRAM_SZ-1 (spilling into the guard word) with the wrap
   landing back in the texture zone */
static void
setup_stage_desc(const r3d_stage_hdr_t *h, r3d_stage_desc_t *sd)
{
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
    sd->pal     = g_texpals[0];
    sd->slot_valid = 0x7ff;
    for (int sl = 0; sl <= h->top && sl <= 10; sl++) {
        int      shift = h->top - sl;
        uint32_t w     = 1u << (h->tsp & 0xf);
        uint32_t hh    = 1u << ((h->tsp >> 8) & 0xf);

        if (shift > 31)
            shift = 31;
        w >>= shift;
        hh >>= shift;
        if (!w) w = 1;
        if (!hh) hh = 1;
        sd->slot[sl].lw      = w;
        sd->slot[sl].lh      = hh;
        sd->slot[sl].texbase = vram_ref;
        uint32_t lvl = level_bytes(h->dt, h->s3tc, w, hh);
        uint32_t r   = rhi(8);
        if (r == 0) {
            /* mask-edge + guard stress */
            uint32_t span = lvl + 4;
            if (span > (240u << 10))
                span = 240u << 10;
            sd->slot[sl].mask = VRAM_MASK;
            sd->slot[sl].base = VRAM_SZ - 1 - (rng() % span);
        } else if (r == 1) {
            sd->slot[sl].mask = (1u << (12 + rhi(7))) - 1; /* 4K..256K */
            /* production resident bases are & 0x3fffffff (bit 31 is the
               staged flag); generate within that contract */
            sd->slot[sl].base = rng() & 0x3fffffffu;
        } else if (r == 2 || r == 3) {
            /* Staged levels use 16-byte-aligned arena offsets, base zero,
               and wrap within their own slice, matching the modeled stager.
               r == 3 pins the level to the arena end to exercise the guard
               word. Draw rr on both arms so placement does not shift the
               random stream or its seed-sensitive coverage paths. */
            uint32_t rr = rng();
            uint32_t so = (r == 3 || lvl + 16 >= STAGE_SZ)
                              ? (STAGE_SZ - lvl) & ~15u
                              : (rr % (STAGE_SZ - lvl - 15)) & ~15u;

            sd->slot[sl].texbase = stage_ref + so;
            sd->slot[sl].mask    = lvl ? lvl - 1 : 0;
            sd->slot[sl].base    = 0;
        } else {
            sd->slot[sl].mask = VRAM_MASK;
            sd->slot[sl].base = rng() % (TEX_ZONE - lvl - 8);
        }
    }
}

static int
stage_need_lod(const r3d_stage_hdr_t *h)
{
    return (!h->mipdis && h->minb >= 2)
        || ((int) (h->minb & 1) != (h->mag == 1));
}

static void
mk_stagehdr(r3d_stage_hdr_t *h, int want_lod)
{
    for (;;) {
        memset(h, 0, sizeof(*h));
        uint32_t lw2 = 2 + rhi(7); /* 4..256 */
        uint32_t lh2 = 2 + rhi(7);

        h->tsp = lw2 | (lh2 << 8);
        h->top = rhi((lw2 < lh2 ? lw2 : lh2) + 1);
        switch (rhi(14)) {
            case 0: h->dt = 4; break;
            case 1: h->dt = 15; break;
            case 2: h->dt = 6; break;
            case 3: h->dt = 1; break;  /* CI4 */
            case 4: h->dt = 2; break;  /* CI8 */
            case 5: h->dt = 3; break;  /* ARGB1555 */
            case 6: h->dt = 5; break;  /* RGB888 */
            case 7: h->dt = 7; break;  /* RGB332 */
            case 8: h->dt = 8; break;  /* Y8 */
            case 9: h->dt = 9; break;  /* RGB8 */
            case 10: h->dt = 11; break; /* VYUY422 */
            case 11: h->dt = 12; break; /* YVYU422 */
            case 12: h->dt = 14; break; /* aYUV444 */
            default: h->dt = 0; h->s3tc = rhi(4); break;
        }
        h->clamp_s = rhi(4);
        h->clamp_t = rhi(4);
        h->border  = rng();
        h->minb    = rhi(8);
        h->mag     = rhi(2);
        h->mipdis  = rhi(4) == 0;
        if (want_lod < 0 || stage_need_lod(h) == want_lod) {
            /* TEX_MAP_AEN clear on half the alpha-bearing stages */
            h->aone = dt_has_alpha(h->dt) && xrhi(2) == 0;
            return;
        }
    }
}

static void
mk_comb(r3d_comb_desc_t *c)
{
    c->comb  = rhi(16);
    c->fmsb  = rhi(2);
    c->cfac  = rhi(9);
    c->ifac  = rhi(10);
    c->comba = rhi(16);
    c->afac  = rhi(8);
    c->ifaca = rhi(4);
}

/* Folded-pipeline pool entries are keyed by stage class and slot.
   Combine codes stride across slots and seeds so the standard three-seed
   run folds every code in both books. tpool_prime builds selectors and
   pipelines at seed start; the discarded mk_comb draws below keep main
   stream consumption independent of whether a tuple is already built. */
static int
tpool_take(int kern)
{
    int cls = (r128_gpu_variants[kern].tex == 2) ? 1 : 0;
    int j   = (int) rhi(TP_SLOTS);
    int idx = cls * TP_SLOTS + j;

    while (ntpool <= idx) {
        r3d_comb_desc_t c;

        mk_comb(&c);
        if (ntpool / TP_SLOTS == 1)
            mk_comb(&c);
        (void) c;
        ntpool++;
    }
    return idx;
}

/* pin a draw's combine state and texel formats to the pool tuple so the
   C reference and the folded pipeline compute the identical book entry
   and format decode. Overriding dt after mk_draw is sound: both sides
   decode the same random bytes with the same recorded lw/lh/base/mask,
   and the palette is wired unconditionally, so the compare stays
   bit-exact whatever format the level layout was built for. */
static void
apply_tuple(fz_draw_t *dr, int tid)
{
    const uint32_t *s = tpool[tid].sel;

    /* a format selector is the datatype with the alpha-one bit in bit 4,
       the way the device folds it */
    dr->tuple           = tid;
    dr->d.sh[0].dt      = s[14] & 0xfu;
    dr->d.sh[0].aone    = (s[14] >> 4) & 1u;
    dr->d.sh[0].s3tc    = s[15];
    dr->d.sh[1].dt      = s[16] & 0xfu;
    dr->d.sh[1].aone    = (s[16] >> 4) & 1u;
    dr->d.sh[1].s3tc    = s[17];
    /* the C reference samples through the stage descs copied at
       mk_draw time -- pin those too or the two sides decode apart */
    dr->tc.sd0.dt       = s[14] & 0xfu;
    dr->tc.sd0.amask    = (s[14] & 0x10u) ? 0xff000000u : 0u;
    dr->tc.sd0.s3tc     = s[15];
    dr->tc.sd1.dt       = s[16] & 0xfu;
    dr->tc.sd1.amask    = (s[16] & 0x10u) ? 0xff000000u : 0u;
    dr->tc.sd1.s3tc     = s[17];
    dr->d.comb[0].comb  = s[0];
    dr->d.comb[0].fmsb  = s[1];
    dr->d.comb[0].cfac  = s[2];
    dr->d.comb[0].ifac  = s[3];
    dr->d.comb[0].comba = s[4];
    dr->d.comb[0].afac  = s[5];
    dr->d.comb[0].ifaca = s[6];
    dr->d.comb[1].comb  = s[7];
    dr->d.comb[1].fmsb  = s[8];
    dr->d.comb[1].cfac  = s[9];
    dr->d.comb[1].ifac  = s[10];
    dr->d.comb[1].comba = s[11];
    dr->d.comb[1].afac  = s[12];
    dr->d.comb[1].ifaca = s[13];
    set_light(&dr->d, s[18]);
}

/* Specialization axes per kernel id come straight from the shipped
   r128_gpu_variants table -- the same values the pipelines above were
   specialized with, so there is no mirror to drift. */
#define kern_axes r128_gpu_variants

/* draw state inside kernel `kern`'s gate envelope, runtime selectors
   randomized */
/* commutative-volume case template: when on, every z-kernel draw of the
   case shares this commutative stencil config (see mk_state) */
static struct {
    int      on;
    uint32_t zshift, sfail_op, zpass_op, zfail_op, sref, svmask, swmask;
} g_commut_tmpl;
static int      g_commut_alt;  /* draws alternate between two classes */
/* >=0 pins every draw's first row there, so a case's draws stack on the
   same rows. Live volume rows carry ~16 overlapping same-class spans;
   unpinned random base_py over 48 rows almost never reproduces that. */
static int      g_py_pin = -1;
static uint32_t g_commut_base;

static void
mk_state(int kern, rage128_draw_state_t *d)
{
    static const uint32_t dsts[4] = { 3, 4, 15, 6 };

    memset(d, 0, sizeof(*d));
    d->draw_ok = 1;
    /* destination surface axes: three 16bpp packed formats
       through the u16 view plus ARGB8888 through the word view, dither
       either way, and a partial plane mask a third of the time so the
       read-modify-write merge is exercised, not just the full-mask fast
       path. 8888 carries a 32-bit mask and cannot dither (no truncation). */
    d->dst_dt = dsts[rhi(4)];
    d->bpp    = (d->dst_dt == 6) ? 4 : 2;
    d->dither = rhi(4) ? 1 : 0;
    /* wmask 0 forced ~1/8: with alpha test off it selects the kernel's
       dead-color fast path (pure z/stencil pixel), which a random mask
       almost never lands on exactly */
    if (rhi(8) == 0)
        d->wmask = 0;
    else if (d->bpp == 4)
        d->wmask = rhi(3) ? 0xffffffffu : rng();
    else
        d->wmask = rhi(3) ? 0xffff : (rng() & 0xffff);
    if (kern_axes[kern].z && g_commut_tmpl.on) {
        /* commutative-volume case: every draw shares one commutative
           stencil config (dead color, no z write, always-pass, sat ops), so
           the packer's order-free exemption and the kernel's atomic
           stencil path get overlapping same-class spans every time */
        d->wmask    = 0;
        d->atest_en = 0;
        d->zbpp     = 4;
        d->zmax     = 0xffffff;
        d->zshift   = g_commut_tmpl.zshift;
        d->zrowpx   = ROW_PITCH / 4;
        d->sten_on  = 1;
        d->sshift   = d->zshift ? 0 : 24;
        d->sfn      = 7;
        d->sfail_op = g_commut_tmpl.sfail_op;
        d->zpass_op = g_commut_tmpl.zpass_op;
        d->zfail_op = g_commut_tmpl.zfail_op;
        d->sref     = g_commut_tmpl.sref;
        d->svmask   = g_commut_tmpl.svmask;
        d->swmask   = g_commut_tmpl.swmask;
        d->z_en     = 1;
        d->z_wr     = 0;
        d->zfn      = 2;
    } else if (kern_axes[kern].z) {
        /* the z axis means the z cell is live: depth test, stencil, or
           both. Depth cell: u16, or the 4-byte cell with 24-bit
           depth at either end (zshift 0 = D24 + stencil high, 8 = D24S8
           with depth high). zmax must track zbpp -- the kernel derives
           it. */
        if (rhi(2)) {
            d->zbpp   = 4;
            d->zmax   = 0xffffff;
            d->zshift = rhi(2) ? 8 : 0;
            d->zrowpx = ROW_PITCH / 4;
            /* Only wide cells carry stencil. Exercise all eight modeled
               operations, including wrap codes 6/7, all comparisons, and
               partial value/write masks. Stencil-only draws still own the
               cell: the model honors TEX_CNTL_C STENCIL_EN even when Z_EN
               is clear (CCE 3D supplement, TEX_CNTL_C). */
            if (rhi(2)) {
                d->sten_on  = 1;
                d->sshift   = d->zshift ? 0 : 24;
                d->sfn      = rhi(8);
                d->sfail_op = rhi(8);
                d->zpass_op = rhi(8);
                d->zfail_op = rhi(8);
                d->sref     = rng() & 0xff;
                d->svmask   = rhi(4) ? 0xff : rng() & 0xff;
                d->swmask   = rhi(4) ? 0xff : rng() & 0xff;
            }
        } else {
            d->zbpp   = 2;
            d->zmax   = 0xffff;
            d->zshift = 0;
            d->zrowpx = ROW_PITCH / 2;
        }
        if (!d->sten_on || rhi(4)) {
            d->z_en = 1;
            d->z_wr = rhi(8) ? 1 : 0;
            d->zfn  = rhi(4) ? 2 : rhi(8);
        }
    }
    if (kern_axes[kern].blend) {
        d->alpha_en = 1;
        d->bsrc     = rhi(13);
        d->bdst     = rhi(11);
        d->bfcn     = rhi(4);
    }
    /* specular is a runtime selector on every kernel; random vertex
       speculars sum past 1.0 often enough to exercise the saturate */
    if (rhi(4) == 0)
        d->spec_en = 1;
    /* Fog factors exercise both clamp sides through out-of-range
       interpolants. Table fog uses the ladder index and fraction on
       every kernel, including draws without a live depth cell. */
    if (rhi(4) == 0) {
        d->fog_en = 1;
        d->fogr   = (float) (rng() & 0xff) / 255.0f;
        d->fogg   = (float) (rng() & 0xff) / 255.0f;
        d->fogb   = (float) (rng() & 0xff) / 255.0f;
        if (rhi(2))
            d->fog_table_en = 1;
    }
    /* alpha test is a runtime selector on every kernel (fn 0 = never and
       7 = always are in range, so fully-killed draws get covered too) */
    if (rhi(3) == 0) {
        d->atest_en  = 1;
        d->atest_fn  = rhi(8);
        d->atest_ref = rhi(256);
    }
    if (rhi(3) == 0) {
        d->aux_on   = 1;
        d->aux_cntl = (rng() >> 16) & 0x3f;
        for (int i = 0; i < 3; i++) {
            d->aux_x0[i] = (int32_t) rhi(400) - 20;
            d->aux_x1[i] = d->aux_x0[i] + (int32_t) rhi(300);
            d->aux_y0[i] = (int32_t) rhi(FUZZ_PY) - 4;
            d->aux_y1[i] = d->aux_y0[i] + (int32_t) rhi(24);
        }
    }
    for (int i = 0; i < 4; i++)
        d->cc[i] = (float) (rng() & 0xff) / 255.0f;
    if (kern_axes[kern].tex) {
        d->tex_en   = 1;
        /* PRIM_TEX_CNTL_C PRIM_TEX_PERSPECTIVE_DIS selects affine
           sampling (CCE 3D supplement, PRIM_TEX_CNTL_C). As a per-triangle
           runtime selector it skips the rhw dot, sets ir to 1.0, and
           uses screen-space LOD gradients. */
        d->do_persp = rhi(4) != 0;
        d->lod_bias = rhi(4) ? 0.0f : frand(-2.0f, 2.0f);
        mk_stagehdr(&d->sh[0], kern_axes[kern].lod);
        d->need_lod = stage_need_lod(&d->sh[0]);
        d->texw0    = (float) (1u << (d->sh[0].tsp & 0xf));
        d->texh0    = (float) (1u << ((d->sh[0].tsp >> 8) & 0xf));
        mk_comb(&d->comb[0]);
        if (kern_axes[kern].tex == 2) {
            d->sec_en    = 1;
            /* Two-stage kernels also carry secondary-only draws; the
               primary-off selector preserves the iterated input. */
            d->tex_en    = xrhi(2) != 0;
            d->sec_sel   = (int) rhi(2);
            d->need_lod2 = (int) rhi(2);
            mk_stagehdr(&d->sh[1], -1);
            d->texw1 = (float) (1u << (d->sh[1].tsp & 0xf));
            d->texh1 = (float) (1u << ((d->sh[1].tsp >> 8) & 0xf));
            mk_comb(&d->comb[1]);
        }
        /* chroma keys (stage 0 only), ~half of textured configs. Keys
           are random (vram re-rolls per case, so texel-sampled keys buy
           nothing); the single-bit masks make both discard and pass
           fragments common on every path/polarity. */
        if (rhi(2)) {
            static const uint32_t ckmsks[] = {
                0xff000000u, 0x00ff0000u, 0x000000ffu,
                0x00080000u, 0x80808080u, 0xffffffffu,
                0x00000000u /* vacuous compare: fn 3 (the code-1 decode)
                               rejects all, fn 2 rejects none */
            };

            d->ck3d_on  = (int) rhi(2);
            d->ckc_on   = (!d->ck3d_on || rhi(4) == 0) ? 1 : 0;
            d->ckfn     = rhi(2) ? 3 : 2;
            d->ck3d_msk = ckmsks[rng() % 7];
            d->ck3d_clr = rng();
            d->ckc_msk  = ckmsks[rng() % 7];
            d->ckc_clr  = rng();
            d->need_ck  = 1;
        }
        /* texture lighting on a third of textured draws: every color
           function with and without the subtract bit, every alpha
           function */
        if (xrhi(3) == 0)
            set_light(d, xrhi(16) | (xrhi(2) << 4) | (xrhi(8) << 5));
    }
}

static void
mk_draw(fz_draw_t *dr, int kern, int py_lim)
{
    memset(dr, 0, sizeof(*dr));
    dr->kern  = kern;
    dr->tuple = -1;
    mk_state(kern, &dr->d);
    dr->d.stip_en = g_stipple_fuzz && rhi(2);
    if (dr->d.stip_en) {
        uint32_t mode = rhi(4);

        for (int k = 0; k < 32; k++)
            dr->stipple[k] = mode == 0 ? 0 : mode == 1 ? UINT32_MAX
                : mode == 2 ? rng() : 1u << (k & 31);
    }

    dr->x0 = (int32_t) rhi(64);
    dr->x1 = dr->x0 + (int32_t) rhi(rhi(4) == 0 ? 300 : 200);
    int64_t area = (int64_t) ((rng() % 2000000) + 1000);

    dr->e0dxi = (int64_t) (int32_t) (rng() % 20000) - 10000;
    dr->e1dxi = (int64_t) (int32_t) (rng() % 20000) - 10000;
    dr->e2dxi = -(dr->e0dxi + dr->e1dxi);
    dr->invs  = 1.0f / (float) area;
    dr->dZdx  = ((double) (int32_t) rng() / 4e12);
    for (int k = 0; k < 4; k++) {
        dr->vca[k] = (float) (rng() & 0xff) / 255.0f;
        dr->vcb[k] = (float) (rng() & 0xff) / 255.0f;
        dr->vcc[k] = (float) (rng() & 0xff) / 255.0f;
    }
    if (dr->d.spec_en)
        for (int k = 0; k < 3; k++) {
            dr->spa[k] = (float) (rng() & 0xff) / 255.0f;
            dr->spb[k] = (float) (rng() & 0xff) / 255.0f;
            dr->spc[k] = (float) (rng() & 0xff) / 255.0f;
        }
    if (dr->d.fog_en) {
        /* out-of-range vertex factors too (same idea as the jit harness):
           interpolation past [0,1] must hit the kernel's clamp */
        for (int k = 0; k < 3; k++)
            dr->fog[k] = (float) (rng() & 0x1ff) / 255.0f - 0.5f;
        if (dr->d.fog_table_en)
            for (int k = 0; k < 256; k++)
                dr->fog_table[k] = (uint8_t) rng();
    }
    {
        float *f = &dr->tc.sta;

        for (int k = 0; k < 12; k++)
            f[k] = frand(-8.0f, 8.0f);
        dr->tc.arhw = rhi(10) ? frand(0.05f, 3.0f) : 0.0f;
        dr->tc.brhw = rhi(10) ? frand(0.05f, 3.0f) : 0.0f;
        dr->tc.crhw = rhi(10) ? frand(0.05f, 3.0f) : 0.0f;
        dr->tc.dSdx = frand(-2.0f, 2.0f);
        dr->tc.dSdy = frand(-2.0f, 2.0f);
        dr->tc.dTdx = frand(-2.0f, 2.0f);
        dr->tc.dTdy = frand(-2.0f, 2.0f);
        dr->tc.dWdx = frand(-0.2f, 0.2f);
        dr->tc.dWdy = frand(-0.2f, 0.2f);
        dr->tc.dS2dx = frand(-2.0f, 2.0f);
        dr->tc.dS2dy = frand(-2.0f, 2.0f);
        dr->tc.dT2dx = frand(-2.0f, 2.0f);
        dr->tc.dT2dy = frand(-2.0f, 2.0f);
        if (rhi(16) == 0) {
            dr->tc.dSdx = dr->tc.dSdy = 0.0f;
            dr->tc.dTdx = dr->tc.dTdy = 0.0f;
            dr->tc.dWdx = dr->tc.dWdy = 0.0f;
        }
    }
    if (dr->d.tex_en)
        setup_stage_desc(&dr->d.sh[0], &dr->tc.sd0);
    if (dr->d.sec_en)
        setup_stage_desc(&dr->d.sh[1], &dr->tc.sd1);
    /* one palette per draw, both stages (the device has a single global
       TEX_PALETTE); reference reads the host copy, GPU reads the arena */
    dr->pal_idx    = rhi(4);
    dr->tc.sd0.pal = g_texpals[dr->pal_idx];
    dr->tc.sd1.pal = g_texpals[dr->pal_idx];

    dr->nrows = 1 + (int) rhi(4);
    int base_py = (int) rhi((uint32_t) (py_lim - dr->nrows));

    if (g_py_pin >= 0) {
        base_py = g_py_pin;
        if (base_py > py_lim - dr->nrows)
            base_py = py_lim - dr->nrows;
    }
    for (int i = 0; i < dr->nrows; i++) {
        fz_row_t *rw = &dr->row[i];

        rw->py = base_py + i; /* distinct: the walk hands each row once */
        rw->e0 = (int64_t) (rng() % (uint32_t) area) - area / 8;
        rw->e1 = (int64_t) (rng() % (uint32_t) area) - area / 8;
        rw->e2 = area - rw->e0 - rw->e1 - (int64_t) rhi(4);
        rw->zline = (double) (int32_t) (rng() % 3000) / 2000.0 - 0.2;
        if (dr->d.fog_en && dr->d.fog_table_en
            && !dr->d.z_en && !dr->d.sten_on) {
            /* Z-off fog uses the ladder's serial double chain through
               both clamp endpoints and nonfinite values. The existing
               random span start selects probes without another RNG draw. */
            static const double depth[][2] = {
                { 0.0, 0.125 }, { 1.0, -0.125 },
                { -0.25, 0.125 }, { 1.25, -0.125 },
                { NAN, 0.001 }, { INFINITY, -INFINITY },
                { -INFINITY, INFINITY }, { 0.5, NAN },
                { 0x1p-1074, 0x1p-1074 }, { 0.5, 0x1p-54 },
                { 0x1.fffffffffffffp-1, 0.0 }, { 1.0 / 255.0, 0x1p-40 }
            };
            unsigned probe = (unsigned) dr->x0 % 16u;

            if (probe < sizeof(depth) / sizeof(depth[0])) {
                rw->zline = depth[probe][0];
                dr->dZdx = depth[probe][1];
            }
        }
    }
}

/* ------------------------------------------------------------------------
 * Segment machinery mirror (device capture + flush sort)
 * ---------------------------------------------------------------------- */

typedef struct fz_run_t {
    uint32_t span0, kernel;
    int      tuple;
    int      serial;
    int      sten, dead; /* folded S_STEN / C_DEAD */
    int      pymin, pymax;
    /* device mirror: exact byte ranges (hi exclusive, empty = lo>hi);
       c/z from spans (staged excluded), q from resident mip slots */
    uint32_t c0, c1, z0, z1, q0, q1;
} fz_run_t;

static uint32_t nspans, ntris, nruns, seg_px;
static fz_run_t runs[FZ_RUN_CAP];
static uint16_t py_of[FZ_SPAN_CAP];
/* host shadow of span byte ranges for the leveler (device bb_of mirror).
   32-bit fields: sx1 is a 14-bit register field, so (x1+1)*bpp reaches
   65536 at the top column and a u16 wraps that to 0 (empty range). */
static struct { uint32_t c0, c1, z0, z1; } bb_of[FZ_SPAN_CAP];
/* commutative-stencil class per span (device sctl_of/srm_of mirror) */
static uint32_t sctl_of[FZ_SPAN_CAP], srm_of[FZ_SPAN_CAP];
static uint64_t commut_exempt; /* alias exemptions taken (pairs packed) */

static void
bb_of_fill(uint32_t i, int32_t x0, int32_t x1, const fz_draw_t *dr)
{
    const rage128_draw_state_t *d  = &dr->d;
    uint32_t                    cb = (d->dst_dt == 6u) ? 4u : 2u;

    bb_of[i].c0 = (uint32_t) x0 * cb;
    bb_of[i].c1 = (uint32_t) (x1 + 1) * cb;
    bb_of[i].z0 = (uint32_t) x0 * (uint32_t) d->zbpp;
    bb_of[i].z1 = (uint32_t) (x1 + 1) * (uint32_t) d->zbpp;
    if (d->sten_on && d->wmask == 0 && !d->atest_en && !d->need_ck
        && !(d->z_en && d->z_wr) && d->sfn == 7
        && d->zpass_op <= 4 && d->zfail_op <= 4
        && (d->zpass_op == d->zfail_op || d->zpass_op == 0
            || d->zfail_op == 0)) {
        sctl_of[i] = 1u | (d->sfn << 4) | (d->sfail_op << 8)
                   | (d->zpass_op << 12) | (d->zfail_op << 16);
        srm_of[i]  = d->sref | (d->svmask << 8) | (d->swmask << 16);
    } else {
        sctl_of[i] = 0;
        srm_of[i]  = 0;
    }
}

static void
seg_reset(void)
{
    nspans = ntris = nruns = seg_px = 0;
}

/* tri-record fill, mirroring rage128_gpu_capture_span */
static void
fill_tri(seg_tri_t *tt, const fz_draw_t *dr)
{
    const rage128_draw_state_t *d  = &dr->d;
    const r3d_texctx_t         *tc = &dr->tc;

    memset(tt, 0, sizeof(*tt));
    tt->e0dxi = dr->e0dxi;
    tt->e1dxi = dr->e1dxi;
    tt->e2dxi = dr->e2dxi;
    memcpy(&tt->dZdx, &dr->dZdx, 8);
    tt->invs      = dr->invs;
    tt->lod_bias  = d->lod_bias;
    tt->texw0     = d->texw0;
    tt->texh0     = d->texh0;
    tt->texw1     = d->texw1;
    tt->texh1     = d->texh1;
    /* stencil-only draw: cell live, depth compare pinned to always-pass,
       depth field never written (mirrors the device capture) */
    tt->zfn       = d->z_en ? d->zfn : 7;
    tt->z_wr      = (d->z_en && d->z_wr) ? 1 : 0;
    tt->zbpp      = (uint32_t) d->zbpp;
    tt->zshift    = (uint32_t) d->zshift;
    if (d->sten_on) {
        tt->sten_ctl = 1u | (d->sfn << 4) | (d->sfail_op << 8)
                     | (d->zpass_op << 12) | (d->zfail_op << 16);
        tt->sten_rm  = d->sref | (d->svmask << 8) | (d->swmask << 16);
    }
    tt->dst_dt    = d->dst_dt;
    tt->wmask     = d->wmask;
    tt->dither    = d->dither ? 1 : 0;
    tt->persp     = (d->do_persp ? 1u : 0u)
        | ((d->sec_en && !d->tex_en) ? 2u : 0u);
    tt->pal_base  = dr->pal_idx * 256u;
    if (d->tex_en)
        tex_fmt_use[d->sh[0].dt & 15]++;
    if (d->sec_en)
        tex_fmt_use[d->sh[1].dt & 15]++;
    if (d->tex_en || d->sec_en)
        tex_persp_use[d->do_persp ? 1 : 0]++;
    if (d->sec_en && !d->tex_en)
        tex_seconly_use++;
    if (d->tex_en && d->sh[0].aone)
        tex_aone_use++;
    if (d->sec_en && d->sh[1].aone)
        tex_aone_use++;
    if (d->light_on)
        tex_light_use++;
    tt->spec_en   = d->spec_en ? 1 : 0;
    memcpy(tt->spa, dr->spa, sizeof(tt->spa));
    memcpy(tt->spb, dr->spb, sizeof(tt->spb));
    memcpy(tt->spc, dr->spc, sizeof(tt->spc));
    tt->fog_en    = d->fog_en ? 1 : 0;
    tt->ftab_en   = (d->fog_en && d->fog_table_en) ? 1 : 0;
    if (tt->ftab_en)
        memcpy(tt->fog_table, dr->fog_table, 256);
    tt->fga       = dr->fog[0];
    tt->fgb       = dr->fog[1];
    tt->fgc       = dr->fog[2];
    tt->fogr      = d->fogr;
    tt->fogg      = d->fogg;
    tt->fogb      = d->fogb;
    if (d->tex_en && d->need_ck) {
        tt->ck_ctl   = (d->ck3d_on ? 1u : 0u) | (d->ckc_on ? 2u : 0u)
                     | (d->ckfn == 3 ? 4u : 0u);
        tt->ck3d_clr = d->ck3d_clr;
        tt->ck3d_msk = d->ck3d_msk;
        tt->ckc_clr  = d->ckc_clr;
        tt->ckc_msk  = d->ckc_msk;
    }
    tt->atest_en  = d->atest_en ? 1 : 0;
    tt->atest_fn  = d->atest_fn;
    tt->atest_ref = d->atest_ref;
    tt->bsrc      = d->bsrc;
    tt->bdst      = d->bdst;
    tt->bfcn      = d->bfcn;
    tt->stip_en = d->stip_en ? 1 : 0;
    if (tt->stip_en)
        memcpy(tt->stipple, dr->stipple, sizeof(tt->stipple));
    tt->aux_cntl  = d->aux_on ? d->aux_cntl : 0;
    memcpy(tt->aux_x0, d->aux_x0, sizeof(tt->aux_x0));
    memcpy(tt->aux_x1, d->aux_x1, sizeof(tt->aux_x1));
    memcpy(tt->aux_y0, d->aux_y0, sizeof(tt->aux_y0));
    memcpy(tt->aux_y1, d->aux_y1, sizeof(tt->aux_y1));
    memcpy(tt->cc, d->cc, sizeof(tt->cc));
    memcpy(tt->vca, dr->vca, sizeof(tt->vca));
    memcpy(tt->vcb, dr->vcb, sizeof(tt->vcb));
    memcpy(tt->vcc, dr->vcc, sizeof(tt->vcc));
    tt->sta = tc->sta; tt->stb = tc->stb; tt->stc = tc->stc;
    tt->tta = tc->tta; tt->ttb = tc->ttb; tt->ttc = tc->ttc;
    tt->s2a = tc->s2a; tt->s2b = tc->s2b; tt->s2c = tc->s2c;
    tt->t2a = tc->t2a; tt->t2b = tc->t2b; tt->t2c = tc->t2c;
    tt->arhw = tc->arhw; tt->brhw = tc->brhw; tt->crhw = tc->crhw;
    tt->dSdx = tc->dSdx; tt->dSdy = tc->dSdy;
    tt->dTdx = tc->dTdx; tt->dTdy = tc->dTdy;
    tt->dWdx = tc->dWdx; tt->dWdy = tc->dWdy;
    tt->dS2dx = tc->dS2dx; tt->dS2dy = tc->dS2dy;
    tt->dT2dx = tc->dT2dx; tt->dT2dy = tc->dT2dy;
    /* The generator gives both stages one perspective setting and leaves
       SEC_SRC_SEL_W clear, the state every driver programs, so the
       secondary stage divides by the primary W: the interpreter's
       triangle setup then carries the vertex rhw as the stage's W and
       zero W2 gradients. */
    tt->a2rhw     = tc->arhw;
    tt->b2rhw     = tc->brhw;
    tt->c2rhw     = tc->crhw;
    tt->persp2    = d->do_persp ? 1 : 0;
    tt->sec_sel   = d->sec_sel ? 1 : 0;
    tt->need_lod2 = d->need_lod2 ? 1 : 0;
    tt->top0      = d->sh[0].top;
    tt->top1      = d->sh[1].top;
    for (int st = 0; st < 2; st++) {
        const r3d_stage_hdr_t *h = &d->sh[st];

        tt->st_cfg[st * 8 + 0] = h->dt | (h->aone ? 0x10u : 0u);
        tt->st_cfg[st * 8 + 1] = h->s3tc;
        tt->st_cfg[st * 8 + 2] = h->clamp_s;
        tt->st_cfg[st * 8 + 3] = h->clamp_t;
        tt->st_cfg[st * 8 + 4] = h->border;
        tt->st_cfg[st * 8 + 5] = h->minb;
        tt->st_cfg[st * 8 + 6] = h->mag;
        tt->st_cfg[st * 8 + 7] = h->mipdis ? 1 : 0;
        tt->comb_cfg[st * 8 + 0] = d->comb[st].comb;
        tt->comb_cfg[st * 8 + 1] = d->comb[st].fmsb;
        tt->comb_cfg[st * 8 + 2] = d->comb[st].cfac;
        tt->comb_cfg[st * 8 + 3] = d->comb[st].ifac;
        tt->comb_cfg[st * 8 + 4] = d->comb[st].comba;
        tt->comb_cfg[st * 8 + 5] = d->comb[st].afac;
        tt->comb_cfg[st * 8 + 6] = d->comb[st].ifaca;
    }
    /* the light word in stage 0's spare slot, as the device's capture
       packs it */
    tt->comb_cfg[7] = light_word(d);
    if (d->tex_en)
        for (int sl = 0; sl <= d->sh[0].top; sl++) {
            tt->slot0[sl * 4 + 0] = dr->tc.sd0.slot[sl].lw;
            tt->slot0[sl * 4 + 1] = dr->tc.sd0.slot[sl].lh;
            tt->slot0[sl * 4 + 2] = slot_base_pack(&dr->tc.sd0.slot[sl]);
            tt->slot0[sl * 4 + 3] = dr->tc.sd0.slot[sl].mask;
        }
    if (d->sec_en)
        for (int sl = 0; sl <= d->sh[1].top; sl++) {
            tt->slot1[sl * 4 + 0] = dr->tc.sd1.slot[sl].lw;
            tt->slot1[sl * 4 + 1] = dr->tc.sd1.slot[sl].lh;
            tt->slot1[sl * 4 + 2] = slot_base_pack(&dr->tc.sd1.slot[sl]);
            tt->slot1[sl * 4 + 3] = dr->tc.sd1.slot[sl].mask;
        }
}

static int
seg_clip_edge(int64_t e, int64_t dx, int32_t *lo, int32_t *hi)
{
    if (dx > 0 && e < 0) {
        uint64_t n = (uint64_t) (-(e + 1)) + 1u;
        uint64_t d = (uint64_t) dx;
        uint64_t q = n / d + (n % d != 0);

        if (q > (uint64_t) *hi)
            return 0;
        if ((int32_t) q > *lo)
            *lo = (int32_t) q;
    } else if (dx < 0) {
        uint64_t d, q;

        if (e < 0)
            return 0;
        d = (uint64_t) (-(dx + 1)) + 1u;
        q = (uint64_t) e / d;
        if (q < (uint64_t) *hi)
            *hi = (int32_t) q;
    } else if (e < 0)
        return 0;
    return *lo <= *hi;
}

static inline int
seg_edge32_span(int64_t e, int64_t dx, int32_t n)
{
    int64_t last = e + (int64_t) (n - 1) * dx;

    return e >= INT32_MIN && e <= INT32_MAX
        && last >= INT32_MIN && last <= INT32_MAX;
}

/* append one draw: tri record, run split at kernel change, spans chunked
   at 256 px with sequential double-stepping (device capture mirror) */
static void
seg_add_draw(const fz_draw_t *dr)
{
    seg_tri_t *tt = &((seg_tri_t *) b_tris.map)[ntris];

    fill_tri(tt, dr);
    /* the device's tuple key carries the stencil-live bit (sel[14]), so
       a folded draw splits the run when sten flips; unfolded draws run
       the uber's runtime selector and never split on it */
    int dsten = (dr->tuple >= 0 && dr->d.sten_on) ? 1 : 0;
    int ddead = (dr->tuple >= 0 && dr->d.wmask == 0 && !dr->d.atest_en
                 && !dr->d.need_ck) ? 1 : 0;

    if (nruns == 0 || runs[nruns - 1].kernel != (uint32_t) dr->kern
        || runs[nruns - 1].tuple != dr->tuple
        || runs[nruns - 1].serial != dr->serial
        || (dr->tuple >= 0
            && (runs[nruns - 1].sten != dsten
                || runs[nruns - 1].dead != ddead))) {
        fz_run_t *ru;

        if (nruns > 0 && runs[nruns - 1].kernel == (uint32_t) dr->kern)
            fold_splits++; /* run opened by a tuple/sten change alone */
        ru = &runs[nruns++];
        ru->span0  = nspans;
        ru->kernel = (uint32_t) dr->kern;
        ru->tuple  = dr->tuple;
        ru->serial = dr->serial;
        ru->sten   = dsten;
        ru->dead   = ddead;
        ru->pymin  = INT32_MAX;
        ru->pymax  = INT32_MIN;
        ru->c0 = ru->z0 = ru->q0 = 0xffffffffu;
        ru->c1 = ru->z1 = ru->q1 = 0;
    }
    /* device mirror: texture bytes this draw may sample, every resident
       mip slot; a mask-wrapping fetch goes full-range (the linear range
       cannot name the wrapped bytes) */
    {
        fz_run_t *ru = &runs[nruns - 1];

        for (int st = 0; st < 2; st++) {
            const r3d_stage_desc_t *sd = st ? &dr->tc.sd1 : &dr->tc.sd0;

            if (!(st == 0 ? dr->d.tex_en : dr->d.sec_en))
                continue;
            for (int sl = 0; sl <= dr->d.sh[st].top; sl++) {
                const struct r3d_slot_desc_t *so = &sd->slot[sl];
                uint32_t                      len;

                if (so->texbase != vram_ref)
                    continue;
                len = level_bytes(sd->dt, sd->s3tc, so->lw, so->lh);
                if ((uint64_t) so->base + len > (uint64_t) VRAM_SZ) {
                    ru->q0 = 0;
                    ru->q1 = 0xffffffffu;
                    break;
                }
                if (so->base < ru->q0) ru->q0 = so->base;
                if (so->base + len > ru->q1) ru->q1 = so->base + len;
            }
        }
    }
    for (int i = 0; i < dr->nrows; i++) {
        const fz_row_t *rw = &dr->row[i];
        double  zc = rw->zline;
        int64_t e0 = rw->e0, e1 = rw->e1, e2 = rw->e2;
        int32_t lo = 0, hi = dr->x1 - dr->x0;
        int32_t cx, rx1;

        if (!seg_clip_edge(e0, dr->e0dxi, &lo, &hi)
            || !seg_clip_edge(e1, dr->e1dxi, &lo, &hi)
            || !seg_clip_edge(e2, dr->e2dxi, &lo, &hi))
            continue;
        for (int32_t q = 0; q < lo; q++)
            zc += dr->dZdx;
        e0 += (int64_t) lo * dr->e0dxi;
        e1 += (int64_t) lo * dr->e1dxi;
        e2 += (int64_t) lo * dr->e2dxi;
        cx  = dr->x0 + lo;
        rx1 = dr->x0 + hi;

        while (cx <= rx1 && nspans < FZ_SPAN_CAP) {
            int32_t     cw = rx1 - cx + 1;
            seg_span_t *sp = &((seg_span_t *) b_spans.map)[nspans];

            if (cw > 256)
                cw = 256;
            sp->e0 = e0; sp->e1 = e1; sp->e2 = e2;
            memcpy(&sp->zline, &zc, 8);
            sp->x0   = cx;
            sp->x1   = cx + cw - 1;
            sp->py   = rw->py;
            sp->tri  = (int32_t) ntris;
            sp->drow    = g_c_stg
                            ? (((uint32_t) rw->py * ROW_PITCH) | 0x80000000u)
                            : g_c_off + (uint32_t) rw->py * ROW_PITCH;
            sp->zrow    = g_z_stg
                            ? (((uint32_t) rw->py * ROW_PITCH) | 0x80000000u)
                            : g_z_off + (uint32_t) rw->py * ROW_PITCH;
            sp->px_base = seg_px;
            sp->pad1    = seg_edge32_span(e0, dr->e0dxi, cw)
                       && seg_edge32_span(e1, dr->e1dxi, cw)
                       && seg_edge32_span(e2, dr->e2dxi, cw);
            /* table-fog spans take two ladder words per pixel */
            seg_px += (uint32_t) cw * ((dr->d.fog_en && dr->d.fog_table_en) ? 2u : 1u);
            py_of[nspans] = (uint16_t) rw->py;
            bb_of_fill(nspans, cx, cx + cw - 1, dr);
            nspans++;
            {
                fz_run_t *ru = &runs[nruns - 1];
                uint32_t  si = nspans - 1;

                if (rw->py < ru->pymin) ru->pymin = rw->py;
                if (rw->py > ru->pymax) ru->pymax = rw->py;
                if (!g_c_stg) {
                    uint32_t dro = g_c_off + (uint32_t) rw->py * ROW_PITCH;

                    if (dro + bb_of[si].c0 < ru->c0)
                        ru->c0 = dro + bb_of[si].c0;
                    if (dro + bb_of[si].c1 > ru->c1)
                        ru->c1 = dro + bb_of[si].c1;
                }
                if (!g_z_stg) {
                    uint32_t zro = g_z_off + (uint32_t) rw->py * ROW_PITCH;

                    if (zro + bb_of[si].z0 < ru->z0)
                        ru->z0 = zro + bb_of[si].z0;
                    if (zro + bb_of[si].z1 > ru->z1)
                        ru->z1 = zro + bb_of[si].z1;
                }
            }
            for (int q = 0; q < cw; q++)
                zc += dr->dZdx;
            e0 += (int64_t) cw * dr->e0dxi;
            e1 += (int64_t) cw * dr->e1dxi;
            e2 += (int64_t) cw * dr->e2dxi;
            cx += cw;
        }
    }
    ntris++;
}

typedef struct fz_disp_t {
    uint32_t rows_base, nrows, kernel;
    int      tuple;
    int      serial;
    int      sten, dead;
    int      pymin, pymax;
    uint32_t c0, c1, z0, z1, q0, q1;
} fz_disp_t;

/* Bench control: one span per batch isolates packing's timing effect
   on an identical workload. */
static int g_bench_nopack;

/* gate coverage: batches that actually packed >1 span, and batches cut
   short by an x overlap (the path that preserves draw order). A fuzz
   run that never hits both has not tested packing at all. */
static uint64_t bat_multi, bat_split, bat_nobar, uniform_rows;
static uint64_t row_mixed, row_dense;
static uint64_t disp_bar, disp_nobar, disp_texbar, disp_overlapbar;
static uint64_t disp_unionbar;

/* Pack one row's spans (already in draw order) into batches. A batch
   holds spans that are pairwise disjoint in x and together fit the 8
   lane-groups, so they shade in one pass; anything overlapping starts a
   new batch, which is what preserves draw order. Returns the batch
   count written at slots + nbat * 8. (device seg_batch_row mirror) */

/* Same-row spans may share a batch only when their color and z byte
   ranges are disjoint. Pixel-disjoint is not enough: a 32bpp draw and a
   16bpp draw on one row byte-alias at different x (lost-draw-order
   race, caught by this fuzzer), and mixed zbpp aliases the z row the
   same way. Ranges come from the bb_of host shadow, filled at span
   append. (device gpu_spans_alias mirror) */
static int
seg_spans_alias(uint32_t a, uint32_t b)
{
    if (sctl_of[a] && sctl_of[a] == sctl_of[b] && srm_of[a] == srm_of[b]) {
        commut_exempt++;
        return 0; /* commutative stencil pair: overlap is order-free */
    }
    if (bb_of[a].c0 < bb_of[b].c1 && bb_of[b].c0 < bb_of[a].c1)
        return 1;
    if (bb_of[a].z0 < bb_of[b].z1 && bb_of[b].z0 < bb_of[a].z1)
        return 1;
    return 0;
}
static uint32_t
seg_batch_row(uint32_t *slots, uint32_t nbat, const uint32_t *ord, uint32_t c)
{
    const seg_span_t *sp = (const seg_span_t *) b_spans.map;
    static uint32_t   lvl[FZ_SPAN_CAP], bylv[FZ_SPAN_CAP], cnt[FZ_SPAN_CAP + 1];
    uint32_t          base = nbat, nlv = 0, used = 0, inbat = 0;
    uint32_t          curlv = 0xffffffffu;

    {
        uint32_t nc = 0;

        for (uint32_t j = 0; j < c; j++)
            nc += (sctl_of[ord[j]] != 0);
        if (nc >= 2)
            row_dense++;
        if (nc && nc != c)
            row_mixed++;
    }
    if (c > 0 && !g_bench_nopack) {
        uint32_t s0 = sctl_of[ord[0]], r0 = srm_of[ord[0]];

        if (s0 != 0) {
            uint32_t j = 1;

            while (j < c && sctl_of[ord[j]] == s0 && srm_of[ord[j]] == r0)
                j++;
            if (j == c) {
                memset(lvl, 0, (size_t) c * sizeof(uint32_t));
                uniform_rows++;
                goto leveled;
            }
        }
    }
    for (uint32_t j = 0; j < c; j++) {
        uint32_t lv = 0;

        if (g_bench_nopack || c > BATCH_LEVEL_MAX) {
            lvl[j] = j;
            continue;
        }
        for (uint32_t t = 0; t < j; t++)
            if (seg_spans_alias(ord[j], ord[t]) && lvl[t] >= lv)
                lv = lvl[t] + 1;
        lvl[j] = lv;
    }
leveled:
    for (uint32_t j = 0; j < c; j++)
        if (lvl[j] + 1u > nlv)
            nlv = lvl[j] + 1u;

    memset(cnt, 0, (size_t) (nlv + 1) * sizeof(uint32_t));
    for (uint32_t j = 0; j < c; j++)
        cnt[lvl[j] + 1u]++;
    for (uint32_t L = 0; L < nlv; L++)
        cnt[L + 1u] += cnt[L];
    for (uint32_t j = 0; j < c; j++)
        bylv[cnt[lvl[j]]++] = j;

    for (uint32_t k = 0; k < c; k++) {
        uint32_t j    = bylv[k];
        uint32_t si   = ord[j];
        uint32_t need = ((uint32_t) (sp[si].x1 - sp[si].x0) + SLOT_W)
                        >> SLOT_SHIFT;
        uint32_t open = 0;

        if (used > 0 && (lvl[j] != curlv || used + need > BATCH_SLOTS)) {
            if (inbat > 1)
                bat_multi++;
            if (lvl[j] != curlv)
                bat_split++;
            nbat++;
            used  = 0;
            inbat = 0;
        }
        if (used == 0) {
            for (uint32_t q = 0; q < BATCH_SLOTS; q++)
                slots[nbat * BATCH_SLOTS + q] = 0xffffffffu;
            open = (lvl[j] != curlv);
            if (!open)
                bat_nobar++;
        }
        for (uint32_t q = 0; q < need; q++)
            slots[nbat * BATCH_SLOTS + used + q] = (si << 8) | (q * SLOT_W);
        slots[nbat * BATCH_SLOTS] |= open;
        used  += need;
        inbat++;
        curlv  = lvl[j];
    }
    if (used > 0) {
        if (inbat > 1)
            bat_multi++;
        nbat++;
    }
    return nbat - base;
}

/* fault knob: execute serial runs through the parallel sort/batch path
   anyway -- a constructed feedback case must then mismatch, proving
   both that the case carries real pixel-order feedback and that the
   serial path is what preserves it */
static int g_serial_strip;
/* The row-order fault uses reverse, single-row parallel dispatches with
   a barrier between them. The feedback fixture has one pixel per row,
   so each dispatch has one active invocation and no scheduling ambiguity. */
static int g_reverse_rows;

/* per-run counting sort + batch build (device flush mirror) */
static uint32_t
seg_sort(fz_disp_t *disp)
{
    uint32_t        *rows  = (uint32_t *) b_rows.map;
    uint32_t        *slots = (uint32_t *) b_order.map;
    static uint32_t  ord[FZ_SPAN_CAP];
    uint32_t         beg[MAX_PY + 130], cur[MAX_PY + 130];
    uint32_t         ndisp = 0, rows_used = 0, nbat = 0;

    for (uint32_t r = 0; r < nruns; r++) {
        uint32_t s0 = runs[r].span0;
        uint32_t s1 = (r + 1 < nruns) ? runs[r + 1].span0 : nspans;
        int      pymin = runs[r].pymin;
        int      span  = runs[r].pymax - pymin + 1;
        uint32_t nrows = 0, off = s0;

        if (s1 <= s0)
            continue;
        if (runs[r].serial && !g_serial_strip) {
            /* device mirror: rows pair repurposed as (first span, span
               count), one workgroup, no sort, no batching */
            rows[rows_used * 2u]      = s0;
            rows[rows_used * 2u + 1u] = s1 - s0;
            disp[ndisp].rows_base = rows_used;
            disp[ndisp].nrows     = 1;
            disp[ndisp].kernel    = runs[r].kernel;
            disp[ndisp].tuple     = -1;
            disp[ndisp].serial    = 1;
            disp[ndisp].sten      = 0;
            disp[ndisp].dead      = 0;
            disp[ndisp].pymin     = runs[r].pymin;
            disp[ndisp].pymax     = runs[r].pymax;
            disp[ndisp].c0 = runs[r].c0; disp[ndisp].c1 = runs[r].c1;
            disp[ndisp].z0 = runs[r].z0; disp[ndisp].z1 = runs[r].z1;
            disp[ndisp].q0 = runs[r].q0; disp[ndisp].q1 = runs[r].q1;
            ndisp++;
            rows_used += 1;
            continue;
        }
        memset(cur, 0, (size_t) span * sizeof(uint32_t));
        for (uint32_t i = s0; i < s1; i++)
            cur[py_of[i] - pymin]++;
        for (int row = 0; row < span; row++) {
            uint32_t c = cur[row];

            beg[row] = off;
            cur[row] = off;
            off += c;
        }
        for (uint32_t i = s0; i < s1; i++)
            ord[cur[py_of[i] - pymin]++] = i;
        for (int row = 0; row < span; row++) {
            uint32_t c = cur[row] - beg[row];

            if (!c)
                continue;
            rows[(rows_used + nrows) * 2u]      = nbat;
            rows[(rows_used + nrows) * 2u + 1u] =
                seg_batch_row(slots, nbat, &ord[beg[row]], c);
            nbat += rows[(rows_used + nrows) * 2u + 1u];
            nrows++;
        }
        disp[ndisp].rows_base = rows_used;
        disp[ndisp].nrows     = nrows;
        disp[ndisp].kernel    = runs[r].kernel;
        disp[ndisp].tuple     = runs[r].tuple;
        disp[ndisp].serial    = 0;
        disp[ndisp].sten      = runs[r].sten;
        disp[ndisp].dead      = runs[r].dead;
        disp[ndisp].pymin     = runs[r].pymin;
        disp[ndisp].pymax     = runs[r].pymax;
        disp[ndisp].c0 = runs[r].c0; disp[ndisp].c1 = runs[r].c1;
        disp[ndisp].z0 = runs[r].z0; disp[ndisp].z1 = runs[r].z1;
        disp[ndisp].q0 = runs[r].q0; disp[ndisp].q1 = runs[r].q1;
        ndisp++;
        rows_used += nrows;
    }
    return ndisp;
}

/* device rng_hit mirror: half-open byte-range intersection */
static inline int
rng_isect(uint32_t lo, uint32_t hi, uint32_t a, uint32_t b)
{
    return hi > lo && b > a && a < hi && b > lo;
}

/* z-ladder pre-pass + barrier (device gpu_dispatch mirror) */
static void
rec_lad_prepass_nobar(void)
{
    uint32_t pcv[2] = { nspans, 0 };

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, r_lad_pipe);
    vkCmdPushConstants(cb, r_plyt, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, pcv);
    vkCmdDispatch(cb, (nspans + 7u) / 8u, 1, 1);
}

/* The production ladder must match the interpreter's rounded double
   chain across binade floors, including both packed table-fog words.
   Exact increments and runs with no further steps cover the endpoint guards. */
static int
ladder_check(void)
{
    static const struct {
        const char *name;
        double      z, dz;
        int         n;
    } vectors[] = {
        { "floor-one",       0x1.0000000000003p+0, -0x1.6p-52, 8 },
        { "floor-half",      0x1.0000000000003p-1, -0x1.6p-53, 8 },
        { "mirror-one",     -0x1.0000000000003p+0,  0x1.6p-52, 8 },
        { "mirror-half",    -0x1.0000000000003p-1,  0x1.6p-53, 8 },
        { "exact-one",       0x1.0000000000003p+0, -0x1p-52,   8 },
        { "exact-half",      0x1.0000000000003p-1, -0x1p-53,   8 },
        { "exact-mirror",   -0x1.0000000000003p-1,  0x1p-53,   8 },
        { "zero-run",        0x1.0000000000002p-1, -0x1.ap-53, 8 },
        { "lane-before",     0x1.000000000001fp-1, -0x1.6p-53, 65 },
        { "lane-at",         0x1.0000000000020p-1, -0x1.6p-53, 65 },
        { "lane-after",      0x1.0000000000021p-1, -0x1.6p-53, 65 },
        { "lane-mirror",    -0x1.0000000000020p-1,  0x1.6p-53, 65 },
        { "lane-exact",      0x1.0000000000020p-1, -0x1p-53,   65 },
        { "lane-second",     0x1.0000000000040p-1, -0x1.6p-53, 256 },
        /* Serial-fallback operand classes: constant positive infinity,
           overflow from the largest finite double, negative infinity,
           NaN, and a chain through subnormals to zero and below. */
        { "inf-chain",       INFINITY,               INFINITY,   8 },
        { "inf-step",        0x1.fffffffffffffp+1023, 0x1.fffffffffffffp+1023, 8 },
        { "neg-inf-chain",  -INFINITY,               0x1p-3,     8 },
        { "nan-chain",       NAN,                    0x1p-3,     8 },
        { "nan-step",        0x1p-1,                 NAN,        8 },
        { "sub-chain",       0x1p-1022,             -0x1p-1023,  8 },
    };
    enum { NV = sizeof(vectors) / sizeof(vectors[0]) };
    uint32_t    expected[NV * 4 * 256 * 2];
    uint32_t   *out = b_lad.map;
    seg_span_t *sp = b_spans.map;
    seg_tri_t  *tt = b_tris.map;
    uint32_t    words = 0;
    int         bad = 0;

    nspans = 0;
    for (int i = 0; i < NV; i++) {
        for (int bpp = 2; bpp <= 4; bpp += 2) {
            uint32_t zmax = bpp == 4 ? 16777215u : 65535u;

            for (int fog = 0; fog <= 1; fog++) {
                double z = vectors[i].z;

                memset(&sp[nspans], 0, sizeof(sp[nspans]));
                memset(&tt[nspans], 0, sizeof(tt[nspans]));
                memcpy(&sp[nspans].zline, &z, sizeof(z));
                memcpy(&tt[nspans].dZdx, &vectors[i].dz, sizeof(z));
                sp[nspans].x1 = vectors[i].n - 1;
                sp[nspans].tri = (int32_t) nspans;
                sp[nspans].px_base = words;
                tt[nspans].zbpp = (uint32_t) bpp;
                tt[nspans].ftab_en = (uint32_t) fog;
                nspans++;
                for (int q = 0; q < vectors[i].n; q++) {
                    double zc = z;

                    if (!(zc > 0.0)) zc = 0.0;
                    if (zc > 1.0) zc = 1.0;
                    double zq = zc * (double) zmax + 0.5;
                    if (zq > (double) zmax) zq = (double) zmax;
                    uint32_t depth = (uint32_t) zq;
                    uint64_t fx = fog ? (uint64_t) (zc * 255.0 * 4294967296.0) : 0;

                    expected[words++] = depth | ((uint32_t) (fx >> 32) << 24);
                    if (fog)
                        expected[words++] = (uint32_t) fx;
                    z += vectors[i].dz;
                }
            }
        }
    }
    memset(out, 0xa5, (words + 1u) * sizeof(*out));
    VK_CHECK(vkResetCommandBuffer(cb, 0));
    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cb, &cbbi));
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, r_plyt, 0, 1, &r_dset, 0, NULL);
    rec_lad_prepass_nobar();
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    VK_CHECK(vkEndCommandBuffer(cb));
    submit_wait();

    for (uint32_t s = 0; s < nspans; s++) {
        uint32_t stride = tt[s].ftab_en ? 2u : 1u;
        int      row_bad = 0;

        for (int q = 0; q <= sp[s].x1; q++) {
            for (uint32_t w = 0; w < stride; w++) {
                uint32_t at = sp[s].px_base + stride * (uint32_t) q + w;

                if (out[at] == expected[at])
                    continue;
                if (bad++ < 10)
                    printf("ladder %s bpp=%u fog=%u q=%d word=%u ref=%08x got=%08x\n",
                           vectors[s / 4u].name, tt[s].zbpp, tt[s].ftab_en,
                           q, w, expected[at], out[at]);
                row_bad++;
            }
        }
        printf("ladder %s bpp=%u fog=%u: %d mismatches\n",
               vectors[s / 4u].name, tt[s].zbpp, tt[s].ftab_en, row_bad);
    }
    if (out[words] != 0xa5a5a5a5u) {
        printf("ladder guard overwritten: %08x\n", out[words]);
        bad++;
    }
    printf("production ladder: %u spans, %u words, %d mismatches\n", nspans, words, bad);
    nspans = 0;
    return bad;
}

static void
rec_lad_prepass(void)
{
    /* timing knob: an empty pre-pass (every thread exits) isolates the
       dispatch + barrier launch floor from the walk itself */
    uint32_t        pcv[2] = { getenv("GPUTRI_PREPASS_EMPTY") ? 0u : nspans, 0 };
    VkMemoryBarrier mb     = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, r_lad_pipe);
    vkCmdPushConstants(cb, r_plyt, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, pcv);
    vkCmdDispatch(cb, (nspans + 7u) / 8u, 1, 1);
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         1, &mb, 0, NULL, 0, NULL);
}

/* dependency-chain dispatches, one submit, one fence (device mirror) */
static void
seg_exec(const fz_disp_t *disp, uint32_t ndisp)
{
    int chain_lo = INT32_MAX, chain_hi = INT32_MIN;
    int prev_lo = INT32_MAX, prev_hi = INT32_MIN;
    uint32_t ch_c0 = 0xffffffffu, ch_c1 = 0;
    uint32_t ch_z0 = 0xffffffffu, ch_z1 = 0;
    uint32_t ch_q0 = 0xffffffffu, ch_q1 = 0;

    VK_CHECK(vkResetCommandBuffer(cb, 0));
    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cb, &cbbi));
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, r_plyt, 0, 1, &r_dset, 0, NULL);
    rec_lad_prepass();
    for (uint32_t i = 0; i < ndisp; i++) {
        uint32_t   pcv[2] = { VRAM_MASK, disp[i].rows_base };
        VkPipeline p      = disp[i].serial
            ? r_pipes_serial[disp[i].kernel]
            : (disp[i].tuple >= 0
                   ? tpool_pipe(disp[i].tuple, (int) disp[i].kernel,
                                disp[i].sten, disp[i].dead)
                   : r_pipes[disp[i].kernel]);
        if (i == 0) {
            chain_lo  = prev_lo = disp[i].pymin;
            chain_hi  = prev_hi = disp[i].pymax;
            ch_c0 = disp[i].c0; ch_c1 = disp[i].c1;
            ch_z0 = disp[i].z0; ch_z1 = disp[i].z1;
            ch_q0 = disp[i].q0; ch_q1 = disp[i].q1;
        } else {
            int overlap = disp[i].pymin <= chain_hi && chain_lo <= disp[i].pymax;
            int prev_overlap = disp[i].pymin <= prev_hi && prev_lo <= disp[i].pymax;
            /* device mirror: byte-exact texture hazard vs the chain
               union (WAR: chain sampled bytes this run stores; RAW:
               this run samples bytes the chain stored). Row overlap
               stays the store-vs-store order guard; the union, not
               only the previous run, prevents run i+2 slipping past
               run i. */
            int tex_bar = rng_isect(ch_q0, ch_q1, disp[i].c0, disp[i].c1)
                       || rng_isect(ch_q0, ch_q1, disp[i].z0, disp[i].z1)
                       || rng_isect(ch_c0, ch_c1, disp[i].q0, disp[i].q1)
                       || rng_isect(ch_z0, ch_z1, disp[i].q0, disp[i].q1);

            if (tex_bar || overlap) {
                VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

                mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT
                                 | VK_ACCESS_SHADER_WRITE_BIT;
                mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
                                 | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                     1, &mb, 0, NULL, 0, NULL);
                chain_lo  = disp[i].pymin;
                chain_hi  = disp[i].pymax;
                ch_c0 = disp[i].c0; ch_c1 = disp[i].c1;
                ch_z0 = disp[i].z0; ch_z1 = disp[i].z1;
                ch_q0 = disp[i].q0; ch_q1 = disp[i].q1;
                disp_bar++;
                if (tex_bar)
                    disp_texbar++;
                else {
                    disp_overlapbar++;
                    if (!prev_overlap)
                        disp_unionbar++;
                }
            } else {
                if (disp[i].pymin < chain_lo) chain_lo = disp[i].pymin;
                if (disp[i].pymax > chain_hi) chain_hi = disp[i].pymax;
                if (disp[i].c0 < ch_c0) ch_c0 = disp[i].c0;
                if (disp[i].c1 > ch_c1) ch_c1 = disp[i].c1;
                if (disp[i].z0 < ch_z0) ch_z0 = disp[i].z0;
                if (disp[i].z1 > ch_z1) ch_z1 = disp[i].z1;
                if (disp[i].q0 < ch_q0) ch_q0 = disp[i].q0;
                if (disp[i].q1 > ch_q1) ch_q1 = disp[i].q1;
                disp_nobar++;
            }
            prev_lo = disp[i].pymin;
            prev_hi = disp[i].pymax;
        }

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p);
        vkCmdPushConstants(cb, r_plyt, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, pcv);
        if (g_reverse_rows) {
            for (uint32_t row = disp[i].nrows; row > 0; row--) {
                VkMemoryBarrier rb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

                rb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                rb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                     1, &rb, 0, NULL, 0, NULL);
                pcv[1] = disp[i].rows_base + row - 1;
                vkCmdPushConstants(cb, r_plyt, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, pcv);
                vkCmdDispatch(cb, 1, 1, 1);
            }
        } else
            vkCmdDispatch(cb, disp[i].nrows, 1, 1);
    }
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    VK_CHECK(vkEndCommandBuffer(cb));
    submit_wait();
}

/* sequential reference replay of one draw (full spans, un-chunked) */
static int g_ref_wrote;

static void
ref_draw(const fz_draw_t *dr)
{
    r128_jit_tri_t t;

    memset(&t, 0, sizeof(t));
    t.vram      = vram_ref;
    t.vram_mask = VRAM_MASK;
    if (g_c_stg) {
        t.cptr   = cstage_ref;
        t.c_base = g_c_off;
        t.c_lim  = CZSTG_SZ;
    }
    if (g_z_stg) {
        t.zptr   = zstage_ref;
        t.z_base = g_z_off;
        t.z_lim  = CZSTG_SZ;
    }
    t.x0        = dr->x0;
    t.x1        = dr->x1;
    t.e0dxi     = dr->e0dxi;
    t.e1dxi     = dr->e1dxi;
    t.e2dxi     = dr->e2dxi;
    t.invs      = dr->invs;
    t.dZdx      = dr->dZdx;
    memcpy(t.vca, dr->vca, sizeof(t.vca));
    memcpy(t.vcb, dr->vcb, sizeof(t.vcb));
    memcpy(t.vcc, dr->vcc, sizeof(t.vcc));
    memcpy(t.spa, dr->spa, sizeof(t.spa));
    memcpy(t.spb, dr->spb, sizeof(t.spb));
    memcpy(t.spc, dr->spc, sizeof(t.spc));
    memcpy(t.fog, dr->fog, sizeof(t.fog));
    t.fog_table = dr->fog_table;
    t.texctx = (void *) &dr->tc;
    for (int i = 0; i < dr->nrows; i++) {
        const fz_row_t *rw = &dr->row[i];
        uint64_t rr = ref_span(&t, &dr->d, rw->e0, rw->e1, rw->e2, rw->zline,
                               g_c_off + (uint32_t) rw->py * ROW_PITCH,
                               g_z_off + (uint32_t) rw->py * ROW_PITCH,
                               rw->py, dr->stipple);

        if ((uint32_t) rr != 0xffffffffu)
            g_ref_wrote = 1;
    }
}

/* A drow probe needs a visible store, not merely a covered pixel. Seed
   its intended color cells before copying the common image to the GPU.
   Each fixture has a seeded first cell that no shifted color or Z
   store reaches. Black or white RGB changes it. Resident cell bases
   wrap once, bytes stay linear into the guard, and staged offsets do not wrap. */
static void
probe_seed_color(const fz_draw_t *dr, int nd)
{
    for (int i = 0; i < nd; i++) {
        if (!dr[i].probe_cinit)
            continue;
        for (int row = 0; row < dr[i].nrows; row++)
            for (int x = dr[i].x0; x <= dr[i].x1; x++) {
                uint32_t off = (uint32_t) dr[i].row[row].py * ROW_PITCH
                             + (uint32_t) x * (uint32_t) dr[i].d.bpp;
                uint8_t *cell = g_c_stg ? cstage_ref + off
                    : vram_ref + ((g_c_off + off) & VRAM_MASK);

                memset(cell, 0x55, (size_t) dr[i].d.bpp);
            }
    }
}

/* one full case: random vram both sides, segment build + GPU exec,
   reference replay, byte compare (vram + guard). fault 1 corrupts every
   span's drow; fault 2 zeroes stencil writes; fault 3 strips serial
   runs onto the parallel path; fault 4 also forces reverse row order. */
static uint64_t cases_run, px_rows_run;

/* last case's dst formats, so a mismatch dump can name the format that
   produced it instead of guessing from the address alone */
static uint32_t g_dbg_dt[8], g_dbg_bpp[8];
static int      g_dbg_nd;

static int
run_case(const fz_draw_t *dr, int nd, int fault)
{
    fz_disp_t disp[FZ_RUN_CAP];
    uint32_t  ndisp;

    for (uint32_t k = 0; k < VRAM_SZ + 4; k += 4)
        *(uint32_t *) &vram_ref[k] = rng();
    for (uint32_t k = 0; k < STAGE_SZ + 4; k += 4)
        *(uint32_t *) &stage_ref[k] = rng();
    memcpy(b_stage.map, stage_ref, STAGE_SZ + 4);
    /* Seed color/depth arenas with a separate generator so their contents
       do not shift the main random stream or later case inputs. */
    {
        uint32_t cz = (uint32_t) cases_run * 2654435761u + 0x169u;

        for (uint32_t k = 0; k < CZSTG_SZ + 4; k += 4) {
            cz = cz * 1664525u + 1013904223u;
            *(uint32_t *) &cstage_ref[k] = cz;
            cz = cz * 1664525u + 1013904223u;
            *(uint32_t *) &zstage_ref[k] = cz;
        }
    }
    probe_seed_color(dr, nd);
    memcpy(b_vram.map, vram_ref, VRAM_SZ + 4);
    memcpy(vram_orig, vram_ref, VRAM_SZ + 4);
    memcpy(b_cstage.map, cstage_ref, CZSTG_SZ + 4);
    memcpy(b_zstage.map, zstage_ref, CZSTG_SZ + 4);

    seg_reset();
    g_dbg_nd = nd < 8 ? nd : 8;
    for (int i = 0; i < g_dbg_nd; i++) {
        g_dbg_dt[i]  = dr[i].d.dst_dt;
        g_dbg_bpp[i] = (uint32_t) dr[i].d.bpp;
    }
    for (int i = 0; i < nd; i++)
        seg_add_draw(&dr[i]);
    if (fault == 1)
        for (uint32_t i = 0; i < nspans; i++)
            ((seg_span_t *) b_spans.map)[i].drow += 4;
    else if (fault == 2) {
        /* stencil fault: zero the GPU-side write mask so the kernel's
           stencil updates go dark while the reference's stand */
        seg_tri_t *tt = (seg_tri_t *) b_tris.map;

        for (uint32_t i = 0; i < ntris; i++)
            if (tt[i].sten_ctl & 1u)
                tt[i].sten_rm ^= 0xff0000u;
    }
    g_serial_strip = (fault == 3 || fault == 4);
    ndisp = seg_sort(disp);
    g_serial_strip = 0;
    g_reverse_rows = (fault == 4);
    if (ndisp)
        seg_exec(disp, ndisp);
    g_reverse_rows = 0;

    g_ref_wrote = 0;
    for (int i = 0; i < nd; i++) {
        ref_draw(&dr[i]);
        px_rows_run += dr[i].nrows;
    }
    cases_run++;
    /* the stage arena is read-only for the kernel: any write there is an
       address bug even if vram matches */
    return memcmp(b_vram.map, vram_ref, VRAM_SZ + 4) != 0
        || memcmp(b_stage.map, stage_ref, STAGE_SZ + 4) != 0
        || memcmp(b_cstage.map, cstage_ref, CZSTG_SZ + 4) != 0
        || memcmp(b_zstage.map, zstage_ref, CZSTG_SZ + 4) != 0;
}

static void
dump_mismatch(const char *tag, int kern, int it)
{
    const uint8_t *gv = b_vram.map;

    uint32_t nbad = 0, shown = 0;

    printf("MISMATCH %s kern=%d it=%d  dst:", tag, kern, it);
    for (int i = 0; i < g_dbg_nd; i++)
        printf(" dt%u/bpp%u", g_dbg_dt[i], g_dbg_bpp[i]);
    printf("\n");
    /* Classify where the divergence lands, not just that it happened: a
       byte inside the color rows is a pack/mask bug, inside the z rows a
       depth bug, and anywhere else means the address itself is wrong. */
    for (uint32_t k = 0; k < VRAM_SZ + 4; k++)
        if (gv[k] != vram_ref[k]) {
            nbad++;
            if (shown < 4) {
                const char *where = "OTHER(addr bug)";
                uint32_t    rel   = 0;

                if (k >= g_c_off && k < g_c_off + 256u * ROW_PITCH) {
                    where = "colour";
                    rel   = k - g_c_off;
                } else if (k >= g_z_off && k < g_z_off + 256u * ROW_PITCH) {
                    where = "z";
                    rel   = k - g_z_off;
                }
                printf("  vram[%06x]: gpu=%02x ref=%02x orig=%02x"
                       " wrote=%s  %s rel=%06x (row=%u byte=%u)\n",
                       k, gv[k], vram_ref[k], vram_orig[k],
                       gv[k] != vram_orig[k]
                           ? (vram_ref[k] != vram_orig[k] ? "BOTH" : "GPU-only")
                           : "REF-only",
                       where, rel, rel / ROW_PITCH, rel % ROW_PITCH);
                shown++;
            }
        }
    {
        const uint8_t *gc = b_cstage.map;
        const uint8_t *gz = b_zstage.map;

        for (uint32_t k = 0; k < CZSTG_SZ + 4; k++) {
            if (gc[k] != cstage_ref[k]) {
                nbad++;
                if (shown < 8) {
                    printf("  cstg[%06x]: gpu=%02x ref=%02x (row=%u byte=%u)\n",
                           k, gc[k], cstage_ref[k], k / ROW_PITCH,
                           k % ROW_PITCH);
                    shown++;
                }
            }
            if (gz[k] != zstage_ref[k]) {
                nbad++;
                if (shown < 8) {
                    printf("  zstg[%06x]: gpu=%02x ref=%02x (row=%u byte=%u)\n",
                           k, gz[k], zstage_ref[k], k / ROW_PITCH,
                           k % ROW_PITCH);
                    shown++;
                }
            }
        }
    }
    printf("  %u differing bytes (c_off=%06x z_off=%06x cstg=%d zstg=%d)\n",
           nbad, g_c_off, g_z_off, g_c_stg, g_z_stg);
    {
        /* resolve the written extent both ways and diff the labels: if the
           two ranges are the same size but offset, the address math differs
           by a constant; if they differ in size, the stride does */
        uint32_t glo = 0xffffffffu, ghi = 0, rlo = 0xffffffffu, rhi_ = 0;

        for (uint32_t k = 0; k < VRAM_SZ + 4; k++) {
            if (gv[k] != vram_orig[k]) {
                if (k < glo) glo = k;
                if (k > ghi) ghi = k;
            }
            if (vram_ref[k] != vram_orig[k]) {
                if (k < rlo) rlo = k;
                if (k > rhi_) rhi_ = k;
            }
        }
        printf("  wrote-extent gpu=[%06x..%06x] ref=[%06x..%06x] delta_lo=%d\n",
               glo, ghi, rlo, rhi_, (int) glo - (int) rlo);
    }
    {
        const seg_span_t *sp = (const seg_span_t *) b_spans.map;

        for (uint32_t i = 0; i < nspans && i < 16; i++)
            printf("  span%u: drow=%06x zrow=%06x py=%d x=%d..%d tri=%d\n",
                   i, sp[i].drow, sp[i].zrow, sp[i].py, sp[i].x0, sp[i].x1,
                   sp[i].tri);
    }
    {
        const seg_tri_t *tt = (const seg_tri_t *) b_tris.map;

        for (uint32_t i = 0; i < ntris; i++)
            printf("  tri%u: dt=%u dith=%u wm=%08x zfn=%u zwr=%u zbpp=%u "
                   "zshift=%u sten=%05x/%06x atest=%u/%u/%u aux=%02x\n",
                   i, tt[i].dst_dt, tt[i].dither, tt[i].wmask, tt[i].zfn,
                   tt[i].z_wr, tt[i].zbpp, tt[i].zshift, tt[i].sten_ctl,
                   tt[i].sten_rm, tt[i].atest_en, tt[i].atest_fn,
                   tt[i].atest_ref, tt[i].aux_cntl);
    }
}

/* ------------------------------------------------------------------------
 * Fuzz drivers
 * ---------------------------------------------------------------------- */

static uint64_t fails;

/* Per-kernel coverage: a kernel whose cases never wrote a pixel is not
   actually covered, however many cases it ran. */
static uint64_t kern_cases[N_KERNELS], kern_wrote[N_KERNELS];

/* Keep every specialization axis live without inheriting generator
   state. One pixel has weights (1,0,0), no rejection, full color writes,
   and always-pass depth on Z kernels. Both texture stages use resident 1x1
   slots; LOD kernels compute LOD zero and sample their only level.
   Zero vertex fog makes RGB black after any combine or folded lighting
   result. Blend kernels use factors one and zero, so destination contents cannot
   hide the store. The seeded first cell differs even if alpha varies. */
static void
mk_probe_draw(fz_draw_t *dr, int kern)
{
    memset(dr, 0, sizeof(*dr));
    dr->kern = kern;
    dr->tuple = -1;
    dr->probe_cinit = 1;
    dr->d.draw_ok = 1;
    dr->d.dst_dt = 6;
    dr->d.bpp = 4;
    dr->d.wmask = 0xffffffffu;
    dr->d.z_en = kern_axes[kern].z;
    dr->d.z_wr = kern_axes[kern].z;
    dr->d.zfn = 7;
    dr->d.zbpp = 4;
    dr->d.zmax = 0xffffff;
    dr->d.zrowpx = ROW_PITCH / 4;
    dr->d.alpha_en = kern_axes[kern].blend;
    dr->d.bsrc = 1;
    dr->d.bdst = 0;
    dr->d.fog_en = 1;
    dr->d.tex_en = kern_axes[kern].tex > 0;
    dr->d.sec_en = kern_axes[kern].tex == 2;
    dr->d.need_lod = dr->d.need_lod2 = kern_axes[kern].lod;
    dr->d.texw0 = dr->d.texh0 = dr->d.texw1 = dr->d.texh1 = 1.0f;
    for (int st = 0; st < kern_axes[kern].tex; st++) {
        r3d_stage_hdr_t *h = &dr->d.sh[st];
        r3d_stage_desc_t *sd = st ? &dr->tc.sd1 : &dr->tc.sd0;

        h->dt = sd->dt = 6;
        h->minb = sd->minb = kern_axes[kern].lod ? 2 : 0;
        sd->pal = g_texpals[0];
        sd->slot_valid = 1;
        sd->slot[0].texbase = vram_ref;
        sd->slot[0].lw = sd->slot[0].lh = 1;
        sd->slot[0].mask = VRAM_MASK;
        sd->slot[0].base = 0x100u + (uint32_t) st * 0x100u;
    }
    dr->tc.dSdx = dr->tc.dS2dx = 1.0f;
    dr->vca[3] = dr->vcb[3] = 1.0f;
    dr->invs = 1.0f;
    dr->nrows = 1;
    dr->row[0].e0 = 1;
}

static void
fuzz_state(int kern, int iters)
{
    fz_draw_t dr[3];
    int       texn = r128_gpu_variants[kern].tex;

    g_c_off = C_BASE;
    g_z_off = Z_BASE;
    g_commut_tmpl.on = 0; /* per-kernel driver: no shared template */
    for (int it = 0; it < iters; it++) {
        int nd = (it & 7) == 0 ? 3 : 1 + (int) rhi(3);

        for (int i = 0; i < nd; i++) {
            mk_draw(&dr[i], kern, FUZZ_PY);
            /* One segment mixes empty, full and random captured patterns;
               keeping the kernel fixed exercises pattern changes without
               relying on a dispatch split to preserve the snapshot. */
            if ((it & 7) == 0) {
                dr[i].d.stip_en = 1;
                for (int k = 0; k < 32; k++)
                    dr[i].stipple[k] = i == 0 ? 0 : i == 1 ? UINT32_MAX : rng();
            }
            /* half the textured draws run the folded pipeline for a
               pool tuple (state pinned to match); mixing folded,
               unfolded and different tuples inside one case exercises
               the tuple run split */
            if (texn > 0 && (it & 7) != 0 && rhi(2) == 0) {
                apply_tuple(&dr[i], tpool_take(kern));
                fold_cases++;
            }
        }
        if (run_case(dr, nd, 0)) {
            fails++;
            if (fails <= 5)
                dump_mismatch("state", kern, it);
        }
        kern_cases[kern]++;
        if (g_ref_wrote)
            kern_wrote[kern]++;
    }

    /* Staged color/depth cases save and restore the random stream so later
       cases retain their inputs. Dither stays off so the
       comparison isolates staged addressing. */
    {
        uint32_t saved = rngs, xsaved = xrngs;

        /* Presence of GPUTRI_STG_RESIDENT runs the identical
           staged-block case content with staging forced off (same rng
           stream) -- a fail that persists resident exonerates staging */
        int stg_off = getenv("GPUTRI_STG_RESIDENT") != NULL;

        for (int it = 0; it < iters / 4; it++) {
            int nd = 1 + (int) rhi(3);

            g_c_stg = !stg_off && (it % 3) != 2;
            g_z_stg = !stg_off && kern_axes[kern].z && (it % 3) != 0;
            for (int i = 0; i < nd; i++) {
                mk_draw(&dr[i], kern, FUZZ_PY);
                dr[i].d.dither = 0;
            }
            if (run_case(dr, nd, 0)) {
                fails++;
                if (fails <= 5)
                    dump_mismatch("staged", kern, it);
            }
            cstg_cases += g_c_stg != 0;
            zstg_cases += g_z_stg != 0;
        }
        g_c_stg = 0;
        g_z_stg = 0;
        rngs  = saved;
        xrngs = xsaved;
    }

    /* Fault probes restore both random streams and the tuple pool's
       lazy-population cursor, so later coverage draws consume the same
       generator stream as if the probes were absent. */
    uint32_t saved = rngs, xsaved = xrngs;
    int psaved = ntpool;

    for (int fold = 0; fold < (texn > 0 ? 2 : 1); fold++) {
        mk_probe_draw(&dr[0], kern);
        if (fold)
            apply_tuple(&dr[0], tpool_take(kern));
        if (run_case(dr, 1, 0)) {
            fails++;
            dump_mismatch(fold ? "prefault-fold" : "prefault", kern, 0);
            goto probes_done;
        }
        if (g_ref_wrote && run_case(dr, 1, 1)) {
            printf("kernel %d (%s%s): fault injection detected OK\n",
                   kern, r128_gpu_variants[kern].name,
                   fold ? ", folded" : "");
            if (fold)
                fold_faults++;
        } else {
            printf("kernel %d%s: FAULT INJECTION NOT DETECTED\n", kern,
                   fold ? " (folded)" : "");
            fails++;
        }
    }

    /* The same visible first-cell store tests bit-31 drow addressing.
       Z kernels also keep a staged Z target live; all arenas and guards
       remain in the normal full-byte comparison. */
    g_c_stg = 1;
    g_z_stg = kern_axes[kern].z;
    mk_probe_draw(&dr[0], kern);
    if (run_case(dr, 1, 0)) {
        fails++;
        dump_mismatch("prefault-staged", kern, 0);
        goto probes_done;
    }
    if (g_ref_wrote && run_case(dr, 1, 1)) {
        printf("kernel %d (%s): staged-RT fault injection detected OK\n",
               kern, r128_gpu_variants[kern].name);
    } else {
        printf("kernel %d: STAGED-RT FAULT INJECTION NOT DETECTED\n", kern);
        fails++;
    }
    g_c_stg = 0;
    g_z_stg = 0;

    /* Each Z kernel tests both stencil byte positions. One covered
       pixel, always-pass tests, invert, and a full stencil mask change every
       possible initial stencil byte. Depth writeback preserves that
       byte, so suppressing the stencil write necessarily differs. */
    if (kern_axes[kern].z) {
        int ok = 1;

        for (int pos = 0; pos < 2; pos++) {
            rage128_draw_state_t *d = &dr[0].d;

            mk_probe_draw(&dr[0], kern);
            dr[0].probe_cinit = 0;
            d->zshift = pos ? 8 : 0;
            d->sten_on = 1;
            d->sshift = pos ? 0 : 24;
            d->sfn = 7;
            d->sfail_op = d->zpass_op = d->zfail_op = 5;
            d->svmask = d->swmask = 0xff;
            if (run_case(dr, 1, 0)) {
                fails++;
                dump_mismatch("presten", kern, pos);
                goto probes_done;
            }
            if (!run_case(dr, 1, 2))
                ok = 0;
        }
        if (ok) {
            printf("kernel %d (%s): stencil fault injection detected OK\n",
                   kern, r128_gpu_variants[kern].name);
        } else {
            printf("kernel %d: STENCIL FAULT INJECTION NOT DETECTED\n", kern);
            fails++;
        }
    }
probes_done:
    g_c_stg = 0;
    g_z_stg = 0;
    rngs = saved;
    xrngs = xsaved;
    ntpool = psaved;
}

/* device acceptance mirror: conservative row byte-ranges (ymin-1..ymax+2)
   as in prim_ranges, self c/z alias -> serial acceptance (device
   submit_tri mirror), cross-draw c/z hazard drop */
static uint64_t ser_self, ser_rtt, rej_cross;

static int
rng_hit(uint32_t lo, uint32_t hi, uint32_t a, uint32_t b)
{
    return hi > lo && b > a && a < hi && b > lo;
}

static void
fuzz_segments(int iters)
{
    static fz_draw_t dr[MAXDRAWS], acc[MAXDRAWS];
    int g_zkern = 0;

    for (int v = 0; v < N_KERNELS; v++)
        if (kern_axes[v].z && !kern_axes[v].tex && !kern_axes[v].blend)
            g_zkern = v;

    for (int it = 0; it < iters; it++) {
        int mode = (int) rhi(8);
        int py_lim = FUZZ_PY;

        g_c_off = C_BASE;
        switch (mode) {
            case 6:  g_z_off = C_BASE + (8u << 10);  break; /* self-alias */
            case 7:  g_z_off = C_BASE + (96u << 10); break; /* cross hazard */
            default: g_z_off = Z_BASE;               break;
        }
        /* straddle arms: run the surfaces at misaligned offsets, residue
           1..3 (odd straddles both bpps, residue 2 the 32bpp cells only).
           Modes 3/4/5 = color-only / z-only / both, so the coverage
           counters attribute per axis. */
        if (mode == 3 || mode == 5)
            g_c_off += 1u + rhi(3);
        if (mode == 4 || mode == 5)
            g_z_off += 1u + rhi(3);
        int nd = 4 + (int) rhi(10);
        int nacc = 0;
        uint32_t seg_clo = ~0u, seg_chi = 0, seg_zlo = ~0u, seg_zhi = 0;

        /* ~1/6 of cases: shadow-volume shape -- overlapping same-class
           commutative stencil draws, the packer's order-free path.
           Every 16th iteration forces the alternating-class variant:
           the pairwise exemption is otherwise seed-position sensitive
           (a fuzzer edit that shifts the rng stream can drop it to
           zero and trip the coverage gate on healthy pixels). */
        int force_commut = (it & 15) == 0;
        g_commut_tmpl.on = (rhi(6) == 0) || force_commut;
        if (g_commut_tmpl.on) {
            g_commut_tmpl.zshift   = rhi(2) ? 8 : 0;
            g_commut_tmpl.sfail_op = rhi(5);
            g_commut_tmpl.zpass_op = rhi(2) ? 3 : rhi(5); /* bias saturating increment */
            g_commut_tmpl.zfail_op = rhi(2) ? 0 : g_commut_tmpl.zpass_op;
            g_commut_tmpl.sref     = rng() & 0xff;
            g_commut_tmpl.svmask   = 0xff;
            g_commut_tmpl.swmask   = rhi(4) ? 0xff : rng() & 0xff;
        }
        /* sref belongs to the class key, so alternating it gives a row two
           commutative classes. Uniform rows short-circuit the pair scan;
           mixed rows exercise the pairwise commutative exemption. */
        g_py_pin      = g_commut_tmpl.on ? (int) rhi(4) : -1;
        g_commut_alt  = g_commut_tmpl.on && (rhi(2) || force_commut);
        g_commut_base = g_commut_tmpl.sref;

        for (int i = 0; i < nd && nacc < MAXDRAWS; i++) {
            fz_draw_t *d = &dr[i];

            if (g_commut_alt)
                /* (i & 2), not (i & 1): consecutive draws must share a
                   class or a row of neighboring draws never contains a
                   same-class pair and the exemption stays unreached */
                g_commut_tmpl.sref = (i & 2) ? (g_commut_base ^ 0x40u)
                                             : g_commut_base;
            /* Pin each forced volume case to one untextured depth kernel. The
               pairwise exemption runs within a run whose class chain breaks;
               random per-draw kernels need not exercise that path. */
            int kern = g_commut_tmpl.on
                           ? (force_commut ? g_zkern : (int) rhi(6))
                           : (int) rhi(N_KERNELS);

            mk_draw(d, kern, py_lim);
            if (r128_gpu_variants[d->kern].tex > 0 && rhi(2) == 0) {
                apply_tuple(d, tpool_take(d->kern));
                fold_cases++;
            }
            int ymin = d->row[0].py, ymax = d->row[d->nrows - 1].py;

            if (ymin > 0)
                ymin--;
            ymax += 2;
            uint32_t clo = g_c_off + (uint32_t) ymin * ROW_PITCH;
            uint32_t chi = g_c_off + (uint32_t) ymax * ROW_PITCH;
            uint32_t zlo = 0xffffffffu, zhi = 0;

            if (d->d.z_en || d->d.sten_on) {
                zlo = g_z_off + (uint32_t) ymin * ROW_PITCH;
                zhi = g_z_off + (uint32_t) ymax * ROW_PITCH;
            }
            if (rng_hit(clo, chi, zlo, zhi)) {
                /* device mirror: self-aliasing draw stays, as a serial
                   run on the uber pipeline (tuple -1) */
                d->serial = 1;
                d->tuple  = -1;
                ser_self++;
            }
            if (rng_hit(seg_clo, seg_chi, zlo, zhi)
                || rng_hit(seg_zlo, seg_zhi, clo, chi)) {
                rej_cross++;
                continue;
            }
            if (clo < seg_clo) seg_clo = clo;
            if (chi > seg_chi) seg_chi = chi;
            if (zhi > zlo) {
                if (zlo < seg_zlo) seg_zlo = zlo;
                if (zhi > seg_zhi) seg_zhi = zhi;
            }
            acc[nacc++] = *d;
        }
        if (!nacc)
            continue;
        if (run_case(acc, nacc, 0)) {
            fails++;
            if (fails <= 5)
                dump_mismatch("segment", mode, it);
        }
    }
}

/* ------------------------------------------------------------------------
 * Directed alias-serialization fuzz: constructed
 * color/z cross-alias and render-to-texture feedback cases, executed
 * through the serial-run path against the sequential reference (which
 * reproduces feedback naturally: ref_span mutates the same bytes the
 * next texel read consumes). Fault arms prove discriminating power.
 * ---------------------------------------------------------------------- */

static uint64_t ser_faults;

static void
mk_alias_cz(fz_draw_t *d, int directed)
{
    int kern;

    if (directed) {
        for (kern = 0; kern < N_KERNELS; kern++)
            if (kern_axes[kern].z && !kern_axes[kern].tex
                && !kern_axes[kern].blend)
                break;
        mk_probe_draw(d, kern);
        /* Color row zero is Z row one. White color and zero 16-bit
           depth are unconditional stores to that shared cell. Forward
           order leaves 0xffff0000; reverse leaves 0xffffffff, whatever
           the initial image. Single-pixel rows make the reverse fault
           independent of invocation scheduling. The last color row's
           seeded first cell witnesses drow+4 without alias overwrites. */
        d->serial = 1;
        d->d.fogr = d->d.fogg = d->d.fogb = 1.0f;
        d->d.zbpp = 2;
        d->d.zmax = 0xffff;
        d->d.zrowpx = ROW_PITCH / 2;
        d->nrows = 2;
        d->row[1].py = 1;
        d->row[1].e0 = 1;
        return;
    }
    do
        kern = (int) rhi(N_KERNELS);
    while (!kern_axes[kern].z);
    mk_draw(d, kern, FUZZ_PY);
    d->serial = 1;
}

static void
mk_alias_rtt(fz_draw_t *d, int directed)
{
    int kern;

    if (directed) {
        /* One covered pixel per row samples the same nearest 1x1 texel.
           Both rows sample the first color cell and invert its alpha.
           Forward order gives alpha 255-a then a; reverse order gives
           255-a on both rows. An eight-bit alpha has no inversion fixed
           point, so the second row differs for every initial image. */
        memset(d, 0, sizeof(*d));
        for (kern = 0; kern < N_KERNELS; kern++)
            if (kern_axes[kern].tex == 1 && !kern_axes[kern].lod
                && !kern_axes[kern].blend && !kern_axes[kern].z)
                break;
        d->kern   = kern;
        d->tuple  = -1;
        d->serial = 1;
        d->d.draw_ok = 1;
        d->d.dst_dt  = 6;
        d->d.bpp     = 4;
        d->d.wmask   = 0xffffffffu;
        d->d.tex_en  = 1;
        d->d.texw0   = d->d.texh0 = 1.0f;
        d->d.sh[0].dt = d->tc.sd0.dt = 6;
        d->d.comb[0].comba = 1;
        d->d.comb[0].afac  = 7;
        d->tc.sd0.slot_valid = 1;
        d->tc.sd0.slot[0].texbase = vram_ref;
        d->tc.sd0.slot[0].lw = d->tc.sd0.slot[0].lh = 1;
        d->tc.sd0.slot[0].mask = VRAM_MASK;
        d->tc.sd0.slot[0].base = g_c_off;
        d->invs  = 1.0f;
        d->nrows = 2;
        for (int row = 0; row < 2; row++) {
            d->row[row].py = row;
            d->row[row].e0 = 1;
        }
        return;
    }
    /* The feedback runs through stage 0, so a secondary-only draw
       reads none of its own stores; draw again. */
    do {
        do
            kern = (int) rhi(N_KERNELS);
        while (r128_gpu_variants[kern].tex < 1);
        mk_draw(d, kern, FUZZ_PY);
    } while (!d->d.tex_en);
    /* Resident stage-0 windows overlap the color surface. Random
       coordinates and formats provide placement variety for coverage. */
    for (int sl = 0; sl <= d->d.sh[0].top; sl++) {
        d->tc.sd0.slot[sl].texbase = vram_ref;
        d->tc.sd0.slot[sl].mask    = VRAM_MASK;
        d->tc.sd0.slot[sl].base    = g_c_off
                                   + (rng() % (FUZZ_PY * ROW_PITCH / 2u));
    }
    d->serial = 1;
}

static void
fuzz_alias(int iters)
{
    fz_draw_t dr[3];

    g_commut_tmpl.on = 0;
    g_py_pin         = -1;
    /* cz feedback: the z surface interleaved into the color rows at a
       small row delta, so row N's color bytes are row N+d's z cells */
    for (int it = 0; it < iters; it++) {
        int nd = 1 + (int) rhi(2);

        g_c_off = C_BASE;
        g_z_off = C_BASE + ((uint32_t) rhi(3) + 1u) * ROW_PITCH
                + (rhi(2) ? 512u : 0u);
        for (int i = 0; i < nd; i++)
            mk_alias_cz(&dr[i], 0);
        if (run_case(dr, nd, 0)) {
            fails++;
            if (fails <= 5)
                dump_mismatch("cz-serial", dr[0].kern, it);
        }
        ser_self++;
    }
    /* rtt feedback: normal surface layout, texture window inside the
       written color rows */
    for (int it = 0; it < iters; it++) {
        int nd = 1 + (int) rhi(2);

        g_c_off = C_BASE;
        g_z_off = Z_BASE;
        for (int i = 0; i < nd; i++)
            mk_alias_rtt(&dr[i], 0);
        if (run_case(dr, nd, 0)) {
            fails++;
            if (fails <= 5)
                dump_mismatch("rtt-serial", dr[0].kern, it);
        }
        ser_rtt++;
    }
    /* Each alias fixture has one pixel per row and a guaranteed order
       dependency. Reverse single-row parallel dispatches expose it on
       one try, with barriers preserving the imposed order. Restore the
       streams after all three legs of each fixture. */
    uint32_t saved = rngs, xsaved = xrngs;

    for (int arm = 0; arm < 2; arm++) {
        int ok_fault, ok_strip;

        if (arm == 0) {
            g_c_off = C_BASE + ROW_PITCH;
            g_z_off = C_BASE;
            mk_alias_cz(&dr[0], 1);
        } else {
            g_c_off = C_BASE;
            g_z_off = Z_BASE;
            mk_alias_rtt(&dr[0], 1);
        }
        if (run_case(dr, 1, 0)) {
            fails++;
            dump_mismatch(arm ? "pre-rtt-serial" : "pre-cz-serial",
                          dr[0].kern, 0);
            rngs = saved;
            xrngs = xsaved;
            g_c_off = C_BASE;
            g_z_off = Z_BASE;
            return;
        }
        int wrote = g_ref_wrote;

        ok_fault = wrote && run_case(dr, 1, 1);
        ok_strip = wrote && run_case(dr, 1, 4);
        if (!ok_fault || !ok_strip) {
            printf("%s: SERIAL FAULT INJECTION NOT DETECTED (drow=%d strip=%d)\n",
                   arm ? "rtt" : "cz", ok_fault, ok_strip);
            fails++;
        } else {
            printf("%s serial fault injection detected OK (drow+strip)\n",
                   arm ? "rtt" : "cz");
            ser_faults++;
        }
    }
    rngs  = saved;
    xrngs = xsaved;
    g_c_off = C_BASE;
    g_z_off = Z_BASE;
}

/* ------------------------------------------------------------------------
 * Lines: the device rasterizes a line as one draw whose spans are the
 * Bresenham walk's per-row runs on a virtual barycentric frame
 * (rage128_3d_line): e2 = 0 with e2dxi = 0, e1 = major pixels from the
 * first walked pixel (e1dxi = +-1 X-major, 0 Y-major), e0 = len - e1,
 * invs = 1/len. The fuzz gives X-major rows 1-4 px runs, with one case
 * in eight a full-length run (a shallow line's row can be the whole
 * walk), and Y-major rows single pixels. The kernel must reproduce the
 * interpreter on that frame,
 * where one weight is exactly 0, the edge steps are +-1 and the
 * seeds sit on the e >= 0 boundary. Points keep the two-sliver shape and
 * ride the ordinary tri arms.
 * ---------------------------------------------------------------------- */

static uint64_t line_draws, line_faults;

static void
mk_line(fz_draw_t *dr)
{
    int     ymajor = (int) rhi(2);
    int     mstep  = rhi(2) ? 1 : -1;
    int64_t len    = 1 + (int64_t) rhi(300);
    int32_t w;

    mk_draw(dr, (int) rhi(N_KERNELS), FUZZ_PY);
    w = ymajor ? 1 : (rhi(8) == 0 ? 300 : 1 + (int32_t) rhi(4));
    if (w > len + 1)
        w = (int32_t) len + 1;
    dr->x1    = dr->x0 + w - 1;
    dr->e1dxi = ymajor ? 0 : mstep;
    dr->e0dxi = -dr->e1dxi;
    dr->e2dxi = 0;
    dr->invs  = 1.0f / (float) len;
    for (int i = 0; i < dr->nrows; i++) {
        fz_row_t *rw = &dr->row[i];
        int64_t   e1;

        /* every pixel of the run stays inside [0, len] */
        if (ymajor)
            e1 = (int64_t) rhi((uint32_t) len + 1u);
        else if (mstep > 0)
            e1 = (int64_t) rhi((uint32_t) (len - w + 1) + 1u);
        else
            e1 = (w - 1) + (int64_t) rhi((uint32_t) (len - w + 1) + 1u);
        rw->e1 = e1;
        rw->e0 = len - e1;
        rw->e2 = 0;
    }
}

static void
fuzz_lines(int iters)
{
    fz_draw_t dr;

    g_c_off          = C_BASE;
    g_z_off          = Z_BASE;
    g_commut_tmpl.on = 0;
    g_py_pin         = -1;
    for (int it = 0; it < iters; it++) {
        mk_line(&dr);
        if (run_case(&dr, 1, 0)) {
            fails++;
            if (fails <= 5)
                dump_mismatch("line", dr.kern, it);
        }
        line_draws++;
    }
    /* A two-pixel X-major line runs from weights (1,0,0) to (0,1,0).
       Both endpoints write black RGB, so drow+4 leaves the seeded first
       cell untouched even though the shifted run overlaps the second. */
    {
        uint32_t saved = rngs, xsaved = xrngs;
        int kern;

        for (kern = 0; kern < N_KERNELS; kern++)
            if (!kern_axes[kern].z && !kern_axes[kern].tex
                && !kern_axes[kern].blend)
                break;
        mk_probe_draw(&dr, kern);
        dr.x1 = 1;
        dr.e0dxi = -1;
        dr.e1dxi = 1;
        if (run_case(&dr, 1, 0)) {
            fails++;
            dump_mismatch("pre-line", dr.kern, 0);
        } else if (g_ref_wrote && run_case(&dr, 1, 1)) {
            printf("line fault injection detected OK\n");
            line_faults++;
        } else {
            printf("line: FAULT INJECTION NOT DETECTED\n");
            fails++;
        }
        rngs = saved;
        xrngs = xsaved;
    }
}

/* ------------------------------------------------------------------------
 * Directed mask-edge straddle: a straddling cell whose base is the last
 * vram byte writes its tail byte(s) past the mask edge into the guard
 * word -- base masked once, bytes linear, never re-wrapped (the
 * interpreter's scheme; on the device the spill lands in the snapshot
 * region's first bytes, the same host bytes both lanes see). Cells past
 * the edge wrap whole to the vram start. The compare spans VRAM_SZ + 4,
 * so the spilled bytes are proven byte-exact; the fault leg proves the
 * arm can see a miss at all. Four legs: color 16bpp / color 32bpp /
 * z16 / z32, one span each, pixel 128's cell base pinned on VRAM_MASK.
 * ---------------------------------------------------------------------- */

static void
fuzz_edge(void)
{
    fz_draw_t d;
    int       gk = 0, zk = 0;

    for (int v = 0; v < N_KERNELS; v++) {
        if (!kern_axes[v].z && !kern_axes[v].tex && !kern_axes[v].blend)
            gk = v;
        if (kern_axes[v].z && !kern_axes[v].tex && !kern_axes[v].blend)
            zk = v;
    }
    uint32_t saved = rngs, xsaved = xrngs;

    g_commut_tmpl.on = 0;
    g_py_pin         = -1;
    for (int leg = 0; leg < 4; leg++) {
        uint32_t py = 0;

        /* Pin the whole draw so every pixel survives and writes black.
           The seeded first cell guarantees detection; the full span
           still crosses the mask edge and guard. */
        mk_probe_draw(&d, leg < 2 ? gk : zk);
        d.x1 = 255;
        d.row[0].zline = 0.5;
        if (leg == 1) {
            d.d.dst_dt = 6;
            d.d.bpp    = 4;
            d.d.wmask  = 0xffffffffu;
        } else {
            d.d.dst_dt = 3;
            d.d.bpp    = 2;
            d.d.wmask  = 0xffff;
        }
        g_c_off = C_BASE;
        g_z_off = Z_BASE;
        if (leg < 2) {
            d.d.z_en = 0;
            d.d.z_wr = 0;
            /* pixel 128's cell base = base + 128*bpp = VRAM_MASK */
            g_c_off = VRAM_SZ - 1u - 128u * (uint32_t) d.d.bpp
                    - py * ROW_PITCH;
        } else {
            d.d.z_en   = 1;
            d.d.z_wr   = 1;
            d.d.zfn    = 7; /* always-pass: coverage does not depend on seed z */
            d.d.zbpp   = (leg == 2) ? 2 : 4;
            d.d.zmax   = (leg == 2) ? 0xffff : 0xffffff;
            d.d.zshift = (leg == 2) ? 0 : 8;
            d.d.zrowpx = ROW_PITCH / (uint32_t) d.d.zbpp;
            g_z_off    = VRAM_SZ - 1u - 128u * (uint32_t) d.d.zbpp
                       - py * ROW_PITCH;
        }
        if (run_case(&d, 1, 0)) {
            fails++;
            if (fails <= 5)
                dump_mismatch("mask-edge", d.kern, leg);
            continue;
        }
        if (leg == 0) {
            if (!run_case(&d, 1, 1)) {
                printf("mask-edge: FAULT INJECTION NOT DETECTED\n");
                fails++;
            } else {
                edge_faults++;
                printf("mask-edge fault injection detected OK\n");
            }
        }
    }
    rngs = saved;
    xrngs = xsaved;
}

/* ------------------------------------------------------------------------
 * Top-column wrap checks: sx1 is a 14-bit field, so a span can end at
 * x=16383, where (x1+1)*4 is 65536 -- one past what 16 bits hold. The
 * leveler's byte ranges must survive that column.
 * ---------------------------------------------------------------------- */

/* Two overlapping 32bpp spans ending at the top column must read as
   aliasing; re-truncating the ranges to 16 bits must flip the decision
   back to disjoint or this check tests nothing. */
static int
wrap_check(void)
{
    fz_draw_t dr;
    int       kern = 0;

    for (int v = 0; v < N_KERNELS; v++)
        if (kern_axes[v].z && !kern_axes[v].tex && !kern_axes[v].blend)
            kern = v;
    rngs = 0xb1820001u;
    mk_draw(&dr, kern, FUZZ_PY);
    dr.d.dst_dt  = 6; /* 32bpp: the only dst whose top column overflows */
    dr.d.zbpp    = 4;
    dr.d.sten_on = 0; /* keep the commutative exemption out of the way */
    bb_of_fill(0, 16256, 16383, &dr);
    bb_of_fill(1, 16350, 16383, &dr);
    if (!seg_spans_alias(0, 1) || !seg_spans_alias(1, 0)) {
        printf("FAIL: top-column overlapping spans read as disjoint "
               "(a c0=%u c1=%u / b c0=%u c1=%u)\n",
               (unsigned) bb_of[0].c0, (unsigned) bb_of[0].c1,
               (unsigned) bb_of[1].c0, (unsigned) bb_of[1].c1);
        return 1;
    }
    bb_of[0].c1 = (uint16_t) bb_of[0].c1;
    bb_of[0].z1 = (uint16_t) bb_of[0].z1;
    bb_of[1].c1 = (uint16_t) bb_of[1].c1;
    bb_of[1].z1 = (uint16_t) bb_of[1].z1;
    if (seg_spans_alias(0, 1)) {
        printf("FAIL: wrap fault injection not detected\n");
        return 1;
    }
    printf("top-column wrap check OK (fault injection detected)\n");
    return 0;
}

/* Directed top-column fuzz: draws pinned to one shared row with x spans
   ending at or near 16383. One row only: past ROW_PITCH a span's bytes
   run into later rows' storage, and cross-row order is not what the
   leveler arbitrates. Renders the case, then direct-checks the alias
   predicate against 64-bit ground truth for every same-row pair. */
static void
fuzz_wrap(int iters)
{
    fz_draw_t dr[3];

    g_c_off          = C_BASE;
    g_z_off          = Z_BASE;
    g_commut_tmpl.on = 0;
    g_py_pin         = -1;
    for (int it = 0; it < iters; it++) {
        int nd = 1 + (int) rhi(3);
        int py = (int) rhi(FUZZ_PY);

        for (int i = 0; i < nd; i++) {
            fz_draw_t *d = &dr[i];

            mk_draw(d, (int) rhi(N_KERNELS), FUZZ_PY);
            d->nrows     = 1;
            d->row[0].py = py;
            d->d.aux_on  = 0; /* aux rects never reach these columns */
            d->x0        = 16383 - (int32_t) rhi(400);
            d->x1        = d->x0 + 40 + (int32_t) rhi(360);
            if (d->x1 > 16383)
                d->x1 = 16383;
        }
        if (run_case(dr, nd, 0)) {
            fails++;
            if (fails <= 5)
                dump_mismatch("wrap", -1, it);
        }
        {
            const seg_span_t *sp = (const seg_span_t *) b_spans.map;
            const seg_tri_t  *tt = (const seg_tri_t *) b_tris.map;

            for (uint32_t a = 0; a < nspans; a++)
                for (uint32_t b = a + 1; b < nspans; b++) {
                    if (sp[a].py != sp[b].py)
                        continue;
                    if (sctl_of[a] && sctl_of[a] == sctl_of[b]
                        && srm_of[a] == srm_of[b])
                        continue;
                    uint64_t acb = (tt[sp[a].tri].dst_dt == 6u) ? 4u : 2u;
                    uint64_t bcb = (tt[sp[b].tri].dst_dt == 6u) ? 4u : 2u;
                    uint64_t ac0 = (uint64_t) sp[a].x0 * acb;
                    uint64_t ac1 = ((uint64_t) sp[a].x1 + 1u) * acb;
                    uint64_t bc0 = (uint64_t) sp[b].x0 * bcb;
                    uint64_t bc1 = ((uint64_t) sp[b].x1 + 1u) * bcb;
                    uint64_t azb = tt[sp[a].tri].zbpp;
                    uint64_t bzb = tt[sp[b].tri].zbpp;
                    uint64_t az0 = (uint64_t) sp[a].x0 * azb;
                    uint64_t az1 = ((uint64_t) sp[a].x1 + 1u) * azb;
                    uint64_t bz0 = (uint64_t) sp[b].x0 * bzb;
                    uint64_t bz1 = ((uint64_t) sp[b].x1 + 1u) * bzb;
                    int      truth = (ac0 < bc1 && bc0 < ac1)
                                  || (az0 < bz1 && bz0 < az1);

                    if (truth && !seg_spans_alias(a, b)) {
                        fails++;
                        if (fails <= 5)
                            printf("FAIL: wrap it=%d spans %u/%u overlap "
                                   "but alias=0 (x %d..%d / %d..%d)\n",
                                   it, a, b, sp[a].x0, sp[a].x1,
                                   sp[b].x0, sp[b].x1);
                    }
                }
        }
    }
}

/* Span-cap headroom checks draws that need more than FZ_SPAN_CAP spans.
 * The fault arm omits the accept-time bound, so capture stops mid-draw
 * and drops pixels. The bounded arm flushes before a draw that cannot
 * fit, captures each accepted draw whole, and executes segments in order.
 * The byte comparison must distinguish the two arms.
 */

#define HR_DRAWS 830 /* 830 x 5 rows x 4 chunks = 16600 spans, 2x the cap */

static fz_draw_t hr_draws[HR_DRAWS]; /* too big for the stack */

static uint32_t
hr_draw_need(const fz_draw_t *d)
{
    uint32_t w = (uint32_t) (d->x1 - d->x0 + 1);

    return (uint32_t) d->nrows * ((w + 255u) / 256u);
}

/* run_case shape, but with the accept-time bound optional and the
   capture kept honest: expect counts the spans the draws' full-coverage
   rows owe, got counts the spans the capture actually emitted. */
static int
run_case_headroom(const fz_draw_t *dr, int nd, int gated,
                  uint32_t *out_expect, uint32_t *out_got, uint32_t *out_segs)
{
    fz_disp_t disp[FZ_RUN_CAP];
    uint32_t  ndisp, expect = 0, got = 0, segs = 1;

    for (uint32_t k = 0; k < VRAM_SZ + 4; k += 4)
        *(uint32_t *) &vram_ref[k] = rng();
    memcpy(b_vram.map, vram_ref, VRAM_SZ + 4);
    memcpy(vram_orig, vram_ref, VRAM_SZ + 4);
    for (uint32_t k = 0; k < STAGE_SZ + 4; k += 4)
        *(uint32_t *) &stage_ref[k] = rng();
    memcpy(b_stage.map, stage_ref, STAGE_SZ + 4);
    /* Seed color/depth arenas with a separate generator so their contents
       do not shift the main random stream or later case inputs. */
    {
        uint32_t cz = (uint32_t) cases_run * 2654435761u + 0x169u;

        for (uint32_t k = 0; k < CZSTG_SZ + 4; k += 4) {
            cz = cz * 1664525u + 1013904223u;
            *(uint32_t *) &cstage_ref[k] = cz;
            cz = cz * 1664525u + 1013904223u;
            *(uint32_t *) &zstage_ref[k] = cz;
        }
    }
    memcpy(b_cstage.map, cstage_ref, CZSTG_SZ + 4);
    memcpy(b_zstage.map, zstage_ref, CZSTG_SZ + 4);

    seg_reset();
    for (int i = 0; i < nd; i++) {
        uint32_t need = hr_draw_need(&dr[i]);
        uint32_t prev;

        expect += need;
        if (gated && nspans + need > FZ_SPAN_CAP) {
            ndisp = seg_sort(disp);
            if (ndisp)
                seg_exec(disp, ndisp);
            seg_reset();
            segs++;
        }
        prev = nspans;
        seg_add_draw(&dr[i]);
        got += nspans - prev;
    }
    ndisp = seg_sort(disp);
    if (ndisp)
        seg_exec(disp, ndisp);

    for (int i = 0; i < nd; i++)
        ref_draw(&dr[i]);

    if (out_expect) *out_expect = expect;
    if (out_got) *out_got = got;
    if (out_segs) *out_segs = segs;
    return memcmp(b_vram.map, vram_ref, VRAM_SZ + 4) != 0
        || memcmp(b_stage.map, stage_ref, STAGE_SZ + 4) != 0
        || memcmp(b_cstage.map, cstage_ref, CZSTG_SZ + 4) != 0
        || memcmp(b_zstage.map, zstage_ref, CZSTG_SZ + 4) != 0;
}

/* full-coverage rows on one untextured z kernel: flat edges (dxi 0,
   seed >= 0) cover the draw's whole x range, z compare pinned to always-pass
   with z write so every covered pixel writes color and z -- a dropped
   span is a guaranteed byte difference, not a maybe */
static void
hr_build(void)
{
    int kern = 0;

    for (int v = 0; v < N_KERNELS; v++)
        if (kern_axes[v].z && !kern_axes[v].tex && !kern_axes[v].blend)
            kern = v;
    rngs = 0x18318301u;
    for (int i = 0; i < HR_DRAWS; i++) {
        fz_draw_t *d = &hr_draws[i];

        mk_draw(d, kern, FUZZ_PY);
        d->d.dst_dt   = 3; /* 16bpp: x to 1023 keeps the row inside its pitch */
        d->d.bpp      = 2;
        d->d.wmask    = 0xffff;
        d->d.dither   = 0;
        d->d.z_en     = 1;
        d->d.z_wr     = 1;
        d->d.zfn      = 7; /* always-pass: draw order alone decides the pixel */
        d->d.zbpp     = 2;
        d->d.zmax     = 0xffff;
        d->d.zshift   = 0;
        d->d.zrowpx   = ROW_PITCH / 2;
        d->d.sten_on  = 0;
        d->d.atest_en = 0;
        d->d.aux_on   = 0;
        d->x0         = 0;
        d->x1         = 1023; /* 4 chunks per row */
        d->e0dxi = d->e1dxi = d->e2dxi = 0;
        d->nrows      = MAXROWS;
        {
            int base = (i * MAXROWS) % (FUZZ_PY - MAXROWS);

            for (int r = 0; r < MAXROWS; r++) {
                d->row[r].py    = base + r;
                d->row[r].e0    = 1 << 20;
                d->row[r].e1    = 1 << 20;
                d->row[r].e2    = 1 << 20;
                d->row[r].zline = 0.5;
            }
        }
    }
}

static int
headroom_check(void)
{
    uint32_t expect, got, segs;
    int      bad = 0;

    hr_build();
    /* fault leg: no gate reproduces the device's silent truncation and
       the compare must see the dropped pixels */
    if (!run_case_headroom(hr_draws, HR_DRAWS, 0, &expect, &got, &segs)) {
        printf("FAIL: headroom fault leg matched (truncated capture "
               "expect=%u got=%u)\n", expect, got);
        bad = 1;
    } else {
        printf("headroom fault leg: %u of %u spans captured, dropped tail "
               "MISMATCHED as required\n", got, expect);
    }
    /* gated leg: the accept-time bound splits segments and no pixel drops */
    if (run_case_headroom(hr_draws, HR_DRAWS, 1, &expect, &got, &segs)) {
        printf("FAIL: headroom gated leg mismatched (expect=%u got=%u segs=%u)\n",
               expect, got, segs);
        dump_mismatch("headroom", 4, 0);
        bad = 1;
    } else if (got != expect) {
        printf("FAIL: headroom gated leg dropped spans (expect=%u got=%u)\n",
               expect, got);
        bad = 1;
    } else {
        printf("headroom gated leg: %u spans over %u segments, all pixels "
               "written\n", got, segs);
    }
    return bad;
}

/* Bench mode ranks intra-kernel costs on the same seeded workload for
 * every leg. It measures timing without comparing pixels. Barrier
 * removal and bench-only selector folds can produce incorrect output.
 */

static uint64_t
now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static VkPipeline
mk_bench_pipe(int kern, int nobar, int foldall, int foldcomb, int commut)
{
    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smci.codeSize = sizeof(r128_gpu_seg_spv);
    smci.pCode    = (const uint32_t *) r128_gpu_seg_spv;
    VkShaderModule sm;
    VK_CHECK(vkCreateShaderModule(dev, &smci, NULL, &sm));
    /* ids 0-3 axes, 5 B_NOBAR, 6 B_FOLD, 7 C_FOLD, 8..21 combine,
       22 S_STEN, 23 C_DEAD. */
    VkSpecializationMapEntry sme[27];
    int32_t                  sd[27];
    for (int i = 0; i < 4; i++)
        sme[i] = (VkSpecializationMapEntry) { (uint32_t) i, (uint32_t) i * 4, 4 };
    sme[4] = (VkSpecializationMapEntry) { 5, 16, 4 };
    sme[5] = (VkSpecializationMapEntry) { 6, 20, 4 };
    sme[6] = (VkSpecializationMapEntry) { 7, 24, 4 };
    for (int i = 0; i < 14; i++)
        sme[7 + i] = (VkSpecializationMapEntry) { (uint32_t) (8 + i),
                                                  (uint32_t) (28 + i * 4), 4 };
    sme[21] = (VkSpecializationMapEntry) { 22, 84, 4 };
    sme[22] = (VkSpecializationMapEntry) { 23, 88, 4 };
    for (int i = 0; i < 4; i++)
        sme[23 + i] = (VkSpecializationMapEntry) { (uint32_t) (25 + i),
                                                   (uint32_t) (92 + i * 4), 4 };
    sd[0] = r128_gpu_variants[kern].tex;
    sd[1] = r128_gpu_variants[kern].lod;
    sd[2] = r128_gpu_variants[kern].blend;
    sd[3] = r128_gpu_variants[kern].z;
    sd[4] = nobar;
    sd[5] = foldall;
    sd[6] = foldcomb;
    /* GPUTRI_COMB selects the folded combine book. Default 1 modulates
       the incoming color by the texel, keeping both stages' samples live.
       Book 0 copies int_color and lets the compiler eliminate texturing.
       Book 2 copies the texel to bound the cost of combine arithmetic. */
    {
        const char *ce   = getenv("GPUTRI_COMB");
        int         book = ce ? atoi(ce) : 1;
        static const int books[3][7] = {
            { 2, 0, 0, 0, 2, 0, 0 }, /* 0: copy int_color, texel dead */
            { 3, 0, 4, 8, 3, 0, 0 }, /* 1: modulate prev * texel, both
                                          stages' texels live (Q3 lightmap) */
            { 1, 0, 4, 0, 0, 0, 0 }, /* 2: col = texel, minimal combine */
        };

        if (book < 0 || book > 2)
            book = 1;
        for (int i = 0; i < 14; i++)
            sd[7 + i] = books[book][i % 7];
    }
    sd[21] = commut; /* S_STEN */
    sd[22] = commut; /* C_DEAD */
    /* production folds the texel format with the book: RGB565, no s3tc */
    for (int i = 0; i < 4; i++)
        sd[23 + i] = foldcomb ? ((i & 1) ? 0 : 4) : -1;
    VkSpecializationInfo si = { 27, sme, sizeof(sd), sd };
    VkComputePipelineCreateInfo cpi = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    cpi.stage.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage               = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.pName               = "main";
    cpi.stage.module              = sm;
    cpi.stage.pSpecializationInfo = &si;
    cpi.layout                    = r_plyt;
    VkPipeline p;
    VK_CHECK(vkCreateComputePipelines(dev, plcache, 1, &cpi, NULL, &p));
    vkDestroyShaderModule(dev, sm, NULL);
    return p;
}

/* Deterministic workload: real mk_draw states (rng reseeded, identical
   across legs), geometry overridden to fixed-width all-pass spans so
   every leg shades the same candidate pixels. zfn forced always-pass +
   z_wr so per-iteration work is stable. Returns candidate px per exec. */
/* rows (= workgroups) the bench workload is spread over; a leg can cut
   it below MAX_PY to test whether the GPU is saturated at 48.
   g_bench_spans caps total spans, so a leg can hold spans-per-row (the
   serial chain each workgroup walks) fixed while varying row count --
   that separates "wall = total work / throughput" from
   "wall = longest row x per-span latency". */
static int g_bench_rows  = MAX_PY;
static int g_bench_spans = (int) FZ_SPAN_CAP;
/* x0 jitter range. 0 stacks every span at the same x (worst case for
   batch packing: nothing is ever disjoint). Live Q3 rows carry ~16
   spans of ~58 px over 640 px -- 1.45x coverage, mostly disjoint --
   which a nonzero spread models. */
static int g_bench_spread;

/* Varying width modes both target mean 57, keeping the candidate-pixel
   timing denominator comparable. WD_FIXED consumes no random draws.
   A fixed width of 57 occupies 64 lanes at slot widths of 32, 16, or 8;
   varying widths exercise differences in lane-budget rounding. */
enum { WD_FIXED = 0, WD_UNIF, WD_SKEW };

/* Rejection coherence controls how dead pixels group: ZC_PIXEL scatters
   them across lane groups, ZC_BLOCK rejects whole 32-lane groups, and
   ZC_SPAN rejects whole spans. Equal rejection rates can therefore leave
   different amounts of texture work active. */
enum { ZC_PIXEL = 0, ZC_BLOCK, ZC_SPAN };

typedef struct bench_leg_t {
    const char *name;
    int         kern, nobar, W, rows, spans, spread, nopack;
    /* Zero-valued fields select the fixed-width, no-rejection workload. */
    int         wmode;
    int         zctl;  /* deterministic z: less-or-equal, flat dZdx, aux off */
    int         zrej;  /* percent of candidate px the z test kills */
    int         zcoh;
    int         akill; /* percent of draws whose alpha test kills every px */
    int         foldall;  /* bench-only B_FOLD bitmask; combine uses C_FOLD */
    int         foldcomb; /* production C_FOLD with representative constants */
    int         commut;   /* volume-fill shape: dead color, stencil CAS only */
    int         nearest;  /* chain ablation: force both stages' filter to
                             nearest (minb = mag = 0), one texel per sample */
} bench_leg_t;

/* Does the seeded z at (py,x) reject a zctl fragment? ZC_PIXEL/ZC_BLOCK only;
   ZC_SPAN decides per span through zline instead. */
static int
bench_rej_at(const bench_leg_t *lg, int py, int x)
{
    uint32_t h;

    if (lg->zrej <= 0)
        return 0;
    h = (uint32_t) py * 0x9e3779b9u
      + (uint32_t) (lg->zcoh == ZC_BLOCK ? (x >> 5) : x) * 0x85ebca6bu;
    h ^= h >> 15;
    h *= 0x2545f491u;
    h ^= h >> 13;
    return (int) (h % 100u) < lg->zrej;
}

/* achieved width mean and z-kill rate: a leg reports what it actually
   built, not what it asked for */
static uint64_t g_bench_wsum, g_bench_wn, g_bench_rejpx;

static uint64_t
bench_build(const bench_leg_t *lg)
{
    uint64_t  px = 0;
    fz_draw_t dr;
    int       py = 0;

    g_c_off      = C_BASE;
    g_z_off      = Z_BASE;
    rngs         = 0x12345678u;
    g_bench_wsum = g_bench_wn = g_bench_rejpx = 0;
    /* A commutative leg uses dead color, depth testing without writes,
       an always-pass stencil comparison, saturating increment, and keep
       on depth failure. The stencil updates commute; the fixed state
       keeps the workload stable across repetitions. */
    g_commut_tmpl.on = lg->commut;
    if (lg->commut) {
        g_commut_tmpl.zshift   = 0;
        g_commut_tmpl.sfail_op = 0;
        g_commut_tmpl.zpass_op = 3; /* INCsat */
        g_commut_tmpl.zfail_op = 0;
        g_commut_tmpl.sref     = 0;
        g_commut_tmpl.svmask   = 0xff;
        g_commut_tmpl.swmask   = 0xff;
    }
    seg_reset();
    while (ntris + 1 < FZ_TRI_CAP
           && nspans + MAXROWS + 1 < (uint32_t) g_bench_spans) {
        mk_draw(&dr, lg->kern, MAX_PY);
        {
            int64_t area = 1200000;
            int     W    = lg->W;
            int     spr;

            if (lg->wmode == WD_UNIF)
                W = 8 + (int) rhi(99);
            else if (lg->wmode == WD_SKEW)
                W = 8 + (int) rhi(rhi(196) + 1);
            /* Keep the 640-pixel row model at every span width. */
            spr = g_bench_spread ? (640 - W > 0 ? 640 - W : 1) : 0;

            dr.x0    = 8 + (spr ? (int) rhi((uint32_t) spr) : 0);
            dr.x1    = dr.x0 + W - 1;
            dr.invs  = 1.0f / (float) area;
            dr.e0dxi = area / (4 * W);
            dr.e1dxi = -(area / (8 * W));
            dr.e2dxi = -(dr.e0dxi + dr.e1dxi);
            dr.d.atest_en = 0;
            if (lg->nearest)
                for (int st = 0; st < 2; st++)
                    dr.d.sh[st].minb = dr.d.sh[st].mag = 0;
            /* a commut leg keeps mk_state's volume config: forcing z_wr
               back on here would turn it into an ordinary color draw */
            if (!lg->commut) {
                dr.d.z_wr = 1;
                if (lg->zctl) {
                    /* less-or-equal against a seeded z row, flat z along the
                       span, aux off -- kill rate is exactly lg->zrej */
                    dr.d.zfn    = 2;
                    dr.d.aux_on = 0;
                    dr.dZdx     = 0.0;
                } else
                    dr.d.zfn = 7;
            }
            if (lg->akill && (int) rhi(100) < lg->akill) {
                /* never-pass: kills the fragment after the combine chain, so
                   this is the discriminator against an equal-rate z
                   kill, which kills before it */
                dr.d.atest_en = 1;
                dr.d.atest_fn = 0;
            }
            dr.nrows = MAXROWS;
            for (int r = 0; r < MAXROWS; r++) {
                int rejspan = 0;

                dr.row[r].py = py;
                py           = (py + 1) % g_bench_rows;
                dr.row[r].e0 = area / 3;
                dr.row[r].e1 = area / 3;
                dr.row[r].e2 = area - dr.row[r].e0 - dr.row[r].e1;
                if (!lg->zctl)
                    dr.row[r].zline = 0.4 + 0.0001 * r;
                else if (lg->zcoh == ZC_SPAN) {
                    rejspan         = (int) rhi(100) < lg->zrej;
                    dr.row[r].zline = rejspan ? 0.9 : 0.1;
                } else
                    dr.row[r].zline = 0.5;
                if (lg->zctl) {
                    if (lg->zcoh == ZC_SPAN)
                        g_bench_rejpx += rejspan ? (uint64_t) W : 0u;
                    else
                        for (int x = dr.x0; x <= dr.x1; x++)
                            g_bench_rejpx +=
                                (uint64_t) bench_rej_at(lg, dr.row[r].py, x);
                }
                g_bench_wsum += (uint64_t) W;
                g_bench_wn++;
            }
            px += (uint64_t) MAXROWS * (uint64_t) W;
        }
        seg_add_draw(&dr);
    }
    return px;
}

/* Seed the z rows so the test kills exactly the intended pixels.
   Stationary across reps by construction: a passing cell is rewritten
   with the value it just compared against and a rejected cell is never
   written, so rep N sees what rep 1 saw. Without that the median over 9
   timed reps would be measuring a moving workload. */
static void
bench_zfill(const bench_leg_t *lg)
{
    for (int py = 0; py < lg->rows; py++) {
        uint8_t *rp = (uint8_t *) b_vram.map
                    + ((g_z_off + (uint32_t) py * ROW_PITCH) & VRAM_MASK);

        for (uint32_t x = 0; x < ROW_PITCH / 2; x++)
            ((uint16_t *) rp)[x] =
                (uint16_t) ((lg->zcoh == ZC_SPAN)
                                ? 32768u
                                : (bench_rej_at(lg, py, (int) x) ? 0u : 0xffffu));
    }
}

/* seg_exec with a pipeline override and the whole segment repeated
   `reps` times barrier-chained in one submit (mirrors the device's
   run chaining, amortizes the fence floor) */
static void
bench_exec(VkPipeline pipe, const fz_disp_t *disp, uint32_t ndisp, int reps)
{
    VK_CHECK(vkResetCommandBuffer(cb, 0));
    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cb, &cbbi));
    /* Timing knobs isolate repeated segment costs. The ladder is identical
       across repetitions, so one pre-pass supplies the same values.
       Omitting inter-repetition barriers permits races and measures only
       their timing cost; these legs do not compare output. */
    static int noprep = -1, nobar, overlap;

    if (noprep < 0) {
        noprep  = getenv("GPUTRI_NOPREPASS") != NULL;
        nobar   = getenv("GPUTRI_NOBARRIER") != NULL;
        /* Overlap records the next repetition's pre-pass after the current
           shading dispatch and before their shared barrier. It reads the
           same spans/tris and rewrites identical ladder values because
           repetitions share one workload. The next shading dispatch waits
           for those writes through the barrier. */
        overlap = getenv("GPUTRI_PREPASS_OVERLAP") != NULL;
    }
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, r_plyt, 0, 1, &r_dset, 0, NULL);
    if (noprep || overlap)
        rec_lad_prepass();
    for (int rep = 0; rep < reps; rep++) {
        /* pre-pass per rep: a rep models one segment, and the segment
           cost includes its ladder walk */
        if (!noprep && !overlap)
            rec_lad_prepass();
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        for (uint32_t i = 0; i < ndisp; i++) {
            uint32_t pcv[2] = { VRAM_MASK, disp[i].rows_base };

            vkCmdPushConstants(cb, r_plyt, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, pcv);
            vkCmdDispatch(cb, disp[i].nrows, 1, 1);
            if (overlap && i + 1 == ndisp && rep + 1 < reps) {
                rec_lad_prepass_nobar();
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
            }
            if (!nobar && (rep + 1 < reps || i + 1 < ndisp)) {
                VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };

                mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                     1, &mb, 0, NULL, 0, NULL);
            }
        }
    }
    VK_CHECK(vkEndCommandBuffer(cb));
    submit_wait();
}

static int
u64_cmp(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *) a, y = *(const uint64_t *) b;

    return x < y ? -1 : x > y;
}

/* profbench: one warmup + one timed exec of one rep per leg, so a
   MoltenVK device-scope GPU capture of the run stays small enough for
   the Xcode shader profiler. Timing printed is not a bench number. */
static int g_prof_mode;

static void
bench_leg(const bench_leg_t *lg)
{
    enum { ITERS = 9 };
    int        iters = g_prof_mode ? 1 : ITERS;
    VkPipeline p;
    uint64_t   px1;
    uint64_t   nspan1, nbat1;
    uint32_t   crit = 0;

    g_bench_rows   = lg->rows;
    g_bench_spans  = lg->spans;
    g_bench_spread = lg->spread;
    g_bench_nopack = lg->nopack;
    p              = mk_bench_pipe(lg->kern, lg->nobar, lg->foldall,
                                   lg->foldcomb, lg->commut);
    px1            = bench_build(lg);
    if (lg->zctl)
        bench_zfill(lg);
    fz_disp_t  disp[FZ_RUN_CAP];
    uint32_t   nd   = seg_sort(disp);
    int        reps = g_prof_mode ? 1 : (int) (4000000u / px1) + 1;
    uint64_t   t[ITERS];

    /* Report both total batch passes and the longest workgroup chain:
       either can limit wall time, so one count alone hides workload shape. */
    nspan1 = nspans;
    nbat1  = 0;
    for (uint32_t i = 0; i < nd; i++) {
        uint32_t maxb = 0;

        for (uint32_t q = 0; q < disp[i].nrows; q++) {
            uint32_t nb = ((uint32_t *) b_rows.map)[(disp[i].rows_base + q) * 2u + 1u];

            nbat1 += nb;
            if (nb > maxb)
                maxb = nb;
        }
        crit += maxb;
    }

    for (int i = 0; i < (g_prof_mode ? 1 : 2); i++)
        bench_exec(p, disp, nd, reps); /* warmup: pipeline + caches */
    for (int i = 0; i < iters; i++) {
        uint64_t t0 = now_ns();

        bench_exec(p, disp, nd, reps);
        t[i] = now_ns() - t0;
    }
    qsort(t, (size_t) iters, sizeof(t[0]), u64_cmp);
    /* ns/px stays per candidate pixel, rejected ones included, so a
       rejection win shows up as the number falling rather than as a
       smaller denominator hiding it. */
    printf("%-18s W=%-5.1f rows=%-3d spr=%-4llu rej=%-3.0f%% bat=%-6llu crit=%-5u "
           "pack=%4.2fx median %8.3f ms  %6.2f ns/px\n",
           lg->name,
           (double) g_bench_wsum / (double) g_bench_wn, lg->rows,
           (unsigned long long) (nspan1 / (uint64_t) lg->rows),
           100.0 * (double) g_bench_rejpx / (double) px1,
           (unsigned long long) nbat1, crit,
           (double) nspan1 / (double) nbat1,
           (double) t[iters / 2] / 1e6,
           (double) t[iters / 2] / ((double) px1 * reps));
    vkDestroyPipeline(dev, p, NULL);
}

static void
bench_run(void)
{
    enum { SPAN_CAP = (int) FZ_SPAN_CAP, ALL = MAX_PY };
    /* 640 px row model: a 57 px span placed anywhere in it */
    enum { Q3SPREAD = 640 - 57 };
    static const bench_leg_t legs[] = {
      /* name              kern nobar   W  rows      spans  spread nopack */
        /* packing A/B on the live-Q3-shaped workload: same kernel, same
           spans, only the batch build differs */
        { "t1_mip nopack",     0, 0,  57, ALL, SPAN_CAP, Q3SPREAD, 1 },
        { "t1_mip pack",       0, 0,  57, ALL, SPAN_CAP, Q3SPREAD, 0 },
        /* live Q3 density: ~16 spans/row over 640 px = 1.45x coverage,
           far less overlap than the span-cap legs, so packing should
           reach the lane limit instead of the overlap limit */
        { "t1_mip q3dens nop", 0, 0,  57, ALL,      800, Q3SPREAD, 1 },
        { "t1_mip q3dens pak", 0, 0,  57, ALL,      800, Q3SPREAD, 0 },
        { "t2_mip q3dens pak", 5, 0,  57, ALL,      800, Q3SPREAD, 0 },
        /* Narrow-span legs use width 10, leaving 22 lanes idle in a 32-lane
           slot. At 192 rows, SPAN_CAP gives about 42 spans per row. Widths
           10, 57, and 256 hold row and span counts fixed to isolate width;
           their packing counts are comparable. The q3dens leg uses a
           different span density and is a separate workload. */
        { "t1_mip W10",        0, 0,  10, 192, SPAN_CAP, Q3SPREAD, 0 },
        { "t1_mip W57",        0, 0,  57, 192, SPAN_CAP, Q3SPREAD, 0 },
        { "t1_mip W256",       0, 0, 256, 192, SPAN_CAP, Q3SPREAD, 0 },
        { "t1_mip_ab W10",     1, 0,  10, 192, SPAN_CAP, Q3SPREAD, 0 },
        { "t1_mip_ab W57",     1, 0,  57, 192, SPAN_CAP, Q3SPREAD, 0 },
        { "t1_mip_ab W256",    1, 0, 256, 192, SPAN_CAP, Q3SPREAD, 0 },
        { "t2_mip W10",        5, 0,  10, 192, SPAN_CAP, Q3SPREAD, 0 },
        { "t2_mip W57",        5, 0,  57, 192, SPAN_CAP, Q3SPREAD, 0 },
        { "t2_mip W256",       5, 0, 256, 192, SPAN_CAP, Q3SPREAD, 0 },
        { "t2_mip nopack",     5, 0,  57, ALL, SPAN_CAP, Q3SPREAD, 1 },
        { "t2_mip pack",       5, 0,  57, ALL, SPAN_CAP, Q3SPREAD, 0 },
        { "gouraud_z nopack",  4, 0,  57, ALL, SPAN_CAP, Q3SPREAD, 1 },
        { "gouraud_z pack",    4, 0,  57, ALL, SPAN_CAP, Q3SPREAD, 0 },
        /* stacked spans: worst case, nothing is ever disjoint, so
           packing must degrade to the nopack chain rather than break */
        { "t1_mip stacked",    0, 0,  57, ALL, SPAN_CAP,        0, 0 },
        /* stacked, unpacked reference workloads */
        { "t1_mip base",       0, 0,  57, ALL, SPAN_CAP,        0, 1 },
        { "t1_mip base",       0, 0, 256, ALL, SPAN_CAP,        0, 1 },
        { "gouraud_z base",    4, 0,  57, ALL, SPAN_CAP,        0, 1 },
        /* width sweep: fixed per-span cost vs marginal per-pixel cost */
        { "t1_mip W",          0, 0,  32, ALL, SPAN_CAP,        0, 1 },
        { "t1_mip W",          0, 0, 128, ALL, SPAN_CAP,        0, 1 },
        { "t1_mip W",          0, 0, 192, ALL, SPAN_CAP,        0, 1 },
        /* rows down at fixed spans-per-row (total work cut to match):
           workgroup count varies, serial chain does not. Flat wall time
           here means wall = longest row x per-pass latency, and spare
           workgroups are free -- so the lever is chain length. */
        { "t1_mip fixchain",   0, 0,  57,  24,     4096,        0, 1 },
        { "t1_mip fixchain",   0, 0,  57,  12,     2048,        0, 1 },
        { "t1_mip fixchain",   0, 0,  57,   6,     1024,        0, 1 },
        /* Volume-fill legs use dead color, no texturing or depth writes, and
           atomic stencil-byte updates. This workload exercises different
           memory traffic and register demand from the textured legs.
           wmask zero with alpha test off selects the dead-color path at
           runtime on the same kernel. */
        { "cdead q3dens nop",  0, 0,  57, ALL,      800, Q3SPREAD, 1, .commut = 1 },
        { "cdead q3dens pak",  0, 0,  57, ALL,      800, Q3SPREAD, 0, .commut = 1 },
        /* chain depth at a fixed workgroup count: the pass-count axis */
        { "cdead chain",       0, 0,  57, ALL,      400, Q3SPREAD, 0, .commut = 1 },
        { "cdead chain",       0, 0,  57, ALL,     1600, Q3SPREAD, 0, .commut = 1 },
        { "cdead chain",       0, 0,  57, ALL,     3200, Q3SPREAD, 0, .commut = 1 },
        /* rows down at fixed spans-per-row: the occupancy axis. Flat
           wall = wall is set by chain length, not workgroup supply */
        { "cdead fixchain",    0, 0,  57,  48,     4096,        0, 1, .commut = 1 },
        { "cdead fixchain",    0, 0,  57,  24,     2048,        0, 1, .commut = 1 },
        { "cdead fixchain",    0, 0,  57,  12,     1024,        0, 1, .commut = 1 },
        /* Hold the serial chain near 17 spans per row while workgroups vary
           from 48 to 480. Flat time per pixel indicates enough workgroups
           to saturate the GPU; falling time indicates more parallel rows
           improve throughput. */
        { "cdead occ48",       0, 0,  57,  48,      816, Q3SPREAD, 0, .commut = 1 },
        { "cdead occ120",      0, 0,  57, 120,     2040, Q3SPREAD, 0, .commut = 1 },
        { "cdead fold120",     0, 0,  57, 120,     2040, Q3SPREAD, 0,
          .foldcomb = 1, .commut = 1 },
        { "cdead edge120",     0, 0,  57, 120,     2040, Q3SPREAD, 0,
          .foldall = 64, .foldcomb = 1, .commut = 1 },
        { "cdead occ240",      0, 0,  57, 240,     4080, Q3SPREAD, 0, .commut = 1 },
        { "cdead occ480",      0, 0,  57, 480,     8160, Q3SPREAD, 0, .commut = 1 },
        { "t1_mip occ48",      0, 0,  57,  48,      816, Q3SPREAD, 0 },
        { "t1_mip occ480",     0, 0,  57, 480,     8160, Q3SPREAD, 0 },
        /* width: live volume spans are wide, and a 256 px span eats a
           whole 8-slot batch, so width is where pass count is really set */
        { "cdead W",           0, 0,  32, ALL, SPAN_CAP, Q3SPREAD, 0, .commut = 1 },
        { "cdead W",           0, 0, 128, ALL, SPAN_CAP, Q3SPREAD, 0, .commut = 1 },
        { "cdead W",           0, 0, 256, ALL, SPAN_CAP, Q3SPREAD, 0, .commut = 1 },
        /* Rejection and variable-width legs isolate work saved before shading
           and lane-budget rounding. They use packing and the same row
           spread. zctl forces less-or-equal depth, constant depth along a
           span, and no auxiliary scissors, making the rejection rate exact. */
        /* width at matched mean 57: does width variance alone cost? */
        { "wfix",        0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_FIXED, 1,   0, ZC_PIXEL,   0 },
        { "wunif",       0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_UNIF,  1,   0, ZC_PIXEL,   0 },
        { "wskew",       0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW,  1,   0, ZC_PIXEL,   0 },
        /* one kill rate, three coherences: measuring only ZC_PIXEL makes a
           coherent-rejection lever read as worthless */
        { "rej50 px",    0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW,  1,  50, ZC_PIXEL,   0 },
        { "rej50 blk",   0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW,  1,  50, ZC_BLOCK,   0 },
        { "rej50 span",  0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW,  1,  50, ZC_SPAN,    0 },
        { "rej80 blk",   0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW,  1,  80, ZC_BLOCK,   0 },
        { "rej100 span", 0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW,  1, 100, ZC_SPAN,    0 },
        /* ordering discriminator: the alpha test kills after the
           combine chain, the z test before it, so an all-kill alpha leg
           must save far less than rej100 span. If the two come out
           alike, the kernel is not rejecting where the source says. */
        { "akill100",    0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW,  1,   0, ZC_PIXEL, 100 },
        /* An untextured control isolates savings in the texture path. */
        { "gz rej0",     4, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW,  1,   0, ZC_PIXEL,   0 },
        { "gz rej50 blk",4, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW,  1,  50, ZC_BLOCK,   0 },
        /* Combine folding uses C_FOLD with representative constants; other
           selector groups use bench-only B_FOLD bits. Compare each leg's
           timing with its matching unfolded control. Pixels are unchecked. */
        { "fold t1 OFF  ", 0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 0, 0 },
        { "fold t1 fmt  ", 0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 1, 0 },
        { "fold t1 wrap ", 0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 2, 0 },
        { "fold t1 filt ", 0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 4, 0 },
        { "fold t1 comb ", 0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 0, 1 },
        { "fold t1 blend", 0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 16, 0 },
        { "fold t1 zaux ", 0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 32, 0 },
        { "fold t1 ALL  ", 0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 55, 1 },
        { "fold t2 OFF  ", 5, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 0, 0 },
        { "fold t2 fmt  ", 5, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 1, 0 },
        { "fold t2 wrap ", 5, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 2, 0 },
        { "fold t2 filt ", 5, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 4, 0 },
        { "fold t2 comb ", 5, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 0, 1 },
        { "fold t2 blend", 5, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 16, 0 },
        { "fold t2 zaux ", 5, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 32, 0 },
        { "fold t2 ALL  ", 5, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_SKEW, 1, 0, ZC_PIXEL, 0, 55, 1 },
        /* Chain ablation isolates dependent per-pixel work through kernel
           axes (LOD and depth) or nearest filtering (one texel per sample).
           Use the q3dens and packed SPAN_CAP shapes without pinning runtime
           selectors to mismatched bench-only constants. */
        { "chain t1_mip    ", 0, 0, 57, ALL, 800, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 0 },
        { "chain nearest   ", 0, 0, 57, ALL, 800, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 1 },
        { "chain nolod     ", 2, 0, 57, ALL, 800, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 0 },
        { "chain nolod near", 2, 0, 57, ALL, 800, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 1 },
        { "chain noz       ", 7, 0, 57, ALL, 800, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 0 },
        { "chain nolod noz ", 9, 0, 57, ALL, 800, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 0 },
        { "chain nl nz near", 9, 0, 57, ALL, 800, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 1 },
        { "chain gouraud_z ", 4, 0, 57, ALL, 800, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 0 },
        { "chain gouraud   ", 11, 0, 57, ALL, 800, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 0 },
        { "chainP t1_mip   ", 0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 0 },
        { "chainP nearest  ", 0, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 1 },
        { "chainP nolod    ", 2, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 0 },
        { "chainP noz      ", 7, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 0 },
        { "chainP nl nz nea", 9, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 1 },
        { "chainP gouraud_z", 4, 0, 57, ALL, SPAN_CAP, Q3SPREAD, 0, WD_FIXED, 0, 0, 0, 0, 0, 1, 0, 0 },
    };

    rngs = 0x12345678u;
    for (uint32_t k = 0; k < VRAM_SZ + 4; k += 4)
        *(uint32_t *) ((uint8_t *) b_vram.map + k) = rng();
    printf("bench: %u rows (workgroups) per dispatch cap, timing only\n", MAX_PY);
    for (size_t i = 0; i < sizeof(legs) / sizeof(legs[0]); i++)
        bench_leg(&legs[i]);
}

/* Profile legs use 10-pixel spans at about 42 spans per row on folded
   production pipelines: combine and texel-format selectors are pinned.
   A 256-pixel span and an unfolded pipeline provide controls. Single
   executions keep device-scope profiling small; they are not timing
   medians unless GPUTRI_PROF_BENCH is set. */
static void
profbench_run(void)
{
    enum { SPR = 640 - 10 };
    static const bench_leg_t legs[] = {
      /* name                  kern nobar   W rows spans  spread nopack wmode  zctl rej coh   ak fold comb */
        { "prof t2_mip fold W10",     5, 0,  10,  48, 2040,    SPR, 0, WD_FIXED, 1, 0, ZC_PIXEL, 0, 55, 1 },
        { "prof t1_mip_ab fold W10",  1, 0,  10,  48, 2040,    SPR, 0, WD_FIXED, 1, 0, ZC_PIXEL, 0, 55, 1 },
        { "prof t1_nl_ab_z fold W10", 6, 0,  10,  48, 2040,    SPR, 0, WD_FIXED, 1, 0, ZC_PIXEL, 0, 55, 1 },
        { "prof t2_mip fold W256",    5, 0, 256,  48, 2040, 640 - 256, 0, WD_FIXED, 1, 0, ZC_PIXEL, 0, 55, 1 },
        { "prof t2_mip uber W10",     5, 0,  10,  48, 2040,    SPR, 0, WD_FIXED, 1, 0, ZC_PIXEL, 0,  0, 0 },
    };

    /* A positive GPUTRI_PROF_ROWS value up to MAX_PY overrides every leg's
       row/workgroup count. The span count stays fixed, so changing rows
       changes both workgroup supply and spans per row. */
    const char *re    = getenv("GPUTRI_PROF_ROWS");
    int         rows  = re ? atoi(re) : 0;

    g_prof_mode = 1;
    rngs        = 0x12345678u;
    for (uint32_t k = 0; k < VRAM_SZ + 4; k += 4)
        *(uint32_t *) ((uint8_t *) b_vram.map + k) = rng();
    /* Presence of GPUTRI_PROF_BENCH enables timed legs: median of nine,
       with repetitions scaled as in bench_run. These narrow-span legs
       use folded pipelines; bench_run's width-10 legs are unfolded. */
    if (getenv("GPUTRI_PROF_BENCH"))
        g_prof_mode = 0;
    printf(g_prof_mode ? "profbench: single exec per leg for GPU capture, "
                         "not a bench\n"
                       : "profbench: timed legs on the folded pipes\n");
    for (size_t i = 0; i < sizeof(legs) / sizeof(legs[0]); i++) {
        bench_leg_t lg = legs[i];

        if (rows > 0 && rows <= MAX_PY)
            lg.rows = rows;
        bench_leg(&lg);
    }
}

/* Menu-shaped legs use stacked alpha-blended quads: full overlap,
   depth off, and t1_nolod_ab_noz. Kernel, width, overlap, and chain-depth
   controls isolate their costs on the same dispatch machinery. */
static void
menubench_run(void)
{
    enum { ALL = MAX_PY };
    static const bench_leg_t legs[] = {
      /* name              kern nobar   W  rows  spans  spread nopack */
        { "menu ab_noz",       3, 0, 176, ALL,  2880,      0, 0 },
        { "menu ab_noz x4",    3, 0, 176, ALL, 11520,      0, 0 },
        { "menu ab_noz W256",  3, 0, 256, ALL,  2880,      0, 0 },
        { "menu ab_noz W57",   3, 0,  57, ALL,  2880,      0, 0 },
        { "menu ab_noz sprd",  3, 0, 176, ALL,  2880,      1, 0 },
        { "menu ab_z ctl",     6, 0, 176, ALL,  2880,      0, 0 },
        { "menu nolod ctl",    2, 0, 176, ALL,  2880,      0, 0 },
        { "menu t1_mip ctl",   0, 0, 176, ALL,  2880,      0, 0 },
    };

    rngs = 0x12345678u;
    for (uint32_t k = 0; k < VRAM_SZ + 4; k += 4)
        *(uint32_t *) ((uint8_t *) b_vram.map + k) = rng();
    printf("menubench: %u rows per dispatch cap\n", MAX_PY);
    for (size_t i = 0; i < sizeof(legs) / sizeof(legs[0]); i++)
        bench_leg(&legs[i]);
}

/* Coverage assert for the shipped kernel matrix. The device gate
   indexes r128_gpu_variants by axes (gpu_kern_by_axes), so this table is
   the gate: a missing combination is a draw state that silently falls
   back to the CPU, and a duplicate is an unreachable kernel. Untextured
   has no lod axis -- there is no sampler to build a chain for. */
static int
axes_matrix_scan(const struct r128_gpu_variant_t *tab, int n, int quiet)
{
    int seen[3][2][2][2];
    int bad = 0;

    memset(seen, 0, sizeof(seen));
    for (int v = 0; v < n; v++) {
        const struct r128_gpu_variant_t *a = &tab[v];

        if (a->tex < 0 || a->tex > 2) {
            if (!quiet)
                printf("FAIL: kernel %d (%s) has tex=%d out of range\n", v,
                       a->name, a->tex);
            bad++;
            continue;
        }
        if (a->tex == 0 && a->lod) {
            if (!quiet)
                printf("FAIL: kernel %d (%s) is untextured with lod set\n", v,
                       a->name);
            bad++;
        }
        if (seen[a->tex][a->lod][a->blend][a->z]++) {
            if (!quiet)
                printf("FAIL: kernel %d (%s) duplicates axes %d/%d/%d/%d\n", v,
                       a->name, a->tex, a->lod, a->blend, a->z);
            bad++;
        }
    }
    for (int tex = 0; tex <= 2; tex++)
        for (int lod = 0; lod <= (tex ? 1 : 0); lod++)
            for (int bl = 0; bl <= 1; bl++)
                for (int z = 0; z <= 1; z++)
                    if (!seen[tex][lod][bl][z]) {
                        if (!quiet)
                            printf("FAIL: no kernel for axes tex=%d lod=%d "
                                   "blend=%d z=%d\n",
                                   tex, lod, bl, z);
                        bad++;
                    }
    return bad;
}

static int
axes_matrix_check(void)
{
    struct r128_gpu_variant_t t[N_KERNELS];

    /* The assert has to be able to fail, or a gate hole passes unseen.
       A table one entry short (missing combination) and a table with a
       duplicated entry must both be rejected. */
    memcpy(t, r128_gpu_variants, sizeof(t));
    if (axes_matrix_scan(t, N_KERNELS - 1, 1) == 0) {
        printf("FAIL: axes check accepts a table missing a combination\n");
        return 1;
    }
    t[0] = t[1];
    if (axes_matrix_scan(t, N_KERNELS, 1) == 0) {
        printf("FAIL: axes check accepts a duplicated kernel\n");
        return 1;
    }
    return axes_matrix_scan(r128_gpu_variants, N_KERNELS, 0);
}

/* Cold folded-pipeline creation mirrors the device's boot precompile.
   Each synthetic leg uses a disjoint tuple slice from the same generator.
   MoltenVK caches shader conversion by module and specialization values,
   so an empty Vulkan pipeline cache alone cannot ensure a cold leg.
   The seed enumeration provides a separate serial reference point.

   Threads share one shader module and an internally synchronized pipeline
   cache. Pipeline creation does not require external synchronization of
   that module, and the cache does not request external synchronization.
   These legs exercise the same sharing arrangement as the device.
 */
#define PLB_BATCH_MAX 16
#define PLB_THR_MAX   16

typedef struct plb_job_t {
    int      kern;
    uint32_t sel[14];
} plb_job_t;

static plb_job_t      *plb_jobs;
static int             plb_njobs;
static int             plb_batch;
static VkPipeline     *plb_out;
static VkPipelineCache plb_cache;
static VkShaderModule  plb_sm;
static _Atomic int     plb_next;

static void *
plb_worker(void *arg)
{
    (void) arg;
    for (;;) {
        VkComputePipelineCreateInfo cpi[PLB_BATCH_MAX];
        VkSpecializationMapEntry    sme[PLB_BATCH_MAX][19];
        int32_t                     sd[PLB_BATCH_MAX][19];
        VkSpecializationInfo        si[PLB_BATCH_MAX];
        int                         i = atomic_fetch_add(&plb_next, plb_batch);
        int                         n = plb_njobs - i;

        if (i >= plb_njobs)
            return NULL;
        if (n > plb_batch)
            n = plb_batch;
        for (int j = 0; j < n; j++) {
            const plb_job_t *jb = &plb_jobs[i + j];

            for (int k = 0; k < 4; k++)
                sme[j][k] = (VkSpecializationMapEntry) { (uint32_t) k,
                                                         (uint32_t) k * 4, 4 };
            sme[j][4] = (VkSpecializationMapEntry) { 7, 16, 4 };
            for (int k = 0; k < 14; k++)
                sme[j][5 + k] = (VkSpecializationMapEntry) {
                    (uint32_t) (8 + k), (uint32_t) (20 + k * 4), 4
                };
            sd[j][0] = r128_gpu_variants[jb->kern].tex;
            sd[j][1] = r128_gpu_variants[jb->kern].lod;
            sd[j][2] = r128_gpu_variants[jb->kern].blend;
            sd[j][3] = r128_gpu_variants[jb->kern].z;
            sd[j][4] = 1; /* C_FOLD */
            for (int k = 0; k < 14; k++)
                sd[j][5 + k] = (int32_t) jb->sel[k];
            si[j] = (VkSpecializationInfo) { 19, sme[j], sizeof(sd[j]), sd[j] };

            cpi[j] = (VkComputePipelineCreateInfo) {
                VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO
            };
            cpi[j].stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpi[j].stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
            cpi[j].stage.pName  = "main";
            cpi[j].stage.module = plb_sm;
            cpi[j].stage.pSpecializationInfo = &si[j];
            cpi[j].layout       = r_plyt;
        }
        VK_CHECK(vkCreateComputePipelines(dev, plb_cache, (uint32_t) n, cpi,
                                          NULL, &plb_out[i]));
    }
}

static double
plb_leg(const char *tag, int nthr, int batch)
{
    VkPipelineCacheCreateInfo pcci = {
        VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO
    };
    pthread_t thr[PLB_THR_MAX];
    uint64_t  t0;
    double    ms;
    int       live = 0;

    VK_CHECK(vkCreatePipelineCache(dev, &pcci, NULL, &plb_cache));
    plb_batch = batch;
    atomic_store(&plb_next, 0);
    memset(plb_out, 0, (size_t) plb_njobs * sizeof(*plb_out));

    t0 = now_ns();
    for (int i = 1; i < nthr; i++)
        if (pthread_create(&thr[live], NULL, plb_worker, NULL) == 0)
            live++;
    plb_worker(NULL);
    for (int i = 0; i < live; i++)
        pthread_join(thr[i], NULL);
    ms = (double) (now_ns() - t0) / 1e6;

    for (int i = 0; i < plb_njobs; i++) {
        if (!plb_out[i]) {
            printf("FAIL: plbench leg %s left pipeline %d unbuilt\n", tag, i);
            exit(1);
        }
        vkDestroyPipeline(dev, plb_out[i], NULL);
    }
    vkDestroyPipelineCache(dev, plb_cache, NULL);
    plb_cache = VK_NULL_HANDLE;
    printf("  %-28s %3d thr, batch %2d: %8.0f ms (%5.1f ms/pipe)\n", tag, nthr,
           batch, ms, ms / plb_njobs);
    return ms;
}

/* The device's real job list: every seed tuple x every kernel of its
   stage count. Timed once as the production reference. */
static void
plb_build_seeds(void)
{
    int nseed = (int) (sizeof(r128_gpu_comb_seed) / sizeof(r128_gpu_comb_seed[0]));

    plb_njobs = 0;
    for (int s = 0; s < nseed; s++)
        for (int v = 0; v < N_KERNELS; v++) {
            if (r128_gpu_variants[v].tex != r128_gpu_comb_seed[s].stages)
                continue;
            plb_jobs[plb_njobs].kern = v;
            for (int k = 0; k < 14; k++)
                plb_jobs[plb_njobs].sel[k] = r128_gpu_comb_seed[s].sel[k];
            plb_njobs++;
        }
}

/* Pool of synthetic (kernel, tuple) jobs: legal codes from both books,
   so the folded pipelines DCE like real ones and cost the same to build.
   Deduped globally -- a tuple repeated in any later leg would read warm
   off the MoltenVK conversion cache and undercount that leg. Untextured
   kernels are excluded because the seed enumeration has no 0-stage
   entries either, so the real precompile never builds them. */
static plb_job_t *plb_pool;
static int        plb_npool;

static void
plb_gen_pool(int n)
{
    static const uint32_t cfacs[]  = { 0, 1, 4, 5, 6, 7, 8 };
    static const uint32_t ifacs[]  = { 2, 3, 4, 5, 8, 9 };
    static const uint32_t combas[] = { 0, 2, 3, 4, 5, 6, 7, 14 };
    static const uint32_t ifacas[] = { 1, 2, 4 };
    int                   ktab[N_KERNELS], nk = 0;
    uint32_t              r = 0x9e3779b9u;

    for (int v = 0; v < N_KERNELS; v++)
        if (r128_gpu_variants[v].tex >= 1)
            ktab[nk++] = v;

    plb_pool  = calloc((size_t) n, sizeof(*plb_pool));
    plb_npool = 0;
    while (plb_npool < n) {
        plb_job_t j = { 0, { 0 } };
        int       stages, dup = 0;

        r       = r * 1664525u + 1013904223u;
        j.kern  = ktab[(r >> 8) % (uint32_t) nk];
        stages  = r128_gpu_variants[j.kern].tex;
        for (int st = 0; st < 2; st++)
            for (int k = 0; k < 7; k++) {
                uint32_t v = 0;

                r = r * 1664525u + 1013904223u;
                if (st < stages)
                    switch (k) {
                        case 0: v = (r >> 9) & 15u; break;            /* comb  */
                        case 1: v = (r >> 9) & 1u; break;             /* fmsb  */
                        case 2: v = cfacs[(r >> 9) % 7u]; break;
                        case 3: v = ifacs[(r >> 9) % 6u]; break;
                        case 4: v = combas[(r >> 9) % 8u]; break;
                        case 5: v = ((r >> 9) & 1u) ? 7u : 6u; break; /* afac  */
                        default: v = ifacas[(r >> 9) % 3u]; break;
                    }
                j.sel[st * 7 + k] = v;
            }
        for (int i = 0; i < plb_npool && !dup; i++)
            dup = plb_pool[i].kern == j.kern
               && !memcmp(plb_pool[i].sel, j.sel, sizeof(j.sel));
        /* also collides with the seed enumeration leg, which runs first */
        for (int s = 0; s < (int) (sizeof(r128_gpu_comb_seed)
                                   / sizeof(r128_gpu_comb_seed[0]))
             && !dup; s++) {
            uint32_t sel[14];

            for (int k = 0; k < 14; k++)
                sel[k] = r128_gpu_comb_seed[s].sel[k];
            dup = r128_gpu_variants[j.kern].tex == r128_gpu_comb_seed[s].stages
               && !memcmp(sel, j.sel, sizeof(sel));
        }
        if (!dup)
            plb_pool[plb_npool++] = j;
    }
}

/* Host leveler cost A/B: times seg_sort -- the
   counting sort plus seg_batch_row's O(n^2) pair scan over bb_of --
   with no GPU exec. Legs pick spans-per-row: c~1024 saturates
   BATCH_LEVEL_MAX (the worst leveled quadratic), c~170 is a dense
   segment, q3dens is the live-Q3 shape. */
static void
lvbench_run(void)
{
    enum { ITERS = 15 };
    static const bench_leg_t legs[] = {
      /* name       kern nobar  W rows              spans    spread nopack */
        { "lv c1024",  4, 0,   57,   8, (int) FZ_SPAN_CAP,        0, 0 },
        { "lv c170",   4, 0,   57,  48, (int) FZ_SPAN_CAP, 640 - 57, 0 },
        { "lv q3dens", 4, 0,   57,  48,               800, 640 - 57, 0 },
    };

    printf("lvbench: host seg_sort timing only, no GPU exec\n");
    for (size_t i = 0; i < sizeof(legs) / sizeof(legs[0]); i++) {
        const bench_leg_t *lg = &legs[i];
        fz_disp_t          disp[FZ_RUN_CAP];
        uint64_t           t[ITERS];

        g_bench_rows   = lg->rows;
        g_bench_spans  = lg->spans;
        g_bench_spread = lg->spread;
        g_bench_nopack = 0;
        bench_build(lg);
        for (int w = 0; w < 2; w++)
            seg_sort(disp);
        for (int k = 0; k < ITERS; k++) {
            uint64_t t0 = now_ns();

            seg_sort(disp);
            t[k] = now_ns() - t0;
        }
        qsort(t, ITERS, sizeof(t[0]), u64_cmp);
        printf("%-10s spans=%-5u rows=%-3d median %8.3f ms  %7.2f ns/span\n",
               lg->name, nspans, lg->rows,
               (double) t[ITERS / 2] / 1e6,
               (double) t[ITERS / 2] / (double) nspans);
    }
}

static void
plb_build_slice(int leg, int n)
{
    memcpy(plb_jobs, &plb_pool[(leg - 1) * n], (size_t) n * sizeof(*plb_jobs));
    plb_njobs = n;
}

static void
plbench_run(void)
{
    VkShaderModuleCreateInfo smci = {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
    };
    int    nseed = (int) (sizeof(r128_gpu_comb_seed) / sizeof(r128_gpu_comb_seed[0]));
    int    hwthr = (int) sysconf(_SC_NPROCESSORS_ONLN);
    int    npar  = hwthr - 2;
    int    slice, cap = nseed * N_KERNELS;
    double base;

    smci.codeSize = sizeof(r128_gpu_seg_spv);
    smci.pCode    = (const uint32_t *) r128_gpu_seg_spv;
    VK_CHECK(vkCreateShaderModule(dev, &smci, NULL, &plb_sm));

    if (npar < 1)
        npar = 1;
    if (npar > PLB_THR_MAX)
        npar = PLB_THR_MAX;
    plb_jobs = calloc((size_t) cap, sizeof(*plb_jobs));
    plb_out  = calloc((size_t) cap, sizeof(*plb_out));

    plb_build_seeds();
    slice = plb_njobs;
    plb_gen_pool(slice * 5);
    printf("plbench: %d pipelines per leg, %d hw threads, %d worker threads\n",
           slice, hwthr, npar);
    printf("  (each leg a disjoint cold tuple slice; leg 0 is the real "
           "seed enumeration)\n");

    /* Time the device's boot precompile job list against the host's
       current caches. MoltenVK's conversion cache persists across processes,
       so some tuples can be warm and others cold. This leg measures that
       host state rather than providing a cold-run speedup baseline. */
    plb_leg("seed enum (as-cached)", 1, 1);
    plb_build_slice(1, slice);
    base = plb_leg("synthetic, serial x1", 1, 1);
    plb_build_slice(2, slice);
    printf("  ---- speedup vs cold serial one-at-a-time ----\n");
    printf("  serial batched:   %.2fx\n",
           base / plb_leg("synthetic, serial x8", 1, 8));
    plb_build_slice(3, slice);
    printf("  parallel batched: %.2fx\n",
           base / plb_leg("synthetic, parallel x8", npar, 8));
    plb_build_slice(4, slice);
    printf("  parallel single:  %.2fx\n",
           base / plb_leg("synthetic, parallel x1", npar, 1));
    plb_build_slice(5, slice);
    printf("  parallel wide:    %.2fx\n",
           base / plb_leg("synthetic, wide x8", hwthr, 8));

    vkDestroyShaderModule(dev, plb_sm, NULL);
    free(plb_jobs);
    free(plb_out);
}

int
main(int argc, char **argv)
{
    int      iters = (argc > 1) ? atoi(argv[1]) : 150;
    unsigned seed  = (argc > 2) ? (unsigned) strtoul(argv[2], NULL, 0) : 0;
    static const unsigned seeds[3] = { 0x12345678u, 0xcafe0001u, 0xdeadbeefu };
    int      nseeds = seed ? 1 : 3;

    {
        const char *e = getenv("GPUTRI_PIXDUMP");

        while (e && e[0] && g_pixdump_n < 8) {
            g_pixdump[g_pixdump_n++] = (uint32_t) strtoul(e, NULL, 0);
            if ((e = strchr(e, ',')) != NULL)
                e++;
        }
    }
    init_vulkan();
    if (argc > 1 && !strcmp(argv[1], "plbench")) {
        plbench_run();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "lvbench")) {
        lvbench_run();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "headroom"))
        return headroom_check();
    if (argc > 1 && !strcmp(argv[1], "bench")) {
        for (int k = 0; k < 4; k++)
            for (int i = 0; i < 256; i++)
                g_texpals[k][i] = rng();
        memcpy(b_pal.map, g_texpals, sizeof(g_texpals));
        bench_run();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "profbench")) {
        for (int k = 0; k < 4; k++)
            for (int i = 0; i < 256; i++)
                g_texpals[k][i] = rng();
        memcpy(b_pal.map, g_texpals, sizeof(g_texpals));
        profbench_run();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "menubench")) {
        for (int k = 0; k < 4; k++)
            for (int i = 0; i < 256; i++)
                g_texpals[k][i] = rng();
        memcpy(b_pal.map, g_texpals, sizeof(g_texpals));
        menubench_run();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "f64")) {
        /* the soft-f64 and ladder checks alone, for a shader edit to
           vid_ati_rage128_gpu_f64.glsl or the ladder kernel */
        int bad = f64_selftest() + f64_except_check() + ladder_check();

        printf("f64 mode: %d mismatches\n", bad);
        return bad != 0;
    }
    if (axes_matrix_check() != 0) {
        printf("FAIL: shipped kernel matrix is not the complete axis set\n");
        return 1;
    }
    if (f64_selftest() != 0) {
        printf("FAIL: soft-f64 diverges from host doubles\n");
        return 1;
    }
    if (f64_except_check() != 0) {
        printf("FAIL: soft-f64 add diverges from host doubles on an exceptional class\n");
        return 1;
    }
    if (ladder_check() != 0) {
        printf("FAIL: production ladder diverges from host doubles\n");
        return 1;
    }
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 256; i++)
            g_texpals[k][i] = rng();
    memcpy(b_pal.map, g_texpals, sizeof(g_texpals));
    if (wrap_check() != 0)
        return 1;
    if (headroom_check() != 0)
        return 1;

    g_stipple_fuzz = 1;
    for (int s = 0; s < nseeds; s++) {
        unsigned sv = seed ? seed : seeds[s];

        rngs      = sv;
        xrngs     = ~sv;
        g_seedidx = s;
        tpool_reset(); /* new seed, new stratified pool */
        tpool_prime(); /* parallel precompile of the seed's fold matrix */
        for (int k = 0; k < N_KERNELS; k++)
            fuzz_state(k, iters);
        fuzz_segments(iters * 2);
        fuzz_alias(iters / 2);
        fuzz_wrap(iters);
        fuzz_lines(iters);
        fuzz_edge();
        printf("odd-offset: c16=%llu c32=%llu z16=%llu z32=%llu both=%llu "
               "edge-c=%llu edge-z=%llu\n",
               (unsigned long long) odd_c_px[0],
               (unsigned long long) odd_c_px[1],
               (unsigned long long) odd_z_px[0],
               (unsigned long long) odd_z_px[1],
               (unsigned long long) odd_both_px,
               (unsigned long long) edge_c_px,
               (unsigned long long) edge_z_px);
        printf("seed %08x done: %llu cases, %llu tri-rows, %llu fails "
               "(serial cz=%llu rtt=%llu rej cross=%llu fold=%llu) "
               "fold-pipes=%d\n",
               sv, (unsigned long long) cases_run,
               (unsigned long long) px_rows_run, (unsigned long long) fails,
               (unsigned long long) ser_self, (unsigned long long) ser_rtt,
               (unsigned long long) rej_cross,
               (unsigned long long) fold_cases, fold_pipes_built);
        fold_pipes_built = 0;
    }
    /* persist the pipeline cache so repeat runs compile warm */
    {
        size_t sz = 0;

        if (vkGetPipelineCacheData(dev, plcache, &sz, NULL) == VK_SUCCESS
            && sz > 0) {
            void *data = malloc(sz);

            if (data
                && vkGetPipelineCacheData(dev, plcache, &sz, data) == VK_SUCCESS) {
                FILE *pf = fopen("gputri.plcache", "wb");

                if (pf) {
                    fwrite(data, 1, sz, pf);
                    fclose(pf);
                }
            }
            free(data);
        }
    }
    if (ser_self == 0 || ser_rtt == 0 || rej_cross == 0
        || ser_faults != 2u * (uint64_t) nseeds) {
        printf("FAIL: alias-serialization paths never exercised "
               "(cz=%llu rtt=%llu cross=%llu faults=%llu)\n",
               (unsigned long long) ser_self, (unsigned long long) ser_rtt,
               (unsigned long long) rej_cross,
               (unsigned long long) ser_faults);
        return 1;
    }
    if (line_draws == 0 || line_faults != (uint64_t) nseeds) {
        printf("FAIL: line arm never exercised (draws=%llu faults=%llu)\n",
               (unsigned long long) line_draws,
               (unsigned long long) line_faults);
        return 1;
    }
    if (bat_multi == 0 || bat_split == 0 || bat_nobar == 0
        || uniform_rows == 0 || row_dense == 0 || row_mixed == 0
        || commut_exempt == 0) {
        printf("FAIL: batch packing never exercised (multi=%llu split=%llu "
               "nobar=%llu uniform=%llu dense=%llu mixed=%llu exempt=%llu)\n",
               (unsigned long long) bat_multi, (unsigned long long) bat_split,
               (unsigned long long) bat_nobar,
               (unsigned long long) uniform_rows,
               (unsigned long long) row_dense, (unsigned long long) row_mixed,
               (unsigned long long) commut_exempt);
        return 1;
    }
    if (disp_bar == 0 || disp_nobar == 0 || disp_texbar == 0
        || disp_overlapbar == 0 || disp_unionbar == 0) {
        printf("FAIL: dispatch barriers never exercised (bar=%llu elide=%llu "
               "texture=%llu overlap=%llu union=%llu)\n",
               (unsigned long long) disp_bar,
               (unsigned long long) disp_nobar,
               (unsigned long long) disp_texbar,
               (unsigned long long) disp_overlapbar,
               (unsigned long long) disp_unionbar);
        return 1;
    }
    if (fold_cases == 0 || fold_splits == 0 || fold_faults == 0) {
        printf("FAIL: folded pipelines never exercised "
               "(cases=%llu splits=%llu faults=%llu)\n",
               (unsigned long long) fold_cases,
               (unsigned long long) fold_splits,
               (unsigned long long) fold_faults);
        return 1;
    }
    if (dst_fmt_px[3] == 0 || dst_fmt_px[4] == 0 || dst_fmt_px[15] == 0
        || dst_fmt_px[6] == 0 || dst_undith_px == 0 || dst_masked_px == 0
        || dst_cdead_px == 0) {
        printf("FAIL: dst surface axes not covered (1555=%llu 565=%llu "
               "4444=%llu 8888=%llu undithered=%llu plane-masked=%llu "
               "cdead=%llu)\n",
               (unsigned long long) dst_fmt_px[3],
               (unsigned long long) dst_fmt_px[4],
               (unsigned long long) dst_fmt_px[15],
               (unsigned long long) dst_fmt_px[6],
               (unsigned long long) dst_undith_px,
               (unsigned long long) dst_masked_px,
               (unsigned long long) dst_cdead_px);
        return 1;
    }
    if (z_cell_px[0] == 0 || z_cell_px[1] == 0 || z_cell_px[2] == 0) {
        printf("FAIL: depth cell shapes not covered "
               "(z16=%llu z32lo=%llu z32hi=%llu)\n",
               (unsigned long long) z_cell_px[0],
               (unsigned long long) z_cell_px[1],
               (unsigned long long) z_cell_px[2]);
        return 1;
    }
    if (odd_c_px[0] == 0 || odd_c_px[1] == 0 || odd_z_px[0] == 0
        || odd_z_px[1] == 0 || odd_both_px == 0 || edge_c_px == 0
        || edge_z_px == 0 || edge_faults != (uint64_t) nseeds) {
        printf("FAIL: straddling-cell axes not covered (c16=%llu c32=%llu "
               "z16=%llu z32=%llu both=%llu edge-c=%llu edge-z=%llu "
               "efaults=%llu)\n",
               (unsigned long long) odd_c_px[0],
               (unsigned long long) odd_c_px[1],
               (unsigned long long) odd_z_px[0],
               (unsigned long long) odd_z_px[1],
               (unsigned long long) odd_both_px,
               (unsigned long long) edge_c_px,
               (unsigned long long) edge_z_px,
               (unsigned long long) edge_faults);
        return 1;
    }
    if (spec_px == 0 || spec_clamp_px == 0) {
        printf("FAIL: specular not covered (spec=%llu clamped=%llu)\n",
               (unsigned long long) spec_px,
               (unsigned long long) spec_clamp_px);
        return 1;
    }
    printf("spec coverage: spec=%llu clamped=%llu (px)\n",
           (unsigned long long) spec_px,
           (unsigned long long) spec_clamp_px);
    if (fogv_px == 0 || fogv_clamp_lo_px == 0 || fogv_clamp_hi_px == 0
        || fogt_px == 0) {
        printf("FAIL: fog not covered (fog=%llu clamp-lo=%llu "
               "clamp-hi=%llu table=%llu)\n",
               (unsigned long long) fogv_px,
               (unsigned long long) fogv_clamp_lo_px,
               (unsigned long long) fogv_clamp_hi_px,
               (unsigned long long) fogt_px);
        return 1;
    }
    printf("fog coverage: fog=%llu clamp-lo=%llu clamp-hi=%llu table=%llu (px)\n",
           (unsigned long long) fogv_px,
           (unsigned long long) fogv_clamp_lo_px,
           (unsigned long long) fogv_clamp_hi_px,
           (unsigned long long) fogt_px);
    if (ck_fn_px[0] == 0 || ck_fn_px[1] == 0 || ck3d_kill_px == 0
        || ckc_kill_px == 0 || ck_pass_px == 0) {
        printf("FAIL: chroma key not covered (fn2=%llu fn3=%llu kill-3d=%llu "
               "kill-c=%llu pass=%llu)\n",
               (unsigned long long) ck_fn_px[0],
               (unsigned long long) ck_fn_px[1],
               (unsigned long long) ck3d_kill_px,
               (unsigned long long) ckc_kill_px,
               (unsigned long long) ck_pass_px);
        return 1;
    }
    printf("ck coverage: fn2=%llu fn3=%llu kill-3d=%llu kill-c=%llu "
           "pass=%llu (px)\n",
           (unsigned long long) ck_fn_px[0],
           (unsigned long long) ck_fn_px[1],
           (unsigned long long) ck3d_kill_px,
           (unsigned long long) ckc_kill_px,
           (unsigned long long) ck_pass_px);
    printf("z coverage: z16=%llu z32-depthlo=%llu z32-depthhi=%llu (px tested)\n",
           (unsigned long long) z_cell_px[0],
           (unsigned long long) z_cell_px[1],
           (unsigned long long) z_cell_px[2]);
    {
        int op_miss = -1;

        for (int o = 0; o < 8; o++)
            if (sten_op_px[o] == 0)
                op_miss = o;
        if (sten_px[0] == 0 || sten_px[1] == 0 || sten_path_px[0] == 0
            || sten_path_px[1] == 0 || sten_path_px[2] == 0
            || sten_zoff_px == 0 || sten_akill_px == 0 || op_miss >= 0) {
            printf("FAIL: stencil axes not covered (lo=%llu hi=%llu "
                   "sfail=%llu zfail=%llu zpass=%llu z-off=%llu "
                   "alpha-killed=%llu missing-op=%d)\n",
                   (unsigned long long) sten_px[0],
                   (unsigned long long) sten_px[1],
                   (unsigned long long) sten_path_px[0],
                   (unsigned long long) sten_path_px[1],
                   (unsigned long long) sten_path_px[2],
                   (unsigned long long) sten_zoff_px,
                   (unsigned long long) sten_akill_px, op_miss);
            return 1;
        }
        printf("sten coverage: lo=%llu hi=%llu paths sfail/zfail/zpass="
               "%llu/%llu/%llu z-off=%llu alpha-killed=%llu ops=",
               (unsigned long long) sten_px[0],
               (unsigned long long) sten_px[1],
               (unsigned long long) sten_path_px[0],
               (unsigned long long) sten_path_px[1],
               (unsigned long long) sten_path_px[2],
               (unsigned long long) sten_zoff_px,
               (unsigned long long) sten_akill_px);
        for (int o = 0; o < 8; o++)
            printf("%s%llu", o ? "/" : "", (unsigned long long) sten_op_px[o]);
        printf(" (px)\n");
    }
    printf("tex coverage:");
    for (int f = 0; f < 16; f++)
        if (tex_fmt_use[f])
            printf(" dt%d=%llu", f, (unsigned long long) tex_fmt_use[f]);
    printf(" | affine=%llu persp=%llu sec-only=%llu | alpha-one=%llu light=%llu "
           "(stage uses, accepted draws)\n",
           (unsigned long long) tex_persp_use[0],
           (unsigned long long) tex_persp_use[1],
           (unsigned long long) tex_seconly_use,
           (unsigned long long) tex_aone_use,
           (unsigned long long) tex_light_use);
    if (tex_seconly_use == 0) {
        printf("FAIL: secondary-only texture stage never exercised\n");
        return 1;
    }
    if (tex_aone_use == 0) {
        printf("FAIL: texture alpha-one never exercised\n");
        return 1;
    }
    if (tex_light_use == 0) {
        printf("FAIL: texture lighting never exercised\n");
        return 1;
    }
    printf("staged-RT coverage: cstg=%llu zstg=%llu (cases)\n",
           (unsigned long long) cstg_cases, (unsigned long long) zstg_cases);
    printf("dst coverage: 1555=%llu 565=%llu 4444=%llu 8888=%llu "
           "undithered=%llu plane-masked=%llu cdead=%llu (pixels written)\n",
           (unsigned long long) dst_fmt_px[3],
           (unsigned long long) dst_fmt_px[4],
           (unsigned long long) dst_fmt_px[15],
           (unsigned long long) dst_fmt_px[6],
           (unsigned long long) dst_undith_px,
           (unsigned long long) dst_masked_px,
           (unsigned long long) dst_cdead_px);
    for (int v = 0; v < N_KERNELS; v++)
        if (kern_cases[v] == 0 || kern_wrote[v] == 0) {
            printf("FAIL: kernel %d (%s) not covered (cases=%llu wrote=%llu)\n",
                   v, r128_gpu_variants[v].name,
                   (unsigned long long) kern_cases[v],
                   (unsigned long long) kern_wrote[v]);
            return 1;
        }
    printf("batch coverage: packed=%llu overlap-split=%llu commut-exempt=%llu "
           "nobar=%llu uniform=%llu dense=%llu mixed=%llu\n",
           (unsigned long long) bat_multi, (unsigned long long) bat_split,
           (unsigned long long) commut_exempt, (unsigned long long) bat_nobar,
           (unsigned long long) uniform_rows,
           (unsigned long long) row_dense, (unsigned long long) row_mixed);
    printf("dispatch coverage: barriers=%llu elided=%llu texture=%llu "
           "overlap=%llu union=%llu\n",
           (unsigned long long) disp_bar,
           (unsigned long long) disp_nobar,
           (unsigned long long) disp_texbar,
           (unsigned long long) disp_overlapbar,
           (unsigned long long) disp_unionbar);
    printf("fold coverage: cases=%llu tuple-splits=%llu folded-faults=%llu\n",
           (unsigned long long) fold_cases, (unsigned long long) fold_splits,
           (unsigned long long) fold_faults);
    printf("%s: %llu cases, %llu fails\n", fails ? "FAIL" : "PASS",
           (unsigned long long) cases_run, (unsigned long long) fails);
    return fails ? 1 : 0;
}
