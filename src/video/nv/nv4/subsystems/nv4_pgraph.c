/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PGRAPH, the graphics engine.
 *
 *          This file is the engine's front end: its registers, the
 *          channel it serves, the objects bound to its subchannels, and
 *          the traps it hands the driver. The classes themselves are in
 *          nv4_pgraph_2d.c.
 *
 *          The engine serves one channel at a time. A method from another
 *          channel raises CONTEXT_SWITCH and is held until the driver has
 *          swapped the register file and named the new channel in
 *          CTX_USER/CTX_CONTROL. The register file therefore holds all
 *          object state that must survive a switch.
 *
 *          Binding an object (method 0) loads its instance words into the
 *          subchannel's CTX_CACHE and, while that subchannel is current,
 *          into CTX_SWITCH; methods that change an object write its
 *          instance too. Methods the chip does not implement raise a
 *          NOTIFY interrupt with ILLEGAL_MTHD (the TNT leaves the
 *          set-context methods and SetOperation to the driver; the TNT2
 *          does them when DEBUG_3 says so) or MISSING_HW (software
 *          methods); either stops the engine's FIFO until the driver
 *          restarts it.
 *
 *          Method semantics follow envytools' hardware tests (hwtest,
 *          nvhw: MIT licensed, by the envytools contributors), which were
 *          checked against real NV4/NV5 chips.
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
#include <86box/plat_unused.h>
#include <86box/nv/vid_nv4.h>

#define GR(a) NV4_GR(nv4, a)

/* CTX_SWITCH1 bits the hardware keeps: class, chroma, clip, swizzle,
   operation, patch status and surface; the TNT2 adds dither and the
   pattern/rop/beta context bits. */
#define NV4_CTXC_MASK 0x0303f0ffu
#define NV5_CTXC_MASK 0x7f73f0ffu
#define NV4_CTX2_MASK 0xffff3f03u

static uint32_t
nv4_ctxc_mask(const nv4_t *nv4)
{
    return (nv4->chip == NV4_CHIP_TNT2) ? NV5_CTXC_MASK : NV4_CTXC_MASK;
}

uint32_t
nv4_pgraph_pending(nv4_t *nv4)
{
    return GR(NV4_PGRAPH_INTR) & GR(NV4_PGRAPH_INTR_EN);
}

static void
nv4_pgraph_intr(nv4_t *nv4, uint32_t bits)
{
    if (nv4->dbg_trace)
        pclog("nv4 trace: pgraph intr %08x (en %08x), fifo off, ctx_user %08x trapped %08x/%08x\n", bits,
              GR(NV4_PGRAPH_INTR_EN), GR(NV4_PGRAPH_CTX_USER), GR(NV4_PGRAPH_TRAPPED_ADDR), GR(NV4_PGRAPH_TRAPPED_DATA));
    GR(NV4_PGRAPH_INTR) |= bits;
    GR(NV4_PGRAPH_FIFO) &= ~1u;
    nv4_pmc_update_irq(nv4);
}

/* A NOTIFY interrupt for <nsource>, with NSTATUS summarising it. */
void
nv4_pgraph_blowup(nv4_t *nv4, uint32_t nsource)
{
    uint32_t nstatus = 0;

    if (nsource & 0x3000)
        nstatus |= 0x0800;
    if (nsource & 0xc4a00)
        nstatus |= 0x1000;
    if (nsource & 0x0002)
        nstatus |= 0x2000;
    if (nsource & 0x004c)
        nstatus |= 0x4000;
    GR(NV4_PGRAPH_NSOURCE) |= nsource;
    GR(NV4_PGRAPH_NSTATUS) |= nstatus;
    nv4_log("NV4 PGRAPH: trap, nsource %08x, class %02x, method %04x\n", nsource,
            GR(NV4_PGRAPH_CTX_SWITCH1) & 0xff, GR(NV4_PGRAPH_TRAPPED_ADDR) & 0x1ffc);
    nv4_pgraph_intr(nv4, NV4_PGRAPH_INTR_NOTIFY);
}

static int
nv4_pgraph_subc(nv4_t *nv4)
{
    return (GR(NV4_PGRAPH_CTX_USER) >> 13) & 7;
}

/* Sets bits <shift>+<bits>-1:<shift> of word <word> (0-2) of the current
   object: its instance, its subchannel's CTX_CACHE and CTX_SWITCH. */
void
nv4_pgraph_set_grobj(nv4_t *nv4, int word, int shift, int bits, uint32_t val)
{
    int      subc = nv4_pgraph_subc(nv4);
    uint32_t inst = GR(NV4_PGRAPH_CTX_SWITCH4) & 0xffff;
    uint32_t mask = ((bits >= 32) ? 0xffffffffu : ((1u << bits) - 1)) << shift;
    uint32_t sw   = GR(NV4_PGRAPH_CTX_SWITCH1 + word * 4);
    uint32_t w;

    val = (val << shift) & mask;
    if (inst) {
        w = nv4_ramin_read32(nv4, (inst << 4) + word * 4);
        nv4_ramin_write32(nv4, (inst << 4) + word * 4, (w & ~mask) | val);
    }
    sw = (sw & ~mask) | val;
    GR(NV4_PGRAPH_CTX_CACHE1 + word * 0x20 + subc * 4) = sw;
    if (GR(NV4_PGRAPH_DEBUG_1) & (1u << 20))
        GR(NV4_PGRAPH_CTX_SWITCH1 + word * 4) = sw;
}

/* Writes notifier <index> of the current object: the time, <info32> and
   a status of done. */
void
nv4_pgraph_notify_write(nv4_t *nv4, uint32_t index, uint32_t info32)
{
    uint32_t inst = GR(NV4_PGRAPH_CTX_SWITCH2) >> 16;
    uint32_t lo;
    uint32_t hi;

    if (!inst || (nv4_dma_class(nv4, inst) == 0x30))
        return;
    lo = nv4_ptimer_read(nv4, NV4_PTIMER_TIME_0);
    hi = nv4_ptimer_read(nv4, NV4_PTIMER_TIME_1);
    nv4_dma_write32(nv4, inst, index * 16 + 0, lo);
    nv4_dma_write32(nv4, inst, index * 16 + 4, hi);
    nv4_dma_write32(nv4, inst, index * 16 + 8, info32);
    nv4_dma_write32(nv4, inst, index * 16 + 12, 0);
}

/* A notify requested by NOTIFY fires once the next method is done. */
static void
nv4_pgraph_do_notify(nv4_t *nv4)
{
    uint32_t notify = GR(NV4_PGRAPH_NOTIFY);

    nv4_3d_flush(nv4);
    /* WRITE_THEN_AWAKEN is the resman's to do: the request stays pending
       with its style, which the interrupt handler reads to know it must
       wake the waiter (it writes the notifier and clears the request).
       Later resmans have no other record of the style. */
    if (notify & (1u << 20)) {
        nv4_pgraph_blowup(nv4, NV4_NSOURCE_NOTIFICATION);
        return;
    }
    GR(NV4_PGRAPH_NOTIFY) = notify & ~0x00110000u;
    nv4_pgraph_notify_write(nv4, 0, 0);
}

/* Loads the subchannel's cache from the object at <inst>. */
static void
nv4_pgraph_load_grobj(nv4_t *nv4, int subc, uint32_t inst)
{
    uint32_t base = inst << 4;

    GR(NV4_PGRAPH_CTX_CACHE1 + subc * 4) = nv4_ramin_read32(nv4, base) & nv4_ctxc_mask(nv4);
    GR(NV4_PGRAPH_CTX_CACHE2 + subc * 4) = nv4_ramin_read32(nv4, base + 4) & NV4_CTX2_MASK;
    GR(NV4_PGRAPH_CTX_CACHE3 + subc * 4) = nv4_ramin_read32(nv4, base + 8);
}

/* Something the guest asked for that is not modeled: logged the first
   time, counted always, listed by the debug hook's "dev". */
void
nv4_gap(nv4_t *nv4, int cls, uint32_t mthd, const char *what)
{
    int i;

    for (i = 0; i < nv4->dbg_gaps; i++)
        if ((nv4->dbg_gap[i].cls == cls) && (nv4->dbg_gap[i].mthd == mthd) && (nv4->dbg_gap[i].what == what)) {
            nv4->dbg_gap[i].count++;
            return;
        }
    if (i == (int) (sizeof(nv4->dbg_gap) / sizeof(nv4->dbg_gap[0])))
        return;
    nv4->dbg_gap[i].cls   = cls;
    nv4->dbg_gap[i].mthd  = mthd;
    nv4->dbg_gap[i].what  = what;
    nv4->dbg_gap[i].count = 1;
    nv4->dbg_gaps++;
    pclog("nv4: not modeled: class %02x method %04x: %s\n", cls, mthd, what);
}

/* Methods every class has. Returns -1 for a class method. */
static int
nv4_pgraph_common_method(nv4_t *nv4, uint32_t mthd, uint32_t data)
{
    switch (mthd) {
        case 0x100: /* NOP */
            return NV4_GR_OK;

        case 0x104: /* NOTIFY */
            if (GR(NV4_PGRAPH_NOTIFY) & (1u << 16)) {
                nv4_pgraph_blowup(nv4, NV4_NSOURCE_DOUBLE_NOTIFY);
                return NV4_GR_OK;
            }
            if (data < 2)
                GR(NV4_PGRAPH_NOTIFY) = (GR(NV4_PGRAPH_NOTIFY) & ~0x00110000u) | (1u << 16) | ((data & 1) << 20);
            return NV4_GR_OK;

        case 0x180: /* SetContextDmaNotifies */
            nv4_pgraph_set_grobj(nv4, 1, 16, 16, data);
            return NV4_GR_OK;

        default:
            return -1;
    }
}

static int
nv4_pgraph_state_only(int cls)
{
    switch (cls) {
        case 0x12: /* beta */
        case 0x72: /* beta4 */
        case 0x43: /* ROP */
        case 0x17: /* chroma */
        case 0x57:
        case 0x18: /* pattern */
        case 0x44:
        case 0x19: /* clip */
        case 0x42: /* surfaces */
        case 0x52:
        case 0x53:
        case 0x58:
        case 0x59:
        case 0x5a:
        case 0x5b:
        case 0x54: /* the 3D classes: their own batch */
        case 0x55:
            return 1;
        default:
            return 0;
    }
}

/* Classes whose only VRAM access is drawing into the destination
   surface through nv4_draw_begin: lines, triangles, rectangles, GDI,
   image from CPU (plain, stretched from the FIFO, indexed). Blits,
   scaled images from memory and M2MF read VRAM and sync up front. */
static int
nv4_pgraph_draws_only(int cls)
{
    switch (cls) {
        case 0x1c:
        case 0x5c:
        case 0x1d:
        case 0x5d:
        case 0x1e:
        case 0x5e:
        case 0x4a:
        case 0x4b:
        case 0x21:
        case 0x61:
        case 0x36:
        case 0x76:
        case 0x60:
        case 0x64:
        case 0x65:
        case 0x66:
            return 1;
        default:
            return 0;
    }
}

nv4_mthd_result_t
nv4_pgraph_method(nv4_t *nv4, int chid, int subc, uint32_t mthd, uint32_t data)
{
    uint32_t user;
    int      cls;
    int      r;

    /* Another channel's method: the driver swaps contexts first. The
       context switch interrupt is raised even with FIFO access off: the
       driver's handler loads the context and turns access back on, and
       after a PGRAPH reset that is how access comes back at all. */
    user = GR(NV4_PGRAPH_CTX_USER);
    if ((((user >> 24) & 0x0f) != (uint32_t) chid) || !(GR(NV4_PGRAPH_CTX_CONTROL) & (1u << 16))) {
        if (!(GR(NV4_PGRAPH_INTR) & NV4_PGRAPH_INTR_CONTEXT_SWITCH)) {
            GR(NV4_PGRAPH_TRAPPED_ADDR) = mthd | (subc << 13) | ((uint32_t) chid << 24);
            GR(NV4_PGRAPH_TRAPPED_DATA) = data;
            nv4_log("NV4 PGRAPH: channel switch %d -> %d\n", (user >> 24) & 0x0f, chid);
            nv4_pgraph_intr(nv4, NV4_PGRAPH_INTR_CONTEXT_SWITCH);
        }
        return NV4_MTHD_STALL;
    }

    if (!(GR(NV4_PGRAPH_FIFO) & 1))
        return NV4_MTHD_STALL;
    /* Nor while a trapped method waits for the driver: the TNT's resource
       manager turns FIFO access back on from other interrupt paths (its
       PFIFO handler) before it has read the trap, and the next method
       would overwrite TRAPPED_ADDR/DATA. The OpenGL ICD's swizzled-surface
       context method for its texture uploads was lost that way. */
    if (GR(NV4_PGRAPH_INTR) & (NV4_PGRAPH_INTR_NOTIFY | NV4_PGRAPH_INTR_BUFFER_NOTIFY | NV4_PGRAPH_INTR_MISSING_HW))
        return NV4_MTHD_STALL;

    GR(NV4_PGRAPH_TRAPPED_ADDR) = mthd | (subc << 13) | ((uint32_t) chid << 24);
    GR(NV4_PGRAPH_TRAPPED_DATA) = data;

    if ((mthd == 0) || (((user >> 13) & 7) != (uint32_t) subc)) {
        if (mthd == 0) {
            GR(NV4_PGRAPH_CTX_CACHE4 + subc * 4) = data & 0xffff;
            nv4_pgraph_load_grobj(nv4, subc, data & 0xffff);
            if (NV4_TRACE(nv4, chid))
                pclog("nv4 trace: ch %d sub %d bind inst %04x class %02x words %08x %08x %08x %08x\n", chid, subc,
                      data & 0xffff, GR(NV4_PGRAPH_CTX_CACHE1 + subc * 4) & 0xff, nv4_ramin_read32(nv4, (data & 0xffff) << 4),
                      nv4_ramin_read32(nv4, ((data & 0xffff) << 4) + 4), nv4_ramin_read32(nv4, ((data & 0xffff) << 4) + 8),
                      nv4_ramin_read32(nv4, ((data & 0xffff) << 4) + 12));
            nv4_pgraph_2d_reset_chan(&nv4->gr_chan[chid]);
        } else if (GR(NV4_PGRAPH_DEBUG_1) & (1u << 15))
            nv4_pgraph_load_grobj(nv4, subc, GR(NV4_PGRAPH_CTX_CACHE4 + subc * 4) & 0xffff);
        GR(NV4_PGRAPH_CTX_USER) = (user & ~0xe000u) | (subc << 13);
        if (GR(NV4_PGRAPH_DEBUG_1) & (1u << 20)) {
            GR(NV4_PGRAPH_CTX_SWITCH1) = GR(NV4_PGRAPH_CTX_CACHE1 + subc * 4);
            GR(NV4_PGRAPH_CTX_SWITCH2) = GR(NV4_PGRAPH_CTX_CACHE2 + subc * 4);
            GR(NV4_PGRAPH_CTX_SWITCH3) = GR(NV4_PGRAPH_CTX_CACHE3 + subc * 4);
            GR(NV4_PGRAPH_CTX_SWITCH4) = GR(NV4_PGRAPH_CTX_CACHE4 + subc * 4);
        }
    }
    if (mthd == 0)
        return NV4_MTHD_DONE;

    cls = GR(NV4_PGRAPH_CTX_SWITCH1) & 0xff;
    nv4->dbg_cls_count[cls]++;
    if (nv4->dbg_trace_novtx && ((cls == 0x54) || (cls == 0x55)) && (mthd >= 0x400) && (mthd < 0x600))
        ;
    else if (NV4_TRACE(nv4, chid) || (cls == nv4->dbg_log_cls)) {
        pclog("nv4 trace: ch %d sub %d class %02x mthd %04x data %08x\n", chid, subc, cls, mthd, data);
    }
    /* 2D pixels waiting to join the 3D batch go before this method */
    nv4_pgraph_2d_flush_run(nv4);
    /* a 3D batch is drawn before anything else touches memory; the
       classes that only hold state (surfaces, ROP, beta, chroma,
       pattern, clip) do not, and draw states are snapshots. The classes
       that only write their destination decide when they draw
       (nv4_draw_begin): join the batch or wait for what reaches it. */
    if (!nv4_pgraph_state_only(cls) && !nv4_pgraph_draws_only(cls))
        nv4_3d_sync(nv4, cls);
    r = nv4_pgraph_common_method(nv4, mthd, data);
    if (r < 0) {
        if ((cls == 0x54) || (cls == 0x55))
            r = nv4_pgraph_3d_method(nv4, chid, cls, mthd, data);
        else
            r = nv4_pgraph_2d_method(nv4, &nv4->gr_chan[chid], cls, mthd, data);
    }

    switch (r) {
        case NV4_GR_ILLEGAL:
            nv4_pgraph_blowup(nv4, NV4_NSOURCE_ILLEGAL_MTHD);
            break;
        case NV4_GR_MISSING:
            nv4_log("NV4 PGRAPH: software method, class %02x, method %04x, data %08x\n", cls, mthd, data);
            nv4_pgraph_intr(nv4, NV4_PGRAPH_INTR_MISSING_HW);
            break;
        default:
            if ((mthd != 0x104) && (GR(NV4_PGRAPH_NOTIFY) & (1u << 16)) && (GR(NV4_PGRAPH_FIFO) & 1))
                nv4_pgraph_do_notify(nv4);
            break;
    }
    return NV4_MTHD_DONE;
}

void
nv4_pgraph_reset(nv4_t *nv4)
{
    if (nv4->dbg_trace)
        pclog("nv4 trace: pgraph reset\n");
    memset(nv4->pgraph_regs, 0, sizeof(nv4->pgraph_regs));
    for (int i = 0; i < NV4_CHANNELS; i++) {
        memset(&nv4->gr_chan[i], 0, sizeof(nv4->gr_chan[i]));
        nv4_pgraph_2d_reset_chan(&nv4->gr_chan[i]);
    }
    nv4_3d_reset(nv4);
    /* no channel until the driver loads one */
    GR(NV4_PGRAPH_CTX_USER) = 0x0f000000;
    /* Its reset values: FIFO access on, every surface invalid. */
    GR(NV4_PGRAPH_FIFO) = 1;
    for (int i = 0; i < 6; i++)
        GR(NV4_PGRAPH_BLIMIT + i * 4) = 0x80000000;
}

uint32_t
nv4_pgraph_read(nv4_t *nv4, uint32_t addr)
{
    addr &= ~3u;
    switch (addr) {
        case NV4_PGRAPH_STATUS:
            /* STATE: busy until the last method's time is up */
            return nv4_pfifo_engine_busy(nv4) ? 0x00000001 : 0;
        default:
            return GR(addr);
    }
}

void
nv4_pgraph_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    int resume = 0;

    addr &= ~3u;
    switch (addr) {
        case NV4_PGRAPH_INTR:
            GR(addr) &= ~val;
            if (val & NV4_PGRAPH_INTR_NOTIFY) {
                GR(NV4_PGRAPH_NSOURCE) = 0;
                GR(NV4_PGRAPH_NSTATUS) = 0;
            }
            nv4_pmc_update_irq(nv4);
            resume = 1;
            break;
        case NV4_PGRAPH_INTR_EN:
            GR(addr) = val & 0x00011311;
            nv4_pmc_update_irq(nv4);
            break;
        case NV4_PGRAPH_NSOURCE:
        case NV4_PGRAPH_STATUS:
            break;
        case NV4_PGRAPH_FIFO:
            GR(addr) = val & 1;
            resume   = val & 1;
            break;
        case NV4_PGRAPH_CTX_SWITCH1:
            GR(addr) = val & nv4_ctxc_mask(nv4);
            break;
        case NV4_PGRAPH_CTX_SWITCH2:
            GR(addr) = val & NV4_CTX2_MASK;
            break;
        case NV4_PGRAPH_CTX_SWITCH4:
            GR(addr) = val & 0xffff;
            break;
        case NV4_PGRAPH_CTX_CONTROL:
            GR(addr) = val & 0x11010003;
            break;
        case NV4_PGRAPH_CTX_USER:
            GR(addr) = val & 0x0f00e000;
            break;
        case NV4_PGRAPH_ROP3:
            GR(addr) = val & 0xff;
            break;
        case NV4_PGRAPH_BETA_AND:
            GR(addr) = val & 0x7f800000;
            break;
        default:
            if ((addr >= NV4_PGRAPH_CTX_CACHE1) && (addr < NV4_PGRAPH_CTX_CACHE1 + 0x20))
                val &= nv4_ctxc_mask(nv4);
            else if ((addr >= NV4_PGRAPH_CTX_CACHE2) && (addr < NV4_PGRAPH_CTX_CACHE2 + 0x20))
                val &= NV4_CTX2_MASK;
            else if ((addr >= NV4_PGRAPH_CTX_CACHE4) && (addr < NV4_PGRAPH_CTX_CACHE4 + 0x20))
                val &= 0xffff;
            GR(addr) = val;
            break;
    }

    if (resume && (GR(NV4_PGRAPH_FIFO) & 1))
        nv4_pfifo_run(nv4);
}
