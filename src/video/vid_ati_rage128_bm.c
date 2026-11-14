/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- descriptor-list bus master: BM_VIP0_BUF to
 *          BM_VIP3_BUF and the registers around them.
 *
 *          Besides the CCE's own command fetches, the chip can move
 *          blocks of data between system memory and the frame buffer by
 *          itself. A client builds a table of descriptors in system
 *          memory, four dwords each: a frame buffer offset, a physical
 *          system address, a command dword with the byte count and flags,
 *          and a reserved dword. The last entry has the end-of-list bit,
 *          bit 31 of the command dword, set. The client then writes the
 *          table's address, with a trigger value in the low four bits, to
 *          one of the channel registers; that write starts the transfer,
 *          and the end of the list sets BUSMASTER_EOL_INT in
 *          GEN_INT_STATUS (SDK: Bus Master Operation, p. 7-37 / PDF 207,
 *          to SDK: Setting up a System Bus Master Transfer, p. 7-39 /
 *          PDF 209). A client that moves a bitmap to the screen this way,
 *          the SDK's example of a system transfer, polls that bit to learn
 *          the copy is done.
 *
 *          The channels are four VIP queues, three capture buffers and a
 *          GUI queue; the guide notes that the buffers need not be used
 *          for capture or VIP transfers. Two triggers start a transfer at
 *          once: 0, system memory to the frame buffer, and 1, the frame
 *          buffer to system memory. Both run here on the four VIP
 *          channels. The other triggers wait for a VIP or MPP device, and
 *          the capture and GUI channels feed blocks the device does not
 *          have, so those writes are kept and start nothing.
 *
 *          A list runs to its end inside the register write that starts
 *          it (modeled): the chip moves a 64 KB bitmap over PCI in well
 *          under a millisecond, and a client learns of the end only
 *          through BUSMASTER_EOL_INT, so no client can see the
 *          difference. The BM_*_ACTIVE registers, BM_QUEUE_FREE_STATUS
 *          and the WAIT_UNTIL bus-master idle conditions therefore always
 *          show the engine idle.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *              It has the interrupt bits, BUS_CNTL and
 *              BM_QUEUE_FREE_STATUS but no page for the descriptor
 *              registers.
 *
 *          [2] ATI Technologies, "Rage 128 VR/GL Register Reference
 *              Supplement", 1999, section Rage 128 Bus Master Registers,
 *              pages BM-1 to BM-8: every register from BM_FRAME_BUF_OFFSET
 *              (0x0A00) to BM_ABORT (0x0A88) with its fields. Cited as
 *              "Bus Master supplement: <register>, page BM-<n>".
 *
 *          [3] ATI Technologies, "RAGE 128 Software Development Manual",
 *              SDK-G04000, 1999, section 7.12, Bus Mastering: the
 *              descriptor layout and the start sequence. Cited as
 *              "SDK: <section>, p. <printed page> / PDF <viewer page>".
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_ati_rage128.h>
#include <86box/vid_ati_rage128_regs.h>

/* Bounds on one list (modeled). A list whose table has no end-of-list
   entry would make the chip walk memory until the guest stops it; here
   the walk stops after RAGE128_BM_MAX_DESC entries (1 MB of table) or
   once RAGE128_BM_MAX_BYTES have moved, and the list does not end, so
   BUSMASTER_EOL_INT stays clear, as it would on the chip. */
#define RAGE128_BM_MAX_DESC  65536u
#define RAGE128_BM_MAX_BYTES (64u << 20)
/* Bytes moved per bus call: one bounce buffer on the stack. */
#define RAGE128_BM_CHUNK 4096u

/* A start waiting for the CPU thread travels as one word, so the two
   threads hand it over in a single atomic step: the table address in
   bits [31:4], the direction in bit 0, and bit 3 set to mark the word as
   holding a start (a table at address 0 with trigger 0 is a valid start,
   so 0 alone cannot mean "none"). */
#define BM_PEND_VALID 0x8u

#define BM_LOG_BLOCKED  (1u << 0)
#define BM_LOG_REGS     (1u << 1)
#define BM_LOG_CAP      (1u << 2)
#define BM_LOG_FIRST    (1u << 3)

/* Store len bytes into local memory at offset a, the way a guest CPU
   store through the linear aperture does it, so every observer of VRAM
   sees the bus master as one more outside writer: the GPU-offload
   flush-point telemetry, the GPU backend (texture levels staged from
   those bytes are dropped, the overlay composes again, and the store
   waits for any GPU work that owns the bytes), the svga core's dirty
   pages, and the GPU mirror's host-newer marks. An offset past the installed memory wraps
   through vram_mask, the wrap every local engine path models. */
static void
bm_vram_store(rage128_t *dev, uint32_t a, const uint8_t *src, uint32_t len)
{
    svga_t *svga = &dev->svga;

    while (len) {
        uint32_t run;

        a   &= dev->vram_mask;
        run = dev->vram_mask + 1u - a;
        if (run > len)
            run = len;
        if (dev->synctel)
            rage128_synctel_cpu_write(dev, a, run);
        if (dev->gpu)
            rage128_gpu_cpu_access(dev, a, run, 1);
        memcpy(&svga->vram[a], src, run);
        for (uint32_t o = 0; o < run; o += 0x1000u)
            svga->changedvram[(a + o) >> 12] = svga->monitor->mon_changeframecount;
        svga->changedvram[(a + run - 1u) >> 12] = svga->monitor->mon_changeframecount;
        r128_vram_hnew(dev, a, run);
        a += run;
        src += run;
        len -= run;
    }
}

/* Load len bytes of local memory from offset a, the way a guest CPU load
   through the linear aperture does it: the GPU backend first finishes
   any work that writes those bytes. */
static void
bm_vram_load(rage128_t *dev, uint32_t a, uint8_t *dst, uint32_t len)
{
    while (len) {
        uint32_t run;

        a   &= dev->vram_mask;
        run = dev->vram_mask + 1u - a;
        if (run > len)
            run = len;
        if (dev->synctel)
            rage128_synctel_cpu_read(dev, a, run);
        if (dev->gpu)
            rage128_gpu_cpu_barrier(dev, a, run, 0);
        memcpy(dst, &dev->svga.vram[a], run);
        a += run;
        dst += run;
        len -= run;
    }
}

/* One bus read of system memory, recorded as a B record of the
   command-stream capture so a replay serves the same bytes. */
static void
bm_sys_read(rage128_t *dev, uint32_t sys, uint8_t *dst, uint32_t len)
{
    dma_bm_read(sys, dst, len, 4);
    rage128_cap_bus_block(dev, sys, dst, len);
}

/* Move one descriptor's bytes. "Transfers use the same byte offsets for
   both the frame-buffer and system-memory addresses": from system memory
   the system address bits [1:0] replace the frame buffer offset bits
   [1:0], and from the frame buffer the frame buffer offset bits [1:0]
   replace the system address bits [1:0] (SDK: Creating a Descriptor
   Table, p. 7-38 / PDF 208). So the source decides the byte alignment.

   FRAME_OFFSET_HOLD keeps the frame buffer offset from advancing (Bus
   Master supplement: BM_COMMAND, page BM-1; the SDK's example is one
   register address fed a list of values): every dword goes to, or comes
   from, the same four bytes, while the system address advances. A
   system address that would pass 4 GB ends the descriptor there
   (modeled). */
static void
bm_move(rage128_t *dev, int to_fb, uint32_t fb, uint32_t sys, uint32_t count, int hold)
{
    uint8_t  buf[RAGE128_BM_CHUNK];
    uint32_t o = 0;

    fb &= RAGE128_BM_FB_OFFSET_MASK;
    if (to_fb)
        fb = (fb & ~3u) | (sys & 3u);
    else
        sys = (sys & ~3u) | (fb & 3u);
    if ((uint64_t) sys + count > 0x100000000ull)
        count = (uint32_t) (0x100000000ull - sys);

    while (o < count) {
        uint32_t n = count - o;

        if (n > RAGE128_BM_CHUNK)
            n = RAGE128_BM_CHUNK;
        if (to_fb) {
            bm_sys_read(dev, sys + o, buf, n);
            if (!hold)
                bm_vram_store(dev, fb + o, buf, n);
            else
                for (uint32_t i = 0; i < n; i += 4)
                    bm_vram_store(dev, fb, buf + i, (n - i < 4) ? n - i : 4u);
        } else {
            if (!hold)
                bm_vram_load(dev, fb + o, buf, n);
            else
                for (uint32_t i = 0; i < n; i += 4)
                    bm_vram_load(dev, fb, buf + i, (n - i < 4) ? n - i : 4u);
            dma_bm_write(sys + o, buf, n, 4);
        }
        o += n;
    }
}

/* Run the list a channel register was written with, on the CPU thread.

   - Nothing moves unless the chip may master the bus:
     COMMAND.BUS_MASTER_EN set and BUS_CNTL.BUS_MASTER_DIS clear, the
     latter cleared first in the guide's start sequence (SDK: Setting up
     a System Bus Master Transfer, p. 7-39 / PDF 209). A blocked chip
     makes no bus cycle, so the list never ends and BUSMASTER_EOL_INT
     stays clear (modeled).
   - The table address is paragraph aligned, bits [31:4] of the register
     (Bus Master supplement: BM_VIP0_BUF, page BM-3); entry n is at
     table + 16 n, and the walk stops after the entry with the
     end-of-list bit set (SDK: Creating a Descriptor Table, p. 7-38 /
     PDF 208). Table and data are read at physical system addresses,
     with no on-chip GART lookup; the SDK names both as physical
     addresses (SDK: Creating a Descriptor Table, pp. 7-37-7-38 / PDF
     207-208), and translation of these addresses is not documented.
   - BYTE_COUNT is the 21-bit field of the Bus Master supplement
     (BM_COMMAND, page BM-1). The SDK gives 12 bits and a 4096-byte
     limit, in the same words as the Rage Pro guide's section on that
     chip's engine; the Rage 128 register page is followed.
   - An entry with TRANSFER_DEST set feeds engine registers (a GUI
     transfer, SDK: Bus Master Operation, p. 7-37 / PDF 207), whose data
     format the guides do not give; it moves nothing (modeled).
   - At the end-of-list entry BUSMASTER_EOL_INT, GEN_INT_STATUS bit 16,
     is set (RRG: GEN_INT_STATUS, p. 3-195 / PDF 213), and it drives INTA
     when BUSMASTER_EOL_INT_EN, GEN_INT_CNTL bit 16, is set (RRG:
     GEN_INT_CNTL, p. 3-193 / PDF 211). An end-of-list entry with
     INTERRUPT_DIS, "Disable the end of list interrupt", leaves the bit
     clear; that it suppresses the status and not only the line is
     modeled.
   - While a list runs, bm_running keeps a second one from starting
     inside it. The list's own bus cycles can land on the chip's register
     aperture: a frame-buffer-to-system entry can name BM_VIPn_BUF as its
     system address, and a table can sit in the aperture so that reading
     it runs the register-read sync. A PCI master is not the target of
     its own cycle; here the nested start is not taken (modeled). */
static void
bm_run_list(rage128_t *dev, int ch, uint32_t buf)
{
    uint32_t table = buf & RAGE128_BM_TABLE_ADDR_MASK;
    int      to_fb = (buf & RAGE128_BM_TRIGGER_MASK) == RAGE128_BM_TRIG_SYS_TO_FB;
    uint64_t bytes = 0;
    uint32_t n;
    int      end = 0;
    int      irq = 0;

    if (!rage128_pm4_bus_master_ok(dev)) {
        if (!(dev->bm_logged & BM_LOG_BLOCKED))
            rage128_log("RAGE128 BM: VIP%d start with bus mastering off, nothing moved\n", ch);
        dev->bm_logged |= BM_LOG_BLOCKED;
        return;
    }

    dev->bm_running = 1;
    for (n = 0; n < RAGE128_BM_MAX_DESC; n++) {
        uint64_t at = (uint64_t) table + (uint64_t) n * 16u;
        uint32_t d[4];
        uint32_t count;

        if (at + 16u > 0x100000000ull)
            break;
        bm_sys_read(dev, (uint32_t) at, (uint8_t *) d, 16);
        count = d[2] & RAGE128_BM_BYTE_COUNT_MASK;
        if (d[2] & RAGE128_BM_TRANSFER_DEST) {
            if (!(dev->bm_logged & BM_LOG_REGS))
                rage128_log("RAGE128 BM: VIP%d register transfer entry skipped\n", ch);
            dev->bm_logged |= BM_LOG_REGS;
        } else if (bytes + count > RAGE128_BM_MAX_BYTES) {
            break;
        } else {
            bm_move(dev, to_fb, d[0], d[1], count, !!(d[2] & RAGE128_BM_FRAME_OFFSET_HOLD));
            bytes += count;
        }
        if (d[2] & RAGE128_BM_END_OF_LIST) {
            end = 1;
            irq = !(d[2] & RAGE128_BM_INTERRUPT_DIS);
            n++;
            break;
        }
    }

    dev->bm_running = 0;
    dev->bm_kicks++;
    if (!end) {
        if (!(dev->bm_logged & BM_LOG_CAP))
            rage128_log("RAGE128 BM: VIP%d table 0x%08x: no end of list within %u entries, "
                        "%llu bytes\n",
                        ch, table, n, (unsigned long long) bytes);
        dev->bm_logged |= BM_LOG_CAP;
    } else if (!(dev->bm_logged & BM_LOG_FIRST)) {
        rage128_log("RAGE128 BM: VIP%d %s: table 0x%08x, %u entries, %llu bytes\n", ch,
                    to_fb ? "sys->fb" : "fb->sys", table, n, (unsigned long long) bytes);
        dev->bm_logged |= BM_LOG_FIRST;
    }

    /* Raised the way every status source the CCE thread can reach does
       it: one atomic step on the status, then the line update. */
    if (irq) {
        atomic_fetch_or(&dev->gen_int_status, RAGE128_GIS_BUSMASTER_EOL);
        rage128_gen_int_update(dev);
    }
}

/* Run the starts that ring packets wrote, in channel order. CPU thread
   only, called from rage128_gen_int_sync at every register read and at
   the end of every ring pump. A pump can run from inside an indirect
   buffer that the CPU thread is parsing; the starts then wait for the
   next sync, outside the parse.

   bm_pend_mask only says that some channel may hold a start; each
   channel's word is taken with one exchange, so a start is run exactly
   once however a packet's store interleaves with this function: a word
   stored after the exchange stays for the next sync, whose mask bit the
   packet sets after the store. */
void
rage128_bm_sync(rage128_t *dev)
{
    if (rage128_on_cce_thread || rage128_in_indirect || dev->bm_running)
        return;
    if (!atomic_exchange(&dev->bm_pend_mask, 0))
        return;
    for (int ch = 0; ch < 4; ch++) {
        uint32_t p = atomic_exchange(&dev->bm_pend_val[ch], 0);

        if (p & BM_PEND_VALID)
            bm_run_list(dev, ch, p & ~BM_PEND_VALID);
    }
}

/* Register reads. The VIP, capture and GUI channel registers read back
   as written. BM_VIP0_ACTIVE to BM_VIP3_ACTIVE describe the entry a
   channel is working on and read 0, since no list is ever in progress.
   BM_FRAME_BUF_OFFSET, BM_SYSTEM_MEM_ADDR, BM_COMMAND and BM_STATUS are
   documented as read-only fields without a description of what they
   hold once a list has ended, and read 0 (modeled); so does
   BM_QUEUE_STATUS. */
int
rage128_bm_reg_read(rage128_t *dev, uint32_t off, uint32_t *val)
{
    if (off >= RAGE128_BM_FRAME_BUF_OFFSET && off <= RAGE128_BM_QUEUE_STATUS) {
        *val = 0;
        return 1;
    }
    if (off >= RAGE128_BM_VIP0_BUF && off <= RAGE128_BM_VIP3_ACTIVE && !(off & 0x08)) {
        *val = (off & 0x04) ? 0 : atomic_load(&dev->bm_buf[(off - RAGE128_BM_VIP0_BUF) >> 4]);
        return 1;
    }
    if (off >= RAGE128_BM_VIDCAP_BUF0 && off <= RAGE128_BM_VIDCAP_ACTIVE) {
        *val = atomic_load(&dev->bm_vidcap[(off - RAGE128_BM_VIDCAP_BUF0) >> 2]);
        return 1;
    }
    if (off == RAGE128_BM_GUI) {
        *val = atomic_load(&dev->bm_gui);
        return 1;
    }
    return 0;
}

/* Merge a write into a stored register in one atomic step: a packet on
   the CCE thread can write it while a CPU write merges into it. */
static uint32_t
bm_merge(atomic_uint *r, uint32_t val, uint32_t mask)
{
    uint32_t old = atomic_load(r);
    uint32_t v;

    do
        v = (old & ~mask) | (val & mask);
    while (!atomic_compare_exchange_weak(r, &old, v));
    return v;
}

/* Register writes. "Writing to BM_SYSTEM_TABLE initiates the bus master
   operation" (SDK: Setting up a System Bus Master Transfer, p. 7-39 /
   PDF 209; BM_SYSTEM_TABLE is the SDK's name for a channel register), so
   a write to BM_VIPn_BUF whose trigger is 0 or 1 starts a list from the
   table address the register then holds. The trigger is in byte 0, and
   only a write that includes byte 0 starts a list: a guest that writes
   the register in pieces sets the address bytes first and the list
   starts once, with the whole address, when the trigger byte arrives
   (what the chip does on a partial write is not documented; modeled).
   A write from a ring packet runs on the
   CCE thread, which must not make the CPU-side bus reads and VRAM
   stores the list needs; its start is handed to the CPU thread through
   bm_pend_val and bm_pend_mask. The pending mark is set before the
   executor moves the ring read pointer past the packet, so the ring
   pump's sync or the CPU's next register read runs it (see
   rage128_gen_int_sync). A second start on the same channel before
   then replaces the first (modeled). A start written while a list is
   running is not taken (see bm_run_list). */
int
rage128_bm_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask)
{
    if (off >= RAGE128_BM_FRAME_BUF_OFFSET && off <= RAGE128_BM_QUEUE_STATUS)
        return 1; /* read-only */
    if (off >= RAGE128_BM_VIP0_BUF && off <= RAGE128_BM_VIP3_ACTIVE && !(off & 0x08)) {
        int      ch = (int) ((off - RAGE128_BM_VIP0_BUF) >> 4);
        uint32_t v;

        if (off & 0x04)
            return 1; /* BM_VIPn_ACTIVE, read-only */
        v = bm_merge(&dev->bm_buf[ch], val, mask);
        if (!(mask & 0xffu) || (v & RAGE128_BM_TRIGGER_MASK) > RAGE128_BM_TRIG_FB_TO_SYS)
            return 1;
        if (rage128_on_cce_thread || rage128_in_indirect) {
            atomic_store(&dev->bm_pend_val[ch], v | BM_PEND_VALID);
            atomic_fetch_or(&dev->bm_pend_mask, 1u << ch);
            return 1;
        }
        if (dev->bm_running)
            return 1;
        rage128_bm_sync(dev);
        bm_run_list(dev, ch, v);
        return 1;
    }
    if (off >= RAGE128_BM_VIDCAP_BUF0 && off <= RAGE128_BM_VIDCAP_ACTIVE) {
        bm_merge(&dev->bm_vidcap[(off - RAGE128_BM_VIDCAP_BUF0) >> 2], val, mask);
        return 1;
    }
    if (off == RAGE128_BM_GUI) {
        bm_merge(&dev->bm_gui, val, mask);
        return 1;
    }
    return 0;
}

/* The channel registers this file stores reset to 0 (Bus Master
   supplement: BM_VIP0_BUF to BM_GUI, pages BM-3 to BM-7); a start still
   waiting for the CPU thread is dropped with them.
   BUSMASTER_EOL_INT is cleared with the rest of GEN_INT_STATUS by the
   device reset. */
void
rage128_bm_reset(rage128_t *dev)
{
    for (int i = 0; i < 4; i++) {
        atomic_store(&dev->bm_buf[i], 0);
        atomic_store(&dev->bm_vidcap[i], 0);
        atomic_store(&dev->bm_pend_val[i], 0);
    }
    atomic_store(&dev->bm_gui, 0);
    atomic_store(&dev->bm_pend_mask, 0);
    dev->bm_kicks   = 0;
    dev->bm_logged  = 0;
    dev->bm_running = 0;
}
