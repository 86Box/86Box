/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PBUS DMA: DMA & Notifier Engine
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2025 starfrost
 *          Copyright 2026 Marcos Alves
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/plat_unused.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

/* Nvidia DMA Engine

   A DMA object is 16+ bytes of RAMIN: word 0 holds the byte adjust (11:0),
   whether a page table follows (16) and the target (25:24: VRAM, cartridge,
   PCI, AGP); word 1 the limit; words 2 on the page table entries (frame
   address in 31:12, bit 0 present, bit 1 writable). An object's grobj word 1
   holds its DMA instance (15:0) and notifier instance (31:16); M2MF's output
   object is in word 2 (15:0). */

/* Debug: log the next N notifier writes ("dev nv3 notify N") */
uint32_t nv3_notify_trace_left;

/* Debug: log the next N M2MF transfers ("dev nv3 m2mf N") */
uint32_t nv3_m2mf_trace_left;

/* A byte offset within a DMA object to an address in its target */
static uint32_t
nv3_dma_translate(uint32_t inst, uint32_t offset, uint32_t *target)
{
    uint32_t base   = inst << 4;
    uint32_t info   = nv3_ramin_read32(base, nv3);
    uint32_t linear = (info & 0xFFF) + offset;
    uint32_t pte    = nv3_ramin_read32(base + 8 + ((linear >> 12) << 2), nv3);

    *target = (info >> NV3_NOTIFICATION_TARGET) & 0x03;

    nv_log_verbose_only("DMA inst %04x offset %x: info %08x pte %08x -> %08x target %d\n", inst, offset, info, pte,
                        (pte & 0xFFFFF000) | (linear & 0xFFF), *target);
    return (pte & 0xFFFFF000) | (linear & 0xFFF);
}

uint32_t
nv3_dma_read8(uint32_t inst, uint32_t offset)
{
    uint32_t target;
    uint32_t addr = nv3_dma_translate(inst, offset, &target);
    uint8_t  val  = 0;

    if (target == NV3_DMA_TARGET_NODE_VRAM)
        return nv3->nvbase.svga.vram[addr & nv3->nvbase.svga.vram_mask];
    dma_bm_read(addr, &val, 1, 1);
    return val;
}

void
nv3_dma_write8(uint32_t inst, uint32_t offset, uint8_t val)
{
    uint32_t target;
    uint32_t addr = nv3_dma_translate(inst, offset, &target);

    if (target == NV3_DMA_TARGET_NODE_VRAM) {
        addr &= nv3->nvbase.svga.vram_mask;
        nv3->nvbase.svga.vram[addr]                  = val;
        nv3->nvbase.svga.changedvram[addr >> 12]     = changeframecount;
    } else
        dma_bm_write(addr, &val, 1, 1);
}

static void
nv3_dma_write32(uint32_t inst, uint32_t offset, uint32_t val)
{
    uint32_t target;
    uint32_t addr = nv3_dma_translate(inst, offset, &target);

    if (target == NV3_DMA_TARGET_NODE_VRAM) {
        addr &= nv3->nvbase.svga.vram_mask & ~3;
        *(uint32_t *) &nv3->nvbase.svga.vram[addr] = val;
        nv3->nvbase.svga.changedvram[addr >> 12]    = changeframecount;
    } else
        dma_bm_write(addr, (uint8_t *) &val, 4, 4);
}

/* For repeated reads (textures): the addresses of `pages` 4K pages of a DMA
   object starting with the one holding `offset`. *lin gets offset's linear
   address within the object (page i holds linear (*lin & ~0xFFF) + i * 4K);
   returns the target. */
uint32_t nv3_dma_map_pages(uint32_t inst, uint32_t offset, uint32_t *addr, uint32_t pages, uint32_t *lin)
{
    uint32_t base = inst << 4;
    uint32_t info = nv3_ramin_read32(base, nv3);

    *lin = (info & 0xFFF) + offset;
    for (uint32_t i = 0; i < pages; i++)
        addr[i] = nv3_ramin_read32(base + 8 + (((*lin >> 12) + i) << 2), nv3) & 0xFFFFF000;
    return (info >> NV3_NOTIFICATION_TARGET) & 0x03;
}

/* Memory to memory format (class 0x0D): NUM_SCANLINES lines of
   SCANLINE_LENGTH bytes, input bytes taken every 1/2/4 bytes and written
   every 1/2/4 bytes, from the DMA_IN object to the DMA_OUT object */
/* Whether bytes [first, last] of a DMA object may be accessed: the instance must be set, the
   range within the object's limit, and (with a page table) every page present -- and writable
   for a write. Returns 0, or the PDMA interrupt (DMA_INTR_0 bit) the access raises. */
static int
nv3_dma_check(uint32_t inst, uint32_t first, uint32_t last, bool write)
{
    uint32_t base, info, limit, lin;

    if (!inst)
        return NV3_PGRAPH_DMA_INTR_INSTANCE;
    base  = inst << 4;
    info  = nv3_ramin_read32(base, nv3);
    limit = nv3_ramin_read32(base + 4, nv3);
    if ((last < first) || (last > limit))
        return NV3_PGRAPH_DMA_INTR_LINEAR;
    if (!((info >> NV3_NOTIFICATION_PT_PRESENT) & 1))
        return 0;
    lin = (info & 0xFFF) + first;
    for (uint32_t page = lin >> 12; page <= (((info & 0xFFF) + last) >> 12); page++) {
        uint32_t pte = nv3_ramin_read32(base + 8 + (page << 2), nv3);

        if (!(pte & 1))
            return NV3_PGRAPH_DMA_INTR_PRESENT;
        if (write && !(pte & 2))
            return NV3_PGRAPH_DMA_INTR_PROTECTION;
    }
    return 0;
}

/* One side of an M2MF: a DMA object, or (NV3_M2MF_VRAM) the framebuffer itself */
#define NV3_M2MF_VRAM 0xFFFFFFFF

static uint8_t
nv3_m2mf_read8(uint32_t inst, uint32_t offset)
{
    if (inst == NV3_M2MF_VRAM)
        return nv3->nvbase.svga.vram[offset & nv3->nvbase.svga.vram_mask];
    return nv3_dma_read8(inst, offset);
}

static void
nv3_m2mf_write8(uint32_t inst, uint32_t offset, uint8_t val)
{
    if (inst == NV3_M2MF_VRAM) {
        offset &= nv3->nvbase.svga.vram_mask;
        nv3->nvbase.svga.vram[offset]              = val;
        nv3->nvbase.svga.changedvram[offset >> 12] = changeframecount;
    } else
        nv3_dma_write8(inst, offset, val);
}

/* Returns false when the transfer faulted (PDMA interrupt raised, nothing copied).
   The Win9x driver's readbacks (NV3DISP.DRV: VRAM rectangles at y * pitch + x into a
   double-buffered system buffer) show the grobj layout: word 1 (CTX_SWITCH_B) is the
   system-memory DMA object, and word 2 (CTX_SWITCH_C) bit 16 makes the framebuffer the
   source (VRAM -> DMA object); with it clear the DMA object is the source and VRAM the
   destination. */
bool nv3_perform_dma_m2mf(nv3_grobj_t grobj)
{
    bool     from_vram = (nv3->pgraph.ctx_switch_c >> 16) & 1;
    uint32_t dma_inst  = nv3->pgraph.dma_settings & 0xFFFF;
    uint32_t in_inst   = from_vram ? NV3_M2MF_VRAM : dma_inst;
    uint32_t out_inst  = from_vram ? dma_inst : NV3_M2MF_VRAM;
    uint32_t offset_in  = nv3->pgraph.m2mf.offset_in;
    uint32_t offset_out = nv3->pgraph.m2mf.offset_out;
    uint32_t inc_in  = (nv3->pgraph.m2mf.format >> NV3_M2MF_FORMAT_INPUT) & 0x07;
    uint32_t inc_out = (nv3->pgraph.m2mf.format >> NV3_M2MF_FORMAT_OUTPUT) & 0x07;

    if (!inc_in)
        inc_in = 1;
    if (!inc_out)
        inc_out = 1;

    if (nv3_m2mf_trace_left) {
        uint32_t t_in, t_out;
        uint32_t a_in  = (in_inst == NV3_M2MF_VRAM) ? (t_in = 0, offset_in) : nv3_dma_translate(in_inst, offset_in, &t_in);
        uint32_t a_out = (out_inst == NV3_M2MF_VRAM) ? (t_out = 0, offset_out) : nv3_dma_translate(out_inst, offset_out, &t_out);
        char     line[3 * 16 + 1];

        nv3_m2mf_trace_left--;
        for (int i = 0; i < 16; i++)
            snprintf(line + 3 * i, 4, " %02x", nv3_m2mf_read8(in_inst, offset_in + i));
        always_log("nv3: m2mf %04x:%x (t%d %08x) -> %04x:%x (t%d %08x) %ux%u pitch %u/%u fmt %x grobj %08x %08x %08x %08x ctxi %04x notify %06x debug1 %08x |%s\n",
                   in_inst, offset_in, t_in, a_in, out_inst, offset_out, t_out, a_out,
                   nv3->pgraph.m2mf.scanline_length, nv3->pgraph.m2mf.num_scanlines,
                   nv3->pgraph.m2mf.pitch_in, nv3->pgraph.m2mf.pitch_out, nv3->pgraph.m2mf.format,
                   grobj.grobj_0, grobj.grobj_1, grobj.grobj_2, grobj.grobj_3, nv3->pgraph.instance, nv3->pgraph.notifier, nv3->pgraph.debug_1, line);
    }

    {
        uint32_t lines = nv3->pgraph.m2mf.num_scanlines;
        uint32_t len   = nv3->pgraph.m2mf.scanline_length;
        int      fault = 0;

        if (lines && len) {
            uint32_t last_in  = offset_in + (lines - 1) * nv3->pgraph.m2mf.pitch_in + (len - 1) * inc_in;
            uint32_t last_out = offset_out + (lines - 1) * nv3->pgraph.m2mf.pitch_out + (len - 1) * inc_out;

            if (in_inst != NV3_M2MF_VRAM)
                fault = nv3_dma_check(in_inst, offset_in, last_in, false);
            if (!fault && (out_inst != NV3_M2MF_VRAM))
                fault = nv3_dma_check(out_inst, offset_out, last_out, true);
        }
        if (fault) {
            static int fault_logs;
            if (fault_logs < 20) {
                fault_logs++;
                always_log("nv3: M2MF DMA fault %d: %04x:%x -> %04x:%x %ux%u%c", fault, in_inst, offset_in, out_inst, offset_out, len, lines, 10);
            }
            nv3->pgraph.intr_dma |= (1 << fault);
            nv3_pmc_handle_interrupts(true);
            return false;
        }
    }

    /* LINE_LENGTH_IN counts the bytes moved, not the span read: the Win9x DirectDraw driver's
       YV12 -> YUY2 conversion copies a 640-byte YUY2 row's 320 chroma bytes to the next row
       with length 320, input and output increment 2 */
    for (uint32_t line = 0; line < nv3->pgraph.m2mf.num_scanlines; line++) {
        uint32_t in  = offset_in;
        uint32_t out = offset_out;

        for (uint32_t i = 0; i < nv3->pgraph.m2mf.scanline_length; i++) {
            nv3_m2mf_write8(out_inst, out, nv3_m2mf_read8(in_inst, in));
            in += inc_in;
            out += inc_out;
        }
        offset_in += nv3->pgraph.m2mf.pitch_in;
        offset_out += nv3->pgraph.m2mf.pitch_out;
    }
    return true;
}

/* Write an NvNotification (16 bytes: the time in ns, info32, info16 and the
   status) at notifier index `index` of the object's notifier DMA object */
void nv3_write_notifier(UNUSED(nv3_grobj_t grobj), uint32_t index, uint16_t status, uint32_t info32, uint16_t info16)
{
    uint32_t inst = nv3->pgraph.notifier & 0xFFFF;
    uint32_t off  = index << 4;
    uint64_t time = nv3->ptimer.time;

    if (!inst)
        return;
    if (nv3_notify_trace_left) {
        uint32_t t;
        uint32_t a = nv3_dma_translate(inst, off + 12, &t);
        nv3_notify_trace_left--;
        always_log("nv3: notifier inst %04x index %u status %04x -> %08x (target %u) chan %d%c", inst, index, status, a, t,
                   nv3->pfifo.cache1_settings.channel, 10);
    }
    nv3_dma_write32(inst, off + 0, (uint32_t) time);
    nv3_dma_write32(inst, off + 4, (uint32_t) (time >> 32));
    nv3_dma_write32(inst, off + 8, info32);
    nv3_dma_write32(inst, off + 12, info16 | ((uint32_t) status << 16));
}

/* After a method: if a notify was armed (NOTIFY method), write the
   notifier and disarm it (NOTIFY register bit 16). */
void nv3_notify_if_needed(UNUSED(uint32_t name), uint32_t method_id, UNUSED(nv3_ramin_context_t context), nv3_grobj_t grobj)
{
    if (!nv3->pgraph.notify_pending)
        return;
    /* the NOTIFY method itself only arms it */
    if (method_id == NV3_SET_NOTIFY)
        return;

    nv3->pgraph.notify_pending = false;
    nv3->pgraph.notifier &= ~(1 << NV3_PGRAPH_NOTIFY_REQUEST_PENDING);
    nv3_write_notifier(grobj, (nv3->pgraph.notify_index), NV3_NOTIFICATION_STATUS_DONE_OK, 0, 0);
}
