/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- register harness, GPU-lane probe
 *          interface.
 *
 *          The GPU backend's state gate and span capture are static
 *          functions inside vid_ati_rage128_gpu.c, and the whole backend
 *          compiles away without the Vulkan headers (the Vulkan library
 *          itself is loaded at run time, never linked). gpu_probe.c
 *          includes that source file directly, the same single-source
 *          pattern reg_harness.c uses for the device shell, and exports
 *          the entry points declared here. No Vulkan object is ever
 *          created: the probes drive the host half of the lane (the gate,
 *          the accept bound, the hazard ranges and the span records)
 *          against a fabricated backend whose device buffers are plain
 *          host memory.
 *
 *          Include this header after the device source: the prototypes
 *          use its types.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#ifndef R128_HARNESS_GPU_PROBE_H
#define R128_HARNESS_GPU_PROBE_H

/* 0 = built without Vulkan headers, so the backend (and every probe here)
   compiled out. The caller reports the hole rather than passing silently. */
int harness_gpu_available(void);

/* R128_GPU=verify: the backend's own verdict -- segments replayed through
   the interpreter and the count that mismatched. 0 = no verifying backend
   on this device. */
int harness_gpu_verify_stats(rage128_t *dev, uint64_t *segments,
                             uint64_t *mismatched);

/* R128_GPU=verify, overlay lane: frames the OV0 kernel composed and the
   pixels the CPU compositor (the oracle) disagreed with. 0 = no verifying
   backend on this device. */
int harness_gpu_ov0_stats(rage128_t *dev, uint64_t *frames,
                          uint64_t *mismatched);

/* gpu_mirror_pieces(): the wrapped [plo, phi) pieces (0-2) a hazard
   interval maps to in the device-local VRAM mirror. */
unsigned harness_gpu_mirror_pieces(uint32_t vram_mask, uint32_t lo,
                                   uint32_t hi, uint32_t plo[2],
                                   uint32_t phi[2]);

/* gpu_mirror_plan(): the disjoint, sorted copy regions (0-6) for up to
   three intervals -- what one vkCmdCopyBuffer is handed. */
unsigned harness_gpu_mirror_plan(uint32_t vram_mask, const uint32_t *ilo,
                                 const uint32_t *ihi, unsigned nin,
                                 uint32_t plo[6], uint32_t phi[6]);

/* rng_fold(): the masked-space fold applied to every color/z store
   interval before it reaches the hazard tracker and the copy plan;
   staged = the state's resolved AGP-staging answer for that surface. */
void harness_gpu_rng_fold(uint32_t vram_mask, int staged, uint32_t *lo,
                          uint32_t *hi);

/* gpu_state_kernel() on a state: kernel id, or -1 for the CPU fallback.
   The fabricated backend reports both arena imports live, so the answer is
   the state's own shape and not a residency artifact. */
int harness_gpu_gate(rage128_t *dev, const rage128_raster_state_t *rs);

/* One captured span record, as the kernel reads it: the column range and
   row bases, plus the seeds the kernel starts the span from (the three
   integer edge values and the depth seed's double bits at x0). */
typedef struct harness_gpu_span_t {
    int32_t  x0, x1, py;
    uint32_t drow, zrow;
    int64_t  e0, e1, e2;
    uint64_t zline;
} harness_gpu_span_t;

/* Result of capturing one triangle through rage128_gpu_capture_span. */
typedef struct harness_gpu_cap_t {
    uint32_t nspans;     /* records written (may exceed the caller's array) */
    uint32_t got;        /* records copied out                             */
    uint64_t need_spans; /* gpu_tri_need's accept-bound reservation         */
    uint64_t need_px;
    uint32_t clo, chi;   /* prim_ranges color hazard interval             */
    uint32_t zlo, zhi;   /* ... and z                                      */
    uint64_t dzdx;       /* the tri record's per-pixel depth step (bits)   */
    int64_t  edx[3];     /* ... and its per-pixel edge steps               */
} harness_gpu_cap_t;

/* Run one triangle through the real capture path with a fabricated
   backend, and report the span records plus the two host-side bounds that
   must cover them. Returns 0 if the probe is unavailable. */
int harness_gpu_capture_tri(rage128_t *dev, const rage128_raster_state_t *rs,
                            const r3d_vtx_t *a, const r3d_vtx_t *b,
                            const r3d_vtx_t *c, harness_gpu_span_t *out,
                            uint32_t max, harness_gpu_cap_t *res);

/* Result of one draw through the production rage128_gpu_submit_tri --
   the full accept path: state gate, two-tier pricing (bbox bound, then
   the exact count-only re-price on a cap trip), capture, and the
   reprice-consumption tripwire. */
typedef struct harness_gpu_sub_t {
    int      accepted;    /* submit_tri return value                    */
    uint64_t bound_spans; /* cheap bbox bound, for the vector's context */
    uint64_t bound_px;
    uint64_t reprice;     /* exact re-price walks run                   */
    uint64_t toobig;      /* draws rejected after the exact count       */
    uint64_t miss;        /* reprice-consumption tripwire, must be 0    */
    uint32_t nspans;      /* span records the capture consumed          */
    uint32_t seg_px;      /* pixels the capture consumed                */
} harness_gpu_sub_t;

/* Run one triangle through the production submit entry against a
   fabricated backend. Returns 0 if the probe is unavailable. */
int harness_gpu_submit_probe(rage128_t *dev, const rage128_raster_state_t *rs,
                             const r3d_vtx_t *a, const r3d_vtx_t *b,
                             const r3d_vtx_t *c, harness_gpu_sub_t *res);

/* Exact texture-read hazard list (the exact texture range list of
   r128_gpu_t) behind the q hull: draws captured into one pending
   segment on a fabricated backend, then the 2D executor's texture test
   asked about a store over [lo,hi). hull = the q-interval verdict,
   exact = the list's verdict, ovf_exact = the list's verdict with the
   overflow flag forced (must equal hull). slot_* repeat the test after
   the pending ranges, list and overflow flag are published into ring
   slot 0 the way a submit publishes them. ovf / slot_ovf = the overflow
   flag the draws themselves left on the segment and on the slot.
   spared/real = the backend's cleared/confirmed counters after the
   pending test and the slot test, folded as one 2D operation the way
   rage128_gpu_2d_barrier folds its tests; over_cap = segments whose
   list fell back to the hull. */
typedef struct harness_gpu_texq_t {
    int      accepted;   /* every draw accepted                        */
    uint32_t ntexq;      /* distinct texture ranges the segment lists  */
    uint32_t q_lo, q_hi; /* the hull                                    */
    int      ovf, slot_ovf;
    int      hull, exact, ovf_exact, slot_hull, slot_exact;
    uint64_t spared, real, over_cap;
} harness_gpu_texq_t;
/* Two draws (rs2 may be null), then the test. Returns 0 if the probe is
   unavailable. */
int harness_gpu_texq_probe(rage128_t *dev, const rage128_raster_state_t *rs,
                           const rage128_raster_state_t *rs2,
                           const r3d_vtx_t *a, const r3d_vtx_t *b,
                           const r3d_vtx_t *c, uint32_t lo, uint32_t hi,
                           harness_gpu_texq_t *res);
/* The same probe in pieces, for a vector that needs its own draw
   sequence (a texture repeated after another, or more textures than the
   list holds): begin installs the fabricated backend (null = probe
   unavailable), draw submits one triangle through the production entry
   and returns its acceptance, end runs the test, fills res and frees the
   backend. */
unsigned harness_gpu_texq_cap(void); /* entries the list holds (0 = unavailable) */
void *harness_gpu_texq_begin(rage128_t *dev);
int   harness_gpu_texq_draw(void *h, rage128_t *dev,
                            const rage128_raster_state_t *rs,
                            const r3d_vtx_t *a, const r3d_vtx_t *b,
                            const r3d_vtx_t *c);
void  harness_gpu_texq_end(void *h, rage128_t *dev, uint32_t lo, uint32_t hi,
                           harness_gpu_texq_t *res);

/* Reload one normalized cache pair and report its four format words and
   exact tuple equality. Returns 0 if the loader probe cannot run. */
int harness_gpu_tuple_reload(uint32_t fmt0, uint32_t fmt1,
                              uint32_t s3tc0, uint32_t s3tc1,
                              uint32_t loaded[4], int *exact);

/* Save two one-stage combines (the seven stage-0 selectors each, the
   formats pinned as a live ARGB8888 draw) as learned pairs and reload
   them through the production loader: pairs = the pairs it recorded,
   distinct = whether the two tuples interned apart. Returns 0 if the
   loader probe cannot run. */
int harness_gpu_tuple_pair(const uint32_t a[7], const uint32_t b[7],
                           uint32_t *pairs, int *distinct);

/* Combine-tuple link on a fabricated backend: intern the state's unlit
   format-ANY seed, then the state's own tuple, and report the light
   word (sel[20]) of the tuple and of its format-ANY buddy (-1 = no
   buddy). The buddy is what the dispatch binds while the pinned fold
   compiles, so its light word must equal the tuple's. Returns 0 if
   the probe is unavailable or the state has no textured kernel. */
int harness_gpu_tuple_buddy(rage128_t *dev, const rage128_raster_state_t *rs,
                            int32_t *light, int32_t *buddy_light);

/* Seed-table enumeration through the production interner: rows = the
   seed table's row count; in_table = whether the 14 combine selectors
   are a row of it; unknown = how many "combine tuple outside
   enumeration" events interning the combine as a live, format-pinned
   tuple raised on a fabricated backend primed by gpu_tuples_prime (1 =
   the seed table has no row for it). Returns 0 if the probe is
   unavailable. */
int harness_gpu_seed_probe(const uint32_t comb[14], uint32_t *rows,
                           int *in_table, uint32_t *unknown);

/* R128_GPU=verify, real backend: compile the folded pipeline of the
   state's unlit format-ANY seed for its kernel, synchronously, so the
   next draw of a still-compiling pinned tuple finds a landed buddy.
   Returns 1 once the pipeline is published, 0 without a verifying
   backend, a textured kernel, or a successful compile. */
int harness_gpu_seed_land(rage128_t *dev, const rage128_raster_state_t *rs);

/* Fabricated backend handle for the arena-lifecycle vectors: installed as
   dev->gpu around rage128_raster_state_capture / rage128_raster_flush so
   the GPU-lane arena arms run. No Vulkan object exists, so the caller must
   keep every capture inside the already-allocated arena -- the grow path
   imports. Returns null when built without Vulkan headers. */
void *harness_gpu_fab_alloc(void);
void  harness_gpu_fab_free(void *g);
/* Pin/unpin ring slot 0 as referencing the texture staging arena. */
void  harness_gpu_fab_stage_ref(void *g, int on);

/* 2D telemetry counters of the fabricated backend:
   qskip[t][b], qnb[t], fill_skip[b]. Zero when built without Vulkan. */
uint64_t harness_gpu_fab_qskip(void *g, int t, int b);
uint64_t harness_gpu_fab_qnb(void *g, int t);
uint64_t harness_gpu_fab_fill_skip(void *g, int b);
uint64_t harness_gpu_fab_tiled(void *g);
uint64_t harness_gpu_fab_fill_rects(void *g);
uint64_t harness_gpu_fab_fill_cpu(void *g);
/* Tiled-fill probe. q_reset empties the fabricated backend's
   queued-2D accumulation (fill list, write hull, pending flag) so a leg
   starts from nothing queued. fill_exec executes the queued solid-fill
   records on host memory the way gpu_disp_fills hands them to
   vkCmdFillBuffer (the dword-replicated color at every byte, phase by
   address; dword offset and size), reports the queued-write hull
   [f_lo, f_hi) as the records left it, empties the accumulation again,
   and returns the record count; -1 = a record is not a solid fill, is
   off a dword, or runs past size. */
void harness_gpu_fab_q_reset(void *g);
int  harness_gpu_fab_fill_exec(void *g, uint8_t *mem, uint32_t size,
                               uint32_t *flo, uint32_t *fhi);
/* Scanout-request probe: make the fabricated backend an async lane with
   one pending 2D write range [lo, hi), then read back the pending flag
   and the scan counters (direct submits, lines that left the submit to
   the executor, requests the executor served). No-ops / zeros when
   built without Vulkan. */
void harness_gpu_fab_scan_arm(void *g, uint32_t lo, uint32_t hi);
/* A further pending 2D store [lo, hi) beside the armed one, recorded
   the way a queued fill or blit records its destination: the hull
   widens over it and the exact list gains it. */
void harness_gpu_fab_scan_add(void *g, uint32_t lo, uint32_t hi);
/* Scanned lines that went out ahead of an unfinished GPU store. */
uint64_t harness_gpu_fab_scan_stale(void *g);
void harness_gpu_fab_scan_read(void *g, int *pending, uint64_t *kicks,
                               uint64_t *defers, uint64_t *served);
/* The request bit itself: set < 0 reads it, otherwise stores set (no
   executor wake). Returns the previous value. Lets a vector post a
   request the executor cannot see coming and call the serve from the
   wrong thread. */
int  harness_gpu_fab_scan_req(void *g, int set);
/* Per-table queue counters: rects = whole ops queued,
   runs = fill/copy runs, skip_wmask = qskip[t][1], cpu = gpu-side bounce. */
void harness_gpu_fab_q2d(void *g, int table, uint64_t *rects, uint64_t *runs,
                         uint64_t *skip_wmask, uint64_t *cpu);
/* Differential-oracle mismatched bytes on table t (R128_GPU_2D=verify);
   reads a real backend as well as a fabricated one. */
uint64_t harness_gpu_q2d_bad(void *g, int table);
/* 2D staging-ring counters: full = reservations refused. */
void harness_gpu_fab_2ds(void *g, uint64_t *full, uint64_t *bytes,
                         uint64_t *recycles);

#endif /* R128_HARNESS_GPU_PROBE_H */
