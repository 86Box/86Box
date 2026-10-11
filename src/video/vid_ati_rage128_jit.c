/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- span JIT driver (the portable half).
 *
 *          The span JIT compiles one host function per 3D draw state.
 *          The function rasterizes one pixel row of a primitive and must
 *          write exactly the bytes the interpreter's pixel loop in
 *          vid_ati_rage128_3d.c writes for that row. This file holds the
 *          block cache, the executable memory and the verify harness.
 *          The emitters are vid_ati_rage128_codegen_arm64.h and
 *          vid_ati_rage128_codegen_x86_64.h; on any other host this file
 *          builds as a stub that never returns a block, and every row
 *          runs on the interpreter.
 *
 *          Threading: rage128_jit_get_block is called only by the thread
 *          that submits primitives, the CCE executor. The raster workers
 *          run only inside rage128_raster_flush, and its callers ensure
 *          no primitive is submitted while it runs. Compiling and
 *          running blocks therefore never overlap, so the cache needs no
 *          lock: a worker only calls a block that was finished before
 *          the flush started.
 *
 *          Modes. The per-VM "recompiler" option selects off or on. The
 *          R128_JIT environment variable overrides it for development:
 *          0, 1 or "verify".
 *            off    -- rage128_jit_get_block returns NULL; interpreter
 *                      only.
 *            on     -- a compiled block replaces the interpreter's loop
 *                      for each row.
 *            verify -- both run for each row. The block's writes are
 *                      captured and rolled back, the interpreter draws
 *                      the row, and the two results are compared byte
 *                      for byte. The interpreter's output is the one
 *                      kept. rage128_raster_init forces a single render
 *                      thread in this mode.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_ati_rage128.h>
#include <86box/vid_ati_rage128_fnv.h>

#if defined(__APPLE__) && (defined(__aarch64__) || defined(_M_ARM64))
#    include <pthread.h>
#endif

/* R128_JIT_HAVE_BACKEND and R128_JIT_BACKEND_A64 are defined in
   vid_ati_rage128.h, because the device config table in
   vid_ati_rage128.c uses the same test to decide whether to offer the
   "recompiler" option. */

/* Map the emitter entry points of the host architecture to neutral
   names, so the code below calls no architecture-specific function. */
#if R128_JIT_HAVE_BACKEND
#    if R128_JIT_BACKEND_A64
#        include <86box/vid_ati_rage128_codegen_arm64.h>
#        define R128_JIT_BLOCK_SIZE            R128_A64_BLOCK_SIZE
#        define r128_jit_backend_generate      r128_jit_arm64_generate
#        define r128_jit_backend_texinline_can r128_a64_texinline_can
#    else
#        include <86box/vid_ati_rage128_codegen_x86_64.h>
#        define R128_JIT_BLOCK_SIZE            R128_X64_BLOCK_SIZE
#        define r128_jit_backend_generate      r128_jit_x64_generate
#        define r128_jit_backend_texinline_can r128_x64_texinline_can
#        ifdef _MSC_VER
#            include <intrin.h>
#        endif
#    endif
#    ifdef _WIN32
#        include <windows.h>
#    endif
#else
#    define R128_JIT_BLOCK_SIZE 16384
#endif

/* Block-cache size. A Quake 3 style OpenGL game cycles through hundreds
   of distinct draw states per frame. A cache of 64 slots thrashes under
   it, and the submit thread recompiles every few draws. 2048 slots hold
   a whole level's states. Each slot occupies 16 KB in one 32 MB
   executable arena, so large-page allocation rounds the cache size
   rather than each slot. A slot's pages are first written when a block
   is compiled into it. */
#define R128_JIT_BLOCKS      2048
#define R128_JIT_MODE_OFF    0
#define R128_JIT_MODE_ON     1
#define R128_JIT_MODE_VERIFY 2

/* Verify scratch holds one row of color and Z, at up to 4 bytes per
   pixel, for each rasterizing thread. A row wider than this many pixels
   is not verified: it is counted in v_rows_skipped and drawn by the
   interpreter alone. */
#define R128_JIT_VMAX 4096

typedef struct r128_jit_block_t {
    rage128_draw_state_t key;
    uint64_t             hash; /* FNV-1a of key, checked before memcmp */
    uint8_t             *code;
    r128_jit_span_fn     fn; /* NULL: refused by the emitter, cached */
    uint64_t             last_used;
    int                  valid;
} r128_jit_block_t;

typedef struct rage128_jit_t {
    int              mode;
    int              prof; /* R128_JIT_PROF=1: span-shape counters */
    int              mru;
    uint64_t         gen;
    uint8_t         *arena;
    r128_jit_block_t blocks[R128_JIT_BLOCKS];

    /* Cache counters, written only by the submitting thread. */
    uint64_t lookups, compiles, rejects, evictions;
    uint64_t compiles_texinline, compiles_texcall;

    /* Verify counters, updated from inside the raster loop. They are
       atomic, so they need no lock whichever thread runs the loop. */
    _Atomic uint64_t v_rows, v_rows_tiled, v_rows_skipped;
    _Atomic uint64_t v_mismatch_rows, v_mismatch_px, v_rx_mismatch;
    _Atomic uint32_t v_detail_logged;

    /* Span-shape profile, added to by each raster worker at most once
       per triangle. Rows per triangle, pixels per row and the share of
       empty rows measure how much per-row call overhead a block that
       draws a whole triangle would save. */
    _Atomic uint64_t p_tris, p_rows, p_rows_empty, p_px;
} rage128_jit_t;

/* Per-thread verify state for the row in progress. verify_pre fills it
   (the original bytes, the block's bytes and its written-column range)
   and verify_post compares the interpreter's result against it. */
typedef struct r128_jit_vrow_t {
    int      active;
    int      c_tld, z_tld;
    int32_t  jit_rx0, jit_rx1;
    uint32_t drow_saved, zrow_saved;
    uint8_t  fb_jit[R128_JIT_VMAX * 4];
    uint8_t  fb_orig[R128_JIT_VMAX * 4];
    uint8_t  z_jit[R128_JIT_VMAX * 4];
    uint8_t  z_orig[R128_JIT_VMAX * 4];
} r128_jit_vrow_t;

static __thread r128_jit_vrow_t r128_jit_vrow;

#if R128_JIT_HAVE_BACKEND && !R128_JIT_BACKEND_A64
static int
r128_jit_host_sse41(void)
{
#    ifdef _MSC_VER
    int r[4];

    __cpuid(r, 1);
    return (r[2] >> 19) & 1; /* CPUID leaf 1, ECX bit 19: SSE4.1 */
#    else
    return __builtin_cpu_supports("sse4.1");
#    endif
}
#endif

void
rage128_jit_init(rage128_t *dev)
{
    rage128_jit_t *jit;
    const char    *env  = getenv("R128_JIT");
    int            mode = dev->jit_enabled ? R128_JIT_MODE_ON : R128_JIT_MODE_OFF;

    if (env && env[0]) {
        if (!strcmp(env, "verify"))
            mode = R128_JIT_MODE_VERIFY;
        else
            mode = atoi(env) ? R128_JIT_MODE_ON : R128_JIT_MODE_OFF;
    }
#if !R128_JIT_HAVE_BACKEND
    mode = R128_JIT_MODE_OFF;
#elif !R128_JIT_BACKEND_A64
    /* The x86-64 emitters assume SSE4.1: the scalar emitter uses
       PINSRD, PEXTRD, PMINUD, INSERTPS and PMOVZXBD, and the vector and
       texture headers add others (PMAXUD, PACKUSDW, ROUNDPS, BLENDVPS).
       A host without it gets the interpreter. */
    if (mode != R128_JIT_MODE_OFF && !r128_jit_host_sse41()) {
        pclog("RAGE128 JIT: host lacks SSE4.1, JIT disabled\n");
        mode = R128_JIT_MODE_OFF;
    }
#endif

    dev->jit = NULL;
    if (mode == R128_JIT_MODE_OFF)
        return;

    jit = (rage128_jit_t *) calloc(1, sizeof(rage128_jit_t));
    if (!jit)
        return;
    jit->mode = mode;
    env       = getenv("R128_JIT_PROF");
    jit->prof = env && atoi(env);

    if ((size_t) R128_JIT_BLOCKS > SIZE_MAX / R128_JIT_BLOCK_SIZE
        || !(jit->arena = (uint8_t *) plat_mmap((size_t) R128_JIT_BLOCKS * R128_JIT_BLOCK_SIZE, 1, NULL))) {
        free(jit);
        pclog("RAGE128 JIT: executable alloc failed, JIT disabled\n");
        return;
    }
    for (int i = 0; i < R128_JIT_BLOCKS; i++)
        jit->blocks[i].code = jit->arena + (size_t) i * R128_JIT_BLOCK_SIZE;
    dev->jit = jit;
    pclog("RAGE128 JIT: mode=%s, %d blocks x %d bytes\n",
          mode == R128_JIT_MODE_VERIFY ? "verify" : "on",
          R128_JIT_BLOCKS, R128_JIT_BLOCK_SIZE);
}

void
rage128_jit_close(rage128_t *dev)
{
    rage128_jit_t *jit = (rage128_jit_t *) dev->jit;

    if (!jit)
        return;

    rage128_log("RAGE128 JIT: close: lookups=%llu compiles=%llu (texinline=%llu texcall=%llu) rejects=%llu evictions=%llu\n",
                (unsigned long long) jit->lookups, (unsigned long long) jit->compiles,
                (unsigned long long) jit->compiles_texinline,
                (unsigned long long) jit->compiles_texcall,
                (unsigned long long) jit->rejects, (unsigned long long) jit->evictions);
    if (jit->mode == R128_JIT_MODE_VERIFY) {
        pclog("RAGE128 JIT: verify: rows=%llu tiled=%llu skipped=%llu mismatch_rows=%llu mismatch_px=%llu rx_mismatch=%llu\n",
              (unsigned long long) atomic_load(&jit->v_rows),
              (unsigned long long) atomic_load(&jit->v_rows_tiled),
              (unsigned long long) atomic_load(&jit->v_rows_skipped),
              (unsigned long long) atomic_load(&jit->v_mismatch_rows),
              (unsigned long long) atomic_load(&jit->v_mismatch_px),
              (unsigned long long) atomic_load(&jit->v_rx_mismatch));
    }
#ifdef ENABLE_RAGE128_LOG
    {
        uint64_t pt = atomic_load(&jit->p_tris), pr = atomic_load(&jit->p_rows);
        uint64_t pe = atomic_load(&jit->p_rows_empty), pp = atomic_load(&jit->p_px);

        if (pr)
            rage128_log("RAGE128 JIT: spans: tris=%llu rows=%llu (empty=%llu, %.1f%%) px=%llu rows/tri=%.2f px/row=%.2f\n",
                        (unsigned long long) pt, (unsigned long long) pr,
                        (unsigned long long) pe, 100.0 * (double) pe / (double) pr,
                        (unsigned long long) pp,
                        pt ? (double) pr / (double) pt : 0.0,
                        (pr - pe) ? (double) pp / (double) (pr - pe) : 0.0);
    }
#endif

    plat_munmap(jit->arena, (size_t) R128_JIT_BLOCKS * R128_JIT_BLOCK_SIZE);
    free(jit);
    dev->jit = NULL;
}

int
rage128_jit_tex_inline(const rage128_draw_state_t *ds)
{
#if R128_JIT_HAVE_BACKEND
    /* The same test the emitters' setup uses to pick inline texture
       sampling. Table fog keeps a state on the per-pixel
       rage128_texstage_run call even when the emitter could sample
       inline, so the answer matches the block that is emitted. Before
       it runs an inline block, the caller resolves every mip slot the
       block can read: an inline block reads the slot cache directly,
       without the call that would resolve a slot on demand. */
    return r128_jit_backend_texinline_can(ds)
        && !(ds->fog_en && ds->fog_table_en);
#else
    (void) ds;
    return 0;
#endif
}

int
rage128_jit_census_verdict(const rage128_draw_state_t *ds)
{
#if R128_JIT_HAVE_BACKEND
    int tex_on, tex_inline, soa_on;

    /* 0 = refused, 1 = scalar block, 2 = block with the vector (SoA)
       loop. The refusals in rage128_jit_get_block come first, then the
       emitter's own gate, then its SoA choice, copied from the
       emitters' setup functions with no_soa clear. The emitters'
       retry without SoA after a block overflows is not modeled. */
    if (ds->tex_tiled)
        return 0;
#    if R128_JIT_BACKEND_A64
    if (!r128_jit_arm64_can(ds))
        return 0;
    tex_on     = ds->tex_en || ds->sec_en;
    tex_inline = tex_on && r128_a64_texinline_can(ds)
        && !(ds->fog_en && ds->fog_table_en);
    soa_on = tex_on ? (!ds->soa_selftex && tex_inline && r128_a64_soa_tex_can(ds))
                    : r128_a64_soa_can(ds);
#    else
    if (!r128_jit_x64_can(ds))
        return 0;
    tex_on     = ds->tex_en || ds->sec_en;
    tex_inline = tex_on && r128_x64_texinline_can(ds)
        && !(ds->fog_en && ds->fog_table_en);
    soa_on = (!ds->soa_selftex && tex_inline && r128_jit_x64_soa_tex_can(ds))
        || (!tex_on && r128_jit_x64_soa_can(ds));
#    endif
    return soa_on ? 2 : 1;
#else
    (void) ds;
    return 0;
#endif
}

void
rage128_jit_verify_stats(rage128_t *dev, uint64_t *rows, uint64_t *tiled,
                         uint64_t *skipped, uint64_t *mismatch_rows)
{
    rage128_jit_t *jit = (rage128_jit_t *) dev->jit;

    *rows = *tiled = *skipped = *mismatch_rows = 0;
    if (!jit || jit->mode != R128_JIT_MODE_VERIFY)
        return;
    *rows          = atomic_load(&jit->v_rows);
    *tiled         = atomic_load(&jit->v_rows_tiled);
    *skipped       = atomic_load(&jit->v_rows_skipped);
    *mismatch_rows = atomic_load(&jit->v_mismatch_rows);
}

int
rage128_jit_verify_mode(rage128_t *dev)
{
    rage128_jit_t *jit = (rage128_jit_t *) dev->jit;

    return jit && jit->mode == R128_JIT_MODE_VERIFY;
}

/* Span-shape profile (R128_JIT_PROF=1, off by default). The raster loop
   counts in locals and calls this at most once per triangle per worker,
   so there is no atomic add per row. count_tri is true only for worker
   0, which calls even when it owns none of the triangle's rows, so each
   triangle is counted once. */
void
rage128_jit_stat_rows(rage128_t *dev, int count_tri, uint64_t rows,
                      uint64_t px, uint64_t empty)
{
    rage128_jit_t *jit = (rage128_jit_t *) dev->jit;

    if (!jit || !jit->prof)
        return;
    if (count_tri)
        atomic_fetch_add_explicit(&jit->p_tris, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&jit->p_rows, rows, memory_order_relaxed);
    atomic_fetch_add_explicit(&jit->p_rows_empty, empty, memory_order_relaxed);
    atomic_fetch_add_explicit(&jit->p_px, px, memory_order_relaxed);
}

r128_jit_span_fn
rage128_jit_get_block(rage128_t *dev, const rage128_draw_state_t *ds)
{
#if !R128_JIT_HAVE_BACKEND
    (void) dev;
    (void) ds;
    return NULL;
#else
    rage128_jit_t    *jit = (rage128_jit_t *) dev->jit;
    r128_jit_block_t *bl;
    uint64_t          hash;
    int               slot;

    if (!jit)
        return NULL;
    if (ds->tex_tiled)
        return NULL; /* a tiled texture level that was not staged untiled
                        is read through the tile transform on every
                        texel fetch: interpreter only */
    /* A tiled color or Z surface does compile. The block addresses a
       row linearly, as base + x * bytes per pixel. rage128_3d_jit_row
       splits the row at each 64-byte tile-column edge and calls the
       block once per piece, with the base moved so that every address
       the block forms is the tiled one. The emitters never read
       c_tiled or z_tiled, so a linear and a tiled state compile to the
       same code; they still take separate cache slots, because the key
       is the whole draw state. */
    jit->lookups++;

    /* Try the most recently used slot first, then scan every slot. The
       key is several hundred bytes, so the scan compares the 64-bit
       hash first and runs memcmp only when the hashes match. */
    bl = &jit->blocks[jit->mru];
    if (bl->valid && !memcmp(&bl->key, ds, sizeof(*ds))) {
        bl->last_used = ++jit->gen;
        return bl->fn;
    }
    hash = r128_fnv1a(ds, sizeof(*ds));
    for (int i = 0; i < R128_JIT_BLOCKS; i++) {
        bl = &jit->blocks[i];
        if (bl->valid && bl->hash == hash && !memcmp(&bl->key, ds, sizeof(*ds))) {
            bl->last_used = ++jit->gen;
            jit->mru      = i;
            return bl->fn;
        }
    }

    /* Miss: take the first empty slot, or else the least recently used
       one, and compile into it. A refused state is cached too, with fn
       NULL, so it is not compiled again on every draw. */
    slot = 0;
    for (int i = 0; i < R128_JIT_BLOCKS; i++) {
        if (!jit->blocks[i].valid) {
            slot = i;
            break;
        }
        if (jit->blocks[i].last_used < jit->blocks[slot].last_used)
            slot = i;
    }
    bl = &jit->blocks[slot];
    if (bl->valid)
        jit->evictions++;

    bl->key       = *ds;
    bl->hash      = hash;
    bl->fn        = NULL;
    bl->valid     = 1;
    bl->last_used = ++jit->gen;
    jit->mru      = slot;

    {
        int len;

#    if defined(__APPLE__) && defined(__aarch64__)
        if (__builtin_available(macOS 11.0, *)) {
            pthread_jit_write_protect_np(0);
        }
#    endif
        len = r128_jit_backend_generate(bl->code, ds);
#    if defined(__APPLE__) && defined(__aarch64__)
        if (__builtin_available(macOS 11.0, *)) {
            pthread_jit_write_protect_np(1);
        }
#    endif
        if (len > 0) {
            /* On macOS, plat_mmap maps executable memory with the
               system's JIT mapping flag. On Apple Silicon such pages are
               either writable or executable for a given thread, never
               both, and pthread_jit_write_protect_np switches the
               calling thread between the two; that is the toggle
               around the generator call above. The function is declared
               in pthread.h, which this file includes only for that
               target. Intel Macs get the flag with read, write and
               execute together and need no toggle. Other hosts get
               plain read-write-execute pages from plat_mmap; a kernel
               that refuses them fails the allocation, and
               rage128_jit_init leaves the JIT off.

               ARM64 does not keep instruction fetch coherent with data
               stores, so the new code is flushed from the caches:
               FlushInstructionCache on Windows, since MSVC has no
               __builtin___clear_cache, and the builtin elsewhere.
               x86-64 keeps them coherent and needs no flush. */
#    if R128_JIT_BACKEND_A64
#        ifdef _WIN32
            FlushInstructionCache(GetCurrentProcess(), bl->code, (SIZE_T) len);
#        else
            __builtin___clear_cache((char *) bl->code, (char *) bl->code + len);
#        endif
#    endif
            bl->fn = (r128_jit_span_fn) (void *) bl->code;
            jit->compiles++;
            if (ds->tex_en || ds->sec_en) {
                if (rage128_jit_tex_inline(ds))
                    jit->compiles_texinline++;
                else
                    jit->compiles_texcall++;
            }
        } else {
            jit->rejects++;
            /* Log the fields that decide whether a state can compile,
               for the first 32 refusals. A refused state stays cached,
               so it normally reaches here once. */
            if (jit->prof && jit->rejects <= 32)
                pclog("RAGE128 JIT reject: tex=%d/%d spec=%d fog=%d(tbl=%d) at=%d(fn=%u) ab=%d(%x/%x/%x) st=%d aux=%d dith=%d dt=%u wm=%08x z=%d/%d zfn=%u zbpp=%d zmax=%08x zsh=%d\n",
                      ds->tex_en, ds->sec_en, ds->spec_en, ds->fog_en,
                      ds->fog_table_en, ds->atest_en, ds->atest_fn,
                      ds->alpha_en, ds->bsrc, ds->bdst, ds->bfcn,
                      ds->sten_on, ds->aux_on,
                      ds->dither, ds->dst_dt, ds->wmask, ds->z_en, ds->z_wr,
                      ds->zfn, ds->zbpp, ds->zmax, ds->zshift);
        }
    }
    return bl->fn;
#endif
}

/* ------------------------------------------------------------------------
 * Verify harness. A pixel's cell is found the way the interpreter finds
 * it: in the staging arena when the surface is staged, where a cell past
 * the staged length is skipped, and otherwise in local VRAM masked with
 * vram_mask.
 * ---------------------------------------------------------------------- */

static uint8_t *
r128_jit_vcell(const r128_jit_tri_t *tri, int is_z, uint32_t addr, uint32_t nb)
{
    uint8_t *sptr  = is_z ? tri->zptr : tri->cptr;
    uint32_t sbase = is_z ? tri->z_base : tri->c_base;
    uint32_t slim  = is_z ? tri->z_lim : tri->c_lim;

    if (sptr) {
        uint32_t off = addr - sbase;

        if (off + nb > slim)
            return NULL;
        return sptr + off;
    }
    return &tri->vram[addr & tri->vram_mask];
}

/* Copy the row's cells to buf (dir 0) or write buf back (dir 1). A
   nonzero tld adds the x half of the tile transform per pixel
   (r128_tile_x), as the interpreter does; rowbase is the render path's
   drow or zrow, which already holds the y half. */
static void
r128_jit_vrow_copy(const r128_jit_tri_t *tri, int is_z, uint32_t rowbase,
                   uint32_t nb, int tld, uint8_t *buf, int dir)
{
    for (int32_t px = tri->x0; px <= tri->x1; px++) {
        uint32_t xoff = tld ? r128_tile_x((uint32_t) px * nb)
                            : (uint32_t) px * nb;
        uint8_t *cell = r128_jit_vcell(tri, is_z, rowbase + xoff, nb);
        uint8_t *b    = buf + (size_t) (px - tri->x0) * nb;

        if (!cell)
            continue;
        if (dir)
            memcpy(cell, b, nb);
        else
            memcpy(b, cell, nb);
    }
}

int
rage128_jit_verify_pre(rage128_t *dev, r128_jit_tri_t *tri,
                       const rage128_draw_state_t *ds, r128_jit_span_fn fn,
                       int64_t e0, int64_t e1, int64_t e2, double zline,
                       uint32_t drow, uint32_t zrow, int32_t py,
                       int c_tld, int z_tld)
{
    rage128_jit_t   *jit   = (rage128_jit_t *) dev->jit;
    r128_jit_vrow_t *v     = &r128_jit_vrow;
    int32_t          count = tri->x1 - tri->x0 + 1;
    uint64_t         ret;

    v->active = 0;
    if (count <= 0 || count > R128_JIT_VMAX) {
        atomic_fetch_add(&jit->v_rows_skipped, 1);
        return 0;
    }

    /* Save the row, run the block, copy out what it wrote, then put the
       saved bytes back so the interpreter starts from the same memory.
       The block runs through rage128_3d_jit_row, the same tile-column
       walk as in normal mode; the copies address each cell with the
       interpreter's transform, so the walk itself is checked too. */
    r128_jit_vrow_copy(tri, 0, drow, (uint32_t) ds->bpp, c_tld, v->fb_orig, 0);
    if (ds->z_en || ds->sten_on)
        r128_jit_vrow_copy(tri, 1, zrow, (uint32_t) ds->zbpp, z_tld, v->z_orig, 0);

    ret        = rage128_3d_jit_row(tri, fn, e0, e1, e2, zline, drow, zrow, py,
                                    c_tld, z_tld, (uint32_t) ds->bpp,
                                    (uint32_t) ds->zbpp);
    v->jit_rx0 = (int32_t) (uint32_t) ret;
    v->jit_rx1 = (int32_t) (uint32_t) (ret >> 32);

    r128_jit_vrow_copy(tri, 0, drow, (uint32_t) ds->bpp, c_tld, v->fb_jit, 0);
    r128_jit_vrow_copy(tri, 0, drow, (uint32_t) ds->bpp, c_tld, v->fb_orig, 1);
    if (ds->z_en || ds->sten_on) {
        r128_jit_vrow_copy(tri, 1, zrow, (uint32_t) ds->zbpp, z_tld, v->z_jit, 0);
        r128_jit_vrow_copy(tri, 1, zrow, (uint32_t) ds->zbpp, z_tld, v->z_orig, 1);
    }

    v->active     = 1;
    v->c_tld      = c_tld;
    v->z_tld      = z_tld;
    v->drow_saved = drow;
    v->zrow_saved = zrow;
    return 1;
}

void
rage128_jit_verify_post(rage128_t *dev, const r128_jit_tri_t *tri,
                        const rage128_draw_state_t *ds,
                        int32_t py, int32_t rx0, int32_t rx1)
{
    rage128_jit_t   *jit       = (rage128_jit_t *) dev->jit;
    r128_jit_vrow_t *v         = &r128_jit_vrow;
    uint64_t         bad_px    = 0;
    int32_t          first_bad = -1;

    if (!v->active)
        return;
    v->active = 0;
    /* Log a progress line every 2^20 rows, so a run with no mismatch
       still shows activity. */
    if ((atomic_fetch_add(&jit->v_rows, 1) & 0xfffff) == 0)
        pclog("RAGE128 JIT verify: rows=%llu mismatch_rows=%llu\n",
              (unsigned long long) atomic_load(&jit->v_rows),
              (unsigned long long) atomic_load(&jit->v_mismatch_rows));
    if (v->c_tld || v->z_tld)
        atomic_fetch_add(&jit->v_rows_tiled, 1);

    /* The interpreter has drawn the row, so memory holds its result.
       Compare it cell by cell with the block's captured writes. */
    for (int32_t px = tri->x0; px <= tri->x1; px++) {
        size_t   bi = (size_t) (px - tri->x0);
        uint32_t cx = v->c_tld ? r128_tile_x((uint32_t) px * (uint32_t) ds->bpp)
                               : (uint32_t) px * (uint32_t) ds->bpp;
        uint8_t *cc, *zc;
        int      diff = 0;

        cc = r128_jit_vcell(tri, 0, v->drow_saved + cx, (uint32_t) ds->bpp);
        if (cc && memcmp(cc, v->fb_jit + bi * (uint32_t) ds->bpp, (uint32_t) ds->bpp))
            diff = 1;
        if (ds->z_en || ds->sten_on) {
            uint32_t zx = v->z_tld ? r128_tile_x((uint32_t) px * (uint32_t) ds->zbpp)
                                   : (uint32_t) px * (uint32_t) ds->zbpp;

            zc = r128_jit_vcell(tri, 1, v->zrow_saved + zx, (uint32_t) ds->zbpp);
            if (zc && memcmp(zc, v->z_jit + bi * (uint32_t) ds->zbpp, (uint32_t) ds->zbpp))
                diff = 1;
        }
        if (diff) {
            bad_px++;
            if (first_bad < 0)
                first_bad = px;
        }
    }

    /* The written-column range must match too; it drives dirty marking.
       When the interpreter wrote nothing (rx0 -1), rx1 is not compared. */
    if (rx0 != v->jit_rx0 || (rx0 >= 0 && rx1 != v->jit_rx1))
        atomic_fetch_add(&jit->v_rx_mismatch, 1);

    if (bad_px) {
        atomic_fetch_add(&jit->v_mismatch_rows, 1);
        atomic_fetch_add(&jit->v_mismatch_px, bad_px);
        if (atomic_fetch_add(&jit->v_detail_logged, 1) < 16) {
            pclog("RAGE128 JIT VERIFY MISMATCH: py=%d x=[%d..%d] bad_px=%llu first=%d\n",
                  py, tri->x0, tri->x1, (unsigned long long) bad_px, first_bad);
            pclog("  ds: dt=%u bpp=%d z=%d/%d zfn=%u zbpp=%d zshift=%d zmax=%08x flat=%d wmask=%08x\n",
                  ds->dst_dt, ds->bpp, ds->z_en, ds->z_wr, ds->zfn, ds->zbpp,
                  ds->zshift, ds->zmax, ds->flat_on, ds->wmask);
            pclog("  rx: interp=[%d..%d] jit=[%d..%d]\n", rx0, rx1, v->jit_rx0, v->jit_rx1);
        }
    }
}
