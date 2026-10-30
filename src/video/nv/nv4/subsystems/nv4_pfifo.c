/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PFIFO.
 *
 *          Sixteen channels share one command queue, CACHE1. A channel in
 *          PIO mode writes methods straight into its control area at
 *          0x800000 + channel * 0x10000 + subchannel * 0x2000; a channel in
 *          DMA mode writes DMA_PUT there, and the DMA pusher reads the
 *          commands from its pushbuffer. Only the channel that owns CACHE1
 *          gets in; another channel takes it over when CACHES.REASSIGN
 *          allows and CACHE1 is empty, the state of the old one going to
 *          its RAMFC slot. A write that cannot get in is logged in RAMRO
 *          with the RUNOUT interrupt.
 *
 *          The puller hands the methods to the engines: method 0 binds an
 *          object, looked up by handle in RAMHT, to a subchannel; methods
 *          0x180-0x1fc carry handles and are looked up too; the rest pass
 *          through. A method for the SOFTWARE engine, or a handle not in
 *          RAMHT, stops the puller with the CACHE_ERROR interrupt, and the
 *          driver completes it.
 *
 *          The pusher and the puller run when they get work (a write to
 *          the channel, a register that restarts them) and go until they
 *          run out or an engine holds them; the engines here are
 *          synchronous.
 *
 *          Public references: envytools (fifo/dma-pusher, fifo/puller,
 *          the RAMHT hash), nouveau (nv04 fifo).
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/nv/vid_nv4.h>
#include "cpu.h"

/* Registers */
#define PFIFO_DELAY_0               0x002040
#define PFIFO_DMA_TIMESLICE         0x002044
#define PFIFO_PIO_TIMESLICE         0x002048
#define PFIFO_TIMESLICE             0x00204c
#define PFIFO_NEXT_CHANNEL          0x002050
#define PFIFO_DEBUG_0               0x002080
#define PFIFO_INTR_0                0x002100
#define PFIFO_INTR_EN_0             0x002140
#define PFIFO_RAMHT                 0x002210
#define PFIFO_RAMFC                 0x002214
#define PFIFO_RAMRO                 0x002218
#define PFIFO_RUNOUT_STATUS         0x002400
#define PFIFO_RUNOUT_PUT            0x002410
#define PFIFO_RUNOUT_GET            0x002420
#define PFIFO_CACHES                0x002500
#define PFIFO_MODE                  0x002504
#define PFIFO_DMA                   0x002508
#define PFIFO_SIZE                  0x00250c
#define PFIFO_CACHE0_PUSH0          0x003000
#define PFIFO_CACHE0_PUSH1          0x003004
#define PFIFO_CACHE0_PUT            0x003010
#define PFIFO_CACHE0_STATUS         0x003014
#define PFIFO_CACHE0_PULL0          0x003050
#define PFIFO_CACHE0_PULL1          0x003054
#define PFIFO_CACHE0_HASH           0x003058
#define PFIFO_CACHE0_GET            0x003070
#define PFIFO_CACHE0_ENGINE         0x003080
#define PFIFO_CACHE0_METHOD         0x003100
#define PFIFO_CACHE0_DATA           0x003104
#define PFIFO_CACHE1_PUSH0          0x003200
#define PFIFO_CACHE1_PUSH1          0x003204
#define PFIFO_CACHE1_PUT            0x003210
#define PFIFO_CACHE1_STATUS         0x003214
#define PFIFO_CACHE1_STATUS1        0x003218
#define PFIFO_CACHE1_DMA_PUSH       0x003220
#define PFIFO_CACHE1_DMA_FETCH      0x003224
#define PFIFO_CACHE1_DMA_STATE      0x003228
#define PFIFO_CACHE1_DMA_INSTANCE   0x00322c
#define PFIFO_CACHE1_DMA_CTL        0x003230
#define PFIFO_CACHE1_DMA_LIMIT      0x003234
#define PFIFO_CACHE1_DMA_TLB_TAG    0x003238
#define PFIFO_CACHE1_DMA_TLB_PTE    0x00323c
#define PFIFO_CACHE1_DMA_PUT        0x003240
#define PFIFO_CACHE1_DMA_GET        0x003244
#define PFIFO_CACHE1_PULL0          0x003250
#define PFIFO_CACHE1_PULL1          0x003254
#define PFIFO_CACHE1_HASH           0x003258
#define PFIFO_CACHE1_GET            0x003270
#define PFIFO_CACHE1_ENGINE         0x003280
#define PFIFO_CACHE1_DMA_DCOUNT     0x0032a0
#define PFIFO_CACHE1_DMA_GET_JMP    0x0032a4
#define PFIFO_CACHE1_DMA_RSVD       0x0032a8
#define PFIFO_CACHE1_DMA_DATA       0x0032ac

#define INTR_CACHE_ERROR  (1u << 0)
#define INTR_RUNOUT       (1u << 4)
#define INTR_RUNOUT_OVF   (1u << 8)
#define INTR_DMA_PUSHER   (1u << 12)
#define INTR_DMA_PT       (1u << 16)

#define PULL0_ACCESS      (1u << 0)
#define PULL0_HASH_FAILED (1u << 4)
#define PULL0_DEVICE_SW   (1u << 8)

#define PUSH1_MODE_DMA    (1u << 8)

#define DMA_PUSH_ACCESS    (1u << 0)
#define DMA_PUSH_BUSY      (1u << 4)
#define DMA_PUSH_EMPTY     (1u << 8)
#define DMA_PUSH_SUSPENDED (1u << 12)

/* DMA_STATE.ERROR */
#define DMA_ERR_NON_CACHE    1
#define DMA_ERR_RESERVED_CMD 2
#define DMA_ERR_PROTECTION   3

/* RAMRO.REASON */
#define RO_ILLEGAL_ACCESS   0
#define RO_NO_CACHE         1
#define RO_CACHE_RAN_OUT    2
#define RO_FREE_OVERRUN     3

#define CHID(p) ((p)->c1_push1 & 0x0f)

/* ------------------------------------------------------------------ */
/* Instance memory structures.                                         */
/* ------------------------------------------------------------------ */
static uint32_t
nv4_ramht_base(const nv4_pfifo_t *p)
{
    return (p->ramht & 0x1f0) << 8;
}

static uint32_t
nv4_ramht_entries(const nv4_pfifo_t *p)
{
    return 512u << ((p->ramht >> 16) & 3); /* 4, 8, 16 or 32 KB of 8-byte entries */
}

static uint32_t
nv4_ramfc_base(const nv4_pfifo_t *p)
{
    return (p->ramfc & 0x1fe) << 8;
}

static uint32_t
nv4_ramro_base(const nv4_pfifo_t *p)
{
    return (p->ramro & 0x1fe) << 8;
}

static uint32_t
nv4_ramro_mask(const nv4_pfifo_t *p)
{
    return ((p->ramro & 0x10000) ? 0x2000 : 0x200) - 8;
}

/* The RAMHT hash: the handle folded into the table's index width by
   XOR, with the channel in the top four bits. */
static uint32_t
nv4_ramht_hash(const nv4_pfifo_t *p, uint32_t handle, int chid)
{
    uint32_t entries = nv4_ramht_entries(p);
    int      bits    = 0;
    uint32_t hash    = 0;

    while ((1u << bits) < entries)
        bits++;
    while (handle) {
        hash ^= handle & (entries - 1);
        handle >>= bits;
    }
    hash ^= (uint32_t) chid << (bits - 4);
    return hash & (entries - 1);
}

/* Looks a handle up for a channel. The search starts at the hash slot
   and goes on through the table: the resman puts an entry in the first
   free slot from there. Returns the context word or 0. */
static uint32_t
nv4_ramht_lookup(nv4_t *nv4, uint32_t handle, int chid)
{
    nv4_pfifo_t *p       = &nv4->pfifo;
    uint32_t     entries = nv4_ramht_entries(p);
    uint32_t     base    = nv4_ramht_base(p);
    uint32_t     slot    = nv4_ramht_hash(p, handle, chid);

    for (uint32_t i = 0; i < entries; i++, slot = (slot + 1) & (entries - 1)) {
        uint32_t h   = nv4_ramin_read32(nv4, base + slot * 8);
        uint32_t ctx = nv4_ramin_read32(nv4, base + slot * 8 + 4);

        if ((h == handle) && (ctx & 0x80000000) && (((ctx >> 24) & 0x0f) == (uint32_t) chid))
            return ctx;
    }
    return 0;
}

/* For "dev ramht": every entry holding <handle>, any channel, with its
   object's first instance word. */
void
nv4_pfifo_ramht_dump(nv4_t *nv4, uint32_t handle)
{
    nv4_pfifo_t *p       = &nv4->pfifo;
    uint32_t     entries = nv4_ramht_entries(p);
    uint32_t     base    = nv4_ramht_base(p);

    for (uint32_t slot = 0; slot < entries; slot++) {
        uint32_t ctx = nv4_ramin_read32(nv4, base + slot * 8 + 4);

        if (nv4_ramin_read32(nv4, base + slot * 8) == handle)
            pclog("nv4: ramht %08x slot %u ctx %08x: ch %u engine %u inst %04x, word0 %08x%s\n", handle, slot, ctx,
                  (ctx >> 24) & 0x0f, (ctx >> 16) & 3, ctx & 0xffff, nv4_ramin_read32(nv4, (ctx & 0xffff) << 4),
                  (ctx & 0x80000000) ? "" : " (invalid)");
    }
}

/* ------------------------------------------------------------------ */
/* Interrupts and RAMRO.                                               */
/* ------------------------------------------------------------------ */
uint32_t
nv4_pfifo_pending(nv4_t *nv4)
{
    return nv4->pfifo.intr & nv4->pfifo.intr_en;
}

static void
nv4_pfifo_intr(nv4_t *nv4, uint32_t bits)
{
    nv4->pfifo.intr |= bits;
    nv4_pmc_update_irq(nv4);
}

static int
nv4_runout_empty(const nv4_pfifo_t *p)
{
    return p->runout_put == p->runout_get;
}

/* An access that could not get into CACHE1 goes to RAMRO. */
static void
nv4_runout(nv4_t *nv4, int chid, uint32_t addr, uint32_t data, int reason)
{
    nv4_pfifo_t *p    = &nv4->pfifo;
    uint32_t     mask = nv4_ramro_mask(p);
    uint32_t     next = (p->runout_put + 8) & mask;

    nv4_log("NV4 PFIFO: runout ch%d %04x = %08x reason %d\n", chid, addr, data, reason);
    if (next == p->runout_get) {
        nv4_pfifo_intr(nv4, INTR_RUNOUT_OVF);
        return;
    }
    nv4_ramin_write32(nv4, nv4_ramro_base(p) + p->runout_put,
                      (addr & 0xfffc) | ((uint32_t) chid << 16) | (0xfu << 24) | ((uint32_t) reason << 28));
    nv4_ramin_write32(nv4, nv4_ramro_base(p) + p->runout_put + 4, data);
    p->runout_put = next;
    nv4_pfifo_intr(nv4, INTR_RUNOUT);
}

/* ------------------------------------------------------------------ */
/* CACHE1 and channel switching.                                       */
/* ------------------------------------------------------------------ */
static uint32_t
nv4_cache1_count(const nv4_pfifo_t *p)
{
    return (p->c1_put - p->c1_get) & (NV4_CACHE1_SIZE - 1);
}

static int
nv4_cache1_full(const nv4_pfifo_t *p)
{
    return nv4_cache1_count(p) == (NV4_CACHE1_SIZE - 1);
}

static void
nv4_cache1_push(nv4_pfifo_t *p, uint32_t method, uint32_t data)
{
    p->c1_method[p->c1_put] = method;
    p->c1_data[p->c1_put]   = data;
    p->c1_put               = (p->c1_put + 1) & (NV4_CACHE1_SIZE - 1);
}

/* RAMFC: 32 bytes per channel. */
static void
nv4_ramfc_save(nv4_t *nv4, int chid)
{
    nv4_pfifo_t *p = &nv4->pfifo;
    uint32_t     a = nv4_ramfc_base(p) + chid * 32;

    nv4_ramin_write32(nv4, a + 0x00, p->c1_dma_put);
    nv4_ramin_write32(nv4, a + 0x04, p->c1_dma_get);
    nv4_ramin_write32(nv4, a + 0x08, p->c1_dma_instance);
    nv4_ramin_write32(nv4, a + 0x0c, p->c1_dma_state);
    nv4_ramin_write32(nv4, a + 0x10, p->c1_dma_fetch);
    nv4_ramin_write32(nv4, a + 0x14, p->c1_engine);
    nv4_ramin_write32(nv4, a + 0x18, p->c1_pull1);
}

static void
nv4_ramfc_load(nv4_t *nv4, int chid)
{
    nv4_pfifo_t *p = &nv4->pfifo;
    uint32_t     a = nv4_ramfc_base(p) + chid * 32;

    p->c1_dma_put      = nv4_ramin_read32(nv4, a + 0x00) & 0x1ffffffc;
    p->c1_dma_get      = nv4_ramin_read32(nv4, a + 0x04) & 0x1ffffffc;
    p->c1_dma_instance = nv4_ramin_read32(nv4, a + 0x08) & 0xffff;
    p->c1_dma_state    = nv4_ramin_read32(nv4, a + 0x0c);
    p->c1_dma_fetch    = nv4_ramin_read32(nv4, a + 0x10) & 0x000fe0f8;
    p->c1_engine       = nv4_ramin_read32(nv4, a + 0x14) & 0x33333333;
    p->c1_pull1        = nv4_ramin_read32(nv4, a + 0x18) & 3;
}

/* A channel that is not idle while another owns CACHE1 is flagged in
   PFIFO_DMA so it gets its turn. */
static void
nv4_pfifo_switch(nv4_t *nv4, int chid)
{
    nv4_pfifo_t *p   = &nv4->pfifo;
    int          old = CHID(p);

    nv4_ramfc_save(nv4, old);
    if ((p->mode & (1u << old)) && (p->c1_dma_put != p->c1_dma_get))
        p->dma |= 1u << old;
    nv4_ramfc_load(nv4, chid);
    p->c1_push1 = chid | ((p->mode & (1u << chid)) ? PUSH1_MODE_DMA : 0);
    /* the pusher runs for a DMA channel, as after the driver's own switch */
    if (p->mode & (1u << chid))
        p->c1_dma_push = (p->c1_dma_push & ~DMA_PUSH_SUSPENDED) | DMA_PUSH_ACCESS;
    else
        p->c1_dma_push &= ~DMA_PUSH_ACCESS;
    p->dma &= ~(1u << chid);
    nv4_log("NV4 PFIFO: channel switch %d -> %d\n", old, chid);
    if (NV4_TRACE(nv4, chid) || NV4_TRACE(nv4, old))
        pclog("nv4 trace: channel switch %d -> %d, put %08x get %08x\n", old, chid, p->c1_dma_put, p->c1_dma_get);
}

/* CACHE1 can change hands: reassign is on, nothing is queued and the
   pusher is not in the middle of a command. */
static int
nv4_pfifo_can_switch(const nv4_pfifo_t *p)
{
    return (p->caches & 1) && !nv4_cache1_count(p)
        && !((p->c1_dma_state >> 18) & 0x7ff);
}

/* ------------------------------------------------------------------ */
/* The puller.                                                         */
/* ------------------------------------------------------------------ */
static int
nv4_engine_of(uint32_t engines, int subc)
{
    return (engines >> (subc * 4)) & 3;
}

/* Delivers one method to its engine. Returns 1 if consumed, 0 if the
   puller has to stop (an error the driver must see, or a held engine). */
static int
nv4_pull_one(nv4_t *nv4, int chid, uint32_t *engines, uint32_t *pull0, uint32_t *pull1, uint32_t *hash,
             uint32_t method, uint32_t data)
{
    int      subc = (method >> 13) & 7;
    uint32_t mthd = method & 0x1ffc;
    int      engine;
    uint32_t ctx;

    *pull0 &= ~(PULL0_HASH_FAILED | PULL0_DEVICE_SW);

    if (mthd == 0) {
        ctx = nv4_ramht_lookup(nv4, data, chid);
        if (!ctx) {
            *pull0 |= PULL0_HASH_FAILED;
            return 0;
        }
        engine = (ctx >> 16) & 3;
        *engines = (*engines & ~(3u << (subc * 4))) | ((uint32_t) engine << (subc * 4));
        *pull1   = engine;
        *hash    = (ctx & 0xffff) | 0x10000;
        if (engine == NV4_ENGINE_SW) {
            *pull0 |= PULL0_DEVICE_SW;
            return 0;
        }
        data = ctx & 0xffff;
    } else {
        engine = nv4_engine_of(*engines, subc);
        *pull1 = engine;
        if ((mthd >= 0x180) && (mthd < 0x200)) {
            /* object handles: the engine gets the instance */
            ctx = nv4_ramht_lookup(nv4, data, chid);
            if (!ctx && data) {
                *pull0 |= PULL0_HASH_FAILED;
                return 0;
            }
            *hash = (ctx & 0xffff) | (ctx ? 0x10000 : 0);
            data  = ctx & 0xffff;
        }
        if (engine == NV4_ENGINE_SW) {
            *pull0 |= PULL0_DEVICE_SW;
            return 0;
        }
    }

    /* The DVD engine is the graphics engine's DVD subpicture class. */
    return nv4_pgraph_method(nv4, chid, subc, mthd, data) == NV4_MTHD_DONE;
}

static void nv4_eng_charge(nv4_t *nv4);
static int  nv4_eng_ready(nv4_t *nv4);

static int
nv4_cache1_pull(nv4_t *nv4)
{
    nv4_pfifo_t *p    = &nv4->pfifo;
    int          work = 0;

    while ((p->c1_pull0 & PULL0_ACCESS) && nv4_cache1_count(p) && !(p->intr & INTR_CACHE_ERROR)
           && nv4_eng_ready(nv4)) {
        int done = nv4_pull_one(nv4, CHID(p), &p->c1_engine, &p->c1_pull0, &p->c1_pull1, &p->c1_hash,
                                p->c1_method[p->c1_get], p->c1_data[p->c1_get]);

        nv4_eng_charge(nv4);
        if (!done) {
            if (p->c1_pull0 & (PULL0_HASH_FAILED | PULL0_DEVICE_SW))
                nv4_pfifo_intr(nv4, INTR_CACHE_ERROR);
            break;
        }
        p->c1_get = (p->c1_get + 1) & (NV4_CACHE1_SIZE - 1);
        work      = 1;
    }
    return work;
}

/* CACHE0 holds a single method the driver resends. */
static int
nv4_cache0_pull(nv4_t *nv4)
{
    nv4_pfifo_t *p = &nv4->pfifo;

    if (!(p->c0_pull0 & PULL0_ACCESS) || (p->c0_put == p->c0_get))
        return 0;
    if (!nv4_pull_one(nv4, p->c0_push1 & 0x0f, &p->c0_engine, &p->c0_pull0, &p->c0_pull1, &p->c0_hash,
                      p->c0_method, p->c0_data))
        return 0;
    p->c0_get = p->c0_put;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The DMA pusher.                                                     */
/* ------------------------------------------------------------------ */
static void
nv4_dma_error(nv4_t *nv4, int err)
{
    nv4_pfifo_t *p = &nv4->pfifo;

    nv4_log("NV4 PFIFO: DMA pusher error %d at get %08x\n", err, p->c1_dma_get);
    p->c1_dma_state = (p->c1_dma_state & 0x3fffffff) | ((uint32_t) err << 30);
    p->c1_dma_push |= DMA_PUSH_SUSPENDED;
    nv4_pfifo_intr(nv4, INTR_DMA_PUSHER);
}

static int
nv4_dma_can_push(const nv4_pfifo_t *p)
{
    return (p->c1_push1 & PUSH1_MODE_DMA) && (p->c1_push0 & 1) && (p->c1_dma_push & DMA_PUSH_ACCESS)
        && !(p->c1_dma_push & DMA_PUSH_SUSPENDED) && (p->c1_dma_get != p->c1_dma_put);
}

/* Reads commands until the pushbuffer is drained, CACHE1 is full or an
   error suspends the pusher. Returns whether it queued anything. */
static int
nv4_dma_push(nv4_t *nv4)
{
    nv4_pfifo_t *p    = &nv4->pfifo;
    int          work = 0;

    while (nv4_dma_can_push(p) && !nv4_cache1_full(p)) {
        uint32_t word;
        uint32_t count = (p->c1_dma_state >> 18) & 0x7ff;

        if (nv4_dma_read32(nv4, p->c1_dma_instance, p->c1_dma_get, &word) != NV4_DMA_OK) {
            nv4_dma_error(nv4, DMA_ERR_PROTECTION);
            nv4_pfifo_intr(nv4, INTR_DMA_PT);
            break;
        }
        if (!nv4->dbg_trace_novtx && NV4_TRACE(nv4, CHID(p)))
            pclog("nv4 trace: push ch %d get %08x word %08x\n", CHID(p), p->c1_dma_get, word);
        p->c1_dma_get = (p->c1_dma_get + 4) & 0x1ffffffc;

        if (count) {
            /* a data word of the current method command */
            uint32_t mthd = p->c1_dma_state & 0x1ffc;
            uint32_t subc = (p->c1_dma_state >> 13) & 7;

            p->c1_dma_data_shadow = word;
            nv4_cache1_push(p, mthd | (subc << 13), word);
            mthd                  = (mthd + 4) & 0x1ffc;
            count--;
            p->c1_dma_state = (p->c1_dma_state & ~0x1ffcfffcu) | mthd | (subc << 13) | (count << 18);
            p->c1_dma_dcount += 4;
            work = 1;
            continue;
        }

        p->c1_dma_rsvd_shadow = word;
        switch (word >> 29) {
            case 0:
                /* method header: count 28:18, subchannel 15:13,
                   method 12:2; bits 17:16 and 1:0 must be clear */
                if (word & 0x00030003) {
                    nv4_dma_error(nv4, DMA_ERR_RESERVED_CMD);
                    return work;
                }
                p->c1_dma_state  = word & 0x1ffcfffc;
                p->c1_dma_dcount = 0;
                break;
            case 1:
                /* jump */
                p->c1_dma_jmp_shadow = p->c1_dma_get - 4;
                p->c1_dma_get        = word & 0x1ffffffc;
                break;
            default:
                nv4_dma_error(nv4, DMA_ERR_RESERVED_CMD);
                return work;
        }
    }
    return work;
}

/* ------------------------------------------------------------------ */
/* Engine time.                                                        */
/* ------------------------------------------------------------------ */
/* A method runs as soon as the puller takes it, and its cost in guest
   time then keeps the engine busy: the next one waits for it. So
   notifiers, DMA GET, the CACHE1 free count, PGRAPH_STATUS and the
   flip counter the D3D driver writes after each no-vsync flip blit
   lag as on the chip, and the drivers' own waits pace the guest (3D
   flips run up to five frames ahead, then wait for that counter).
   Rates: the 2D engine moves 16 bytes per memory clock over the
   128-bit framebuffer interface, the 3D pipeline draws two pixels per
   clock (one with two textures) and is held to the same memory rate;
   every method takes at least one clock. */
#define NV4_ENG_SLACK_US 20.0 /* run ahead this far before deferring */

static void
nv4_eng_charge(nv4_t *nv4)
{
    uint64_t now = tsc;
    double   mem = (nv4->mclk_hz > 0.0) ? (double) nv4->eng_bytes / (16.0 * nv4->mclk_hz) : 0.0;
    double   px  = (nv4->nvclk_hz > 0.0) ? (double) nv4->eng_px3d / (2.0 * nv4->nvclk_hz) : 0.0;
    double   sec = (nv4->nvclk_hz > 0.0) ? 1.0 / nv4->nvclk_hz : 0.0;

    sec += (mem > px) ? mem : px;
    nv4->eng_bytes = 0;
    nv4->eng_px3d  = 0;
    if (cpuclock <= 0.0)
        return;
    if (nv4->eng_tsc < now)
        nv4->eng_tsc = now;
    nv4->eng_tsc += (uint64_t) (sec * cpuclock);
}

/* Whether PGRAPH is still busy with the work it was given. */
int
nv4_pfifo_engine_busy(nv4_t *nv4)
{
    uint64_t now = tsc;

    if (nv4->eng_tsc <= now)
        return 0;
    if ((double) (nv4->eng_tsc - now) > 2.0 * cpuclock) {
        /* the TSC was reset (a speed change) */
        nv4->eng_tsc = now;
        return 0;
    }
    return 1;
}

/* Whether the puller may hand PGRAPH another method now; if not, the
   engine timer resumes it when the engine is done. */
static int
nv4_eng_ready(nv4_t *nv4)
{
    uint64_t now   = tsc;
    uint64_t slack = (uint64_t) (cpuclock * NV4_ENG_SLACK_US / 1000000.0);

    if (!nv4_pfifo_engine_busy(nv4) || (nv4->eng_tsc <= now + slack))
        return 1;
    if (!timer_is_on(&nv4->eng_timer))
        timer_set_delay_u64(&nv4->eng_timer, (nv4->eng_tsc - now) << 32);
    return 0;
}

static void
nv4_eng_timer(void *priv)
{
    nv4_pfifo_run((nv4_t *) priv);
}

void
nv4_pfifo_init(nv4_t *nv4)
{
    timer_add(&nv4->eng_timer, nv4_eng_timer, nv4, 0);
}

void
nv4_pfifo_close(nv4_t *nv4)
{
    timer_disable(&nv4->eng_timer);
}

/* ------------------------------------------------------------------ */
/* The run loop.                                                       */
/* ------------------------------------------------------------------ */
void
nv4_pfifo_run(nv4_t *nv4)
{
    nv4_pfifo_t *p = &nv4->pfifo;
    int          work;

    if (p->busy)
        return;
    p->busy = 1;

    do {
        work = nv4_cache0_pull(nv4);
        work |= nv4_cache1_pull(nv4);
        work |= nv4_dma_push(nv4);

        /* Hand CACHE1 to a DMA channel with work queued once the current
           one is idle. */
        if (nv4_pfifo_can_switch(p) && p->dma
            && (!(p->c1_push1 & PUSH1_MODE_DMA) || (p->c1_dma_get == p->c1_dma_put))) {
            for (int i = 1; i <= NV4_CHANNELS; i++) {
                int ch = (CHID(p) + i) % NV4_CHANNELS;
                if (p->dma & (1u << ch)) {
                    if (ch == CHID(p))
                        p->dma &= ~(1u << ch);
                    else {
                        nv4_pfifo_switch(nv4, ch);
                        work = 1;
                    }
                    break;
                }
            }
        }
    } while (work);

    /* The 3D batch is not drawn when the FIFO runs dry: the driver
       moves PUT every few triangles, and nothing can see the pixels
       until a sync point (nv4_pgraph_3d.c) draws them. */
    p->busy = 0;
}

/* ------------------------------------------------------------------ */
/* The channel control areas.                                          */
/* ------------------------------------------------------------------ */
uint32_t
nv4_user_read(nv4_t *nv4, uint32_t addr)
{
    nv4_pfifo_t *p    = &nv4->pfifo;
    int          chid = (addr >> 16) & 0x0f;
    uint32_t     off  = addr & 0x1fff;

    nv4->dbg_user[chid].reads++;
    if (p->mode & (1u << chid)) {
        uint32_t a = nv4_ramfc_base(p) + chid * 32;
        if (off == 0x40)
            return (chid == CHID(p)) ? p->c1_dma_put : nv4_ramin_read32(nv4, a);
        if (off == 0x44)
            return (chid == CHID(p)) ? p->c1_dma_get : nv4_ramin_read32(nv4, a + 4);
        return 0;
    }

    /* PIO: the free space in bytes at 0x10, zeros after it */
    if (off == 0x10) {
        if ((chid != CHID(p)) && !nv4_pfifo_can_switch(p))
            return 0;
        return (NV4_CACHE1_SIZE - 1 - ((chid == CHID(p)) ? nv4_cache1_count(p) : 0)) << 2;
    }
    return 0;
}

void
nv4_user_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    nv4_pfifo_t *p    = &nv4->pfifo;
    int          chid = (addr >> 16) & 0x0f;
    int          subc = (addr >> 13) & 7;
    uint32_t     off  = addr & 0x1fff;

    nv4->dbg_user[chid].writes++;
    if (!nv4->dbg_trace_novtx && NV4_TRACE(nv4, chid))
        pclog("nv4 trace: user ch %d %04x = %08x (cache1 ch %d) from %08x\n", chid, (subc << 13) | off, val, CHID(p),
              cs + cpu_state.pc);
    nv4->dbg_user[chid].last_off = (subc << 13) | off;
    nv4->dbg_user[chid].last_val = val;
    if (p->mode & (1u << chid)) {
        if (off == 0x40) {
            /* DMA_PUT */
            if (chid == CHID(p))
                p->c1_dma_put = val & 0x1ffffffc;
            else {
                nv4_ramin_write32(nv4, nv4_ramfc_base(p) + chid * 32, val & 0x1ffffffc);
                p->dma |= 1u << chid;
            }
            nv4_pfifo_run(nv4);
        } else
            nv4_runout(nv4, chid, (subc << 13) | off, val, RO_ILLEGAL_ACCESS);
        return;
    }

    /* PIO method write */
    if (chid != CHID(p)) {
        if (!(p->c1_push0 & 1) || !nv4_pfifo_can_switch(p)) {
            nv4_runout(nv4, chid, (subc << 13) | off, val, RO_NO_CACHE);
            return;
        }
        nv4_pfifo_switch(nv4, chid);
    }
    if (!(p->c1_push0 & 1)) {
        nv4_runout(nv4, chid, (subc << 13) | off, val, RO_NO_CACHE);
        return;
    }
    if (nv4_cache1_full(p)) {
        nv4_runout(nv4, chid, (subc << 13) | off, val, RO_CACHE_RAN_OUT);
        return;
    }
    nv4_cache1_push(p, (subc << 13) | off, val);
    nv4_pfifo_run(nv4);
}

/* ------------------------------------------------------------------ */
/* Registers.                                                          */
/* ------------------------------------------------------------------ */
void
nv4_pfifo_reset(nv4_t *nv4)
{
    nv4_pfifo_t *p = &nv4->pfifo;

    memset(p, 0, sizeof(*p));
    nv4->eng_tsc   = 0;
    nv4->eng_bytes = 0;
    nv4->eng_px3d  = 0;
    p->ramht        = 0x00000100; /* 0x10000, 4 KB */
    p->ramfc        = 0x00000110; /* 0x11000 */
    p->ramro        = 0x00000112; /* 0x11200, 512 bytes */
    p->dma_timeslice = 0x01000000;
    p->pio_timeslice = 0x01000000;
    p->timeslice    = 0x0003ffff;
    p->c1_dma_fetch = (0x0f << 3) | (3 << 13);
    p->c1_dma_push  = DMA_PUSH_EMPTY;
}

uint32_t
nv4_pfifo_read(nv4_t *nv4, uint32_t addr)
{
    nv4_pfifo_t *p = &nv4->pfifo;

    if ((addr >= 0x003800) && (addr < 0x004000)) {
        /* CACHE1 METHOD/DATA, and their alias at 0x3c00 */
        uint32_t i = ((addr & 0x3ff) >> 3) & (NV4_CACHE1_SIZE - 1);
        return (addr & 4) ? p->c1_data[i] : p->c1_method[i];
    }
    if ((addr >= 0x002800) && (addr < 0x002900))
        return CHID(p); /* PFIFO_DEVICE: the channel each engine is on */

    switch (addr) {
        case PFIFO_DELAY_0:
            return p->delay_0;
        case PFIFO_DMA_TIMESLICE:
            return p->dma_timeslice;
        case PFIFO_PIO_TIMESLICE:
            return p->pio_timeslice;
        case PFIFO_TIMESLICE:
            return p->timeslice;
        case PFIFO_NEXT_CHANNEL:
            return p->next_channel;
        case PFIFO_DEBUG_0:
            return 0;
        case PFIFO_INTR_0:
            return p->intr;
        case PFIFO_INTR_EN_0:
            return p->intr_en;
        case PFIFO_RAMHT:
            return p->ramht;
        case PFIFO_RAMFC:
            return p->ramfc;
        case PFIFO_RAMRO:
            return p->ramro;
        case PFIFO_RUNOUT_STATUS:
            return (nv4_runout_empty(p) ? 0x10 : 0x01)
                 | ((((p->runout_put + 8) & nv4_ramro_mask(p)) == p->runout_get) ? 0x100 : 0);
        case PFIFO_RUNOUT_PUT:
            return p->runout_put;
        case PFIFO_RUNOUT_GET:
            return p->runout_get;
        case PFIFO_CACHES:
            return (p->caches & 1) | ((p->c1_dma_push & DMA_PUSH_BUSY) ? 0x10 : 0);
        case PFIFO_MODE:
            return p->mode;
        case PFIFO_DMA:
            return p->dma;
        case PFIFO_SIZE:
            return p->size;

        case PFIFO_CACHE0_PUSH0:
            return p->c0_push0;
        case PFIFO_CACHE0_PUSH1:
            return p->c0_push1;
        case PFIFO_CACHE0_PUT:
            return p->c0_put;
        case PFIFO_CACHE0_STATUS:
            return (p->c0_put == p->c0_get) ? 0x10 : 0x100;
        case PFIFO_CACHE0_PULL0:
            return p->c0_pull0;
        case PFIFO_CACHE0_PULL1:
            return p->c0_pull1;
        case PFIFO_CACHE0_HASH:
            return p->c0_hash;
        case PFIFO_CACHE0_GET:
            return p->c0_get;
        case PFIFO_CACHE0_ENGINE:
            return p->c0_engine;
        case PFIFO_CACHE0_METHOD:
            return p->c0_method;
        case PFIFO_CACHE0_DATA:
            return p->c0_data;

        case PFIFO_CACHE1_PUSH0:
            return p->c1_push0;
        case PFIFO_CACHE1_PUSH1:
            return p->c1_push1;
        case PFIFO_CACHE1_PUT:
            return p->c1_put << 2;
        case PFIFO_CACHE1_STATUS:
            return (nv4_cache1_count(p) ? 0 : 0x10) | (nv4_cache1_full(p) ? 0x100 : 0);
        case PFIFO_CACHE1_STATUS1:
            return 0;
        case PFIFO_CACHE1_DMA_PUSH:
            return (p->c1_dma_push & ~DMA_PUSH_EMPTY)
                 | ((p->c1_dma_get == p->c1_dma_put) ? DMA_PUSH_EMPTY : 0);
        case PFIFO_CACHE1_DMA_FETCH:
            return p->c1_dma_fetch;
        case PFIFO_CACHE1_DMA_STATE:
            return p->c1_dma_state;
        case PFIFO_CACHE1_DMA_INSTANCE:
            return p->c1_dma_instance;
        case PFIFO_CACHE1_DMA_CTL:
            return p->c1_dma_ctl;
        case PFIFO_CACHE1_DMA_LIMIT:
            return p->c1_dma_limit;
        case PFIFO_CACHE1_DMA_TLB_TAG:
            return p->c1_dma_tlb_tag;
        case PFIFO_CACHE1_DMA_TLB_PTE:
            return p->c1_dma_tlb_pte;
        case PFIFO_CACHE1_DMA_PUT:
            return p->c1_dma_put;
        case PFIFO_CACHE1_DMA_GET:
            return p->c1_dma_get;
        case PFIFO_CACHE1_PULL0:
            return p->c1_pull0;
        case PFIFO_CACHE1_PULL1:
            return p->c1_pull1;
        case PFIFO_CACHE1_HASH:
            return p->c1_hash;
        case PFIFO_CACHE1_GET:
            return p->c1_get << 2;
        case PFIFO_CACHE1_ENGINE:
            return p->c1_engine;
        case PFIFO_CACHE1_DMA_DCOUNT:
            return p->c1_dma_dcount;
        case PFIFO_CACHE1_DMA_GET_JMP:
            return p->c1_dma_jmp_shadow;
        case PFIFO_CACHE1_DMA_RSVD:
            return p->c1_dma_rsvd_shadow;
        case PFIFO_CACHE1_DMA_DATA:
            return p->c1_dma_data_shadow;
        default:
            nv4_log("NV4 PFIFO: read %06x\n", addr);
            return 0;
    }
}

void
nv4_pfifo_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    nv4_pfifo_t *p = &nv4->pfifo;

    if ((addr >= 0x003800) && (addr < 0x004000)) {
        uint32_t i = ((addr & 0x3ff) >> 3) & (NV4_CACHE1_SIZE - 1);
        if (addr & 4)
            p->c1_data[i] = val;
        else
            p->c1_method[i] = val & 0xfffc;
        return;
    }

    switch (addr) {
        case PFIFO_DELAY_0:
            p->delay_0 = val & 0x3ff;
            break;
        case PFIFO_DMA_TIMESLICE:
            p->dma_timeslice = val & 0x0101ffff;
            break;
        case PFIFO_PIO_TIMESLICE:
            p->pio_timeslice = val & 0x0101ffff;
            break;
        case PFIFO_TIMESLICE:
            p->timeslice = val & 0x3ffff;
            break;
        case PFIFO_NEXT_CHANNEL:
            p->next_channel = val & 0x110f;
            break;
        case PFIFO_INTR_0:
            p->intr &= ~val;
            nv4_pmc_update_irq(nv4);
            break;
        case PFIFO_INTR_EN_0:
            p->intr_en = val & 0x11111;
            nv4_pmc_update_irq(nv4);
            break;
        case PFIFO_RAMHT:
            p->ramht = val & 0x030301f0;
            break;
        case PFIFO_RAMFC:
            p->ramfc = val & 0x1fe;
            break;
        case PFIFO_RAMRO:
            p->ramro = val & 0x101fe;
            break;
        case PFIFO_RUNOUT_PUT:
            p->runout_put = val & nv4_ramro_mask(p);
            break;
        case PFIFO_RUNOUT_GET:
            p->runout_get = val & nv4_ramro_mask(p);
            break;
        case PFIFO_CACHES:
            p->caches = val & 1;
            break;
        case PFIFO_MODE:
            p->mode = val & 0xffff;
            break;
        case PFIFO_DMA:
            p->dma = val & 0xffff;
            break;
        case PFIFO_SIZE:
            p->size = val & 0xffff;
            break;

        case PFIFO_CACHE0_PUSH0:
            p->c0_push0 = val & 1;
            break;
        case PFIFO_CACHE0_PUSH1:
            p->c0_push1 = val & 0x0f;
            break;
        case PFIFO_CACHE0_PUT:
            p->c0_put = val & 4;
            break;
        case PFIFO_CACHE0_PULL0:
            p->c0_pull0 = (p->c0_pull0 & ~PULL0_ACCESS) | (val & PULL0_ACCESS);
            break;
        case PFIFO_CACHE0_PULL1:
            p->c0_pull1 = val & 3;
            break;
        case PFIFO_CACHE0_HASH:
            p->c0_hash = val & 0x1ffff;
            break;
        case PFIFO_CACHE0_GET:
            p->c0_get = val & 4;
            break;
        case PFIFO_CACHE0_ENGINE:
            p->c0_engine = val & 0x33333333;
            break;
        case PFIFO_CACHE0_METHOD:
            p->c0_method = val & 0xfffc;
            break;
        case PFIFO_CACHE0_DATA:
            p->c0_data = val;
            break;

        case PFIFO_CACHE1_PUSH0:
            p->c1_push0 = val & 1;
            break;
        case PFIFO_CACHE1_PUSH1:
            p->c1_push1 = val & 0x10f;
            break;
        case PFIFO_CACHE1_PUT:
            p->c1_put = (val >> 2) & (NV4_CACHE1_SIZE - 1);
            break;
        case PFIFO_CACHE1_DMA_PUSH:
            p->c1_dma_push = val & (DMA_PUSH_ACCESS | DMA_PUSH_SUSPENDED);
            break;
        case PFIFO_CACHE1_DMA_FETCH:
            p->c1_dma_fetch = val & 0x000fe0f8;
            break;
        case PFIFO_CACHE1_DMA_STATE:
            p->c1_dma_state = val & 0xdffcfffc;
            break;
        case PFIFO_CACHE1_DMA_INSTANCE:
            p->c1_dma_instance = val & 0xffff;
            break;
        case PFIFO_CACHE1_DMA_CTL:
            p->c1_dma_ctl = val & 0x80033ffc;
            /* Clearing AT_INFO (bit 31) makes the pusher fetch the push
               buffer's DMA object again: adjust, page table and target
               into CTL, its limit into LIMIT, the TLB invalidated. The
               fetch is immediate here; the resman spins until bit 31 is
               set again after a channel switch. */
            if (!(val & 0x80000000)) {
                uint32_t w0 = nv4_ramin_read32(nv4, p->c1_dma_instance << 4);

                p->c1_dma_ctl     = 0x80000000 | ((w0 >> 20) & 0xffc) | (w0 & 0x33000);
                p->c1_dma_limit   = nv4_ramin_read32(nv4, (p->c1_dma_instance << 4) + 4) & 0x1ffffffc;
                p->c1_dma_tlb_tag &= ~1u;
            }
            break;
        case PFIFO_CACHE1_DMA_LIMIT:
            p->c1_dma_limit = val & 0x1ffffffc;
            break;
        case PFIFO_CACHE1_DMA_TLB_TAG:
            p->c1_dma_tlb_tag = val & 0x1ffff001;
            break;
        case PFIFO_CACHE1_DMA_TLB_PTE:
            p->c1_dma_tlb_pte = val & 0xfffff003;
            break;
        case PFIFO_CACHE1_DMA_PUT:
            p->c1_dma_put = val & 0x1ffffffc;
            break;
        case PFIFO_CACHE1_DMA_GET:
            p->c1_dma_get = val & 0x1ffffffc;
            break;
        case PFIFO_CACHE1_PULL0:
            /* the failure flags are the last pull's and stay until the
               next one: the driver disables the puller before it reads
               them */
            p->c1_pull0 = (p->c1_pull0 & ~PULL0_ACCESS) | (val & PULL0_ACCESS);
            break;
        case PFIFO_CACHE1_PULL1:
            p->c1_pull1 = val & 3;
            break;
        case PFIFO_CACHE1_HASH:
            p->c1_hash = val & 0x1ffff;
            break;
        case PFIFO_CACHE1_GET:
            p->c1_get = (val >> 2) & (NV4_CACHE1_SIZE - 1);
            break;
        case PFIFO_CACHE1_ENGINE:
            p->c1_engine = val & 0x33333333;
            break;
        case PFIFO_CACHE1_DMA_DCOUNT:
            p->c1_dma_dcount = val & 0x1ffc;
            break;
        case PFIFO_CACHE1_DMA_GET_JMP:
            p->c1_dma_jmp_shadow = val & 0x1ffffffc;
            break;
        case PFIFO_CACHE1_DMA_RSVD:
            p->c1_dma_rsvd_shadow = val;
            break;
        case PFIFO_CACHE1_DMA_DATA:
            p->c1_dma_data_shadow = val;
            break;
        default:
            nv4_log("NV4 PFIFO: write %06x = %08x\n", addr, val);
            break;
    }

    /* Anything may have unblocked the pusher or the pullers. */
    nv4_pfifo_run(nv4);
}
