/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- surface access by card address.
 *
 *          The drawing engines address one 64 MB card space. The lower
 *          32 MB is the frame buffer and the upper 32 MB is system memory
 *          at AGP_BASE plus the low 25 bits of the address (RRG:
 *          DST_OFFSET, p. 3-137 / PDF 155); r128_card_is_agp in
 *          vid_ati_rage128.h tests bit 25 for that split. Local bytes live
 *          in svga.vram. System-memory bytes are reached through the bus
 *          block helpers in vid_ati_rage128_pm4.c, which go through the
 *          chipset's AGP aperture or through the chip's own page table
 *          (RRG: PCI_GART_PAGE, p. 3-207 / PDF 225), depending on how the
 *          driver set up the bus.
 *
 *          The engine code indexes a surface as one flat, linear byte
 *          array, and the 3D raster workers run on threads that may not
 *          call the bus helpers. So for a surface in system memory, and
 *          for any tiled 2D window, this file copies the bytes into a host
 *          buffer (an arena) on the submit thread and copies the changed
 *          bytes back later:
 *
 *          - r128_span_stage_acquire, r128_span_stage_acquire_tiled and
 *            r128_span_stage_writeback hold an AGP-resident 3D color or
 *            depth buffer for a whole batch of draws. The raster workers
 *            read and write the arena, and the batch barrier writes it
 *            back.
 *          - r128_surf_map, r128_surf_map_tiled, r128_surf_commit and
 *            r128_surf_release give one 2D operation a window over its
 *            source or destination. A linear window wholly in local
 *            memory is svga.vram itself, with no copy.
 *
 *          A tiled surface is held detiled in the arena, so the code that
 *          reads it uses plain row math; the copies in and out go through
 *          the tile transform.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
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

/* Grow st->arena to at least `need` bytes. The capacity doubles from
   1 MB, so it stays a power of two and is never smaller than the staged
   length rounded up to a power of two; r128_surf_map uses that rounded
   length as its index mask, so a masked index stays inside the
   allocation. Returns 0 on OOM with the old arena intact. Called only
   while no worker holds a pointer into the arena.

   The color and depth arenas (c_stage, z_stage) may be imported into the
   optional GPU backend, and a realloc would move memory the GPU still
   uses. rage128_gpu_czstage_grow handles those: it drains the GPU,
   imports a larger buffer and rebinds it. It returns -1 for an arena it
   does not own, which is then grown here. */
static int
r128_span_stage_grow(rage128_t *dev, struct rage128_span_stage *st,
                     uint32_t need)
{
    uint32_t ncap;
    uint8_t *na;
    int      gr;

    if (need <= st->cap)
        return 1;
    gr = rage128_gpu_czstage_grow(dev, st, need);
    if (gr >= 0)
        return gr;
    ncap = st->cap ? st->cap : (1u << 20);
    while (ncap < need)
        ncap <<= 1;
    na = (uint8_t *) realloc(st->arena, ncap);
    if (!na)
        return 0;
    st->arena = na;
    st->cap   = ncap;
    return 1;
}

/* Copy freshly staged arena bytes [off, off+len) into st->shadow, the
   baseline r128_span_stage_writeback compares against. If the shadow
   cannot grow, shadow_ok drops to 0 for this staging and the write-back
   pushes the whole extent. */
static void
r128_stage_shadow_save(struct rage128_span_stage *st, uint32_t off, uint32_t len)
{
    if (!st->shadow_ok)
        return;
    if (st->shadow_cap < st->cap) {
        uint8_t *ns = (uint8_t *) realloc(st->shadow, st->cap);

        if (!ns) {
            st->shadow_ok = 0;
            return;
        }
        st->shadow     = ns;
        st->shadow_cap = st->cap;
    }
    memcpy(st->shadow + off, st->arena + off, len);
}

/* Read or write card-space bytes [vm, vm+len). The guide defines the
   split per address: the lower 32 MB maps to the frame buffer and the
   upper 32 MB to AGP_BASE plus the low 25 bits (RRG: DST_OFFSET,
   p. 3-137 / PDF 155). So a span that starts below 32 MB and runs past
   it is cut there, and the rest continues at the start of the AGP half.
   Local bytes index svga.vram through vram_mask, so an offset past the
   installed memory is modeled as wrapping. AGP bytes go through the bus
   block helpers. Returns 0 when the AGP part fails: bus mastering is
   off, AGP_BASE is not set, or a page-table entry cannot be read. */
static int
r128_card_read_block(rage128_t *dev, uint32_t vm, uint8_t *dst, uint32_t len)
{
    if (!r128_card_is_agp(vm)) {
        uint32_t loc = len;

        if ((uint64_t) (vm & (R128_CARD_AGP_HALF - 1u)) + len > R128_CARD_AGP_HALF)
            loc = R128_CARD_AGP_HALF - (vm & (R128_CARD_AGP_HALF - 1u));
        for (uint32_t o = 0; o < loc; o++)
            dst[o] = dev->svga.vram[(vm + o) & dev->vram_mask];
        if (loc == len)
            return 1;
        return rage128_pm4_bus_read_block(dev, R128_CARD_AGP_HALF,
                                          dst + loc, len - loc);
    }
    return rage128_pm4_bus_read_block(dev, vm, dst, len);
}

static int
r128_card_write_block(rage128_t *dev, uint32_t vm, const uint8_t *src, uint32_t len)
{
    if (!len)
        return 1;
    if (!r128_card_is_agp(vm)) {
        svga_t  *svga = &dev->svga;
        uint32_t loc  = len;

        if ((uint64_t) (vm & (R128_CARD_AGP_HALF - 1u)) + len > R128_CARD_AGP_HALF)
            loc = R128_CARD_AGP_HALF - (vm & (R128_CARD_AGP_HALF - 1u));
        for (uint32_t o = 0; o < loc; o++)
            svga->vram[(vm + o) & dev->vram_mask] = src[o];
        /* Mark every touched 4 KB page changed so the display redraws
           it. The loop counts by length and masks each page address: a
           run that wraps through vram_mask has a masked end below its
           masked start, and a loop bounded by those two addresses would
           mark nothing. */
        for (uint32_t o = 0; o < loc; o += 0x1000u)
            svga->changedvram[((vm + o) & dev->vram_mask) >> 12] = svga->monitor->mon_changeframecount;
        svga->changedvram[((vm + loc - 1u) & dev->vram_mask) >> 12] = svga->monitor->mon_changeframecount;
        if (loc == len)
            return 1;
        return rage128_pm4_bus_write_block(dev, R128_CARD_AGP_HALF,
                                           src + loc, len - loc);
    }
    return rage128_pm4_bus_write_block(dev, vm, src, len);
}

/* Copy between a tiled card surface and a linear buffer. lin and len
   select bytes [lin, lin+len) of the surface's linear image at tpitch
   bytes per row; byte (x, y) of that image sits at tbase plus
   r128_tile_off, the 64-byte by 16-line tile transform. Card bytes are
   contiguous only within the 64 bytes one tile covers on one line, so
   the copy moves runs of at most 64 bytes, each through the block
   helpers above, which split local and AGP per address. dir 0 gathers
   (card to buf), 1 scatters (buf to card). Returns 0 when an AGP part
   fails. The caller guarantees r128_tiled_ok(1, tpitch). */
int
r128_card_copy_tiled(rage128_t *dev, uint32_t tbase, uint32_t tpitch,
                     uint32_t lin, uint8_t *buf, uint32_t len, int dir)
{
    while (len) {
        uint32_t y   = lin / tpitch;
        uint32_t xb  = lin % tpitch;
        uint32_t run = 64u - (xb & 63u);
        uint32_t ca;

        if (run > tpitch - xb)
            run = tpitch - xb;
        if (run > len)
            run = len;
        ca = tbase + r128_tile_off(xb, y, tpitch);
        if (dir ? !r128_card_write_block(dev, ca, buf, run)
                : !r128_card_read_block(dev, ca, buf, run))
            return 0;
        buf += run;
        lin += run;
        len -= run;
    }
    return 1;
}

/* Stage card bytes [vm, vm+extent) into st->arena, reusing a copy of the
 * same surface that is already staged. Used by the 3D color and depth
 * staging and by r128_surf_map. Returns 1 when staged, 0 on OOM or a
 * failed bus read; the caller then skips the work rather than fall back
 * to local memory. Reuse keeps what the batch has already drawn:
 * - the same vm with enough bytes staged returns at once, because
 *   reading guest memory again would overwrite depth or color the batch
 *   already wrote;
 * - the same vm with a larger extent reads only the new tail;
 * - a different vm writes the old staging back first. Draws already
 *   queued read and write this arena when they run, so the caller must
 *   flush them before staging another surface (r3d_stage_guard does).
 * Submit thread only. */
int
r128_span_stage_acquire(rage128_t *dev, struct rage128_span_stage *st,
                        uint32_t vm, uint32_t extent)
{
    if (!extent)
        return 0;

    /* A tiled staging holds a detiled image, so its arena bytes do not
       line up with card bytes [vm, vm+len). Write it back instead of
       reusing it. */
    if (st->active && st->tiled)
        r128_span_stage_writeback(dev, st);

    if (st->active && st->vm == vm && st->len >= extent)
        return 1;

    if (st->active && st->vm == vm) {
        uint32_t old_len = st->len;

        if (!r128_span_stage_grow(dev, st, extent))
            return 0;
        if (!r128_card_read_block(dev, vm + old_len,
                                  st->arena + old_len, extent - old_len))
            return 0;
        r128_stage_shadow_save(st, old_len, extent - old_len);
        st->len = extent;
        return 1;
    }

    if (st->active)
        r128_span_stage_writeback(dev, st);

    if (!r128_span_stage_grow(dev, st, extent))
        return 0;
    if (!r128_card_read_block(dev, vm, st->arena, extent))
        return 0;
    st->shadow_ok = 1;
    r128_stage_shadow_save(st, 0, extent);
    st->vm     = vm;
    st->len    = extent;
    st->active = 1;
    st->tiled  = 0;
    return 1;
}

/* Tiled form of r128_span_stage_acquire, used by the 3D staging: gather
 * bytes [0, extent) of the detiled linear image of the surface at
 * (tbase, tpitch), so the raster code reads the arena with plain row
 * math. The write-back scatters it through the tile transform. Reuse
 * works as in the linear form: the same (tbase, tpitch) keeps the staged
 * bytes, because a new gather would overwrite depth or color the batch
 * already wrote, and a larger extent gathers only the new tail. Any
 * other active staging is written back first; the caller must have
 * flushed the queued draws that use it. The caller guarantees
 * r128_tiled_ok(1, tpitch). Submit thread only. */
int
r128_span_stage_acquire_tiled(rage128_t *dev, struct rage128_span_stage *st,
                              uint32_t tbase, uint32_t tpitch, uint32_t extent)
{
    if (!extent)
        return 0;

    if (st->active && (!st->tiled || st->tbase != tbase || st->tpitch != tpitch))
        r128_span_stage_writeback(dev, st);

    if (st->active && st->len >= extent)
        return 1;

    if (st->active) {
        uint32_t old_len = st->len;

        if (!r128_span_stage_grow(dev, st, extent))
            return 0;
        if (!r128_card_copy_tiled(dev, tbase, tpitch, old_len,
                                  st->arena + old_len, extent - old_len, 0))
            return 0;
        r128_stage_shadow_save(st, old_len, extent - old_len);
        st->len = extent;
        return 1;
    }

    if (!r128_span_stage_grow(dev, st, extent))
        return 0;
    if (!r128_card_copy_tiled(dev, tbase, tpitch, 0, st->arena, extent, 0))
        return 0;
    st->shadow_ok = 1;
    r128_stage_shadow_save(st, 0, extent);
    st->vm     = tbase;
    st->len    = extent;
    st->active = 1;
    st->tiled  = 1;
    st->tbase  = tbase;
    st->tpitch = tpitch;
    return 1;
}

/* Write a staged surface back to guest memory. The real chip has no
   staged copy: the engine writes memory directly, so a CPU store to a
   byte the engine does not draw stays in memory. To keep such stores,
   only bytes that differ from the acquire-time baseline in st->shadow
   are written, as runs of changed bytes; writing the whole extent would
   replace them with the stale staged values. A byte the engine rewrote
   with its baseline value looks unchanged and is not written. With no
   baseline (shadow_ok 0 after an OOM) the whole extent is written. A
   second call with nothing staged does nothing.

   A run's write can fail on the bus: bus mastering turned off, AGP_BASE
   cleared or a page-table entry that cannot be read after the acquire
   went through. Real memory would simply hold what the engine wrote;
   here the bytes exist only in the arena, so a failed run loses them.
   The remaining runs are still tried (a single bad page-table entry
   leaves the other pages reachable), the loss is announced once per
   write-back, and the staging is released either way: a retry against
   a dead bus gains nothing, and a batch that keeps the arena would
   write the stale bytes over later guest stores. Submit thread only. */
void
r128_span_stage_writeback(rage128_t *dev, struct rage128_span_stage *st)
{
    int ok = 1;

    if (!st->active)
        return;
    /* The color and depth arenas may be imported into the optional GPU
       backend, whose queued or running work can still be writing them.
       Retire that work first so the push carries the final bytes. */
    if (st == &dev->c_stage || st == &dev->z_stage)
        rage128_gpu_czstage_quiesce(dev);
    if (st->arena && st->len) {
        const uint8_t *a   = st->arena;
        const uint8_t *b   = st->shadow;
        uint32_t       len = st->len;
        uint32_t       i   = 0;

        while (i < len) {
            uint32_t s;

            if (st->shadow_ok) {
                uint32_t blk = len - i > 64u ? 64u : len - i;

                if (!memcmp(a + i, b + i, blk)) {
                    i += blk;
                    continue;
                }
                /* This block holds a changed byte, so the scan stops
                   inside it. */
                while (a[i] == b[i])
                    i++;
                s = i;
                while (i < len && a[i] != b[i])
                    i++;
            } else {
                s = 0;
                i = len;
            }
            /* For a tiled staging, arena byte 0 is byte st->vm - st->tbase
               of the surface's linear image. */
            if (st->tiled)
                ok &= r128_card_copy_tiled(dev, st->tbase, st->tpitch,
                                           (st->vm - st->tbase) + s,
                                           st->arena + s, i - s, 1);
            else
                ok &= r128_card_write_block(dev, st->vm + s, st->arena + s, i - s);
        }
    }
    if (!ok)
        pclog("[r128 mem] staged write-back lost: vm=%08x len=%u (bus off, AGP_BASE 0 or unreadable page entry)\n",
              st->vm, st->len);
    st->active = 0;
    st->tiled  = 0;
}

/* Resolve a window over card bytes [lo, lo+len) for one 2D operation
   (the 2D engine runs each operation to completion before it returns).
   A window wholly below 32 MB is svga.vram itself, indexed through
   vram_mask, with no copy. A window that touches the
   AGP half, either starting there or starting below 32 MB and running
   past it, is staged whole through r128_span_stage_acquire, whose block
   helpers cut the span at 32 MB instead of wrapping it through
   vram_mask. The mask is len rounded up to a power of two, minus one, so
   a stray index wraps inside the staged copy. Returns 0 when the span
   cannot be staged (OOM or a failed bus read); the caller must then skip
   the operation and never fall back to local memory. */
int
r128_surf_map(rage128_t *dev, r128_surf_t *s, struct rage128_span_stage *st,
              uint32_t lo, uint32_t len)
{
    uint32_t off = lo & (2u * R128_CARD_AGP_HALF - 1u);
    uint32_t m;

    if (!len)
        len = 1;

    /* Clamp the span at the 64 MB top of card space. No real surface
       reaches past it, but degenerate geometry could otherwise ask for
       a huge bus read. */
    if ((uint64_t) off + len > 2u * R128_CARD_AGP_HALF)
        len = 2u * R128_CARD_AGP_HALF - off;

    /* Local means the whole interval [off, off+len) lies below the 32 MB
       boundary. The interval test, not bit 25 of the two end addresses:
       a span longer than 32 MB can start and end with bit 25 clear and
       still run through the AGP half in between. */
    if (off < R128_CARD_AGP_HALF && off + len <= R128_CARD_AGP_HALF) {
        s->base = dev->svga.vram;
        s->rel  = 0;
        s->mask = dev->vram_mask;
        s->st   = NULL;
        return 1;
    }

    if (!r128_span_stage_acquire(dev, st, lo, len))
        return 0;

    m = 1;
    while (m < len)
        m <<= 1;
    s->base = st->arena;
    s->rel  = lo;
    s->mask = m - 1u;
    s->st   = st;
    return 1;
}

/* Resolve a window over a tiled surface for one 2D operation. The
   surface starts at card address tbase with tpitch bytes per row, and
   [lo, lo+len) is a span of its linear image, so lo - tbase is the
   linear offset. The span is gathered detiled into the arena, so the
   operation's linear row math works unchanged, and r128_surf_commit
   scatters it back through the tile transform. Any active staging in st
   is written back first, and the window is always a fresh copy: even a
   window wholly in local memory needs the gather. Returns 0 when it
   cannot be staged (OOM or a failed AGP read); the caller must then
   skip the operation. */
int
r128_surf_map_tiled(rage128_t *dev, r128_surf_t *s, struct rage128_span_stage *st,
                    uint32_t tbase, uint32_t tpitch, uint32_t lo, uint32_t len)
{
    uint32_t m;

    if (!len)
        len = 1;
    if ((uint64_t) ((lo - tbase) & (2u * R128_CARD_AGP_HALF - 1u)) + len
        > 2u * R128_CARD_AGP_HALF)
        len = 2u * R128_CARD_AGP_HALF - ((lo - tbase) & (2u * R128_CARD_AGP_HALF - 1u));

    if (st->active)
        r128_span_stage_writeback(dev, st);
    if (!r128_span_stage_grow(dev, st, len))
        return 0;
    if (!r128_card_copy_tiled(dev, tbase, tpitch, lo - tbase, st->arena, len, 0))
        return 0;
    st->shadow_ok = 1;
    r128_stage_shadow_save(st, 0, len);
    st->vm     = lo;
    st->len    = len;
    st->active = 1;
    st->tiled  = 1;
    st->tbase  = tbase;
    st->tpitch = tpitch;

    m = 1;
    while (m < len)
        m <<= 1;
    s->base = st->arena;
    s->rel  = lo;
    s->mask = m - 1u;
    s->st   = st;
    return 1;
}

/* Finish a read-write window: write a staged copy back to guest memory
   and release it. A local window was written in place and needs
   nothing. */
void
r128_surf_commit(rage128_t *dev, r128_surf_t *s)
{
    if (s->st) {
        r128_span_stage_writeback(dev, s->st);
        s->st = NULL;
    }
}

/* Release a read-only window, dropping any staged copy without writing
   it back. */
void
r128_surf_release(r128_surf_t *s)
{
    if (s->st) {
        s->st->active = 0;
        s->st->tiled  = 0;
        s->st         = NULL;
    }
}
