/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- fence-count telemetry for the GPU backend.
 *
 *          This file counts, on the CPU renderer paths and without
 *          changing what they do, how often a segment-batched GPU raster
 *          backend would have to fence. In that design the draw spans of
 *          one segment go to the GPU as a single dispatch with no barrier
 *          between them. The CPU then has to wait for the GPU only when
 *          it would read pixels the GPU has not yet written, or overwrite
 *          bytes a queued span has not yet read. Each such wait is counted
 *          in one of five classes:
 *
 *            rd   - a guest CPU read of VRAM the 3D engine wrote in the
 *                   current segment
 *            2d   - a 2D engine source or destination that touches
 *                   3D-written bytes
 *            upl  - a CPU write or 2D destination store into bytes that a
 *                   queued draw samples as a texture
 *            scan - a scanout line fetch from 3D-written bytes
 *            wr   - a guest CPU write into 3D-written bytes (a
 *                   write-after-write hazard: the same fence, counted
 *                   separately)
 *
 *          A CPU write that hits both cases counts as upl; a 2D access
 *          that hits both counts as 2d.
 *
 *          Tracking is coarse: one bit per 4 KB block of local VRAM, and a
 *          draw marks whole rows of its target. Counting too many fences
 *          is acceptable; counting too few is not. Textures and surfaces
 *          in the AGP half of card space are not tracked. They are copied
 *          into staging arenas when the draw or 2D op is set up
 *          (tex_stage, span_stage) and read from the copy, so a later
 *          guest write to AGP memory cannot change what queued work
 *          reads.
 *
 *          Two more events are counted for information only; neither
 *          makes the CPU wait. A segment split is a draw whose color or
 *          Z target differs from the previous draw's, or a draw that
 *          samples bytes written earlier in the same segment: the GPU
 *          has to end the segment there. A present is the latch of a new
 *          scanout base, a page flip, where the segment is flushed in
 *          any case.
 *
 *          Setting R128_GPU_SYNC_TELEMETRY to any value other than empty
 *          or "0" turns counting on. A value that contains '/' is the CSV
 *          path; otherwise the CSV is r128_synctel.csv in the working
 *          directory. Each vblank with a nonzero count writes one CSV
 *          row. At close the log gets the run totals per class and, over
 *          the present-to-present intervals that drew anything, the
 *          median, 90th and 99th percentile and maximum fence count.
 *
 *          The taps sit on the CPU paths only. With the GPU raster
 *          backend also on, the variable turns off async present,
 *          snapshots and the GPU 2D queue so every access still passes a
 *          tap: a GPU-backend run with it set does not behave like a
 *          normal one.
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
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_ati_rage128.h>

#define ST_BLKSHIFT 12u
#define ST_BLKSZ    (1u << ST_BLKSHIFT)
#define ST_BLOCKS   (R128_CARD_AGP_HALF >> ST_BLKSHIFT) /* local half only */
#define ST_WORDS    (ST_BLOCKS / 32u)
#define ST_IVALS    65536u /* present-interval ring; wraps, close() notes it */

enum { ST_RD,
       ST_2D,
       ST_UPL,
       ST_SCAN,
       ST_WR,
       ST_NCLASS };

#ifdef REG_HARNESS
/* Harness hook, run by the draw tap right after its render-target
   validity exchange: the harness imposes a present at that point to pin
   down the order the two sides observe. */
void (*harness_synctel_draw_hook)(rage128_t *dev);
#endif

typedef struct r128_synctel_t {
    /* Blocks the 3D engine wrote since the last fence or present: the
       bytes whose CPU copy a GPU segment would leave stale. */
    atomic_uint dirty[ST_WORDS];
    /* Blocks that the textures of queued draws occupy in local VRAM: a
       write here would change what the GPU reads for a draw queued
       before the write. */
    atomic_uint qread[ST_WORDS];

    /* Event counts for the current vblank. Any thread increments them;
       rage128_synctel_frame takes and zeroes them at vsync. */
    atomic_uint fcls[ST_NCLASS];
    atomic_uint fsplit;
    atomic_uint fpresent;
    atomic_uint fdraws;

    /* The current present-to-present interval, one per rendered frame. */
    atomic_uint pf_forced; /* fences since the last present            */
    atomic_uint pf_draws;  /* 1 once the interval has drawn anything   */
    atomic_uint ival_n;    /* intervals recorded (ring write index)    */
    uint32_t    ivals[ST_IVALS];

    /* The previous draw's color and Z target, for split detection.
       rage128_synctel_draw alone reads and writes the tuple, on the
       submit thread. rt_valid says whether the tuple is on record: the
       draw tap exchanges it to 1 and rage128_synctel_present stores 0
       from the thread that latches the scanout base, so it is atomic
       while the tuple stays submit-owned. */
    uint32_t   rt_dst, rt_pitch, rt_z;
    atomic_int rt_valid;

    /* Run totals and the CSV, touched only at vsync and at close. */
    uint64_t tot[ST_NCLASS];
    uint64_t tot_split, tot_present, tot_draws;
    uint32_t frame;
    FILE    *csv;
} r128_synctel_t;

/* ------------------------------------------------------------------ */
/* Block bitmap helpers, on card addresses. A range that starts in the */
/* AGP half is ignored, and one that runs into it stops at             */
/* R128_CARD_AGP_HALF. Each block address is masked with vram_mask,    */
/* the same wrap the engines apply to local VRAM addresses.            */
/* ------------------------------------------------------------------ */

static void
st_mark(rage128_t *dev, atomic_uint *bm, uint32_t lo, uint32_t hi)
{
    if (r128_card_is_agp(lo) || hi <= lo)
        return;
    if (hi > R128_CARD_AGP_HALF)
        hi = R128_CARD_AGP_HALF;
    for (uint32_t a = lo & ~(ST_BLKSZ - 1u); a < hi; a += ST_BLKSZ) {
        uint32_t b = (a & dev->vram_mask) >> ST_BLKSHIFT;

        atomic_fetch_or_explicit(&bm[b >> 5], 1u << (b & 31u),
                                 memory_order_relaxed);
    }
}

static int
st_test(rage128_t *dev, atomic_uint *bm, uint32_t lo, uint32_t hi)
{
    if (r128_card_is_agp(lo) || hi <= lo)
        return 0;
    if (hi > R128_CARD_AGP_HALF)
        hi = R128_CARD_AGP_HALF;
    for (uint32_t a = lo & ~(ST_BLKSZ - 1u); a < hi; a += ST_BLKSZ) {
        uint32_t b = (a & dev->vram_mask) >> ST_BLKSHIFT;

        if (atomic_load_explicit(&bm[b >> 5], memory_order_relaxed)
            & (1u << (b & 31u)))
            return 1;
    }
    return 0;
}

static void
st_clear(atomic_uint *bm)
{
    for (uint32_t w = 0; w < ST_WORDS; w++)
        atomic_store_explicit(&bm[w], 0, memory_order_relaxed);
}

/* Count one fence. The modeled fence completes all queued GPU work, so
   both maps are cleared. */
static void
st_fence(r128_synctel_t *tel, int cls)
{
    atomic_fetch_add_explicit(&tel->fcls[cls], 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&tel->pf_forced, 1, memory_order_relaxed);
    st_clear(tel->dirty);
    st_clear(tel->qread);
}

/* ------------------------------------------------------------------ */
/* Event taps.                                                         */
/* ------------------------------------------------------------------ */

void
rage128_synctel_cpu_read(rage128_t *dev, uint32_t addr, uint32_t len)
{
    r128_synctel_t *tel = (r128_synctel_t *) dev->synctel;

    if (st_test(dev, tel->dirty, addr, addr + len))
        st_fence(tel, ST_RD);
}

void
rage128_synctel_cpu_write(rage128_t *dev, uint32_t addr, uint32_t len)
{
    r128_synctel_t *tel = (r128_synctel_t *) dev->synctel;

    if (st_test(dev, tel->qread, addr, addr + len))
        st_fence(tel, ST_UPL);
    else if (st_test(dev, tel->dirty, addr, addr + len))
        st_fence(tel, ST_WR);
}

void
rage128_synctel_2d_span(rage128_t *dev, uint32_t lo, uint32_t len, int is_write)
{
    r128_synctel_t *tel = (r128_synctel_t *) dev->synctel;

    if (st_test(dev, tel->dirty, lo, lo + len))
        st_fence(tel, ST_2D);
    else if (is_write && st_test(dev, tel->qread, lo, lo + len))
        st_fence(tel, ST_UPL);
}

void
rage128_synctel_scanline(rage128_t *dev, uint32_t addr, uint32_t len)
{
    r128_synctel_t *tel = (r128_synctel_t *) dev->synctel;

    if (st_test(dev, tel->dirty, addr, addr + len))
        st_fence(tel, ST_SCAN);
}

void
rage128_synctel_draw(rage128_t *dev, const rage128_raster_state_t *rs,
                     const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c)
{
    r128_synctel_t  *tel  = (r128_synctel_t *) dev->synctel;
    const r3d_vtx_t *v[3] = { a, b, c };
    float            ymin = 0.0f, ymax = 0.0f;
    int              have = 0, every = 0;
    int              y0 = 0, y1 = 0;
    int              split = 0;
    uint32_t         stride, lo, hi;

    /* A draw that samples bytes written earlier in the segment, or one
       whose color or Z target differs from the previous draw's, ends the
       GPU segment: a split, not a CPU fence. The texture test runs
       before this draw marks its own rows, so a draw does not split on
       its own output. */
    for (int st = 0; st < 2; st++) {
        if (rs->tex_hi[st] > rs->tex_lo[st]
            && st_test(dev, tel->dirty, rs->tex_lo[st], rs->tex_hi[st])) {
            split = 1;
            break;
        }
    }
    /* The exchange is this draw's validity point: it reads whether a
       previous draw's target is on record and publishes this draw's in
       one step, so a present on the display thread lands either before
       it (this draw sees no target and counts no split) or after it
       (the present's clear stands for the next draw). A separate read
       and write would let this draw's write undo a present that landed
       between them, and the next draw would count a split against a
       target the present had already invalidated. */
    if (atomic_exchange_explicit(&tel->rt_valid, 1, memory_order_relaxed)
        && (rs->dst_offset != tel->rt_dst || rs->dst_pitch != tel->rt_pitch
            || rs->t3d.z_offset != tel->rt_z))
        split = 1;
#ifdef REG_HARNESS
    if (harness_synctel_draw_hook)
        harness_synctel_draw_hook(dev);
#endif
    tel->rt_dst   = rs->dst_offset;
    tel->rt_pitch = rs->dst_pitch;
    tel->rt_z     = rs->t3d.z_offset;
    if (split) {
        atomic_fetch_add_explicit(&tel->fsplit, 1, memory_order_relaxed);
    }

    for (int k = 0; k < 3; k++) {
        if (!v[k])
            continue;
        float fy = v[k]->y;

        if (fy >= -65536.0f && fy <= 65536.0f) {
            if (!have || fy < ymin)
                ymin = fy;
            if (!have || fy > ymax)
                ymax = fy;
            have = 1;
        } else {
            /* A NaN or far off-surface Y: the rasterizer's row range
               (rb_prim_rows) claims every row in this case, so the
               dirty map does too. */
            every = 1;
            break;
        }
    }

    if (every) {
        y0 = 0;
        y1 = 0x7fff;
    } else if (have) {
        /* The rows the rasterizer may write are the vertex Y range
           shifted by the window Y offset, floored and ceiled, with two
           rows of slack on each side: the same arithmetic as
           rb_prim_rows, so the marked rows cover the written ones. The
           offset is WINDOW_XY_OFFSET [15:0], signed fixed point with 2
           or 4 fraction bits as SUB_PIX_AMNT (SETUP_CNTL bit 19)
           selects (RRG: WINDOW_XY_OFFSET, p. 3-249 / PDF 267; Registers
           for CCE 3D Packets, SETUP_CNTL). The clamp to [0, 0x7fff]
           keeps the byte arithmetic below in range; rows above the
           target are marked anyway, which only over-reports. */
        int   sub = (rs->t3d.setup_cntl & (1u << 19)) ? 16 : 4;
        float wof = (float) (int16_t) (rs->t3d.window_xy_offset & 0xffff) / (float) sub;

        y0 = (int) floorf(ymin + wof) - 2;
        y1 = (int) ceilf(ymax + wof) + 2;
        if (y0 < 0)
            y0 = 0;
        if (y1 > 0x7fff)
            y1 = 0x7fff;
    }

    stride = rs->dst_pitch * rs->dst_bpp;
    if ((every || have) && y1 >= y0 && stride) {
        /* Whole rows y0 to y1 of the color target, and of the Z buffer
           when Z writes are on. */
        lo = rs->dst_offset + (uint32_t) y0 * stride;
        hi = rs->dst_offset + (uint32_t) (y1 + 1) * stride;
        st_mark(dev, tel->dirty, lo, hi);

        if (rs->d.z_en && rs->d.z_wr && rs->d.zrowpx) {
            uint32_t zstride = rs->d.zrowpx * (uint32_t) rs->d.zbpp;

            lo = rs->t3d.z_offset + (uint32_t) y0 * zstride;
            hi = rs->t3d.z_offset + (uint32_t) (y1 + 1) * zstride;
            st_mark(dev, tel->dirty, lo, hi);
        }
    }

    for (int st = 0; st < 2; st++) {
        if (rs->tex_hi[st] > rs->tex_lo[st])
            st_mark(dev, tel->qread, rs->tex_lo[st], rs->tex_hi[st]);
    }

    atomic_fetch_add_explicit(&tel->fdraws, 1, memory_order_relaxed);
    atomic_store_explicit(&tel->pf_draws, 1, memory_order_relaxed);
}

void
rage128_synctel_present(rage128_t *dev)
{
    r128_synctel_t *tel = (r128_synctel_t *) dev->synctel;
    uint32_t        forced, drew;

    atomic_fetch_add_explicit(&tel->fpresent, 1, memory_order_relaxed);
    forced = atomic_exchange_explicit(&tel->pf_forced, 0, memory_order_relaxed);
    drew   = atomic_exchange_explicit(&tel->pf_draws, 0, memory_order_relaxed);
    if (drew) {
        uint32_t i = atomic_fetch_add_explicit(&tel->ival_n, 1,
                                               memory_order_relaxed);

        tel->ivals[i % ST_IVALS] = forced;
    }
    st_clear(tel->dirty);
    st_clear(tel->qread);
    atomic_store_explicit(&tel->rt_valid, 0, memory_order_relaxed);
}

#ifdef REG_HARNESS
/* Harness probes. The bitmaps and the per-vblank counters have no
   register view, so the register harness reads them here: whether the
   block holding a card address is in the dirty map (qread 0) or the
   queued-read map (qread 1), and the counts since the last vblank
   drain. */
int
harness_synctel_marked(rage128_t *dev, int qread, uint32_t addr)
{
    r128_synctel_t *tel = (r128_synctel_t *) dev->synctel;

    if (!tel)
        return -1;
    return st_test(dev, qread ? tel->qread : tel->dirty, addr, addr + 1);
}

void
harness_synctel_counts(rage128_t *dev, uint32_t *cls, uint32_t *split)
{
    r128_synctel_t *tel = (r128_synctel_t *) dev->synctel;

    for (int i = 0; i < ST_NCLASS; i++)
        cls[i] = tel ? atomic_load_explicit(&tel->fcls[i], memory_order_relaxed) : 0;
    *split = tel ? atomic_load_explicit(&tel->fsplit, memory_order_relaxed) : 0;
}
#endif

/* ------------------------------------------------------------------ */
/* Per-vblank drain and run summary.                                   */
/* ------------------------------------------------------------------ */

void
rage128_synctel_frame(rage128_t *dev)
{
    r128_synctel_t *tel = (r128_synctel_t *) dev->synctel;
    uint32_t        c[ST_NCLASS], spl, pres, draws, forced = 0;

    for (int i = 0; i < ST_NCLASS; i++) {
        c[i] = atomic_exchange_explicit(&tel->fcls[i], 0, memory_order_relaxed);
        tel->tot[i] += c[i];
        forced += c[i];
    }
    spl   = atomic_exchange_explicit(&tel->fsplit, 0, memory_order_relaxed);
    pres  = atomic_exchange_explicit(&tel->fpresent, 0, memory_order_relaxed);
    draws = atomic_exchange_explicit(&tel->fdraws, 0, memory_order_relaxed);
    tel->tot_split += spl;
    tel->tot_present += pres;
    tel->tot_draws += draws;
    tel->frame++;

    if (tel->csv && (draws || forced || spl || pres)) {
        fprintf(tel->csv, "%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                tel->frame, draws, c[ST_RD], c[ST_2D], c[ST_UPL],
                c[ST_SCAN], c[ST_WR], spl, pres);
        fflush(tel->csv);
    }
}

static int
st_cmp_u32(const void *pa, const void *pb)
{
    uint32_t a = *(const uint32_t *) pa, b = *(const uint32_t *) pb;

    return a < b ? -1 : a > b;
}

void
rage128_synctel_init(rage128_t *dev)
{
    const char     *env = getenv("R128_GPU_SYNC_TELEMETRY");
    const char     *path;
    r128_synctel_t *tel;

    dev->synctel = NULL;
    if (!env || !env[0] || !strcmp(env, "0"))
        return;

    tel = (r128_synctel_t *) calloc(1, sizeof(*tel));
    if (!tel)
        return;
    atomic_init(&tel->rt_valid, 0);
    path     = strchr(env, '/') ? env : "r128_synctel.csv";
    tel->csv = fopen(path, "w");
    if (tel->csv)
        fprintf(tel->csv, "frame,draws,rd,2d,upl,scan,wr,splits,presents\n");
    if (tel->csv)
        rage128_log("RAGE128 SYNCTEL: on, csv=%s%s\n", path,
                    tel->csv ? "" : " (OPEN FAILED, close-summary only)");
    else
        pclog("RAGE128 SYNCTEL: on, csv=%s%s\n", path,
              tel->csv ? "" : " (OPEN FAILED, close-summary only)");
    dev->synctel = tel;
}

void
rage128_synctel_close(rage128_t *dev)
{
    r128_synctel_t *tel = (r128_synctel_t *) dev->synctel;
    uint32_t        n;

    if (!tel)
        return;
    dev->synctel = NULL;

    rage128_log("RAGE128 SYNCTEL: frames=%u draws=%llu presents=%llu splits=%llu\n",
                tel->frame, (unsigned long long) tel->tot_draws,
                (unsigned long long) tel->tot_present,
                (unsigned long long) tel->tot_split);
    rage128_log("RAGE128 SYNCTEL: forced fences: rd=%llu 2d=%llu upl=%llu scan=%llu wr=%llu\n",
                (unsigned long long) tel->tot[ST_RD],
                (unsigned long long) tel->tot[ST_2D],
                (unsigned long long) tel->tot[ST_UPL],
                (unsigned long long) tel->tot[ST_SCAN],
                (unsigned long long) tel->tot[ST_WR]);

    n = atomic_load_explicit(&tel->ival_n, memory_order_relaxed);
    if (n) {
        uint32_t kept = n < ST_IVALS ? n : ST_IVALS;

        qsort(tel->ivals, kept, sizeof(uint32_t), st_cmp_u32);
        rage128_log("RAGE128 SYNCTEL: fences per 3D present-interval (n=%u%s): "
                    "median=%u p90=%u p99=%u max=%u\n",
                    n, n > ST_IVALS ? ", ring wrapped, stats over last 64K" : "",
                    tel->ivals[kept / 2], tel->ivals[(uint32_t) (kept * 0.90)],
                    tel->ivals[(uint32_t) (kept * 0.99)], tel->ivals[kept - 1]);
    } else {
        rage128_log("RAGE128 SYNCTEL: no 3D present-intervals recorded\n");
    }

    if (tel->csv)
        fclose(tel->csv);
    free(tel);
}
