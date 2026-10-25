/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 3D -- GPU lane for the Direct3D triangle.
 *
 *          Optional, off by default: the "Draw 3D on the host GPU"
 *          option, or NV3_GPU=1 in the environment. A flushed batch
 *          (nv3_render_d3d.h) is shaded by a Vulkan compute kernel
 *          (nv3_render_d3d_gpu.comp) instead of the CPU. Setup stays on
 *          the CPU; the kernel takes the same draw states, triangles and
 *          spans, one invocation per pixel walking its row's spans in
 *          submission order, and writes the bytes the CPU span shader
 *          would.
 *
 *          A flush submits and returns (nv_gpu_vk_t.nslots batches can
 *          be in flight): the colour and zeta rows the batch touches are
 *          copied into a device-local mirror of VRAM (but not those a
 *          batch in flight owns: the mirror's are newer), the batch's
 *          textures are gathered through their pages (VRAM or system
 *          memory alike) into a texture arena that keeps them across
 *          batches, the records go to device memory, the kernel runs and
 *          the rows go back: with VRAM imported (nv_gpu_vk.h) the GPU
 *          copies them straight from and into VRAM, else through a
 *          staging buffer from which they reach VRAM when the next flush
 *          or a sync point (nv3_d3d_sync) waits for the batch. A
 *          batch the lane cannot take (rows past the end of VRAM, a full
 *          arena) is left to the CPU.
 *
 *          Vulkan is opened at run time, so a host without it only logs
 *          that the lane is off. NV3_3D_VERIFY=1 compares every batch
 *          with the CPU's result (nv3_render_d3d.c).
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/plat_unused.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>
#include "nv3_render_d3d.h"

#ifndef NV3_GPU_HAVE_VULKAN

struct nv3_gpu_s *
nv3_gpu_init(void)
{
    pclog("NV3 GPU: built without Vulkan headers, 3D stays on the CPU\n");
    return NULL;
}

int
nv3_gpu_shade(UNUSED(struct nv3_gpu_s *g), UNUSED(const nv3_d3d_batch_t *b))
{
    return 0;
}

void
nv3_gpu_close(UNUSED(struct nv3_gpu_s *g))
{
}

void
nv3_gpu_wait(UNUSED(struct nv3_gpu_s *g))
{
}

int
nv3_gpu_pending_overlaps(UNUSED(struct nv3_gpu_s *g), UNUSED(uint32_t lo), UNUSED(uint32_t hi))
{
    return 0;
}

void
nv3_gpu_counts(UNUSED(struct nv3_gpu_s *g), uint64_t *batches, uint64_t *fallbacks)
{
    *batches   = 0;
    *fallbacks = 0;
}

#else

#    include <86box/nv/nv_gpu_vk.h>
#    include <86box/nv/nv3_d3d_gpu_spv.h>

#    define GPU_TEX_ARENA (32u << 20)
#    define GPU_ROWS      2048

/* A draw state as the kernel reads it (std430, every field 4 bytes) */
typedef struct {
    uint32_t config, alpha_ctl, buf_on;
    int32_t  z_on;
    uint32_t boff[4], bpitch[4];
    int32_t  ncr, cr_inv;
    int32_t  cr_x0[2], cr_y0[2], cr_x1[2], cr_y1[2];
    int32_t  dither;
    uint32_t fog_rgb;
    int32_t  textured;
    uint32_t tex_format;
    uint32_t tex_base; /* in the texture arena */
    uint32_t tex_size;
    int32_t  fill; /* a 2D fill's bytes a pixel, its value in the triangle's c[0] */
} nv3_gpu_state_t;

typedef struct {
    int32_t y, x0, x1, tri;
} nv3_gpu_span_t;

typedef struct {
    uint32_t first, count;
} nv3_gpu_row_t;

_Static_assert(sizeof(nv3_gpu_state_t) == 29 * 4, "nv3_gpu_state_t layout");
_Static_assert(sizeof(nv3_d3d_tri_t) == (3 + 3 * D3_AT_COUNT) * 4, "nv3_d3d_tri_t layout");
_Static_assert(D3_AT_COUNT == 9, "the kernel interpolates 9 attributes");

/* Batches in flight (nv_gpu_vk_t.nslots): each has its own records, command buffer and fence
   (nv_gpu_vk.h). The rows a batch in flight owns are newer in the GPU's VRAM mirror than in VRAM,
   so a later batch does not upload them; batches complete (rows copied back) oldest first. */
typedef struct {
    nv_gpu_vk_slot_t *vk;
    int               pending;
    uint64_t          seq;
    VkBufferCopy      pdown[D3_REGIONS]; /* its rows, to copy back */
    int               pndown;
    VkBufferCopy      ptex[D3_STATES]; /* the texture staging it uploads from */
    int               pntex;
} gpu_slot_t;

/* A texture in the arena: where its bytes came from, a hash of them, and where they are */
typedef struct {
    int      vram;
    uint32_t l0, size, pages, base;
    uint64_t hash;
} gpu_texent_t;

#    define GPU_TEX_CACHE 1024

/* The pieces a batch's rows upload in: each region less the rows of every batch in flight */
#    define GPU_UPLOADS (D3_REGIONS * (1 + NV_GPU_SLOTS * D3_REGIONS))

typedef struct nv3_gpu_s {
    nv_gpu_vk_t           vk;
    gpu_slot_t            slot[NV_GPU_SLOTS];
    int                   next; /* the slot the next batch takes */
    uint64_t              seq;

    uint32_t     vram_size;
    /* The texture arena keeps textures across batches: each batch hashes its textures' bytes
       where they are, and copies only the ones that are new or changed. */
    gpu_texent_t cache[GPU_TEX_CACHE];
    int          ncache;
    uint32_t     tex_used;              /* arena bytes taken */
    int          flush_tex[D3_STATES]; /* cache entries this batch checked */
    int          nflush_tex;
    VkBufferCopy tcopy[D3_STATES];     /* arena regions to upload */
    int          ntcopy;
    uint32_t     row_count[GPU_ROWS];
    uint32_t     row_first[GPU_ROWS];
    uint64_t     batches, fallbacks;
    uint64_t     us_gather, us_copy, us_wait, tex_bytes, hash_bytes, row_bytes; /* where the time goes */
} nv3_gpu_t;

struct nv3_gpu_s *
nv3_gpu_init(void)
{
    nv3_gpu_t             *g = calloc(1, sizeof(nv3_gpu_t));
    const nv_gpu_vk_desc_t d = {
        .tag       = "NV3 GPU",
        .app_name  = "86Box RIVA 128",
        .vram_size = nv3->nvbase.svga.vram_max,
        .tex_arena = GPU_TEX_ARENA,
        .rec_size  = { sizeof(nv3_gpu_state_t) * D3_STATES, sizeof(nv3_d3d_tri_t) * D3_TRIS,
                       sizeof(nv3_gpu_span_t) * D3_SPANS, sizeof(nv3_gpu_row_t) * GPU_ROWS },
        .spv       = (const uint32_t *) nv3_gpu_spv,
        .spv_size   = sizeof(nv3_gpu_spv),
        .vram       = &nv3->nvbase.svga.vram,
        .vram_alloc = nv3->nvbase.svga.vram_max + 4096 /* svga_init's */
    };

    g->vram_size = nv3->nvbase.svga.vram_max;
    if (!nv_gpu_vk_open(&g->vk, &d)) {
        free(g);
        return NULL;
    }
    for (int i = 0; i < g->vk.nslots; i++)
        g->slot[i].vk = &g->vk.slot[i];
    return g;
}

void
nv3_gpu_counts(struct nv3_gpu_s *g, uint64_t *batches, uint64_t *fallbacks)
{
    *batches   = g->batches;
    *fallbacks = g->fallbacks;
    pclog("nv3 3D: GPU textures %llu us (%llu KB hashed, %llu KB copied), row copies %llu us (%llu KB), waiting %llu us\n",
          (unsigned long long) g->us_gather, (unsigned long long) (g->hash_bytes >> 10), (unsigned long long) (g->tex_bytes >> 10),
          (unsigned long long) g->us_copy, (unsigned long long) (g->row_bytes >> 10), (unsigned long long) g->us_wait);
    g->us_gather = g->us_copy = g->us_wait = g->tex_bytes = g->row_bytes = g->hash_bytes = 0;
}

void
nv3_gpu_close(struct nv3_gpu_s *g)
{
    if (!g)
        return;
    for (int i = 0; i < g->vk.nslots; i++)
        if (g->slot[i].pending)
            g->vk.vkWaitForFences(g->vk.dev, 1, &g->vk.slot[i].fence, VK_TRUE, UINT64_MAX);
    pclog("NV3 GPU: %llu batches on the GPU, %llu left to the CPU\n", (unsigned long long) g->batches,
          (unsigned long long) g->fallbacks);
    nv_gpu_vk_close(&g->vk);
    free(g);
}

static void gpu_complete_upto(nv3_gpu_t *g, uint64_t seq);

/* The bytes of a state's texture, a page's worth at a time: fn(p, at, len) for each piece */
#    define GPU_TEX_PIECES(s, body)                                                    \
        for (uint32_t done = 0; done < (s)->tex_size;) {                              \
            uint8_t        tmp[0x1000];                                                \
            uint32_t       len = 0x1000 - (((s)->tex_l0 + done) & 0xFFF);              \
            const uint8_t *p;                                                          \
                                                                                       \
            if (len > (s)->tex_size - done)                                            \
                len = (s)->tex_size - done;                                            \
            p = nv3_d3d_tex_ptr((s), done, len, tmp);                                  \
            body;                                                                      \
            done += len;                                                               \
        }

/* A texture drawn into by a batch still in flight (render to texture) reads that batch's pixels:
   it completes first. */
static void
gpu_tex_settle(nv3_gpu_t *g, const nv3_d3d_dstate_t *s)
{
    if (!s->tex_vram)
        return;
    for (uint32_t p = 0; p <= ((s->tex_l0 + s->tex_size - 1) >> 12); p++) {
        uint32_t a = s->tex_pages[p] & nv3->nvbase.svga.vram_mask & ~0xFFFu;

        if (nv3_gpu_pending_overlaps(g, a, a + 0xFFF)) {
            gpu_complete_upto(g, UINT64_MAX);
            return;
        }
    }
}

/* A 64-bit hash of a texture's bytes, read where they are. */
static uint64_t
gpu_tex_hash(nv3_gpu_t *g, const nv3_d3d_dstate_t *s)
{
    uint64_t h = 0x9E3779B97F4A7C15ull ^ s->tex_size;

    gpu_tex_settle(g, s);
    GPU_TEX_PIECES(s, {
        uint32_t i = 0;

        for (; i + 8 <= len; i += 8) {
            uint64_t w;

            memcpy(&w, p + i, 8);
            h = (h ^ w) * 0x100000001B3ull;
            h ^= h >> 29;
        }
        for (; i < len; i++)
            h = (h ^ p[i]) * 0x100000001B3ull;
    });
    g->hash_bytes += s->tex_size;
    return h;
}

/* Where a texture's pages are, as one number */
static uint32_t
gpu_tex_pages(const nv3_d3d_dstate_t *s)
{
    uint32_t h = 2166136261u;

    for (uint32_t p = 0; p <= ((s->tex_l0 + s->tex_size - 1) >> 12); p++)
        h = (h ^ s->tex_pages[p]) * 16777619u;
    return h;
}

/* Copy a texture's bytes into the arena's staging at <base>, to upload. A batch in flight may
   still upload from those staging bytes: it (and any older one) finishes first. Its kernel may
   still read the arena there, but the GPU orders that: every batch's kernel is followed by a
   barrier before the next transfer. */
static void
gpu_tex_copy(nv3_gpu_t *g, const nv3_d3d_dstate_t *s, uint32_t base)
{
    uint8_t *dst  = (uint8_t *) g->vk.b[NV_GPU_B_TSTAGE].map + base;
    uint32_t size = (s->tex_size + 3) & ~3u;
    uint64_t seq  = 0;

    for (int n = 0; n < g->vk.nslots; n++) {
        const gpu_slot_t *sl = &g->slot[n];

        for (int i = 0; sl->pending && (i < sl->pntex); i++)
            if ((sl->ptex[i].srcOffset < base + size) && (base < sl->ptex[i].srcOffset + sl->ptex[i].size) && (sl->seq > seq))
                seq = sl->seq;
    }
    if (seq)
        gpu_complete_upto(g, seq);
    GPU_TEX_PIECES(s, memcpy(dst + done, p, len));
    g->tcopy[g->ntcopy].srcOffset = base;
    g->tcopy[g->ntcopy].dstOffset = base;
    g->tcopy[g->ntcopy].size      = (s->tex_size + 3) & ~3u;
    g->ntcopy++;
    g->tex_bytes += s->tex_size;
}

/* A texture's place in the arena for this batch: from the cache when its bytes have not changed,
   else copied in. UINT32_MAX when the arena is full. */
static uint32_t
gpu_gather_tex(nv3_gpu_t *g, const nv3_d3d_dstate_t *s)
{
    uint32_t      pages = gpu_tex_pages(s);
    uint64_t      h;
    gpu_texent_t *e = NULL;

#    define GPU_TEX_SAME(c) (((c)->vram == s->tex_vram) && ((c)->l0 == s->tex_l0) && ((c)->size == s->tex_size) && ((c)->pages == pages))
    for (int i = 0; i < g->nflush_tex; i++)
        if (GPU_TEX_SAME(&g->cache[g->flush_tex[i]]))
            return g->cache[g->flush_tex[i]].base;
    h = gpu_tex_hash(g, s);
    for (int i = 0; i < g->ncache; i++)
        if (GPU_TEX_SAME(&g->cache[i])) {
            e = &g->cache[i];
            break;
        }
#    undef GPU_TEX_SAME
    if (!e) {
        if ((g->ncache >= GPU_TEX_CACHE) || (s->tex_size + 4 > GPU_TEX_ARENA - g->tex_used))
            return UINT32_MAX;
        e           = &g->cache[g->ncache++];
        e->vram     = s->tex_vram;
        e->l0       = s->tex_l0;
        e->size     = s->tex_size;
        e->pages    = pages;
        e->base     = g->tex_used;
        e->hash     = ~h; /* not there yet */
        g->tex_used = (e->base + s->tex_size + 3) & ~3u;
    }
    if (e->hash != h) {
        gpu_tex_copy(g, s, e->base);
        e->hash = h;
    }
    g->flush_tex[g->nflush_tex++] = (int) (e - g->cache);
    return e->base;
}

static void
gpu_pack_state(nv3_gpu_state_t *o, const nv3_d3d_dstate_t *s, uint32_t tex_base)
{
    memset(o, 0, sizeof(*o));
    o->config    = s->config;
    o->alpha_ctl = s->alpha_ctl;
    o->buf_on    = s->buf_on;
    o->z_on      = s->z_on;
    for (int i = 0; i < 4; i++) {
        o->boff[i]   = s->boff[i];
        o->bpitch[i] = s->bpitch[i];
    }
    o->ncr    = s->ncr;
    o->cr_inv = s->cr_inv;
    for (int i = 0; i < 2; i++) {
        o->cr_x0[i] = s->cr_x0[i];
        o->cr_y0[i] = s->cr_y0[i];
        o->cr_x1[i] = s->cr_x1[i];
        o->cr_y1[i] = s->cr_y1[i];
    }
    o->dither     = s->dither;
    o->fog_rgb    = s->fog_rgb;
    o->textured   = s->textured;
    o->tex_format = s->tex_format;
    o->tex_base   = tex_base;
    o->tex_size   = s->textured ? s->tex_size : 0;
    o->fill       = s->fill;
}

/* Wait for one batch in flight and copy its rows back into VRAM. */
static void
gpu_complete(nv3_gpu_t *g, gpu_slot_t *sl)
{
    uint8_t *stage = (uint8_t *) g->vk.b[NV_GPU_B_VSTAGE].map;
    uint8_t *vram  = nv3->nvbase.svga.vram;
    uint64_t t0, t1;

    t0 = nv_gpu_now_us();
    g->vk.vkWaitForFences(g->vk.dev, 1, &sl->vk->fence, VK_TRUE, UINT64_MAX);
    t1 = nv_gpu_now_us();
    g->us_wait += t1 - t0;
    for (int i = 0; i < sl->pndown; i++) {
        if (!g->vk.vram_import) /* else the GPU wrote VRAM itself */
            memcpy(vram + sl->pdown[i].srcOffset, stage + sl->pdown[i].srcOffset, sl->pdown[i].size);
        g->row_bytes += sl->pdown[i].size;
        for (uint32_t a = sl->pdown[i].srcOffset & ~0xFFFu; a < sl->pdown[i].srcOffset + sl->pdown[i].size; a += 0x1000)
            nv3->nvbase.svga.changedvram[a >> 12] = changeframecount;
    }
    g->us_copy += nv_gpu_now_us() - t1;
    sl->pending = 0;
}

/* Complete the batches in flight up to number <seq>, oldest first (a later batch's rows are
   newer). */
static void
gpu_complete_upto(nv3_gpu_t *g, uint64_t seq)
{
    for (;;) {
        gpu_slot_t *old = NULL;

        for (int i = 0; i < g->vk.nslots; i++)
            if (g->slot[i].pending && (g->slot[i].seq <= seq) && (!old || (g->slot[i].seq < old->seq)))
                old = &g->slot[i];
        if (!old)
            return;
        gpu_complete(g, old);
    }
}

void
nv3_gpu_wait(struct nv3_gpu_s *g)
{
    gpu_complete_upto(g, UINT64_MAX);
}

int
nv3_gpu_pending_overlaps(struct nv3_gpu_s *g, uint32_t lo, uint32_t hi)
{
    for (int s = 0; s < g->vk.nslots; s++) {
        const gpu_slot_t *sl = &g->slot[s];

        for (int i = 0; sl->pending && (i < sl->pndown); i++)
            if ((sl->pdown[i].srcOffset <= hi) && (lo < sl->pdown[i].srcOffset + sl->pdown[i].size))
                return 1;
    }
    return 0;
}

/* Upload the bytes lo..hi of VRAM to the GPU's mirror, except those a batch in flight owns (the
   mirror's are newer). */
static void
gpu_upload_range(nv3_gpu_t *g, uint32_t lo, uint32_t hi, VkBufferCopy *up, int *nup, int max)
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
                gpu_upload_range(g, lo, plo - 1, up, nup, max);
            if (phi < hi)
                gpu_upload_range(g, phi + 1, hi, up, nup, max);
            return;
        }
    }
    /* GPU_UPLOADS: <max> is never reached */
    if (*nup >= max)
        return;
    if (!g->vk.vram_import) /* else the GPU reads VRAM itself */
        memcpy(stage + lo, nv3->nvbase.svga.vram + lo, hi - lo + 1);
    up[*nup].srcOffset = up[*nup].dstOffset = lo;
    up[*nup].size                           = hi - lo + 1;
    (*nup)++;
}

int
nv3_gpu_shade(struct nv3_gpu_s *g, const nv3_d3d_batch_t *b)
{
    gpu_slot_t      *sl = &g->slot[g->next];
    nv3_gpu_state_t *gs;
    nv3_gpu_span_t  *gsp;
    nv3_gpu_row_t   *gr;
    VkCommandBuffer  cb;
    VkBufferCopy     up[GPU_UPLOADS], down[D3_REGIONS];
    int              nup = 0, ndown = 0;
    uint32_t         nrows = 0;
    int              xmax  = 0;
    uint32_t         pc[2];
    uint64_t         t0, t1, t2;

    /* this slot's last batch (and any older one) first */
    if (sl->pending)
        gpu_complete_upto(g, sl->seq);
    gs  = (nv3_gpu_state_t *) sl->vk->rech[NV_GPU_R_STATES].map;
    gsp = (nv3_gpu_span_t *) sl->vk->rech[NV_GPU_R_SPANS].map;
    gr  = (nv3_gpu_row_t *) sl->vk->rech[NV_GPU_R_ROWS].map;
    cb  = sl->vk->cb;
    if (!b->nregions)
        goto fallback;
    for (int r = 0; r < b->nregions; r++)
        if ((b->regions[r].wlo <= b->regions[r].whi) && (b->regions[r].whi >= g->vram_size))
            goto fallback;
    /* the kernel reads texels as whole words of the arena */
    for (int i = 0; i < b->nstates; i++)
        if (b->states[i].textured && (b->states[i].tex_l0 & 1))
            goto fallback;

    /* draw states and their textures; a full arena is emptied once and the batch's textures
       gathered again */
    t0 = nv_gpu_now_us();
    for (int pass = 0;; pass++) {
        int full = 0;

        g->nflush_tex = 0;
        g->ntcopy     = 0;
        for (int i = 0; (i < b->nstates) && !full; i++) {
            const nv3_d3d_dstate_t *s    = &b->states[i];
            uint32_t                base = 0;

            if (s->textured && s->tex_size && ((base = gpu_gather_tex(g, s)) == UINT32_MAX))
                full = 1;
            gpu_pack_state(&gs[i], s, base);
        }
        if (!full)
            break;
        g->ncache   = 0;
        g->tex_used = 0;
        if (pass)
            goto fallback;
    }
    t1 = nv_gpu_now_us();
    g->us_gather += t1 - t0;
    memcpy(sl->vk->rech[NV_GPU_R_TRIS].map, b->tris, sizeof(nv3_d3d_tri_t) * b->ntris);

    /* spans grouped by row, each row in submission order */
    memset(g->row_count, 0, sizeof(g->row_count));
    for (int i = 0; i < b->nspans; i++)
        g->row_count[b->spans[i].y & (GPU_ROWS - 1)]++;
    {
        uint32_t at = 0;

        for (int y = 0; y < GPU_ROWS; y++) {
            g->row_first[y] = at;
            if (g->row_count[y]) {
                gr[nrows].first = at;
                gr[nrows].count = g->row_count[y];
                nrows++;
            }
            at += g->row_count[y];
        }
    }
    for (int i = 0; i < b->nspans; i++) {
        const nv3_d3d_span_t *sp = &b->spans[i];
        nv3_gpu_span_t       *o  = &gsp[g->row_first[sp->y & (GPU_ROWS - 1)]++];

        o->y   = sp->y;
        o->x0  = sp->x0;
        o->x1  = sp->x1;
        o->tri = sp->tri;
        if (sp->x1 > xmax)
            xmax = sp->x1;
    }
    if (!nrows)
        goto fallback;

    /* rows in */
    for (int r = 0; r < b->nregions; r++) {
        const nv3_d3d_region_t *rg = &b->regions[r];

        if (rg->wlo > rg->whi)
            continue;
        gpu_upload_range(g, rg->wlo, rg->whi, up, &nup, GPU_UPLOADS);
        down[ndown].srcOffset = down[ndown].dstOffset = rg->wlo;
        down[ndown].size                              = rg->whi - rg->wlo + 1;
        ndown++;
    }
    if (!ndown)
        goto fallback;

    t2 = nv_gpu_now_us();
    g->us_copy += t2 - t1;
    {
        VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VkSubmitInfo             si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        g->vk.vkResetCommandBuffer(cb, 0);
        g->vk.vkBeginCommandBuffer(cb, &bi);
        if (nup)
            g->vk.vkCmdCopyBuffer(cb, g->vk.b[NV_GPU_B_VSTAGE].buf, g->vk.b[NV_GPU_B_VRAM].buf, nup, up);
        {
            /* the records, read many times per pixel, to device memory */
            VkBufferCopy rc[4] = {
                { 0, 0, sizeof(nv3_gpu_state_t) * b->nstates },
                { 0, 0, sizeof(nv3_d3d_tri_t) * b->ntris },
                { 0, 0, sizeof(nv3_gpu_span_t) * b->nspans },
                { 0, 0, sizeof(nv3_gpu_row_t) * nrows }
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
        g->vk.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->vk.pipe);
        g->vk.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->vk.pl, 0, 1, &sl->vk->dset, 0, NULL);
        pc[0] = nrows;
        pc[1] = nv3->nvbase.svga.vram_mask;
        g->vk.vkCmdPushConstants(cb, g->vk.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
        /* a workgroup per row and 64 pixels */
        g->vk.vkCmdDispatch(cb, nrows, (xmax + 63) / 64, 1);
        nv_gpu_vk_barrier(&g->vk, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT);
        g->vk.vkCmdCopyBuffer(cb, g->vk.b[NV_GPU_B_VRAM].buf, g->vk.b[NV_GPU_B_VSTAGE].buf, ndown, down);
        nv_gpu_vk_barrier(&g->vk, cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                    VK_ACCESS_HOST_READ_BIT);
        g->vk.vkEndCommandBuffer(cb);
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cb;
        g->vk.vkResetFences(g->vk.dev, 1, &sl->vk->fence);
        if (g->vk.vkQueueSubmit(g->vk.queue, 1, &si, sl->vk->fence) != VK_SUCCESS)
            goto fallback;
    }

    /* in flight: the rows come back when a later batch needs the slot or a sync point waits */
    sl->pending = 1;
    sl->seq     = ++g->seq;
    sl->pndown  = ndown;
    for (int i = 0; i < ndown; i++)
        sl->pdown[i] = down[i];
    sl->pntex = g->ntcopy;
    for (int i = 0; i < g->ntcopy; i++)
        sl->ptex[i] = g->tcopy[i];
    g->next = (g->next + 1) % g->vk.nslots;
    g->batches++;
    return 1;

fallback:
    g->fallbacks++;
    return 0;
}

#endif
