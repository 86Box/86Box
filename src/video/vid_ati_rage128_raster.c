/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- parallel rasterizer.
 *
 *          Profiling shows the per-pixel rasterizer dominates heavy 3D
 *          scenes, so this file can spread it over worker threads. Each
 *          primitive the 3D engine assembles goes into a batch together
 *          with a copy of the engine state it was submitted under. When
 *          the batch is flushed, every worker walks the whole batch but
 *          draws only the rows r128_row_owned gives to it, so no two
 *          threads write the same color or Z bytes and no per-pixel lock
 *          is needed. Each worker walks the batch in submission order, so
 *          every pixel sees the same sequence of writes as on the serial
 *          path and the output is bit-exact with it.
 *
 *          The thread count is the "Render threads" option (1, 2, 3, 4, 6
 *          or 8). With 1 there are no workers: each primitive is drawn on
 *          the submitting thread as it arrives. The GPU backend, the bus
 *          capture tap and JIT verify mode each force the count to 1.
 *
 *          A flush runs the batch on the workers and waits for all of
 *          them. It happens before anything that must see the batch's
 *          pixels. This file flushes on a render-target change and before
 *          a draw that samples bytes the batch has written; other files
 *          call rage128_raster_flush for page flips and display waits, 2D
 *          operations, an empty command FIFO, a full texture staging
 *          arena, a Z or color restage, and an engine drain.
 *
 *          Every assembled primitive enters through the submit functions
 *          at the end of this file. They count it in COMPOSITE_SHADOW_ID
 *          and apply the supersample scale, then hand it to the GPU
 *          backend, draw it inline or add it to the batch. The pixel work
 *          itself is rage128_3d_tri, rage128_3d_line and rage128_3d_point
 *          in vid_ati_rage128_3d.c.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 Register Reference Supplement:
 *              Registers for CCE 3D Packets", 1999. Cited by its title
 *              and register; it has no page numbers.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifdef __APPLE__
#    include <pthread/qos.h>
#endif
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_ati_rage128.h>
#include <86box/vid_ati_rage128_regs.h>

#define RB_TRI         0
#define RB_LINE        1
#define RB_POINT       2
#define RB_MAX_WORKERS 8

/* One deferred primitive: the index of the state it was submitted under
   (in the batch's state table) and its vertices (a line uses v[0] and
   v[1], a point v[0]). py0..py1 is a superset of the pixel rows the
   primitive can touch, so a worker that owns none of them skips it
   before any setup. */
typedef struct rb_cmd_t {
    uint32_t  state_idx;
    uint32_t  kind;
    int32_t   py0, py1;
    r3d_vtx_t v[3];
} rb_cmd_t;

typedef struct rb_worker_t {
    rage128_t *dev;
    int        id;
    int        mask;
    thread_t  *thread;
    event_t   *wake; /* a batch is ready, or the pool is closing */
    event_t   *done; /* this worker finished the current batch */
} rb_worker_t;

typedef struct rage128_raster_t {
    int nthreads; /* 1 = no workers, draw inline              */
    int mask;     /* row map for r128_row_owned: n-1 or -n    */
    int run;      /* cleared at close to stop the workers     */

    /* Deferred batch, appended to on the CCE executor thread. Workers
       read it only inside rage128_raster_flush, and the flush's callers
       guarantee no primitive is submitted meanwhile, so it does not
       change while they run. */
    rb_cmd_t *cmds;
    uint32_t  cmd_count;
    uint32_t  cmd_cap;

    /* Engine state per command. Consecutive primitives nearly always
       share state, so a command stores an index here instead of its own
       copy, which is large: it embeds the 256-entry texture palette and
       the fog table. state_jfn[i] is the span-JIT block for states[i].d,
       looked up when the state is stored, on the submitting thread, the
       only thread allowed to use the block cache. Workers only call it. */
    rage128_raster_state_t *states;
    r128_jit_span_fn       *state_jfn;
    uint32_t                state_count;
    uint32_t                state_cap;

    /* Render target of the current batch. A draw to another target could
       sample what this batch wrote (render to texture), so a change
       flushes first. The key holds every field that maps a pixel row to
       bytes: the row stride is pitch times bytes per pixel, so the
       destination format belongs in it, and tiling changes how rows are
       addressed. Leave one out and two row layouts could share a batch,
       and row ownership could hand the same bytes to two workers. The Z
       half is recorded and compared only for draws that use Z or
       stencil. */
    int      rt_valid;
    uint32_t rt_dst_offset;
    uint32_t rt_dst_pitch;
    uint32_t rt_dst_bpp;
    uint32_t rt_dst_tiled;
    int      rt_z_valid;
    uint32_t rt_z_offset;
    uint32_t rt_z_stride;
    uint32_t rt_z_tiled;

    /* VRAM bytes the batch has written so far, color and Z kept apart
       (one interval over both would also cover everything between two
       distant surfaces). A draw whose texture overlaps either one reads
       pixels an earlier draw in the batch produced. If it were deferred,
       a worker could read rows another worker has not drawn yet, so the
       batch is flushed first. */
    int      wr_valid;
    uint32_t wr_c_lo, wr_c_hi;
    uint32_t wr_z_lo, wr_z_hi;

    rb_worker_t workers[RB_MAX_WORKERS];
} rage128_raster_t;

static void rb_reset_written(rage128_raster_t *rb);

static void
rb_worker_thread(void *priv)
{
    rb_worker_t      *w   = (rb_worker_t *) priv;
    rage128_t        *dev = w->dev;
    rage128_raster_t *rb  = (rage128_raster_t *) dev->raster;

#ifdef __APPLE__
    /* The same quality-of-service class as the CCE executor, one below
       the emulation thread, so the scheduler favors the emulation thread
       over the workers. */
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif
    for (;;) {
        thread_wait_event(w->wake, -1);
        thread_reset_event(w->wake);
        if (!rb->run)
            break;

        for (uint32_t i = 0; i < rb->cmd_count; i++) {
            const rb_cmd_t               *c  = &rb->cmds[i];
            const rage128_raster_state_t *rs = &rb->states[c->state_idx];

            /* Skip a primitive whose row range holds no row this worker
               owns, before any setup. A range of nthreads rows or more
               is not tested, since it nearly always holds an owned row;
               running a primitive that has none costs time, not
               correctness, because the walk itself skips rows it does
               not own. A range wider than the real rows only makes the
               skip rarer. */
            if (c->py1 - c->py0 + 1 < rb->nthreads) {
                int own = 0;

                for (int32_t y = c->py0; y <= c->py1; y++)
                    if (r128_row_owned(y, w->id, w->mask)) {
                        own = 1;
                        break;
                    }
                if (!own)
                    continue;
            }

            r128_jit_span_fn jfn = rb->state_jfn[c->state_idx];

            if (c->kind == RB_TRI)
                rage128_3d_tri(dev, rs, w->id, w->mask, &c->v[0], &c->v[1], &c->v[2], jfn);
            else if (c->kind == RB_LINE)
                rage128_3d_line(dev, rs, w->id, w->mask, &c->v[0], &c->v[1], jfn);
            else
                rage128_3d_point(dev, rs, w->id, w->mask, &c->v[0], jfn);
        }
        thread_set_event(w->done);
    }
}

void
rage128_raster_init(rage128_t *dev)
{
    rage128_raster_t *rb;
    int               n       = dev->render_threads;
    const char       *jitmode = getenv("R128_JIT");

    /* JIT verify mode draws every row twice (with the JIT, rolled back,
       then with the interpreter) and compares the bytes. The workers
       share engine and staging state, so with more than one thread the
       compare can report mismatches that never occur in normal output.
       Verify mode therefore runs serially; every other mode keeps the
       configured thread count. */
    if (jitmode && !strcmp(jitmode, "verify"))
        n = 1;

    if (n < 1)
        n = 1;
    if (n > RB_MAX_WORKERS)
        n = RB_MAX_WORKERS;

    rb           = (rage128_raster_t *) calloc(1, sizeof(rage128_raster_t));
    rb->nthreads = n;
    /* Row map for r128_row_owned: n - 1 for a power-of-two count (row y
       goes to worker y & (n - 1)), -n otherwise (worker y mod n). With
       n = 1 this is 0, which owns every row. */
    rb->mask    = (n & (n - 1)) ? -n : n - 1;
    rb->run     = 1;
    dev->raster = rb;

    if (n <= 1)
        return; /* serial: draw inline on the submitting thread */

    for (int i = 0; i < n; i++) {
        rb_worker_t *w = &rb->workers[i];

        w->dev    = dev;
        w->id     = i;
        w->mask   = rb->mask;
        w->wake   = thread_create_event();
        w->done   = thread_create_event();
        w->thread = thread_create_named(rb_worker_thread, w, "rage128_rast");
    }
}

void
rage128_raster_close(rage128_t *dev)
{
    rage128_raster_t *rb = (rage128_raster_t *) dev->raster;

    if (!rb)
        return;

    if (rb->nthreads > 1) {
        rb->run = 0;
        for (int i = 0; i < rb->nthreads; i++)
            thread_set_event(rb->workers[i].wake);
        for (int i = 0; i < rb->nthreads; i++) {
            thread_wait(rb->workers[i].thread);
            thread_destroy_event(rb->workers[i].wake);
            thread_destroy_event(rb->workers[i].done);
        }
    }

    free(rb->cmds);
    free(rb->states);
    free(rb->state_jfn);
    free(rb);
    dev->raster = NULL;
}

/* Wake every worker on the current batch and wait until all have
   finished it. */
static void
rb_run_parallel(rage128_raster_t *rb)
{
    for (int i = 0; i < rb->nthreads; i++)
        thread_set_event(rb->workers[i].wake);
    for (int i = 0; i < rb->nthreads; i++)
        thread_wait_event(rb->workers[i].done, -1);
    for (int i = 0; i < rb->nthreads; i++)
        thread_reset_event(rb->workers[i].done);
}

/* Draw the current batch and empty it. The caller guarantees that no
   primitive is submitted meanwhile: either it is the CCE executor
   itself, or the drain that calls this has stopped the executor.

   Two flushes can still overlap, and no caller can rule that out, so
   this takes raster_flush_mtx. The drain's idle test reads
   cce_executing, which stays 0 from the wake signal until the woken
   executor stores 1, so a drain can flush while the executor is on its
   way to its own idle flush. Both would write the same staged surface
   back to guest memory. With the lock, the second flush finds an empty
   batch and an inactive stage and does nothing. */
void
rage128_raster_flush(rage128_t *dev)
{
    rage128_raster_t *rb = (rage128_raster_t *) dev->raster;

    thread_wait_mutex(dev->raster_flush_mtx);

    if (rb && rb->nthreads > 1 && rb->cmd_count != 0) {
        atomic_store(&dev->pace_3d_seen, 1); /* tells the speed governor 3D was drawn */
        rb_run_parallel(rb);

        rb->cmd_count   = 0;
        rb->state_count = 0;
        rb->rt_valid    = 0;
        rb->rt_z_valid  = 0;
        rb_reset_written(rb);
        atomic_store(&dev->cce_batch_pending, 0); /* workers have finished */
    }

    /* Recycle the texture staging arena and start a new batch generation.
       No worker is running here (they finished above, or there was no
       batch). With workers this is the only place the arena is reset: a
       deferred command reads its staged texels through offsets into the
       arena, so the arena must not be reset or moved while such a
       command is queued. */
    if (dev->tex_stage.used) {
        /* GPU backend: reclaim the arena only when no queued GPU work
           samples it (rage128_gpu_stage_idle) and it holds no levels
           cached across draws (src_lo == src_hi); reclaiming cached
           levels would make later draws stage them again. Otherwise the
           arena keeps allocating upward, and r3d_stage_reserve waits for
           the GPU and drops the cache when it actually runs out of
           room. */
        if (!dev->gpu || rage128_gpu_stage_idle(dev)) {
            r128_texcache_lock(&dev->tex_stage);
            if (!dev->gpu
                || dev->tex_stage.src_lo == dev->tex_stage.src_hi) {
                dev->tex_stage.n_rec_flush++;
                dev->tex_stage.used      = 0;
                dev->tex_stage.ent_count = 0;
                dev->tex_stage.gen++;
                dev->tex_stage.tgen++; /* cached levels lived in the arena */
                dev->tex_stage.src_lo = 0;
                dev->tex_stage.src_hi = 0;
            }
            r128_texcache_unlock(&dev->tex_stage);
        }
    }

    /* Write a staged Z or color buffer that lives in AGP memory back to
       guest memory. This runs with or without workers (the workers
       finished above; a serial draw finished its writes before it
       returned), so every caller of the flush, such as a page flip, a 2D
       operation or an engine drain, sees current guest surfaces. It must
       come after the workers finish and before the next batch stages
       again: the write-back marks the stage inactive, and only an
       inactive stage is read from guest memory again. */
    rage128_z_stage_writeback(dev);
    rage128_c_stage_writeback(dev);

    thread_release_mutex(dev->raster_flush_mtx);
}

/* Drop the deferred batch without drawing it: a GUI soft reset
   (rage128_pm4_gui_reset) discards work the engine accepted but has not
   drawn. The caller must have stopped the executor. Workers run only
   inside rage128_raster_flush, so none is running here. */
void
rage128_raster_abandon(rage128_t *dev)
{
    rage128_raster_t *rb = (rage128_raster_t *) dev->raster;

    if (!rb)
        return;
    rb->cmd_count   = 0;
    rb->state_count = 0;
    rb->rt_valid    = 0;
    rb->rt_z_valid  = 0;
    rb_reset_written(rb);
    atomic_store(&dev->cce_batch_pending, 0);
}

/* Worker count (1 = no workers). rage128_raster_state_capture uses it to
   decide who recycles the texture staging arena. With workers, only
   rage128_raster_flush may, because a deferred command can still read
   the arena. Without them every draw is finished before the next one is
   captured, so the capture recycles the arena for each draw. */
int
rage128_raster_nthreads(rage128_t *dev)
{
    rage128_raster_t *rb = (rage128_raster_t *) dev->raster;

    return rb ? rb->nthreads : 1;
}

/* Byte interval [lo, hi) of rows y0..y1 (inclusive) of a surface, from
   r128_rows_bytes64, clamped to 32 bits. A 32-bit base plus a large row
   count can pass 4 GB, and an interval that wrapped would look empty and
   hide an overlap. lo and hi are left untouched when the interval is
   empty. */
static void
rb_rows_to_bytes(uint32_t base, uint32_t stride, int tiled, int32_t y0, int32_t y1,
                 uint32_t *lo, uint32_t *hi)
{
    uint64_t a, b;

    if (!stride || y1 < y0)
        return;
    if (y0 < 0)
        y0 = 0;
    r128_rows_bytes64(base, stride, tiled, (uint32_t) y0, (uint32_t) y1, &a, &b);
    if (a > 0xffffffffull)
        a = 0xffffffffull;
    if (b > 0xffffffffull)
        b = 0xffffffffull;
    if (b <= a)
        return;
    *lo = (uint32_t) a;
    *hi = (uint32_t) b;
}

/* Color and Z byte intervals a primitive can touch, computed from the
   same rows the worker ownership test uses (window offset and the slack
   of rb_prim_rows included) and clamped to the scissor's rows (d.sy0 to
   d.sy1). The clamp matters: the row walk clips to the scissor, so slack
   rows outside it never reach memory. Without the clamp, a full-screen
   draw's slack would reach into whatever the driver placed right after
   the render target, such as a Z buffer, and every such draw would look
   as if it sampled its own output. In X the interval covers whole rows,
   which is safe because a hazard test may only over-report. */
static void
rb_prim_ranges(const rage128_raster_state_t *rs, int32_t y0, int32_t y1,
               uint32_t *clo, uint32_t *chi, uint32_t *zlo, uint32_t *zhi)
{
    int32_t sy0 = rs->d.sy0;
    int32_t sy1 = rs->d.sy1;

    *clo = *zlo = 0xffffffffu;
    *chi = *zhi = 0;
    if (y0 < sy0)
        y0 = sy0;
    if (y1 > sy1)
        y1 = sy1;
    if (y1 < y0)
        return;
    rb_rows_to_bytes(rs->dst_offset, rs->dst_pitch * rs->dst_bpp,
                     rs->d.c_tiled, y0, y1, clo, chi);
    /* Z rows count when the Z test or stencil is on. Stencil shares the
       Z cell, so a stencil-only draw touches it too, and a draw that
       tests Z without writing it still reads it. */
    if (rs->d.z_en || rs->d.sten_on)
        rb_rows_to_bytes(rs->t3d.z_offset,
                         rs->d.zrowpx * (uint32_t) rs->d.zbpp,
                         rs->d.z_tiled, y0, y1,
                         zlo, zhi);
}

/* True if either enabled texture stage samples bytes in [lo,hi). */
static int
rb_tex_hits(const rage128_raster_state_t *rs, uint32_t lo, uint32_t hi)
{
    for (int st = 0; st < 2; st++)
        if (r3d_tex_rng_hit(rs, st, lo, hi))
            return 1;
    return 0;
}

/* True if this draw samples a texture that overlaps bytes the batch has
   already written. That is a render-to-texture read, which needs the
   finished pixels; deferred, a worker could sample rows another worker
   has not drawn yet. Color and Z are tested as separate intervals,
   because one interval over both would cover everything between two
   distant surfaces. */
static int
rb_tex_hits_written(const rage128_raster_t *rb, const rage128_raster_state_t *rs)
{
    if (!rb->wr_valid)
        return 0;
    return rb_tex_hits(rs, rb->wr_c_lo, rb->wr_c_hi)
        || rb_tex_hits(rs, rb->wr_z_lo, rb->wr_z_hi);
}

static void
rb_extend_written(rage128_raster_t *rb, uint32_t clo, uint32_t chi,
                  uint32_t zlo, uint32_t zhi)
{
    if (chi > clo) {
        if (clo < rb->wr_c_lo)
            rb->wr_c_lo = clo;
        if (chi > rb->wr_c_hi)
            rb->wr_c_hi = chi;
        rb->wr_valid = 1;
    }
    if (zhi > zlo) {
        if (zlo < rb->wr_z_lo)
            rb->wr_z_lo = zlo;
        if (zhi > rb->wr_z_hi)
            rb->wr_z_hi = zhi;
        rb->wr_valid = 1;
    }
}

static void
rb_reset_written(rage128_raster_t *rb)
{
    rb->wr_valid = 0;
    rb->wr_c_lo = rb->wr_z_lo = 0xffffffffu;
    rb->wr_c_hi = rb->wr_z_hi = 0;
}

/* Store the submit-time state, reusing the most recent entry when it is
   identical (consecutive primitives of one draw share it), and return
   its index in the batch. A new entry gets its span-JIT block here, on
   the submitting thread, the only thread that may call
   rage128_jit_get_block. */
static uint32_t
rb_intern_state(rage128_t *dev, rage128_raster_t *rb, const rage128_raster_state_t *rs)
{
    if (rb->state_count > 0
        && memcmp(&rb->states[rb->state_count - 1], rs, sizeof(*rs)) == 0)
        return rb->state_count - 1;

    if (rb->state_count == rb->state_cap) {
        rb->state_cap = rb->state_cap ? rb->state_cap * 2u : 256u;
        rb->states    = (rage128_raster_state_t *) realloc(rb->states,
                                                           rb->state_cap * sizeof(*rb->states));
        rb->state_jfn = (r128_jit_span_fn *) realloc(rb->state_jfn,
                                                     rb->state_cap * sizeof(*rb->state_jfn));
    }
    rb->states[rb->state_count]    = *rs;
    rb->state_jfn[rb->state_count] = rage128_jit_get_block(dev, &rs->d);
    return rb->state_count++;
}

/* Flush first if this primitive's render target differs from the
   batch's, so no batch spans a target change, then record this
   primitive's target as the batch's. */
static void
rb_guard_rt(rage128_t *dev, rage128_raster_t *rb, const rage128_raster_state_t *rs)
{
    int      zt = rs->d.z_en || rs->d.sten_on;
    uint32_t zs = rs->d.zrowpx * (uint32_t) rs->d.zbpp;

    if (rb->rt_valid
        && ((rs->dst_offset != rb->rt_dst_offset
             || rs->dst_pitch != rb->rt_dst_pitch
             || rs->dst_bpp != rb->rt_dst_bpp
             || (uint32_t) rs->d.c_tiled != rb->rt_dst_tiled)
            || (zt && rb->rt_z_valid
                && (rs->t3d.z_offset != rb->rt_z_offset || zs != rb->rt_z_stride
                    || (uint32_t) rs->d.z_tiled != rb->rt_z_tiled))))
        rage128_raster_flush(dev);

    rb->rt_valid      = 1;
    rb->rt_dst_offset = rs->dst_offset;
    rb->rt_dst_pitch  = rs->dst_pitch;
    rb->rt_dst_bpp    = rs->dst_bpp;
    rb->rt_dst_tiled  = (uint32_t) rs->d.c_tiled;
    if (zt) {
        rb->rt_z_valid  = 1;
        rb->rt_z_offset = rs->t3d.z_offset;
        rb->rt_z_stride = zs;
        rb->rt_z_tiled  = (uint32_t) rs->d.z_tiled;
    }
}

static rb_cmd_t *
rb_alloc_cmd(rage128_raster_t *rb)
{
    if (rb->cmd_count == rb->cmd_cap) {
        rb->cmd_cap = rb->cmd_cap ? rb->cmd_cap * 2u : 4096u;
        rb->cmds    = (rb_cmd_t *) realloc(rb->cmds, rb->cmd_cap * sizeof(*rb->cmds));
    }
    return &rb->cmds[rb->cmd_count++];
}

/* Conservative pixel-row range of a primitive: a superset of the rows the
   snapped walk in rage128_3d_tri can visit. It is the vertex Y range plus
   the window Y offset, widened by 2 rows for the subpixel snap and float
   rounding. A line stays within its endpoints' rows, and a point (drawn
   as a quad reaching half a pixel past its center) within one row of
   it, so the same slack covers both. Any error must be toward too many
   rows: the ownership skip and the hazard tests both use this range, and
   a range that is too small drops owned rows from the first and misses
   overlaps in the second. */
static void
rb_prim_rows(const rage128_raster_state_t *rs, const r3d_vtx_t *v, int n,
             int32_t *y0, int32_t *y1)
{
    float ymin = v[0].y, ymax = v[0].y;

    for (int k = 1; k < n; k++) {
        if (v[k].y < ymin)
            ymin = v[k].y;
        if (v[k].y > ymax)
            ymax = v[k].y;
    }
    if (!(ymin <= ymax) || ymin < -65536.0f || ymax > 65536.0f) {
        /* NaN or an extent far off the surface: claim every row, so
           every worker owns part of it. */
        *y0 = INT32_MIN / 2;
        *y1 = INT32_MAX / 2;
        return;
    }
    {
        /* WINDOW_OFFSET_Y, WINDOW_XY_OFFSET [15:0], is signed fixed point
           with 2 or 4 fraction bits, as SUB_PIX_AMNT (SETUP_CNTL bit 19)
           selects (RRG: WINDOW_XY_OFFSET, p. 3-249 / PDF 267; Registers
           for CCE 3D Packets, SETUP_CNTL). */
        int   sub = (rs->t3d.setup_cntl & (1 << 19)) ? 16 : 4;
        float wof = 0.0f;

        if (rs->t3d.window_xy_offset)
            wof = (float) (int16_t) (rs->t3d.window_xy_offset & 0xffff) / (float) sub;
        *y0 = (int32_t) floorf(ymin + wof) - 2;
        *y1 = (int32_t) ceilf(ymax + wof) + 2;
    }
}

/* COMPOSITE_SHADOW_ID [23:0] is a count of executed 3D primitives, and
   while bit 27 is set the count does not increment (RRG:
   COMPOSITE_SHADOW_ID, p. 3-145 / PDF 163; the guide lists the bit as
   COMPOSITE_SHADOW_AUTO_INC, here RAGE128_SHADOW_AUTO_INC_DIS). The
   count wraps within its 24 bits and the upper bits keep their written
   value; the guide does not describe overflow. The three submit
   functions below are the last point every render path passes through
   (serial interpreter or JIT, worker threads, GPU backend), and they run
   on the executor thread in submission order, so the count is the same
   in every path however that path later draws the pixels. A draw dropped
   for a failed AGP staging is still counted. A triangle removed by face
   culling never reaches these functions and is not counted; the guide
   does not say whether the chip counts it. */
static inline void
rage128_shadow_id_advance(rage128_t *dev)
{
    uint32_t id = dev->t3d.composite_shadow_id;

    if (id & RAGE128_SHADOW_AUTO_INC_DIS)
        return;
    dev->t3d.composite_shadow_id = (id & ~RAGE128_SHADOW_ID_MASK)
        | ((id + 1) & RAGE128_SHADOW_ID_MASK);
}

/* Draw a primitive on the submitting thread with row map 0, which owns
   every row: the serial path's pixel order. */
static void
rb_run_inline(rage128_t *dev, const rage128_raster_state_t *rs, int kind,
              const r3d_vtx_t *v)
{
    r128_jit_span_fn jfn = rage128_jit_get_block(dev, &rs->d);

    atomic_store(&dev->pace_3d_seen, 1);
    if (kind == RB_TRI)
        rage128_3d_tri(dev, rs, 0, 0, &v[0], &v[1], &v[2], jfn);
    else if (kind == RB_LINE)
        rage128_3d_line(dev, rs, 0, 0, &v[0], &v[1], jfn);
    else
        rage128_3d_point(dev, rs, 0, 0, &v[0], jfn);
}

/* Add a primitive to the batch after resolving what it depends on.
   Three cases, checked in this order:

     - The primitive samples bytes it writes itself. Drawn serially, a
       pixel can read texels that earlier pixels of the same primitive
       wrote; split across workers, a row could read texels another
       worker has not written yet. The serial result is the reference,
       so the batch is flushed and this primitive is drawn inline
       instead of deferred.
     - It samples bytes an earlier primitive in the batch wrote (plain
       render to texture): flush, then defer it as usual.
     - Its render target differs from the batch's: rb_guard_rt.

   The first two need the primitive's own byte intervals, so its rows
   are computed once here and also stored as the command's row range
   for the ownership skip. */
static void
rb_enqueue(rage128_t *dev, rage128_raster_t *rb,
           const rage128_raster_state_t *rs, int kind, const r3d_vtx_t *v, int n)
{
    rb_cmd_t *cmd;
    uint32_t  clo, chi, zlo, zhi;
    int32_t   y0, y1;

    rb_prim_rows(rs, v, n, &y0, &y1);
    rb_prim_ranges(rs, y0, y1, &clo, &chi, &zlo, &zhi);

    if (rb_tex_hits(rs, clo, chi) || rb_tex_hits(rs, zlo, zhi)) {
        rage128_raster_flush(dev);
        rb_run_inline(dev, rs, kind, v);
        return;
    }
    if (rb_tex_hits_written(rb, rs))
        rage128_raster_flush(dev);
    rb_guard_rt(dev, rb, rs);

    cmd            = rb_alloc_cmd(rb);
    cmd->state_idx = rb_intern_state(dev, rb, rs);
    cmd->kind      = (uint32_t) kind;
    cmd->py0       = y0;
    cmd->py1       = y1;
    for (int k = 0; k < n; k++)
        cmd->v[k] = v[k];
    rb_extend_written(rb, clo, chi, zlo, zhi);
    atomic_store(&dev->cce_batch_pending, 1); /* engine busy until flushed */
}

/* SUPERSAMPLE (PM4_VC_FPU_SETUP bit 11) multiplies vertex X and Y by 2
   or 4 before the float-to-integer conversion, per axis as XFACTOR (bit
   12) and YFACTOR (bit 13) select (Registers for CCE 3D Packets,
   PM4_VC_FPU_SETUP). The window offset (WINDOW_XY_OFFSET) is added later,
   to the scaled value, so it counts in subpixels of the scaled target;
   the documents do not say which order the chip uses. */
static const r3d_vtx_t *
rb_supersample(const rage128_raster_state_t *rs, const r3d_vtx_t *v, r3d_vtx_t *out)
{
    uint32_t fs = rs->t3d.fpu_setup;

    if (!(fs & RAGE128_FPU_SUPERSAMPLE))
        return v;
    *out = *v;
    out->x *= (fs & RAGE128_FPU_XFACTOR_4) ? 4.0f : 2.0f;
    out->y *= (fs & RAGE128_FPU_YFACTOR_4) ? 4.0f : 2.0f;
    return out;
}

void
rage128_raster_submit_tri(rage128_t *dev, const rage128_raster_state_t *rs,
                          const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c)
{
    rage128_raster_t *rb = (rage128_raster_t *) dev->raster;
    r3d_vtx_t         v[3];
    r3d_vtx_t         ss[3];

    /* No face culling here. Every caller is a vertex walk in
       vid_ati_rage128_3d.c that has already applied the PM4_VC_FPU_SETUP
       face functions (r3d_vc_face_draw), so a triangle that arrives here
       is drawn whatever its winding. That function also covers the
       Windows 98 Direct3D driver, which in the cases seen here writes
       0x000403d8 (back faces set to cull) whatever the Direct3D
       cull-mode render state is and culls on the CPU itself. Bit 18 of
       that value, which the supplement lists as reserved, makes
       r3d_vc_face_fn draw both windings. */

    rage128_shadow_id_advance(dev);

    if (rs->stage_dead)
        return; /* AGP staging failed: drop, do not wrap onto local VRAM */

    a = rb_supersample(rs, a, &ss[0]);
    b = rb_supersample(rs, b, &ss[1]);
    c = rb_supersample(rs, c, &ss[2]);

    if (dev->synctel)
        rage128_synctel_draw(dev, rs, a, b, c);
    if (dev->census)
        rage128_census_draw(dev, rs, b, c);

    /* Optional GPU backend: it takes the primitive when it supports the
       state. A 0 return falls through to the CPU path below, which is
       always serial while the GPU backend is on (init forces one render
       thread), so a primitive the backend refuses is drawn at once
       rather than batched. */
    if (dev->gpu && rage128_gpu_submit_tri(dev, rs, a, b, c))
        return;

    if (!rb || rb->nthreads <= 1) {
        atomic_store(&dev->pace_3d_seen, 1);
        rage128_3d_tri(dev, rs, 0, 0, a, b, c, rage128_jit_get_block(dev, &rs->d));
        return;
    }

    v[0] = *a;
    v[1] = *b;
    v[2] = *c;
    rb_enqueue(dev, rb, rs, RB_TRI, v, 3);
}

void
rage128_raster_submit_line(rage128_t *dev, const rage128_raster_state_t *rs,
                           const r3d_vtx_t *a, const r3d_vtx_t *b)
{
    rage128_raster_t *rb = (rage128_raster_t *) dev->raster;
    r3d_vtx_t         v[2];
    r3d_vtx_t         ss[2];

    rage128_shadow_id_advance(dev);

    if (rs->stage_dead)
        return; /* AGP staging failed: drop, do not wrap onto local VRAM */

    a = rb_supersample(rs, a, &ss[0]);
    b = rb_supersample(rs, b, &ss[1]);

    if (dev->synctel)
        rage128_synctel_draw(dev, rs, a, b, NULL);
    if (dev->census)
        rage128_census_draw(dev, rs, b, NULL);

    if (dev->gpu && rage128_gpu_submit_line(dev, rs, a, b))
        return;

    if (!rb || rb->nthreads <= 1) {
        atomic_store(&dev->pace_3d_seen, 1);
        rage128_3d_line(dev, rs, 0, 0, a, b, rage128_jit_get_block(dev, &rs->d));
        return;
    }

    v[0] = *a;
    v[1] = *b;
    rb_enqueue(dev, rb, rs, RB_LINE, v, 2);
}

void
rage128_raster_submit_point(rage128_t *dev, const rage128_raster_state_t *rs,
                            const r3d_vtx_t *v)
{
    rage128_raster_t *rb = (rage128_raster_t *) dev->raster;
    r3d_vtx_t         ss;

    rage128_shadow_id_advance(dev);

    if (rs->stage_dead)
        return; /* AGP staging failed: drop, do not wrap onto local VRAM */

    v = rb_supersample(rs, v, &ss);

    if (dev->synctel)
        rage128_synctel_draw(dev, rs, v, NULL, NULL);
    if (dev->census)
        rage128_census_draw(dev, rs, NULL, NULL);

    if (dev->gpu && rage128_gpu_submit_point(dev, rs, v))
        return;

    if (!rb || rb->nthreads <= 1) {
        atomic_store(&dev->pace_3d_seen, 1);
        rage128_3d_point(dev, rs, 0, 0, v, rage128_jit_get_block(dev, &rs->d));
        return;
    }

    rb_enqueue(dev, rb, rs, RB_POINT, v, 1);
}
