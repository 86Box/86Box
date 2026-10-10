/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- register harness, GPU-lane probes.
 *
 *          This file includes the production GPU backend source, so the
 *          vectors drive the backend's real state-to-kernel gate,
 *          per-triangle needs, primitive ranges and span capture, not a
 *          transcription. The fabricated backend below is host memory
 *          only: the capture path writes plain records and calls nothing
 *          from Vulkan, so a host with no GPU costs the probes nothing.
 *          What they cannot reach is the kernel itself: these vectors
 *          prove what the host hands the kernel. gpu_probe.h describes
 *          the entry points.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include "../../../../src/video/vid_ati_rage128_gpu.c"

#include "gpu_probe.h"

#ifdef R128_GPU_HAVE_VULKAN

int
harness_gpu_available(void)
{
    return 1;
}

/* A backend with no Vulkan objects: both arena imports reported live (so
   a gate answer is about the state, not residency) and the reject log
   silenced (the probe is not traffic). Freed by harness_gpu_free. */
static r128_gpu_t *
harness_gpu_alloc(void)
{
    r128_gpu_t *g = (r128_gpu_t *) calloc(1, sizeof(*g));

    if (!g)
        return NULL;
    g->stage_ok      = 1;
    g->czstage_ok    = 1;
    g->rejects_logged = 16;
    g->cur_tri        = -1;
    g->cur_kernel     = 0;
    g->cur_tuple      = -1;
    /* tuple book: heap-allocated in production init; the submit path
       interns into it (memo -1 = no last lookup, quiet = not traffic) */
    g->tuples       = calloc(GPU_TUPLE_CAP, sizeof(gpu_tuple_t));
    g->memo_tuple   = -1;
    g->quiet_intern = 1;
    if (!g->tuples) {
        free(g);
        return NULL;
    }
    /* The flush lock, as production builds it (recursive): calloc cannot
       stand in for an initialized mutex, and a vector that drives a real
       submit through gpu_flush_cause takes it. */
    {
        pthread_mutexattr_t fa;

        pthread_mutexattr_init(&fa);
        pthread_mutexattr_settype(&fa, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&g->flush_mtx, &fa);
        pthread_mutexattr_destroy(&fa);
    }
    return g;
}

static void
harness_gpu_dispose(r128_gpu_t *g)
{
    if (!g)
        return;
    pthread_mutex_destroy(&g->flush_mtx);
    free(g->tuples);
    free(g);
}

int
harness_gpu_gate(rage128_t *dev, const rage128_raster_state_t *rs)
{
    r128_gpu_t *g   = harness_gpu_alloc();
    void       *old = dev->gpu;
    int         k;

    if (!g)
        return -1;
    dev->gpu = g;
    k        = gpu_state_kernel(dev, rs);
    dev->gpu = old;
    harness_gpu_dispose(g);
    return k;
}

int
harness_gpu_capture_tri(rage128_t *dev, const rage128_raster_state_t *rs,
                        const r3d_vtx_t *a, const r3d_vtx_t *b,
                        const r3d_vtx_t *c, harness_gpu_span_t *out,
                        uint32_t max, harness_gpu_cap_t *res)
{
    r128_gpu_t       *g   = harness_gpu_alloc();
    void             *old = dev->gpu;
    const seg_span_t *sp;

    memset(res, 0, sizeof(*res));
    if (!g)
        return 0;
    /* the records the capture writes; device-visible in production, plain
       host memory here -- the capture path never touches Vulkan */
    g->slot[0].b_spans.map = calloc(GPU_SPAN_CAP, sizeof(seg_span_t));
    g->slot[0].b_tris.map  = calloc(GPU_TRI_CAP, sizeof(seg_tri_t));
    g->slot[0].b_pal.map   = calloc(GPU_PAL_CAP * 256, sizeof(uint32_t));
    if (!g->slot[0].b_spans.map || !g->slot[0].b_tris.map
        || !g->slot[0].b_pal.map) {
        free(g->slot[0].b_spans.map);
        free(g->slot[0].b_tris.map);
        free(g->slot[0].b_pal.map);
        harness_gpu_dispose(g);
        return 0;
    }

    gpu_tri_need(rs, a, b, c, &res->need_spans, &res->need_px);
    prim_ranges(rs, dev->vram_mask, a, b, c, &res->clo, &res->chi, &res->zlo,
                &res->zhi);

    dev->gpu = g;
    rage128_3d_tri(dev, rs, 0, 0, a, b, c, rage128_gpu_capture_span);
    dev->gpu = old;

    sp          = (const seg_span_t *) g->slot[0].b_spans.map;
    res->nspans = g->nspans;
    res->got    = g->nspans < max ? g->nspans : max;
    for (uint32_t i = 0; i < res->got; i++) {
        out[i].x0   = sp[i].x0;
        out[i].x1   = sp[i].x1;
        out[i].py   = sp[i].py;
        out[i].drow = sp[i].drow;
        out[i].zrow = sp[i].zrow;
        out[i].e0   = sp[i].e0;
        out[i].e1   = sp[i].e1;
        out[i].e2   = sp[i].e2;
        out[i].zline = sp[i].zline;
    }
    if (g->ntris > 0) {
        const seg_tri_t *tt = (const seg_tri_t *) g->slot[0].b_tris.map;

        res->dzdx   = tt->dZdx;
        res->edx[0] = tt->e0dxi;
        res->edx[1] = tt->e1dxi;
        res->edx[2] = tt->e2dxi;
    }
    free(g->slot[0].b_spans.map);
    free(g->slot[0].b_tris.map);
    free(g->slot[0].b_pal.map);
    harness_gpu_dispose(g);
    return 1;
}

int
harness_gpu_submit_probe(rage128_t *dev, const rage128_raster_state_t *rs,
                         const r3d_vtx_t *a, const r3d_vtx_t *b,
                         const r3d_vtx_t *c, harness_gpu_sub_t *res)
{
    r128_gpu_t *g   = harness_gpu_alloc();
    void       *old = dev->gpu;

    memset(res, 0, sizeof(*res));
    if (!g)
        return 0;
    g->slot[0].b_spans.map = calloc(GPU_SPAN_CAP, sizeof(seg_span_t));
    g->slot[0].b_tris.map  = calloc(GPU_TRI_CAP, sizeof(seg_tri_t));
    g->slot[0].b_pal.map   = calloc(GPU_PAL_CAP * 256, sizeof(uint32_t));
    if (!g->slot[0].b_spans.map || !g->slot[0].b_tris.map
        || !g->slot[0].b_pal.map) {
        free(g->slot[0].b_spans.map);
        free(g->slot[0].b_tris.map);
        free(g->slot[0].b_pal.map);
        harness_gpu_dispose(g);
        return 0;
    }

    gpu_tri_need(rs, a, b, c, &res->bound_spans, &res->bound_px);

    dev->gpu      = g;
    res->accepted = rage128_gpu_submit_tri(dev, rs, a, b, c);
    dev->gpu      = old;

    res->reprice = g->st_reprice;
    res->toobig  = g->st_toobig;
    res->miss    = g->st_reprice_miss;
    res->nspans  = g->nspans;
    res->seg_px  = g->seg_px;
    free(g->slot[0].b_spans.map);
    free(g->slot[0].b_tris.map);
    free(g->slot[0].b_pal.map);
    harness_gpu_dispose(g);
    return 1;
}

/* The texq probe's backend between begin and end: the fabricated
   backend installed as dev->gpu, the backend it displaced, and whether
   every draw so far was accepted. */
typedef struct harness_texq_h {
    r128_gpu_t *g;
    void       *old;
    int         accepted;
} harness_texq_h;

unsigned
harness_gpu_texq_cap(void)
{
    return GPU_TEXQ_CAP;
}

void *
harness_gpu_texq_begin(rage128_t *dev)
{
    harness_texq_h *h = (harness_texq_h *) calloc(1, sizeof(*h));
    r128_gpu_t     *g = harness_gpu_alloc();

    if (!h || !g) {
        free(h);
        harness_gpu_dispose(g);
        return NULL;
    }
    g->slot[0].b_spans.map = calloc(GPU_SPAN_CAP, sizeof(seg_span_t));
    g->slot[0].b_tris.map  = calloc(GPU_TRI_CAP, sizeof(seg_tri_t));
    g->slot[0].b_pal.map   = calloc(GPU_PAL_CAP * 256, sizeof(uint32_t));
    if (!g->slot[0].b_spans.map || !g->slot[0].b_tris.map
        || !g->slot[0].b_pal.map) {
        free(g->slot[0].b_spans.map);
        free(g->slot[0].b_tris.map);
        free(g->slot[0].b_pal.map);
        harness_gpu_dispose(g);
        free(h);
        return NULL;
    }
    rng_reset(g);
    h->g        = g;
    h->old      = dev->gpu;
    h->accepted = 1;
    dev->gpu    = g;
    return h;
}

int
harness_gpu_texq_draw(void *hp, rage128_t *dev, const rage128_raster_state_t *rs,
                      const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c)
{
    harness_texq_h *h  = (harness_texq_h *) hp;
    int             ok = rage128_gpu_submit_tri(dev, rs, a, b, c);

    h->accepted = h->accepted && ok;
    return ok;
}

void
harness_gpu_texq_end(void *hp, rage128_t *dev, uint32_t lo, uint32_t hi,
                     harness_gpu_texq_t *res)
{
    harness_texq_h *h = (harness_texq_h *) hp;
    r128_gpu_t     *g = h->g;
    gpu_slot_t     *sl;
    int             ovf = g->texq_ovf;

    memset(res, 0, sizeof(*res));
    res->accepted = h->accepted;
    res->ntexq    = g->ntexq;
    res->ovf      = ovf;
    res->over_cap = g->st_texq_ovf;
    res->q_lo     = atomic_load(&g->q_lo);
    res->q_hi     = atomic_load(&g->q_hi);
    /* one 2D operation's window around the pending test and the slot
       test below, as rage128_gpu_2d_barrier opens around its tests */
    gpu_texq_op_begin(g);
    res->hull  = gpu_pending_hit(g, lo, hi, 1);
    res->exact = gpu_pending_hit_2d(g, lo, hi, 1);
    g->texq_ovf    = 1;
    res->ovf_exact = gpu_pending_hit_2d(g, lo, hi, 1);
    g->texq_ovf    = ovf;

    /* publish into slot 0 as the submit path does (ranges, list, then
       the submitted flag) and retire the pending half, so the walk the
       2D barrier takes over in-flight slots is what answers */
    sl        = &g->slot[0];
    sl->c_lo  = atomic_load(&g->c_lo); sl->c_hi = atomic_load(&g->c_hi);
    sl->z_lo  = atomic_load(&g->z_lo); sl->z_hi = atomic_load(&g->z_hi);
    sl->q_lo  = res->q_lo;             sl->q_hi = res->q_hi;
    sl->r_lo  = atomic_load(&g->r_lo); sl->r_hi = atomic_load(&g->r_hi);
    sl->f_lo  = atomic_load(&g->f_lo); sl->f_hi = atomic_load(&g->f_hi);
    sl->ntexq    = g->ntexq;
    sl->texq_ovf = g->texq_ovf;
    memcpy(sl->texq_lo, g->texq_lo, g->ntexq * sizeof(uint32_t));
    memcpy(sl->texq_hi, g->texq_hi, g->ntexq * sizeof(uint32_t));
    atomic_store(&g->pending, 0);
    atomic_store(&g->inflight, 1);
    atomic_store(&sl->submitted, 1);
    res->slot_ovf   = sl->texq_ovf;
    res->slot_hull  = gpu_range_hit(g, lo, hi, 1);
    res->slot_exact = gpu_range_hit_2d(g, lo, hi, 1);
    gpu_texq_op_end(g);
    res->spared = g->st_texq_spared;
    res->real   = g->st_texq_real;
    atomic_store(&sl->submitted, 0);
    atomic_store(&g->inflight, 0);
    dev->gpu = h->old;

    free(g->slot[0].b_spans.map);
    free(g->slot[0].b_tris.map);
    free(g->slot[0].b_pal.map);
    harness_gpu_dispose(g);
    free(h);
}

int
harness_gpu_texq_probe(rage128_t *dev, const rage128_raster_state_t *rs,
                       const rage128_raster_state_t *rs2,
                       const r3d_vtx_t *a, const r3d_vtx_t *b,
                       const r3d_vtx_t *c, uint32_t lo, uint32_t hi,
                       harness_gpu_texq_t *res)
{
    void *h = harness_gpu_texq_begin(dev);

    if (!h) {
        memset(res, 0, sizeof(*res));
        return 0;
    }
    harness_gpu_texq_draw(h, dev, rs, a, b, c);
    if (rs2)
        harness_gpu_texq_draw(h, dev, rs2, a, b, c);
    harness_gpu_texq_end(h, dev, lo, hi, res);
    return 1;
}

/* The fabricated backend gets a host-malloc'd 2D staging ring: without it
   rage128_gpu_2d_stage returns null and every staged/RMW route reports a
   STAGING refusal, which reads exactly like the route refusals a wmask or
   cap test is trying to measure. */
void *
harness_gpu_fab_alloc(void)
{
    r128_gpu_t *g = harness_gpu_alloc();

    if (!g)
        return NULL;
    g->stage2d.map = calloc(GPU_2DSTAGE_SIZE, 1);
    if (g->stage2d.map) {
        g->stage2d_ok   = 1;
        g->stage2d_head = 0;
    }
    return g;
}

void
harness_gpu_fab_free(void *g)
{
    free(((r128_gpu_t *) g)->stage2d.map);
    ((r128_gpu_t *) g)->stage2d.map = NULL;
    ((r128_gpu_t *) g)->stage2d_ok  = 0;
    harness_gpu_dispose((r128_gpu_t *) g);
}

void
harness_gpu_fab_q2d(void *gp, int table, uint64_t *rects, uint64_t *runs,
                    uint64_t *skip_wmask, uint64_t *cpu)
{
    r128_gpu_t *g = (r128_gpu_t *) gp;

    if (rects)
        *rects = g->st_qdone[table];
    if (runs)
        *runs = g->st_q_runs[table];
    if (skip_wmask)
        *skip_wmask = g->st_qskip[table][1];
    if (cpu)
        *cpu = g->st_q_cpu[table];
}

uint64_t
harness_gpu_q2d_bad(void *gp, int table)
{
    return ((r128_gpu_t *) gp)->st_qbad[table];
}

void
harness_gpu_fab_2ds(void *gp, uint64_t *full, uint64_t *bytes,
                    uint64_t *recycles)
{
    r128_gpu_t *g = (r128_gpu_t *) gp;

    if (full)
        *full = g->st_2ds_full;
    if (bytes)
        *bytes = g->st_2ds_bytes;
    if (recycles)
        *recycles = g->st_2ds_recycles;
}

void
harness_gpu_fab_stage_ref(void *g, int on)
{
    ((r128_gpu_t *) g)->slot[0].stage_ref = on;
}

uint64_t
harness_gpu_fab_qskip(void *g, int t, int b)
{
    return ((r128_gpu_t *) g)->st_qskip[t][b];
}

uint64_t
harness_gpu_fab_qnb(void *g, int t)
{
    return ((r128_gpu_t *) g)->st_qnb[t];
}

uint64_t
harness_gpu_fab_fill_skip(void *g, int b)
{
    return ((r128_gpu_t *) g)->st_fill_skip[b];
}

uint64_t
harness_gpu_fab_tiled(void *g)
{
    return ((r128_gpu_t *) g)->st_tiled_cpu;
}

uint64_t
harness_gpu_fab_fill_rects(void *g)
{
    return ((r128_gpu_t *) g)->st_fill_rects;
}

uint64_t
harness_gpu_fab_fill_cpu(void *g)
{
    return ((r128_gpu_t *) g)->st_fill_cpu;
}

void
harness_gpu_fab_q_reset(void *gp)
{
    r128_gpu_t *g = (r128_gpu_t *) gp;

    g->nfills = 0;
    g->nmid   = 0;
    rng_reset(g);
    atomic_store(&g->pending, 0);
}

int
harness_gpu_fab_fill_exec(void *gp, uint8_t *mem, uint32_t size,
                          uint32_t *flo, uint32_t *fhi)
{
    r128_gpu_t *g = (r128_gpu_t *) gp;
    int         n = (int) g->nfills;

    for (uint32_t i = 0; i < g->nfills; i++) {
        const gpu_fill_t *f = &g->fills[i];

        if (f->kind != 0 || f->mid >= 0 || (f->len & 3u))
            return -1;
        for (uint32_t r = 0; r < f->rows; r++) {
            uint64_t a = (uint64_t) f->addr + (uint64_t) r * f->pitch;

            if ((a & 3u) || a + f->len > size)
                return -1;
            for (uint32_t b = 0; b < f->len; b++)
                mem[a + b] = (uint8_t) (f->color >> (((a + b) & 3u) * 8));
        }
    }
    *flo = atomic_load(&g->f_lo);
    *fhi = atomic_load(&g->f_hi);
    harness_gpu_fab_q_reset(gp);
    return n;
}

/* Scanout-request probe: the fabricated backend becomes an async lane
   whose pending accumulation owns one 2D write range and nothing else
   (no spans, no queued ops), so a submit of it is the accumulation
   reset alone and needs no Vulkan object. */
void
harness_gpu_fab_scan_arm(void *gp, uint32_t lo, uint32_t hi)
{
    r128_gpu_t *g = (r128_gpu_t *) gp;

    g->async  = 1;
    g->verify = 0;
    rng_reset(g);
    gpu_f_add(g, lo, hi);
    atomic_store(&g->pending, 1);
}

void
harness_gpu_fab_scan_add(void *gp, uint32_t lo, uint32_t hi)
{
    gpu_f_add((r128_gpu_t *) gp, lo, hi);
}

uint64_t
harness_gpu_fab_scan_stale(void *gp)
{
    return ((r128_gpu_t *) gp)->st_scan_stale;
}

void
harness_gpu_fab_scan_read(void *gp, int *pending, uint64_t *kicks,
                          uint64_t *defers, uint64_t *served)
{
    r128_gpu_t *g = (r128_gpu_t *) gp;

    /* served first: its acquire pairs with the executor's release after
       the submit, so a served request reads with its pending cleared */
    *served  = atomic_load_explicit(&g->st_scan_served, memory_order_acquire);
    *pending = atomic_load(&g->pending);
    *kicks   = g->st_scan_kicks;
    *defers  = g->st_scan_defers;
}

int
harness_gpu_fab_scan_req(void *gp, int set)
{
    r128_gpu_t *g = (r128_gpu_t *) gp;

    if (set < 0)
        return atomic_load(&g->scan_req);
    return atomic_exchange(&g->scan_req, set ? 1 : 0);
}

int
harness_gpu_verify_stats(rage128_t *dev, uint64_t *segments, uint64_t *mismatched)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g || !g->verify)
        return 0;
    *segments   = g->st_verify_seg;
    *mismatched = g->st_verify_bad;
    return 1;
}

int
harness_gpu_ov0_stats(rage128_t *dev, uint64_t *frames, uint64_t *mismatched)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;

    if (!g || !g->verify)
        return 0;
    *frames     = g->st_ov0_frames;
    *mismatched = g->st_ov0_bad;
    return 1;
}

/* The unlit format-ANY seed of a state's combine: the tuple
   gpu_tuples_prime interns for it (formats runtime, light word 0). */
static int32_t
harness_gpu_seed_intern(r128_gpu_t *g, const uint32_t *sel)
{
    uint32_t seed[GPU_TUPLE_SEL];

    memcpy(seed, sel, sizeof(seed));
    for (int k = 16; k < 20; k++)
        seed[k] = GPU_TUPLE_FMT_ANY;
    seed[GPU_TUPLE_LIGHT] = 0;
    return gpu_tuple_intern(g, seed);
}

extern int  harness_gpu_cache_begin(void);
extern void harness_gpu_cache_end(void);

/* Serialize a normalized two-stage tuple and reload it through the
   production loader. Invalid format words are injected after normalization
   to exercise the cache's fallback without inventing live draw states. */
int
harness_gpu_tuple_reload(uint32_t fmt0, uint32_t fmt1,
                         uint32_t s3tc0, uint32_t s3tc1,
                         uint32_t loaded[4], int *exact)
{
    r128_gpu_t *g = harness_gpu_alloc();
    rage128_draw_state_t d = { 0 };
    uint32_t sel[GPU_TUPLE_SEL];
    uint32_t fmt[2] = { fmt0, fmt1 }, s3tc[2] = { s3tc0, s3tc1 };
    char path[1088];
    FILE *f;
    unsigned kern;
    int ok = 0, cache_active = 0;

    *exact = 0;
    if (!g)
        return 0;
    g->learned = calloc(GPU_TUPLE_CAP * GPU_KERNELS, sizeof(gpu_fold_job_t));
    if (!g->learned)
        goto done;
    for (kern = 0; kern < GPU_KERNELS; kern++)
        if (r128_gpu_variants[kern].tex == 2)
            break;
    if (kern == GPU_KERNELS)
        goto done;
    d.wmask = 0xffffffffu;
    for (int st = 0; st < 2; st++) {
        d.comb[st].comb = 1;
        d.comb[st].cfac = 1;
        d.comb[st].comba = 1;
        d.comb[st].afac = 1;
        d.sh[st].dt = fmt[st] & 0xfu;
        d.sh[st].aone = !!(fmt[st] & 0x10u);
        d.sh[st].s3tc = s3tc[st] & 3u;
    }
    gpu_tuple_normalize(&d, 2, sel);
    for (int st = 0; st < 2; st++) {
        if (fmt[st] > 31)
            sel[16 + st * 2] = fmt[st];
        if (s3tc[st] > 3)
            sel[17 + st * 2] = s3tc[st];
    }
    if (!harness_gpu_cache_begin())
        goto done;
    cache_active = 1;
    gpu_cache_path(path, sizeof(path), "r128gpu.tuples");
    f = fopen(path, "w");
    if (!f)
        goto done;
    fprintf(f, GPU_TUPLES_HDR "\n%u", kern);
    for (unsigned i = 0; i < GPU_TUPLE_SEL; i++)
        fprintf(f, " %u", sel[i]);
    fprintf(f, "\n");
    if (fclose(f))
        goto done;
    gpu_tuples_load(g);
    if (g->nlearn == 1 && g->learned[0].kern == kern) {
        const gpu_tuple_t *t = &g->tuples[g->learned[0].tid];

        memcpy(loaded, &t->sel[16], 4 * sizeof(*loaded));
        *exact = !memcmp(t->sel, sel, sizeof(sel));
        ok = 1;
    }
done:
    if (cache_active)
        harness_gpu_cache_end();
    free(g->learned);
    harness_gpu_dispose(g);
    return ok;
}

int
harness_gpu_tuple_pair(const uint32_t a[7], const uint32_t b[7],
                       uint32_t *pairs, int *distinct)
{
    r128_gpu_t *g = harness_gpu_alloc();
    uint32_t    sel[2][GPU_TUPLE_SEL];
    char        path[1088];
    FILE       *f;
    unsigned    kern;
    int         ok = 0, cache_active = 0;

    *pairs    = 0;
    *distinct = 0;
    if (!g)
        return 0;
    g->learned = calloc(GPU_TUPLE_CAP * GPU_KERNELS, sizeof(gpu_fold_job_t));
    if (!g->learned)
        goto done;
    for (kern = 0; kern < GPU_KERNELS; kern++)
        if (r128_gpu_variants[kern].tex == 1)
            break;
    if (kern == GPU_KERNELS)
        goto done;
    for (int i = 0; i < 2; i++) {
        rage128_draw_state_t d = { 0 };
        const uint32_t      *c = i ? b : a;

        d.wmask         = 0xffffffffu;
        d.comb[0].comb  = c[0];
        d.comb[0].fmsb  = c[1];
        d.comb[0].cfac  = c[2];
        d.comb[0].ifac  = c[3];
        d.comb[0].comba = c[4];
        d.comb[0].afac  = c[5];
        d.comb[0].ifaca = c[6];
        d.sh[0].dt      = 6;
        gpu_tuple_normalize(&d, 1, sel[i]);
    }
    if (!harness_gpu_cache_begin())
        goto done;
    cache_active = 1;
    gpu_cache_path(path, sizeof(path), "r128gpu.tuples");
    f = fopen(path, "w");
    if (!f)
        goto done;
    fprintf(f, GPU_TUPLES_HDR "\n");
    for (int i = 0; i < 2; i++) {
        fprintf(f, "%u", kern);
        for (unsigned k = 0; k < GPU_TUPLE_SEL; k++)
            fprintf(f, " %u", sel[i][k]);
        fprintf(f, "\n");
    }
    if (fclose(f))
        goto done;
    gpu_tuples_load(g);
    *pairs = g->nlearn;
    if (g->nlearn == 2)
        *distinct = g->learned[0].tid != g->learned[1].tid;
    ok = 1;
done:
    if (cache_active)
        harness_gpu_cache_end();
    free(g->learned);
    harness_gpu_dispose(g);
    return ok;
}

int
harness_gpu_tuple_buddy(rage128_t *dev, const rage128_raster_state_t *rs,
                        int32_t *light, int32_t *buddy_light)
{
    r128_gpu_t *g   = harness_gpu_alloc();
    void       *old = dev->gpu;
    uint32_t    sel[GPU_TUPLE_SEL];
    int         kern;
    int32_t     tid;

    *light = *buddy_light = -1;
    if (!g)
        return 0;
    dev->gpu = g;
    kern     = gpu_state_kernel(dev, rs);
    dev->gpu = old;
    if (kern < 0 || r128_gpu_variants[kern].tex <= 0) {
        harness_gpu_dispose(g);
        return 0;
    }
    gpu_tuple_normalize(&rs->d, r128_gpu_variants[kern].tex, sel);
    *light = (int32_t) sel[GPU_TUPLE_LIGHT];
    harness_gpu_seed_intern(g, sel);
    tid = gpu_tuple_intern(g, sel);
    if (tid >= 0 && g->tuples[tid].any_tid >= 0)
        *buddy_light = (int32_t) g->tuples[g->tuples[tid].any_tid]
                           .sel[GPU_TUPLE_LIGHT];
    harness_gpu_dispose(g);
    return 1;
}

int
harness_gpu_seed_probe(const uint32_t comb[14], uint32_t *rows,
                       int *in_table, uint32_t *unknown)
{
    /* A bare backend for gpu_tuples_prime: it initializes the three
       mutexes itself, so none is set up here. The cache directory is
       redirected to an empty one so the learned list stays out. */
    r128_gpu_t *g = (r128_gpu_t *) calloc(1, sizeof(*g));
    uint32_t    sel[GPU_TUPLE_SEL];
    uint64_t    before;
    int         ok = 0;

    *rows = 0;
    *in_table = 0;
    *unknown = 0;
    if (!g)
        return 0;
    g->tuples     = calloc(GPU_TUPLE_CAP, sizeof(gpu_tuple_t));
    g->learned    = calloc(GPU_TUPLE_CAP * GPU_KERNELS, sizeof(gpu_fold_job_t));
    g->memo_tuple = -1;
    g->cur_tuple  = -1;
    g->cur_tri    = -1;
    if (!g->tuples || !g->learned || !harness_gpu_cache_begin())
        goto done;
    gpu_tuples_prime(g);
    harness_gpu_cache_end();

    *rows = (uint32_t) (sizeof(r128_gpu_comb_seed) / sizeof(r128_gpu_comb_seed[0]));
    for (uint32_t i = 0; i < *rows && !*in_table; i++) {
        int same = 1;

        for (int k = 0; k < 14 && same; k++)
            same = r128_gpu_comb_seed[i].sel[k] == comb[k];
        *in_table = same;
    }

    /* the combine as a live draw interns it: formats pinned (opaque
       ARGB8888, no S3TC) on a stage with selectors, ANY on an off
       stage; stencil off, color live, unlit */
    for (int st = 0; st < 2; st++) {
        int on = 0;

        for (int k = 0; k < 7; k++) {
            sel[st * 7 + k] = comb[st * 7 + k];
            on |= comb[st * 7 + k] != 0;
        }
        sel[16 + st * 2] = on ? 6u : GPU_TUPLE_FMT_ANY;
        sel[17 + st * 2] = on ? 0u : GPU_TUPLE_FMT_ANY;
    }
    sel[14] = 0;
    sel[15] = 0;
    sel[GPU_TUPLE_LIGHT] = 0;
    gpu_tuple_canon(sel);
    before = g->st_tunknown;
    gpu_tuple_intern(g, sel);
    *unknown = (uint32_t) (g->st_tunknown - before);
    pthread_mutex_destroy(&g->fold_mtx);
    pthread_mutex_destroy(&g->queue_mtx);
    pthread_mutex_destroy(&g->flush_mtx);
    ok = 1;
done:
    free(g->learned);
    free(g->tuples);
    free(g);
    return ok;
}

int
harness_gpu_seed_land(rage128_t *dev, const rage128_raster_state_t *rs)
{
    r128_gpu_t *g = (r128_gpu_t *) dev->gpu;
    uint32_t    sel[GPU_TUPLE_SEL];
    int         kern, quiet, landed = 0;
    int32_t     tid;

    if (!g || !g->verify)
        return 0;
    kern = gpu_state_kernel(dev, rs);
    if (kern < 0 || r128_gpu_variants[kern].tex <= 0)
        return 0;
    gpu_tuple_normalize(&rs->d, r128_gpu_variants[kern].tex, sel);
    quiet           = g->quiet_intern;
    g->quiet_intern = 1;
    tid             = harness_gpu_seed_intern(g, sel);
    g->quiet_intern = quiet;
    if (tid < 0)
        return 0;
    /* a null here means the live worker holds the claim from an earlier
       buddy request: wait for its publish (bounded) rather than race it */
    if (gpu_fold_compile(g, (uint32_t) kern, tid, 0) != VK_NULL_HANDLE)
        return 1;
    for (int i = 0; i < 6000 && !landed; i++) {
        struct timespec ts = { 0, 10000000L };

        pthread_mutex_lock(&g->fold_mtx);
        landed = g->tuples[tid].pipes[kern] != VK_NULL_HANDLE
            || g->tuples[tid].failed[kern];
        pthread_mutex_unlock(&g->fold_mtx);
        if (!landed)
            nanosleep(&ts, NULL);
    }
    pthread_mutex_lock(&g->fold_mtx);
    landed = g->tuples[tid].pipes[kern] != VK_NULL_HANDLE;
    pthread_mutex_unlock(&g->fold_mtx);
    return landed;
}

unsigned
harness_gpu_mirror_pieces(uint32_t vram_mask, uint32_t lo, uint32_t hi,
                          uint32_t plo[2], uint32_t phi[2])
{
    gpu_rng_t p[2];
    unsigned  n = gpu_mirror_pieces(vram_mask, lo, hi, p);

    for (unsigned i = 0; i < n; i++) {
        plo[i] = p[i].lo;
        phi[i] = p[i].hi;
    }
    return n;
}

unsigned
harness_gpu_mirror_plan(uint32_t vram_mask, const uint32_t *ilo,
                        const uint32_t *ihi, unsigned nin, uint32_t plo[6],
                        uint32_t phi[6])
{
    gpu_rng_t in[GPU_MIRROR_NIN], p[GPU_MIRROR_NIN * 2];
    unsigned  n;

    if (nin > GPU_MIRROR_NIN)
        nin = GPU_MIRROR_NIN;
    for (unsigned i = 0; i < nin; i++) {
        in[i].lo = ilo[i];
        in[i].hi = ihi[i];
    }
    n = gpu_mirror_plan(vram_mask, in, nin, p);
    for (unsigned i = 0; i < n; i++) {
        plo[i] = p[i].lo;
        phi[i] = p[i].hi;
    }
    return n;
}

void
harness_gpu_rng_fold(uint32_t vram_mask, int staged, uint32_t *lo, uint32_t *hi)
{
    rng_fold(vram_mask, staged, lo, hi);
}

#else /* !R128_GPU_HAVE_VULKAN */

unsigned
harness_gpu_mirror_plan(uint32_t vram_mask, const uint32_t *ilo,
                        const uint32_t *ihi, unsigned nin, uint32_t plo[6],
                        uint32_t phi[6])
{
    (void) vram_mask; (void) ilo; (void) ihi; (void) nin;
    (void) plo; (void) phi;
    return 0;
}

unsigned
harness_gpu_mirror_pieces(uint32_t vram_mask, uint32_t lo, uint32_t hi,
                          uint32_t plo[2], uint32_t phi[2])
{
    (void) vram_mask; (void) lo; (void) hi;
    (void) plo; (void) phi;
    return 0;
}

void
harness_gpu_rng_fold(uint32_t vram_mask, int staged, uint32_t *lo, uint32_t *hi)
{
    (void) vram_mask; (void) staged; (void) lo; (void) hi;
}

int
harness_gpu_verify_stats(rage128_t *dev, uint64_t *segments, uint64_t *mismatched)
{
    (void) dev;
    (void) segments;
    (void) mismatched;
    return 0;
}

int
harness_gpu_ov0_stats(rage128_t *dev, uint64_t *frames, uint64_t *mismatched)
{
    (void) dev;
    (void) frames;
    (void) mismatched;
    return 0;
}

int
harness_gpu_available(void)
{
    return 0;
}

int
harness_gpu_gate(rage128_t *dev, const rage128_raster_state_t *rs)
{
    (void) dev;
    (void) rs;
    return -1;
}

int
harness_gpu_capture_tri(rage128_t *dev, const rage128_raster_state_t *rs,
                        const r3d_vtx_t *a, const r3d_vtx_t *b,
                        const r3d_vtx_t *c, harness_gpu_span_t *out,
                        uint32_t max, harness_gpu_cap_t *res)
{
    (void) dev; (void) rs; (void) a; (void) b; (void) c; (void) out;
    (void) max;
    memset(res, 0, sizeof(*res));
    return 0;
}

int
harness_gpu_submit_probe(rage128_t *dev, const rage128_raster_state_t *rs,
                         const r3d_vtx_t *a, const r3d_vtx_t *b,
                         const r3d_vtx_t *c, harness_gpu_sub_t *res)
{
    (void) dev; (void) rs; (void) a; (void) b; (void) c;
    memset(res, 0, sizeof(*res));
    return 0;
}

int
harness_gpu_tuple_reload(uint32_t fmt0, uint32_t fmt1,
                         uint32_t s3tc0, uint32_t s3tc1,
                         uint32_t loaded[4], int *exact)
{
    (void) fmt0; (void) fmt1; (void) s3tc0; (void) s3tc1;
    (void) loaded;
    *exact = 0;
    return 0;
}

int
harness_gpu_tuple_pair(const uint32_t a[7], const uint32_t b[7],
                       uint32_t *pairs, int *distinct)
{
    (void) a; (void) b;
    *pairs    = 0;
    *distinct = 0;
    return 0;
}

int
harness_gpu_tuple_buddy(rage128_t *dev, const rage128_raster_state_t *rs,
                        int32_t *light, int32_t *buddy_light)
{
    (void) dev; (void) rs;
    *light = *buddy_light = -1;
    return 0;
}

int
harness_gpu_texq_probe(rage128_t *dev, const rage128_raster_state_t *rs,
                       const rage128_raster_state_t *rs2,
                       const r3d_vtx_t *a, const r3d_vtx_t *b,
                       const r3d_vtx_t *c, uint32_t lo, uint32_t hi,
                       harness_gpu_texq_t *res)
{
    (void) dev; (void) rs; (void) rs2; (void) a; (void) b; (void) c;
    (void) lo; (void) hi;
    memset(res, 0, sizeof(*res));
    return 0;
}

unsigned
harness_gpu_texq_cap(void)
{
    return 0;
}

void *
harness_gpu_texq_begin(rage128_t *dev)
{
    (void) dev;
    return NULL;
}

int
harness_gpu_texq_draw(void *h, rage128_t *dev, const rage128_raster_state_t *rs,
                      const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c)
{
    (void) h; (void) dev; (void) rs; (void) a; (void) b; (void) c;
    return 0;
}

void
harness_gpu_texq_end(void *h, rage128_t *dev, uint32_t lo, uint32_t hi,
                     harness_gpu_texq_t *res)
{
    (void) h; (void) dev; (void) lo; (void) hi;
    memset(res, 0, sizeof(*res));
}

int
harness_gpu_seed_probe(const uint32_t comb[14], uint32_t *rows,
                       int *in_table, uint32_t *unknown)
{
    (void) comb;
    *rows = 0;
    *in_table = 0;
    *unknown = 0;
    return 0;
}

int
harness_gpu_seed_land(rage128_t *dev, const rage128_raster_state_t *rs)
{
    (void) dev; (void) rs;
    return 0;
}

void *
harness_gpu_fab_alloc(void)
{
    return NULL;
}

void
harness_gpu_fab_free(void *g)
{
    (void) g;
}

void
harness_gpu_fab_stage_ref(void *g, int on)
{
    (void) g; (void) on;
}

uint64_t
harness_gpu_fab_qskip(void *g, int t, int b)
{
    (void) g; (void) t; (void) b;
    return 0;
}

void
harness_gpu_fab_2ds(void *g, uint64_t *full, uint64_t *bytes,
                    uint64_t *recycles)
{
    (void) g;
    if (full) *full = 0;
    if (bytes) *bytes = 0;
    if (recycles) *recycles = 0;
}

void
harness_gpu_fab_q2d(void *g, int table, uint64_t *rects, uint64_t *runs,
                    uint64_t *skip_wmask, uint64_t *cpu)
{
    (void) g; (void) table;
    if (rects) *rects = 0;
    if (runs) *runs = 0;
    if (skip_wmask) *skip_wmask = 0;
    if (cpu) *cpu = 0;
}

uint64_t
harness_gpu_q2d_bad(void *g, int table)
{
    (void) g; (void) table;
    return 0;
}

uint64_t
harness_gpu_fab_qnb(void *g, int t)
{
    (void) g; (void) t;
    return 0;
}

uint64_t
harness_gpu_fab_fill_skip(void *g, int b)
{
    (void) g; (void) b;
    return 0;
}

uint64_t
harness_gpu_fab_tiled(void *g)
{
    (void) g;
    return 0;
}

uint64_t
harness_gpu_fab_fill_rects(void *g)
{
    (void) g;
    return 0;
}

uint64_t
harness_gpu_fab_fill_cpu(void *g)
{
    (void) g;
    return 0;
}

void
harness_gpu_fab_q_reset(void *g)
{
    (void) g;
}

int
harness_gpu_fab_fill_exec(void *g, uint8_t *mem, uint32_t size,
                          uint32_t *flo, uint32_t *fhi)
{
    (void) g; (void) mem; (void) size; (void) flo; (void) fhi;
    return -1;
}

void
harness_gpu_fab_scan_arm(void *g, uint32_t lo, uint32_t hi)
{
    (void) g; (void) lo; (void) hi;
}

void
harness_gpu_fab_scan_add(void *g, uint32_t lo, uint32_t hi)
{
    (void) g; (void) lo; (void) hi;
}

uint64_t
harness_gpu_fab_scan_stale(void *g)
{
    (void) g;
    return 0;
}

void
harness_gpu_fab_scan_read(void *g, int *pending, uint64_t *kicks,
                          uint64_t *defers, uint64_t *served)
{
    (void) g;
    *pending = 0;
    *kicks = *defers = *served = 0;
}

int
harness_gpu_fab_scan_req(void *g, int set)
{
    (void) g; (void) set;
    return 0;
}

#endif /* R128_GPU_HAVE_VULKAN */
