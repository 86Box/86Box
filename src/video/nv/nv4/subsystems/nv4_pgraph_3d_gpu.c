/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- GPU lane for the Direct3D classes.
 *
 *          Optional, off by default: the "Draw 3D on the host GPU"
 *          option, or NV4_GPU=1 in the environment. A flushed batch
 *          (nv4_pgraph_3d.h) is shaded by a Vulkan compute kernel
 *          (nv4_pgraph_3d_gpu.comp) instead of the CPU: setup stays on
 *          the CPU, the kernel takes the same draw states, triangles
 *          and spans, one invocation per pixel walking its row's spans
 *          in submission order, and writes the bytes the CPU span
 *          shader would. The spans go in runs: a run is one dispatch of
 *          the kernel compiled without what its draw states do not use
 *          (gpu_build_runs; the variants are compiled on a thread of
 *          their own, the generic kernel standing in until then). A
 *          batch whose rows alias (render to texture, odd pitches) is
 *          one workgroup taking every span in order, over the bytes its
 *          spans reach (gpu_serial_ranges).
 *
 *          A flush submits and returns (nv_gpu_vk_t.nslots batches can
 *          be in flight): the color and depth rows the batch touches are
 *          copied into a device-local mirror of VRAM (but not those a
 *          batch in flight owns: the mirror's are newer),
 *          the batch's textures are gathered through their DMA objects
 *          (VRAM or system memory alike) into a texture arena, the
 *          records go to device memory, the kernel runs and the rows go
 *          back. With VRAM imported (nv_gpu_vk.h) the rows are copied
 *          straight from and into VRAM by the GPU; else through a
 *          staging buffer, from which they reach VRAM when the next
 *          flush or a sync point (nv4_3d_sync) waits for the batch. Either
 *          way the GPU works while the guest goes on, and whatever reads
 *          or writes a batch's rows waits for it first. A batch the lane
 *          cannot take (rows past the end of VRAM, textures beyond the
 *          arena) is left to the CPU.
 *
 *          Vulkan is opened at run time, so a host without it only
 *          logs that the lane is off. NV4_3D_VERIFY=1 compares every
 *          batch with the CPU's result (nv4_pgraph_3d.c).
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/plat_unused.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/agpgart.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/nv/vid_nv4.h>
#include "nv4_pgraph_3d.h"

#ifndef NV4_GPU_HAVE_VULKAN

struct nv4_gpu_s *
nv4_gpu_init(UNUSED(nv4_t *nv4))
{
    pclog("NV4 GPU: built without Vulkan headers, 3D stays on the CPU\n");
    return NULL;
}

int
nv4_gpu_shade(UNUSED(struct nv4_gpu_s *g), UNUSED(nv4_t *nv4), UNUSED(const nv4_3d_batch_t *b))
{
    return 0;
}

void
nv4_gpu_close(UNUSED(struct nv4_gpu_s *g))
{
}

void
nv4_gpu_wait(UNUSED(struct nv4_gpu_s *g), UNUSED(nv4_t *nv4))
{
}

int
nv4_gpu_pending_overlaps(UNUSED(struct nv4_gpu_s *g), UNUSED(uint32_t lo), UNUSED(uint32_t hi))
{
    return 0;
}

void
nv4_gpu_counts(UNUSED(struct nv4_gpu_s *g), uint64_t *batches, uint64_t *fallbacks)
{
    *batches   = 0;
    *fallbacks = 0;
}

#else

#    include <86box/nv/nv_gpu_vk.h>
#    include <86box/nv/nv4_3d_gpu_spv.h>

#    define GPU_TEX_ARENA (32u << 20)

/* Records as the kernel reads them (std430, every field 4 bytes). */
typedef struct {
    int32_t  on, fmt, bpp, wlog, hlog, levels, wrap_u, wrap_v, min, mag;
    float    lod_bias;
    int32_t  center, ckey;
    uint32_t base; /* level 0 in the texture arena */
    uint32_t level_off[12];
} nv4_gpu_tex_t;

typedef struct {
    uint32_t      col_base, z_base, col_pitch, z_pitch, col_lim, z_lim;
    int32_t       col_fmt, bpp, z_on, dx6;
    float         origin;
    uint32_t      color_key;
    int32_t       texblend;
    uint32_t      comb_a0, comb_a1, comb_c0, comb_c1, factor;
    int32_t       flat, persp, spec, fog, ablend, sblend, dblend;
    uint32_t      fog_color;
    int32_t       atest, afunc, aref, zen, zfunc, zwrite, zfloat, dither;
    int32_t       wr_a, wr_r, wr_g, wr_b;
    int32_t       sten, sfunc, sref, smask_r, smask_w, sop_fail, sop_zfail, sop_zpass, swrite;
    int32_t       fill;
    int32_t       swz, swz_wl, swz_hl;
    nv4_gpu_tex_t tex[2];
} nv4_gpu_state_t;

typedef struct {
    int32_t y, x0, x1, tri;
} nv4_gpu_span_t;

typedef struct {
    uint32_t first, count;
} nv4_gpu_row_t;

_Static_assert(sizeof(nv4_gpu_tex_t) == 26 * 4, "nv4_gpu_tex_t layout");
_Static_assert(sizeof(nv4_gpu_state_t) == 103 * 4, "nv4_gpu_state_t layout");
_Static_assert(sizeof(nv4_3d_tri_t) == (3 + 3 * AT_COUNT) * 4, "nv4_3d_tri_t layout");
_Static_assert(AT_COUNT == 14, "the kernel interpolates 14 attributes");

/* The ranges of VRAM a batch reads and writes: its regions, or for rows
   that alias the merged reach of its states */
#    define GPU_DOWNS 16

/* Batches in flight (nv_gpu_vk_t.nslots): each has its own records,
   command buffer and fence (nv_gpu_vk.h). The rows a batch in flight owns
   are newer in the GPU's VRAM mirror than in VRAM, so a later batch does
   not upload them; batches complete (rows copied back) oldest first. */
typedef struct {
    nv_gpu_vk_slot_t *vk; /* qpool: GPU timestamps: start, rows in, kernel, rows out */
    int               pending;
    uint64_t          seq;
    VkBufferCopy      pdown[GPU_DOWNS]; /* its rows, to copy back */
    int               pndown;
    VkBufferCopy      ptex[B3D_STATES * 2]; /* the texture staging it uploads from */
    int               pntex;
    int               p_spans, p_chunks;
    uint32_t          p_rows;
    uint64_t          p_px;
} gpu_slot_t;

/* A texture in the arena: where its bytes came from, a hash of them,
   and where they are */
typedef struct {
    uint32_t dma, offset, size, base;
    uint64_t hash;
} gpu_texent_t;

#    define GPU_TEX_CACHE 1024

/* The pieces a batch's rows upload in: each range less the rows of
   every batch in flight */
#    define GPU_UPLOADS (GPU_DOWNS * (1 + NV_GPU_SLOTS * GPU_DOWNS))

/* Kernel variants: V_TEX (0-2), V_DX6, V_DST, V_Z as an index
   (gpu_state_key), and the serial kernel after them */
#    define GPU_KEY(tex, dx6, dst, z) (((tex) << 3) | ((dx6) << 2) | ((dst) << 1) | (z))
#    define GPU_KEY_TEX(k)            ((k) >> 3)
#    define GPU_KEY_BITS(k)           ((k) &7)
#    define GPU_VARIANTS              24
#    define GPU_V_SERIAL              GPU_VARIANTS

/* A batch is one dispatch per run (at most GPU_RUNS); a run shorter
   than GPU_RUN_MIN spans (NV4_GPU_RUN_MIN in the environment) takes in
   the next spans' features rather than end */
#    define GPU_RUNS    8
#    define GPU_RUN_MIN 2048

typedef struct {
    int      key; /* its variant */
    int      first, count; /* its spans */
    uint32_t row0, nrows;  /* its rows in the rows record */
    int      chunks;       /* workgroups across */
} gpu_run_t;

typedef struct nv4_gpu_s {
    nv_gpu_vk_t           vk;
    gpu_slot_t            slot[NV_GPU_SLOTS];
    int                   next; /* the slot the next batch takes */
    uint64_t              seq;

    uint32_t      vram_size;
    /* The texture arena keeps textures across batches: each batch hashes
       its textures' bytes where they are, and copies only the ones that
       are new or changed. */
    gpu_texent_t  cache[GPU_TEX_CACHE];
    int           ncache;
    uint32_t      tex_used;           /* arena bytes taken */
    int           flush_tex[B3D_STATES * 2]; /* cache entries this batch checked */
    int           nflush_tex;
    VkBufferCopy  tcopy[B3D_STATES * 2];     /* arena regions to upload */
    int           ntcopy;
    uint64_t      hash_bytes;
    uint32_t      row_count[4096];
    uint32_t      row_first[4096];
    uint64_t      batches, fallbacks;
    uint64_t      us_gather, us_copy, us_wait, tex_bytes; /* where the time goes */
    uint64_t      row_bytes;
    uint64_t      ts_ns[3];  /* uploads, kernel, download */
    int           prof_left; /* flushes left to log one by one (NV4_GPU_PROFILE=n) */
    VkBufferCopy  up[GPU_UPLOADS];

    gpu_run_t     runs[GPU_RUNS];
    int           nruns;
    uint8_t       skey[B3D_STATES];
    int           run_min;
    VkPipeline    var[GPU_VARIANTS + 1];
    atomic_int    var_ready[GPU_VARIANTS + 1];
    atomic_int    stop;
    event_t      *compiled; /* set once the compiler thread is done: thread_wait cannot tell (on
                               Windows _beginthread closes a finished thread's handle) */
    uint64_t      serial_batches, runs_total, var_runs;
} nv4_gpu_t;

static void gpu_compile_variants(void *priv);

struct nv4_gpu_s *
nv4_gpu_init(nv4_t *nv4)
{
    nv4_gpu_t             *g = calloc(1, sizeof(nv4_gpu_t));
    const nv_gpu_vk_desc_t d = {
        .tag       = "NV4 GPU",
        .app_name  = "86Box RIVA TNT",
        .vram_size = nv4->vram_size,
        .tex_arena = GPU_TEX_ARENA,
        .rec_size  = { sizeof(nv4_gpu_state_t) * B3D_STATES, sizeof(nv4_3d_tri_t) * B3D_TRIS,
                       sizeof(nv4_gpu_span_t) * B3D_SPANS, sizeof(nv4_gpu_row_t) * 4096 * GPU_RUNS },
        .spv       = (const uint32_t *) nv4_gpu_spv,
        .spv_size  = sizeof(nv4_gpu_spv),
        .queries    = 4,
        .push_size  = 12,
        .vram       = &nv4->svga.vram,
        .vram_alloc = nv4->vram_size + 4096 /* svga_init's */
    };

    g->vram_size = nv4->vram_size;
    if (getenv("NV4_GPU_PROFILE"))
        g->prof_left = atoi(getenv("NV4_GPU_PROFILE"));
    g->run_min = getenv("NV4_GPU_RUN_MIN") ? atoi(getenv("NV4_GPU_RUN_MIN")) : GPU_RUN_MIN;
    if (!nv_gpu_vk_open(&g->vk, &d)) {
        free(g);
        return NULL;
    }
    for (int i = 0; i < g->vk.nslots; i++)
        g->slot[i].vk = &g->vk.slot[i];
    g->compiled = thread_create_event();
    thread_create(gpu_compile_variants, g);
    return g;
}

void
nv4_gpu_counts(struct nv4_gpu_s *g, uint64_t *batches, uint64_t *fallbacks)
{
    *batches   = g->batches;
    *fallbacks = g->fallbacks;
    nv_gpu_log("nv4 3D: GPU textures %llu us (%llu KB hashed, %llu KB copied), row copies %llu us, waiting %llu us\n",
          (unsigned long long) g->us_gather, (unsigned long long) (g->hash_bytes >> 10),
          (unsigned long long) (g->tex_bytes >> 10),
          (unsigned long long) g->us_copy, (unsigned long long) g->us_wait);
    nv_gpu_log("nv4 3D: GPU rows %llu KB in and out; on the GPU: uploads %llu us, kernel %llu us, download %llu us\n",
          (unsigned long long) (g->row_bytes >> 10), (unsigned long long) (g->ts_ns[0] / 1000),
          (unsigned long long) (g->ts_ns[1] / 1000), (unsigned long long) (g->ts_ns[2] / 1000));
    nv_gpu_log("nv4 3D: GPU %llu dispatches (%llu by a variant), %llu batches whose rows alias\n", (unsigned long long) g->runs_total,
          (unsigned long long) g->var_runs, (unsigned long long) g->serial_batches);
    g->us_gather = g->us_copy = g->us_wait = g->tex_bytes = g->row_bytes = g->hash_bytes = 0;
    g->ts_ns[0] = g->ts_ns[1] = g->ts_ns[2] = 0;
    nv_gpu_vk_fallbacks(&g->vk, 0);
    g->runs_total = g->var_runs = g->serial_batches = 0;
}

void
nv4_gpu_close(struct nv4_gpu_s *g)
{
    if (!g)
        return;
    for (int i = 0; i < g->vk.nslots; i++)
        if (g->slot[i].pending)
            g->vk.vkWaitForFences(g->vk.dev, 1, &g->vk.slot[i].fence, VK_TRUE, UINT64_MAX);
    nv_gpu_log("NV4 GPU: %llu batches on the GPU, %llu left to the CPU\n", (unsigned long long) g->batches,
               (unsigned long long) g->fallbacks);
    nv_gpu_vk_fallbacks(&g->vk, 1);
    atomic_store(&g->stop, 1);
    if (g->compiled) {
        thread_wait_event(g->compiled, -1);
        thread_destroy_event(g->compiled);
    }
    for (int i = 0; i <= GPU_V_SERIAL; i++)
        if (g->var[i])
            g->vk.vkDestroyPipeline(g->vk.dev, g->var[i], NULL);
    nv_gpu_vk_close(&g->vk);
    free(g);
}

/* <len> bytes of a texture at DMA offset <off> (within one page of the
   window: nv4_dma_page_left) as
   the CPU span shader reads them: a pointer into VRAM or system memory,
   or <tmp> filled. */
static const uint8_t *
gpu_tex_bytes(nv4_t *nv4, const nv4_3d_tex_t *t, uint32_t off, uint32_t len, uint8_t *tmp)
{
    uint32_t addr;
    int      target;

    if (nv4_dma_translate(nv4, t->dma, off, 0, &addr, &target) != NV4_DMA_OK) {
        memset(tmp, 0, len);
        return tmp;
    }
    if (!target) {
        uint32_t va = addr & nv4->vram_mask;

        if (va + len <= nv4->vram_size)
            return nv4->svga.vram + va;
        for (uint32_t i = 0; i < len; i++)
            tmp[i] = nv4->svga.vram[(va + i) & nv4->vram_mask];
        return tmp;
    }
    {
        uint32_t pa = addr;
        uint32_t ra;

        /* through the AGP aperture to the page behind it (len stays
           within one page) */
        if (((pa & 0xfff) + len) > 0x1000) {
            dma_bm_read(pa, tmp, len, 1);
            return tmp;
        }
        if (agpgart_resolve(pa, &ra))
            pa = ra;
        /* system memory above 1 MB is one flat array */
        if ((pa >= 0x100000) && ((uint64_t) pa + len <= (uint64_t) mem_size * 1024))
            return ram + pa;
        dma_bm_read(pa, tmp, len, 1);
        return tmp;
    }
}

static void gpu_complete_upto(nv4_gpu_t *g, nv4_t *nv4, uint64_t seq);
int         nv4_gpu_pending_overlaps(struct nv4_gpu_s *g, uint32_t lo, uint32_t hi);

/* A texture drawn into by a batch still in flight (render to texture)
   reads that batch's pixels: it completes first. */
static void
gpu_tex_settle(nv4_gpu_t *g, nv4_t *nv4, const nv4_3d_tex_t *t)
{
    for (uint32_t done = 0; done < t->size;) {
        uint32_t off   = t->offset + done;
        uint32_t chunk = nv4_dma_page_left(nv4, t->dma, off);
        uint32_t addr;
        int      target;

        if (chunk > t->size - done)
            chunk = t->size - done;
        if ((nv4_dma_translate(nv4, t->dma, off, 0, &addr, &target) == NV4_DMA_OK) && !target) {
            uint32_t va = addr & nv4->vram_mask;

            if (nv4_gpu_pending_overlaps(g, va, va + chunk - 1)) {
                gpu_complete_upto(g, nv4, UINT64_MAX);
                return;
            }
        }
        done += chunk;
    }
}

/* A 64-bit hash of a texture's bytes, read where they are. */
static uint64_t
gpu_tex_hash(nv4_gpu_t *g, nv4_t *nv4, const nv4_3d_tex_t *t)
{
    uint8_t  tmp[0x1000];
    uint64_t h = 0x9e3779b97f4a7c15ull ^ t->size;

    gpu_tex_settle(g, nv4, t);

    for (uint32_t done = 0; done < t->size;) {
        uint32_t       off   = t->offset + done;
        uint32_t       chunk = nv4_dma_page_left(nv4, t->dma, off);
        const uint8_t *p;
        uint32_t       i = 0;

        if (chunk > t->size - done)
            chunk = t->size - done;
        p = gpu_tex_bytes(nv4, t, off, chunk, tmp);
        for (; i + 8 <= chunk; i += 8) {
            uint64_t w;

            memcpy(&w, p + i, 8);
            h = (h ^ w) * 0x100000001b3ull;
            h ^= h >> 29;
        }
        for (; i < chunk; i++)
            h = (h ^ p[i]) * 0x100000001b3ull;
        done += chunk;
    }
    g->hash_bytes += t->size;
    return h;
}

/* Copy a texture's bytes into the arena's staging at <base>, to upload.
   A batch in flight may still upload from those staging bytes: it (and
   any older one) finishes first. Its kernel may still read the arena
   there, but the GPU orders that: every batch's kernel is followed by
   a barrier before the next transfer. */
static void
gpu_tex_copy(nv4_gpu_t *g, nv4_t *nv4, const nv4_3d_tex_t *t, uint32_t base)
{
    uint8_t *dst = (uint8_t *) g->vk.b[NV_GPU_B_TSTAGE].map + base;
    uint64_t seq = 0;

    for (int s = 0; s < g->vk.nslots; s++) {
        const gpu_slot_t *sl = &g->slot[s];

        for (int i = 0; sl->pending && (i < sl->pntex); i++)
            if ((sl->ptex[i].srcOffset < base + t->size) && (base < sl->ptex[i].srcOffset + sl->ptex[i].size)
                && (sl->seq > seq))
                seq = sl->seq;
    }
    if (seq)
        gpu_complete_upto(g, nv4, seq);

    for (uint32_t done = 0; done < t->size;) {
        uint32_t off   = t->offset + done;
        uint32_t chunk = nv4_dma_page_left(nv4, t->dma, off);

        if (chunk > t->size - done)
            chunk = t->size - done;
        {
            const uint8_t *src = gpu_tex_bytes(nv4, t, off, chunk, dst + done);

            if (src != dst + done)
                memcpy(dst + done, src, chunk);
        }
        done += chunk;
    }
    g->tcopy[g->ntcopy].srcOffset = base;
    g->tcopy[g->ntcopy].dstOffset = base;
    g->tcopy[g->ntcopy].size      = t->size;
    g->ntcopy++;
    g->tex_bytes += t->size;
}

/* A texture's place in the arena for this batch: from the cache when
   its bytes have not changed, else copied in. UINT32_MAX when the arena
   is full. */
static uint32_t
gpu_gather_tex(nv4_gpu_t *g, nv4_t *nv4, const nv4_3d_tex_t *t)
{
    uint64_t      h;
    gpu_texent_t *e = NULL;

    for (int i = 0; i < g->nflush_tex; i++) {
        gpu_texent_t *c = &g->cache[g->flush_tex[i]];

        if ((c->dma == t->dma) && (c->offset == t->offset) && (c->size == t->size))
            return c->base;
    }
    if (!t->size)
        return UINT32_MAX;
    h = gpu_tex_hash(g, nv4, t);
    for (int i = 0; i < g->ncache; i++)
        if ((g->cache[i].dma == t->dma) && (g->cache[i].offset == t->offset) && (g->cache[i].size == t->size)) {
            e = &g->cache[i];
            break;
        }
    if (!e) {
        if ((g->ncache >= GPU_TEX_CACHE) || (t->size > GPU_TEX_ARENA - g->tex_used))
            return UINT32_MAX;
        e         = &g->cache[g->ncache++];
        e->dma    = t->dma;
        e->offset = t->offset;
        e->size   = t->size;
        e->base   = g->tex_used;
        e->hash   = ~h; /* not there yet */
        g->tex_used = (e->base + t->size + 3) & ~3u;
    }
    if (e->hash != h) {
        gpu_tex_copy(g, nv4, t, e->base);
        e->hash = h;
    }
    g->flush_tex[g->nflush_tex++] = (int) (e - g->cache);
    return e->base;
}

static void
gpu_pack_tex(nv4_gpu_tex_t *o, const nv4_3d_tex_t *t, uint32_t base)
{
    o->on       = t->on;
    o->fmt      = t->fmt;
    o->bpp      = t->bpp;
    o->wlog     = t->wlog;
    o->hlog     = t->hlog;
    o->levels   = t->levels;
    o->wrap_u   = t->wrap_u;
    o->wrap_v   = t->wrap_v;
    o->min      = t->min;
    o->mag      = t->mag;
    o->lod_bias = t->lod_bias;
    o->center   = t->center;
    o->ckey     = t->ckey;
    o->base     = base;
    for (int l = 0; l < 12; l++)
        o->level_off[l] = t->level_off[l];
}

static void
gpu_pack_state(nv4_gpu_state_t *o, const nv4_3d_state_t *s)
{
    memset(o, 0, sizeof(*o));
    o->col_base  = s->col_base;
    o->z_base    = s->z_base;
    o->col_pitch = s->col_pitch;
    o->z_pitch   = s->z_pitch;
    o->col_lim   = s->col_lim;
    o->z_lim     = s->z_lim;
    o->col_fmt   = s->col_fmt;
    o->bpp       = s->bpp;
    o->z_on      = s->z_on;
    o->dx6       = s->dx6;
    o->origin    = s->origin;
    o->color_key = s->color_key;
    o->texblend  = s->texblend;
    o->comb_a0   = s->comb_a[0];
    o->comb_a1   = s->comb_a[1];
    o->comb_c0   = s->comb_c[0];
    o->comb_c1   = s->comb_c[1];
    o->factor    = s->factor;
    o->flat      = s->flat;
    o->persp     = s->persp;
    o->spec      = s->spec;
    o->fog       = s->fog;
    o->ablend    = s->ablend;
    o->sblend    = s->sblend;
    o->dblend    = s->dblend;
    o->fog_color = s->fog_color;
    o->atest     = s->atest;
    o->afunc     = s->afunc;
    o->aref      = s->aref;
    o->zen       = s->zen;
    o->zfunc     = s->zfunc;
    o->zwrite    = s->zwrite;
    o->zfloat    = s->zfloat;
    o->dither    = s->dither;
    o->wr_a      = s->wr_a;
    o->wr_r      = s->wr_r;
    o->wr_g      = s->wr_g;
    o->wr_b      = s->wr_b;
    o->sten      = s->sten;
    o->sfunc     = s->sfunc;
    o->sref      = s->sref;
    o->smask_r   = s->smask_r;
    o->smask_w   = s->smask_w;
    o->sop_fail  = s->sop_fail;
    o->sop_zfail = s->sop_zfail;
    o->sop_zpass = s->sop_zpass;
    o->swrite    = s->swrite;
    o->fill      = s->fill;
    o->swz       = s->swz;
    o->swz_wl    = s->swz_wl;
    o->swz_hl    = s->swz_hl;
}

/* Wait for one batch in flight and copy its rows back into VRAM. */
static void
gpu_complete(nv4_gpu_t *g, nv4_t *nv4, gpu_slot_t *sl)
{
    uint8_t *stage = (uint8_t *) g->vk.b[NV_GPU_B_VSTAGE].map;
    uint64_t t0, t1;

    t0 = nv_gpu_now_us();
    g->vk.vkWaitForFences(g->vk.dev, 1, &sl->vk->fence, VK_TRUE, UINT64_MAX);
    t1 = nv_gpu_now_us();
    g->us_wait += t1 - t0;
    if (sl->vk->qpool) {
        uint64_t ts[4];

        if (g->vk.vkGetQueryPoolResults(g->vk.dev, sl->vk->qpool, 0, 4, sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT)
            == VK_SUCCESS) {
            for (int i = 0; i < 3; i++)
                g->ts_ns[i] += (uint64_t) ((double) (ts[i + 1] - ts[i]) * g->vk.ts_period);
            if (g->prof_left) {
                g->prof_left--;
                pclog("nv4 GPU prof: %d spans %llu px rows %u x%d: up %.0f kernel %.0f down %.0f us\n", sl->p_spans,
                      (unsigned long long) sl->p_px, sl->p_rows, sl->p_chunks, (ts[1] - ts[0]) * g->vk.ts_period / 1000.0,
                      (ts[2] - ts[1]) * g->vk.ts_period / 1000.0, (ts[3] - ts[2]) * g->vk.ts_period / 1000.0);
            }
        }
    }
    for (int i = 0; i < sl->pndown; i++) {
        if (!g->vk.vram_import) /* else the GPU wrote VRAM itself */
            memcpy(nv4->svga.vram + sl->pdown[i].srcOffset, stage + sl->pdown[i].srcOffset, sl->pdown[i].size);
        g->row_bytes += sl->pdown[i].size;
    }
    for (int i = 0; i < sl->pndown; i++)
        for (uint32_t a = sl->pdown[i].srcOffset & ~0xfffu; a < sl->pdown[i].srcOffset + sl->pdown[i].size; a += 0x1000)
            nv4->svga.changedvram[a >> 12] = nv4->svga.monitor->mon_changeframecount;
    g->us_copy += nv_gpu_now_us() - t1;
    sl->pending = 0;
}

/* Complete the batches in flight up to number <seq>, oldest first (a
   later batch's rows are newer). */
static void
gpu_complete_upto(nv4_gpu_t *g, nv4_t *nv4, uint64_t seq)
{
    for (;;) {
        gpu_slot_t *old = NULL;

        for (int i = 0; i < g->vk.nslots; i++)
            if (g->slot[i].pending && (g->slot[i].seq <= seq) && (!old || (g->slot[i].seq < old->seq)))
                old = &g->slot[i];
        if (!old)
            return;
        gpu_complete(g, nv4, old);
    }
}

void
nv4_gpu_wait(struct nv4_gpu_s *g, nv4_t *nv4)
{
    gpu_complete_upto(g, nv4, UINT64_MAX);
}

int
nv4_gpu_pending_overlaps(struct nv4_gpu_s *g, uint32_t lo, uint32_t hi)
{
    for (int s = 0; s < g->vk.nslots; s++) {
        const gpu_slot_t *sl = &g->slot[s];

        for (int i = 0; sl->pending && (i < sl->pndown); i++)
            if ((sl->pdown[i].srcOffset <= hi) && (lo < sl->pdown[i].srcOffset + sl->pdown[i].size))
                return 1;
    }
    return 0;
}

/* Upload the bytes lo..hi of VRAM to the GPU's mirror, except those a
   batch in flight owns (the mirror's are newer). */
static void
gpu_upload_range(nv4_gpu_t *g, nv4_t *nv4, uint32_t lo, uint32_t hi, VkBufferCopy *up, int *nup, int max)
{
    uint8_t *stage = (uint8_t *) g->vk.b[NV_GPU_B_VSTAGE].map;

    for (int s = 0; s < g->vk.nslots; s++) {
        const gpu_slot_t *sl = &g->slot[s];

        for (int i = 0; sl->pending && (i < sl->pndown); i++) {
            uint32_t plo = (uint32_t) sl->pdown[i].srcOffset;
            uint32_t phi = plo + (uint32_t) sl->pdown[i].size - 1;

            if ((plo > hi) || (phi < lo))
                continue;
            /* the parts either side */
            if (plo > lo)
                gpu_upload_range(g, nv4, lo, plo - 1, up, nup, max);
            if (phi < hi)
                gpu_upload_range(g, nv4, phi + 1, hi, up, nup, max);
            return;
        }
    }
    /* GPU_UPLOADS: <max> is never reached */
    if (*nup >= max)
        return;
    if (!g->vk.vram_import) /* else the GPU reads VRAM itself */
        memcpy(stage + lo, nv4->svga.vram + lo, hi - lo + 1);
    up[*nup].srcOffset = up[*nup].dstOffset = lo;
    up[*nup].size                           = hi - lo + 1;
    (*nup)++;
}

/* ---- runs and variants (#5) ---- */

/* A state's variant: what of the kernel it may use (V_* in the kernel).
   Fills return before any of it. */
static int
gpu_state_key(const nv4_3d_state_t *s)
{
    int tex, dst, z;

    if (s->fill)
        return 0;
    tex = s->tex[1].on ? 2 : (s->tex[0].on ? 1 : 0);
    dst = s->ablend || !(s->wr_r && s->wr_g && s->wr_b && s->wr_a);
    z   = s->z_on || s->zen || s->sten;
    return GPU_KEY(tex, s->dx6 ? 1 : 0, dst, z);
}

static int
gpu_key_within(int k, int of)
{
    return (GPU_KEY_TEX(k) <= GPU_KEY_TEX(of)) && ((GPU_KEY_BITS(k) & ~GPU_KEY_BITS(of)) == 0);
}

static int
gpu_key_union(int a, int b)
{
    int tex = (GPU_KEY_TEX(a) > GPU_KEY_TEX(b)) ? GPU_KEY_TEX(a) : GPU_KEY_TEX(b);

    return (tex << 3) | GPU_KEY_BITS(a) | GPU_KEY_BITS(b);
}

/* The batch's spans in runs, in submission order: a run takes the next
   span while that span's variant is within the run's, or while the run
   is short (widening it); else the next run starts. */
static void
gpu_build_runs(nv4_gpu_t *g, const nv4_3d_batch_t *b)
{
    for (int i = 0; i < b->nstates; i++)
        g->skey[i] = (uint8_t) gpu_state_key(&b->states[i]);
    g->nruns = 0;
    for (int i = 0; i < b->nspans; i++) {
        int        k = g->skey[b->tris[b->spans[i].tri].state];
        gpu_run_t *r = g->nruns ? &g->runs[g->nruns - 1] : NULL;

        if (r && gpu_key_within(k, r->key))
            r->count++;
        else if (r && ((r->count < g->run_min) || (g->nruns == GPU_RUNS))) {
            r->key = gpu_key_union(r->key, k);
            r->count++;
        } else {
            r        = &g->runs[g->nruns++];
            r->key   = k;
            r->first = i;
            r->count = 1;
        }
    }
}

/* The variants are compiled away from the emulation thread; until one
   is ready its runs use the generic pipeline (and a batch whose rows
   alias goes to the CPU). */
static void
gpu_compile_variants(void *priv)
{
    nv4_gpu_t *g  = (nv4_gpu_t *) priv;
    uint64_t   t0 = nv_gpu_now_us();

    for (int i = GPU_V_SERIAL; (i >= 0) && !atomic_load(&g->stop); i--) {
        int32_t spec[5] = { 2, 1, 1, 1, 1 }; /* the serial one: everything, in order */

        if (i != GPU_V_SERIAL) {
            spec[0] = GPU_KEY_TEX(i);
            spec[1] = (i >> 2) & 1;
            spec[2] = (i >> 1) & 1;
            spec[3] = i & 1;
            spec[4] = 0;
        }
        g->var[i] = nv_gpu_vk_pipeline(&g->vk, spec, 5);
        if (g->var[i])
            atomic_store(&g->var_ready[i], 1);
        else
            nv_gpu_log("NV4 GPU: kernel variant %d failed to compile\n", i);
    }
    if (!atomic_load(&g->stop))
        nv_gpu_log("NV4 GPU: %d kernel variants compiled in %llu ms\n", GPU_VARIANTS + 1,
                   (unsigned long long) ((nv_gpu_now_us() - t0) / 1000));
    if (g->compiled)
        thread_set_event(g->compiled);
}

/* ---- batches whose rows alias (#6) ---- */

/* The bytes such a batch can touch, in <lo>/<hi> (merged, ascending):
   each state's color and depth bytes over the rows and pixels of its
   spans (the whole target when swizzled). 0 when the GPU cannot take
   the batch: too many pieces, bytes past VRAM, a texture reading bytes
   the batch writes (the arena holds the bytes from before it), or a
   state whose color and depth bytes meet (pixels of one span would
   race). */
static int
gpu_serial_ranges(nv4_gpu_t *g, const nv4_3d_batch_t *b, uint32_t *lo, uint32_t *hi, int *why)
{
    int32_t  ymin[B3D_STATES], ymax[B3D_STATES], xmin[B3D_STATES], xmax[B3D_STATES];
    uint32_t rl[B3D_STATES * 2], rh[B3D_STATES * 2];
    int      n = 0, m = 0;

    for (int i = 0; i < b->nstates; i++) {
        ymin[i] = xmin[i] = INT32_MAX;
        ymax[i] = xmax[i] = INT32_MIN;
    }
    for (int i = 0; i < b->nspans; i++) {
        const nv4_3d_span_t *sp = &b->spans[i];
        int                  si = b->tris[sp->tri].state;

        if (sp->x1 <= sp->x0)
            continue;
        if (sp->y < ymin[si])
            ymin[si] = sp->y;
        if (sp->y > ymax[si])
            ymax[si] = sp->y;
        if (sp->x0 < xmin[si])
            xmin[si] = sp->x0;
        if (sp->x1 > xmax[si])
            xmax[si] = sp->x1;
    }
    for (int i = 0; i < b->nstates; i++) {
        const nv4_3d_state_t *s = &b->states[i];
        uint32_t              clo, chi, zlo, zhi;
        int                   col, z;

        if ((ymin[i] > ymax[i]) || (ymin[i] < 0) || (xmin[i] < 0))
            continue;
        if (s->swz) {
            clo = s->col_base;
            chi = clo + ((uint32_t) s->bpp << (s->swz_wl + s->swz_hl)) - 1;
            zlo = s->z_base;
            zhi = zlo + ((uint32_t) s->bpp << (s->swz_wl + s->swz_hl)) - 1;
        } else {
            clo = s->col_base + (uint32_t) ymin[i] * s->col_pitch + (uint32_t) xmin[i] * s->bpp;
            chi = s->col_base + (uint32_t) ymax[i] * s->col_pitch + (uint32_t) xmax[i] * s->bpp - 1;
            zlo = s->z_base + (uint32_t) ymin[i] * s->z_pitch + (uint32_t) xmin[i] * s->bpp;
            zhi = s->z_base + (uint32_t) ymax[i] * s->z_pitch + (uint32_t) xmax[i] * s->bpp - 1;
        }
        /* color past the limit is never written; depth and stencil are
           written without one */
        if (chi > s->col_lim)
            chi = s->col_lim;
        col = (clo <= chi);
        z   = !s->fill && (s->z_on || s->sten);
        if (col && z && (clo <= zhi) && (zlo <= chi)) {
            *why = NV_GPU_FB_ALIAS_ZCOL;
            return 0;
        }
        if (col) {
            rl[n]   = clo;
            rh[n++] = chi;
        }
        if (z) {
            rl[n]   = zlo;
            rh[n++] = zhi;
        }
    }
    /* sorted by start, then merged */
    for (int i = 1; i < n; i++)
        for (int j = i; (j > 0) && (rl[j] < rl[j - 1]); j--) {
            uint32_t t = rl[j];

            rl[j]     = rl[j - 1];
            rl[j - 1] = t;
            t         = rh[j];
            rh[j]     = rh[j - 1];
            rh[j - 1] = t;
        }
    for (int i = 0; i < n; i++) {
        if (m && (rl[i] <= hi[m - 1] + 1)) {
            if (rh[i] > hi[m - 1])
                hi[m - 1] = rh[i];
            continue;
        }
        if (m == GPU_DOWNS) {
            *why = NV_GPU_FB_ALIAS_PIECES;
            return 0;
        }
        lo[m]   = rl[i];
        hi[m++] = rh[i];
    }
    for (int i = 0; i < m; i++) {
        if (hi[i] >= g->vram_size) {
            *why = NV_GPU_FB_ALIAS_PIECES;
            return 0;
        }
        if ((b->tex_lo <= b->tex_hi) && (lo[i] <= b->tex_hi) && (b->tex_lo <= hi[i])) {
            *why = NV_GPU_FB_ALIAS_TEX;
            return 0;
        }
    }
    if (!m)
        *why = NV_GPU_FB_NOREGION;
    return m;
}

int
nv4_gpu_shade(struct nv4_gpu_s *g, nv4_t *nv4, const nv4_3d_batch_t *b)
{
    gpu_slot_t      *sl = &g->slot[g->next];
    nv4_gpu_state_t *gs;
    nv4_gpu_span_t  *gsp;
    nv4_gpu_row_t   *gr;
    VkCommandBuffer  cb;
    VkBufferCopy     down[GPU_DOWNS];
    uint32_t         dlo[GPU_DOWNS], dhi[GPU_DOWNS];
    int              nup = 0, ndown = 0;
    uint32_t         nrows = 0;
    int              chunks = 0;
    uint32_t         pc[3];
    uint64_t         t0, t1, t2;
    int              why = NV_GPU_FB_NOREGION;

    /* this slot's last batch (and any older one) first */
    if (sl->pending)
        gpu_complete_upto(g, nv4, sl->seq);
    gs  = (nv4_gpu_state_t *) sl->vk->rech[NV_GPU_R_STATES].map;
    gsp = (nv4_gpu_span_t *) sl->vk->rech[NV_GPU_R_SPANS].map;
    gr  = (nv4_gpu_row_t *) sl->vk->rech[NV_GPU_R_ROWS].map;
    cb  = sl->vk->cb;

    /* the bytes it touches: its regions, or for rows that alias what its
       spans reach */
    if (b->serial) {
        if (!atomic_load(&g->var_ready[GPU_V_SERIAL])) {
            why = NV_GPU_FB_ALIAS_WAIT;
            goto fallback;
        }
        if (!(ndown = gpu_serial_ranges(g, b, dlo, dhi, &why)))
            goto fallback;
    } else {
        for (int r = 0; r < b->nregions; r++) {
            if (b->regions[r].wlo > b->regions[r].whi)
                continue;
            if (b->regions[r].whi >= g->vram_size) {
                why = NV_GPU_FB_PASTVRAM;
                goto fallback;
            }
            dlo[ndown]   = b->regions[r].wlo;
            dhi[ndown++] = b->regions[r].whi;
        }
        if (!ndown)
            goto fallback;
    }

    /* draw states and their textures; a full arena is emptied once and
       the batch's textures gathered again */
    t0 = nv_gpu_now_us();
    for (int pass = 0;; pass++) {
        int full = 0;

        g->nflush_tex = 0;
        g->ntcopy     = 0;
        for (int i = 0; (i < b->nstates) && !full; i++) {
            const nv4_3d_state_t *s = &b->states[i];

            gpu_pack_state(&gs[i], s);
            for (int n = 0; n < 2; n++) {
                uint32_t base = 0;

                if (s->tex[n].on && ((base = gpu_gather_tex(g, nv4, &s->tex[n])) == UINT32_MAX)) {
                    full = 1;
                    break;
                }
                gpu_pack_tex(&gs[i].tex[n], &s->tex[n], base);
            }
        }
        if (!full)
            break;
        g->ncache   = 0;
        g->tex_used = 0;
        why         = NV_GPU_FB_ARENA;
        if (pass)
            goto fallback;
    }
    t1 = nv_gpu_now_us();
    g->us_gather += t1 - t0;
    memcpy(sl->vk->rech[NV_GPU_R_TRIS].map, b->tris, sizeof(nv4_3d_tri_t) * b->ntris);

    if (b->serial) {
        /* every span in submission order, one workgroup */
        for (int i = 0; i < b->nspans; i++) {
            gsp[i].y   = b->spans[i].y;
            gsp[i].x0  = b->spans[i].x0;
            gsp[i].x1  = b->spans[i].x1;
            gsp[i].tri = b->spans[i].tri;
        }
        g->nruns           = 1;
        g->runs[0].key     = GPU_V_SERIAL;
        g->runs[0].first   = 0;
        g->runs[0].count   = b->nspans;
        g->runs[0].row0    = 0;
        g->runs[0].nrows   = b->nspans;
        g->runs[0].chunks  = 1;
        nrows              = 0;
        chunks             = 1;
    } else {
        /* runs, and in each the spans grouped by row, each row in
           submission order */
        gpu_build_runs(g, b);
        for (int k = 0; k < g->nruns; k++) {
            gpu_run_t *r    = &g->runs[k];
            uint32_t   at   = r->first;
            int        xmax = 0;

            memset(g->row_count, 0, sizeof(g->row_count));
            for (int i = r->first; i < r->first + r->count; i++)
                g->row_count[b->spans[i].y & 0xfff]++;
            r->row0 = nrows;
            for (int y = 0; y < 4096; y++) {
                g->row_first[y] = at;
                if (g->row_count[y]) {
                    gr[nrows].first = at;
                    gr[nrows].count = g->row_count[y];
                    nrows++;
                }
                at += g->row_count[y];
            }
            for (int i = r->first; i < r->first + r->count; i++) {
                const nv4_3d_span_t *sp = &b->spans[i];
                nv4_gpu_span_t      *o  = &gsp[g->row_first[sp->y & 0xfff]++];

                o->y   = sp->y;
                o->x0  = sp->x0;
                o->x1  = sp->x1;
                o->tri = sp->tri;
                if (sp->x1 > xmax)
                    xmax = sp->x1;
            }
            r->nrows  = nrows - r->row0;
            r->chunks = (xmax + 63) / 64;
            if (r->chunks > chunks)
                chunks = r->chunks;
        }
        if (!nrows) {
            why = NV_GPU_FB_NOROWS;
            goto fallback;
        }
    }

    /* rows in and out */
    for (int i = 0; i < ndown; i++) {
        gpu_upload_range(g, nv4, dlo[i], dhi[i], g->up, &nup, GPU_UPLOADS);
        down[i].srcOffset = down[i].dstOffset = dlo[i];
        down[i].size                          = dhi[i] - dlo[i] + 1;
    }

    t2 = nv_gpu_now_us();
    g->us_copy += t2 - t1;
    {
        VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VkSubmitInfo             si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        g->vk.vkResetCommandBuffer(cb, 0);
        g->vk.vkBeginCommandBuffer(cb, &bi);
        if (sl->vk->qpool) {
            g->vk.vkCmdResetQueryPool(cb, sl->vk->qpool, 0, 4);
            g->vk.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, sl->vk->qpool, 0);
        }
        if (nup)
            g->vk.vkCmdCopyBuffer(cb, g->vk.b[NV_GPU_B_VSTAGE].buf, g->vk.b[NV_GPU_B_VRAM].buf, nup, g->up);
        {
            /* the records, read many times per pixel, to device memory */
            VkBufferCopy rc[4] = {
                { 0, 0, sizeof(nv4_gpu_state_t) * b->nstates },
                { 0, 0, sizeof(nv4_3d_tri_t) * b->ntris },
                { 0, 0, sizeof(nv4_gpu_span_t) * b->nspans },
                { 0, 0, sizeof(nv4_gpu_row_t) * nrows }
            };

            for (int i = 0; i < 4; i++)
                if (rc[i].size)
                    g->vk.vkCmdCopyBuffer(cb, sl->vk->rech[i].buf, sl->vk->rec[i].buf, 1, &rc[i]);
        }
        if (g->ntcopy)
            g->vk.vkCmdCopyBuffer(cb, g->vk.b[NV_GPU_B_TSTAGE].buf, g->vk.b[NV_GPU_B_TEX].buf, g->ntcopy, g->tcopy);
        nv_gpu_vk_barrier(&g->vk, cb, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        if (sl->vk->qpool)
            g->vk.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, sl->vk->qpool, 1);
        g->vk.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->vk.pl, 0, 1, &sl->vk->dset, 0, NULL);
        for (int k = 0; k < g->nruns; k++) {
            const gpu_run_t *r = &g->runs[k];
            VkPipeline       p;

            if (!r->nrows)
                continue;
            if (b->serial)
                p = g->var[GPU_V_SERIAL];
            else if (atomic_load(&g->var_ready[r->key])) {
                p = g->var[r->key];
                g->var_runs++;
            } else
                p = g->vk.pipe;
            /* each run sees the pixels of the ones before it */
            if (k)
                nv_gpu_vk_barrier(&g->vk, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            g->vk.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p);
            pc[0] = r->nrows;
            pc[1] = nv4->vram_mask;
            pc[2] = b->serial ? r->first : r->row0;
            g->vk.vkCmdPushConstants(cb, g->vk.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
            /* a workgroup per row and 64 pixels; one for rows that alias */
            if (b->serial)
                g->vk.vkCmdDispatch(cb, 1, 1, 1);
            else
                g->vk.vkCmdDispatch(cb, r->nrows, r->chunks, 1);
            g->runs_total++;
        }
        nv_gpu_vk_barrier(&g->vk, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT);
        if (sl->vk->qpool)
            g->vk.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, sl->vk->qpool, 2);
        g->vk.vkCmdCopyBuffer(cb, g->vk.b[NV_GPU_B_VRAM].buf, g->vk.b[NV_GPU_B_VSTAGE].buf, ndown, down);
        if (sl->vk->qpool)
            g->vk.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, sl->vk->qpool, 3);
        nv_gpu_vk_barrier(&g->vk, cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                    VK_ACCESS_HOST_READ_BIT);
        g->vk.vkEndCommandBuffer(cb);
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cb;
        g->vk.vkResetFences(g->vk.dev, 1, &sl->vk->fence);
        if (g->vk.vkQueueSubmit(g->vk.queue, 1, &si, sl->vk->fence) != VK_SUCCESS) {
            why = NV_GPU_FB_SUBMIT;
            goto fallback;
        }
    }

    /* in flight: the rows come back when a later batch needs the slot
       or a sync point waits */
    sl->pending  = 1;
    sl->seq      = ++g->seq;
    sl->pndown   = ndown;
    for (int i = 0; i < ndown; i++)
        sl->pdown[i] = down[i];
    sl->pntex = g->ntcopy;
    for (int i = 0; i < g->ntcopy; i++)
        sl->ptex[i] = g->tcopy[i];
    sl->p_spans  = b->nspans;
    sl->p_rows   = nrows;
    sl->p_chunks = chunks;
    sl->p_px     = 0;
    if (g->prof_left)
        for (int i = 0; i < b->nspans; i++)
            sl->p_px += b->spans[i].x1 - b->spans[i].x0;
    g->next = (g->next + 1) % g->vk.nslots;
    g->batches++;
    if (b->serial)
        g->serial_batches++;
    return 1;

fallback:
    g->fallbacks++;
    nv_gpu_vk_fallback(&g->vk, why);
    return 0;
}

#endif
