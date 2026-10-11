/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- CCE command processor.
 *
 *          The drivers submit 2D and 3D work as PM4 packets in a ring
 *          buffer in system memory. The CCE (concurrent command engine)
 *          reads the ring by bus mastering, through the chipset's AGP
 *          aperture or through the chip's own PCI GART table, and writes
 *          its read pointer back to a host address the driver polls.
 *          This file holds the ring fetch, the FIFO that carries the
 *          fetched dwords to the executor, the packet parser, indirect
 *          buffers, the device's bus reads and writes, and engine idle
 *          and reset.
 *
 *          Execution is split across two threads, as vid_voodoo_fifo.c
 *          does for the Voodoo command FIFO. On every write to the ring's
 *          write pointer the CPU thread copies the new ring dwords into
 *          a FIFO inside the device (the pump), and the CCE thread pops
 *          them, parses the packets and runs them. The FIFO and its
 *          per-slot state are described with cce_fifo in
 *          vid_ati_rage128.h.
 *
 *          Through the on-chip PCI GART, the table and the pages it maps
 *          in system RAM are read and written straight through the ram
 *          array, which touches no CPU-thread state. The AGP aperture,
 *          and table entries that point into it, go through dma_bm_read
 *          and dma_bm_write, from the CCE thread as well, the way the
 *          Voodoo FIFO thread reads an AGP command FIFO with
 *          mem_readl_phys.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 Software Development Guide",
 *              SDK-G04000 Rev 0.01, August 1999. Cited as "SDK: ...".
 *
 *          [3] ATI Technologies, "RAGE 128 Register Reference Supplement:
 *              Registers for CCE 3D Packets", 1999. Cited as "CCE 3D
 *              supplement" with the register name; it has no page numbers.
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
#ifdef __APPLE__
#    include <pthread/qos.h>
#endif
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/dma.h>
#include <86box/pci.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_ati_rage128.h>
#include <86box/vid_ati_rage128_regs.h>

/* Largest type-3 body the header can encode: COUNT [29:16] holds the
   body length minus 1 (SDK: Type 3 CCE Packet, p. F-9 / PDF 299), so
   0x3fff + 1 dwords. The full range is needed: captured game traffic
   contains draws of about 12k dwords. */
#define RAGE128_PM4_MAX_PAYLOAD 16384

_Thread_local int        rage128_on_cce_thread = 0;
_Thread_local int        rage128_in_indirect   = 0;
static _Thread_local int rage128_ring_fetch    = 0;
/* Set while rage128_pm4_bus_read_block walks a PCI GART block dword by
   dword, so the block lands in the capture as one B record with its
   payload instead of one record per dword. */
static _Thread_local int rage128_cap_in_block = 0;

void
rage128_pm4_reset(rage128_t *dev)
{
    /* Wait for the executor to go idle before clearing engine state
       (does nothing before the thread exists). */
    rage128_pm4_drain_wait(dev);
    if (dev->cce_thread)
        timer_disable(&dev->cce_pump_timer);

    dev->pm4_buffer_offset = 0;
    dev->pm4_buffer_cntl   = 0;
    dev->pm4_wm_cntl       = 0;
    dev->pm4_wptr_delay    = 0; /* PRE_WRITE_TIMER and PRE_WRITE_LIMIT default 0 (RRG:
                                   PM4_BUFFER_DL_WPTR_DELAY, p. 3-218 / PDF 236) */
    dev->pm4_micro_cntl     = 0;
    dev->pm4_microcode_addr = 0;
    dev->pm4_rptr           = 0;
    dev->pm4_wptr           = 0;
    dev->pm4_rptr_addr      = 0;
    atomic_store(&dev->cce_retire_rptr, 0);
    dev->cce_retire_pending = 0;
    dev->cce_shadow_last    = 0xffffffffu;
    dev->pm4_iw_indoff      = 0;
    dev->pm4_iw_indsize     = 0;
    dev->pm4_fifo_data[0]   = 0;
    dev->pm4_fifo_data[1]   = 0;
    dev->pci_gart_page      = 1; /* PCI_GART_DIS defaults to 1, table off
                                    (RRG: PCI_GART_PAGE, p. 3-207 / PDF 225) */
    atomic_store(&dev->cce_hung, 0);
    dev->pm4_ind_busy    = 0;
    dev->pm4_ind_pending = 0;
    dev->pump_frame_rem  = 0;
    dev->pump_ib_state   = 0;
}

/* One dword of guest system RAM, read straight from the ram array.
   Usable from any thread: unlike mem_read_phys and dma_bm_read it
   touches no CPU-thread state (mem_readl_phys stores mem_logical_addr).
   Returns 0 for an address past the end of RAM. */
static inline int
rage128_guest_ram_readl(uint32_t phys, uint32_t *val)
{
    if ((uint64_t) phys + 4 > ((uint64_t) mem_size << 10))
        return 0;
    memcpy(val, &ram[phys], 4);
    return 1;
}

/* Command-stream capture (RAGE128_CAPTURE; record types listed with
   cap_file in vid_ati_rage128.h). Only reads made while packets execute
   are captured as bus data: indirect-buffer walks, vertex and host-data
   walks. Ring fetches by the pump are not, because the ring dwords
   already appear as D records when the executor consumes them and the
   replay never fetches the ring. The pump runs on the CPU thread at the
   same time as CCE-thread parses, and it can also run from inside a
   CPU-thread parse (a register write that drains the executor, which
   pumps), so its reads are excluded by a thread-local mark,
   rage128_ring_fetch, rather than inferred from parse state. */
/* B <bus_addr> <len> <hex payload>: a block of bus data with its bytes,
   one record per gathered block (texture level, staged depth or color
   span, blit source) or per run of contiguous dword reads (a vertex
   array, an indirect-buffer body). Without the payload a replay would
   read every texture held in AGP memory as the value of a failed bus
   read, and comparing render backends over such a capture would compare
   them on a constant. Hex keeps the capture one text line per record;
   the replay reads a B record whole or dword by dword. The record goes
   out in one stdio call because the CPU thread writes its R records to
   the same stream concurrently, and only a whole-record write keeps them
   out of the middle of this one. */
static void
rage128_cap_block_write(rage128_t *dev, uint32_t bus_addr, const uint8_t *src, uint32_t len)
{
    static const char hx[] = "0123456789abcdef";
    char             *buf;
    size_t            n;

    buf = malloc((size_t) len * 2 + 32);
    if (!buf)
        return;
    n = (size_t) snprintf(buf, 32, "B %08x %x ", bus_addr, len);
    for (uint32_t i = 0; i < len; i++) {
        buf[n++] = hx[src[i] >> 4];
        buf[n++] = hx[src[i] & 15];
    }
    buf[n++] = '\n';
    fwrite(buf, 1, n, dev->cap_file);
    free(buf);
    rage128_cap_tick(dev);
}

/* Pending run of contiguous per-dword reads, one per thread. The vertex
   and indirect-buffer walkers fetch arrays one dword at a time; a text
   record per dword costs about 20 bytes each, tens of megabytes a second
   under a 3D benchmark. The run is written as one B record when the next
   read is not contiguous, when the run is full, before any other bus
   record this thread writes, and when the walk or the thread ends. One
   run per thread keeps the records in the device's read order because
   a CPU-thread indirect walk and the CCE thread never read the bus at
   the same time: a direct submit drains the executor first. */
#define RAGE128_CAP_RUN_MAX 4096
static _Thread_local struct {
    uint32_t start;
    uint32_t n;
    uint32_t dw[RAGE128_CAP_RUN_MAX];
} rage128_cap_run;

static void
rage128_cap_run_flush(rage128_t *dev)
{
    if (rage128_cap_run.n && dev->cap_file)
        rage128_cap_block_write(dev, rage128_cap_run.start,
                                (const uint8_t *) rage128_cap_run.dw,
                                rage128_cap_run.n * 4u);
    rage128_cap_run.n = 0;
}

static inline void
rage128_cap_run_add(rage128_t *dev, uint32_t bus_addr, uint32_t val)
{
    if (rage128_cap_run.n
        && (rage128_cap_run.n == RAGE128_CAP_RUN_MAX
            || bus_addr != rage128_cap_run.start + rage128_cap_run.n * 4u))
        rage128_cap_run_flush(dev);
    if (!rage128_cap_run.n)
        rage128_cap_run.start = bus_addr;
    rage128_cap_run.dw[rage128_cap_run.n++] = val;
}

static inline void
rage128_cap_bus_read(rage128_t *dev, uint32_t bus_addr, uint32_t val)
{
    if (dev->cap_file && !rage128_ring_fetch && !rage128_cap_in_block
        && (rage128_on_cce_thread || rage128_in_indirect))
        rage128_cap_run_add(dev, bus_addr, val);
}

static void
rage128_cap_bus_block(rage128_t *dev, uint32_t bus_addr, const uint8_t *src, uint32_t len)
{
    if (!dev->cap_file)
        return;
    rage128_cap_run_flush(dev);
    rage128_cap_block_write(dev, bus_addr, src, len);
}

/* W <bus_addr> <len>: the resolved absolute bus address of every write
   the device makes. Writes have no ring fetches to exclude, and both
   write entry points run only on the thread executing packets, so the
   only condition is an open capture. Without this record a capture
   shows what the device read but not where its own writes went. */
static inline void
rage128_cap_bus_write(rage128_t *dev, uint32_t bus_addr, uint32_t len)
{
    if (dev->cap_file) {
        rage128_cap_run_flush(dev);
        fprintf(dev->cap_file, "W %08x %x\n", bus_addr, len);
        rage128_cap_tick(dev);
    }
}

/* Whether the chip may master the bus, checked before every access the
   device makes: COMMAND.BUS_MASTER_EN set (RRG: COMMAND, p. 3-2 /
   PDF 20) and BUS_CNTL.BUS_MASTER_DIS clear (RRG: BUS_CNTL, p. 3-106 /
   PDF 124). BUS_MASTER_DIS defaults to 1, and the Linux DRM clears it
   before it starts the ring (linux r128 DRM r128_cce.c
   r128_cce_init_ring_buffer). A blocked access fails like a read through
   a bad GART entry; the engine is modeled as staying busy until the
   driver reprograms it. */
int
rage128_pm4_bus_master_ok(rage128_t *dev)
{
    return (dev->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_L_BM)
        && !(dev->bus_cntl & RAGE128_BUS_CNTL_BUS_MASTER_DIS);
}

int
rage128_pm4_bus_read(rage128_t *dev, uint32_t bus_addr, uint32_t *val)
{
    uint32_t table = dev->pci_gart_page & 0xfffff000;
    uint32_t pte;
    uint32_t phys;

    if (!rage128_pm4_bus_master_ok(dev))
        return 0;

    if (!rage128_gart_walk(dev)) {
        /* No on-chip table in use (rage128_gart_walk): the chip masters
           into the chipset's AGP aperture, and the chipset's own GART
           (agpgart.c) maps that to host RAM inside dma_bm_read. bus_addr
           is already the absolute bus address; callers resolve ring,
           vertex and PM4_IW_INDOFF addresses with rage128_pm4_vm_addr.
           With AGP_BASE still 0 there is no aperture to read, which is
           modeled as a failed read. */
        if (!dev->agp_base)
            return 0;
        dma_bm_read(bus_addr, (uint8_t *) val, 4, 4);
        rage128_cap_bus_read(dev, bus_addr, *val);
        return 1;
    }

    /* On-chip PCI GART: bits 24:12 of the address index a table of 8192
       dword entries at PCI_GART_PAGE [31:12], and the low 12 bits are
       the offset in the page (RRG: PCI_GART_PAGE, p. 3-208 / PDF 226).
       Each entry holds the physical base address of a 4 KB page (SDK:
       RAGE 128 PCI GART, p. 2-23 / PDF 39). */
    if (!rage128_guest_ram_readl(table + ((bus_addr >> 12) & 0x1fff) * 4, &pte))
        return 0;
    phys = (pte & 0xfffff000) | (bus_addr & 0xfff);
    /* An entry past the end of guest RAM is a bus address in the
       chipset's AGP aperture. The Windows 98 driver for the dual-chip
       MAXX board has been observed mapping the second chip's command
       buffers, which live in AGP memory, through the on-chip table this
       way. Such a page is read through the aperture, as in the branch
       above. */
    if (!rage128_guest_ram_readl(phys, val))
        dma_bm_read(phys, (uint8_t *) val, 4, 4);
    rage128_cap_bus_read(dev, bus_addr, *val);
    return 1;
}

/* Copy `len` bytes from card address `vm` into `dst`. Bit 25 of a card
   address selects the upper 32 MB, which maps to the AGP aperture (RRG:
   DST_OFFSET, p. 3-137 / PDF 155). Without the on-chip table this is
   one dma_bm_read over the contiguous aperture range; with it, the
   copy goes dword by dword through rage128_pm4_bus_read so every page
   is looked up exactly as a single read would be. Called only on the
   thread executing packets, never from a render worker. Returns 1 when
   the whole block was read, 0 on any failed read. */
int
rage128_pm4_bus_read_block(rage128_t *dev, uint32_t vm, uint8_t *dst, uint32_t len)
{
    if (!rage128_pm4_bus_master_ok(dev))
        return 0;
    if (!rage128_gart_walk(dev)) { /* AGP: aperture is linear in bus space */
        if (!dev->agp_base)
            return 0;
        dma_bm_read(rage128_pm4_vm_addr(dev, vm), dst, len, 4);
        rage128_cap_bus_block(dev, rage128_pm4_vm_addr(dev, vm), dst, len);
        return 1;
    }
    /* A surface address with bit 25 set (R128_AGP_OFFSET in the Linux
       DRM) is in the AGP half of card space: surface offsets are 64 MB
       virtual addresses whose upper 32 MB map to AGP_BASE + offset[24:0]
       (RRG: DST_OFFSET, p. 3-137 / PDF 155; RRG: SRC_OFFSET, p. 3-146 /
       PDF 164). Such a surface is read through the chipset aperture even
       while the on-chip table is in use. The Windows drivers have been
       observed mapping textures through the chipset's GART and filling
       the on-chip table with a linear map of the contiguous command
       buffer only (entry n holding a fixed base plus n << 12), so a
       scattered texture page looked up in that table reads the wrong
       memory. Only with AGP_BASE set: with no aperture programmed, as
       under the Linux DRM on a PCI board, the on-chip table is the only
       way to the memory. This applies to surface blocks only; the Linux
       DRM sets bit 25 on the ring base as well (linux r128 DRM
       r128_cce.c r128_cce_init_ring_buffer), but ring and command reads
       do not come through here. */
    if ((vm & 0x02000000u) && dev->agp_base) {
        dma_bm_read(dev->agp_base + (vm & 0x01ffffffu), dst, len, 4);
        rage128_cap_bus_block(dev, dev->agp_base + (vm & 0x01ffffffu), dst, len);
        return 1;
    }
    rage128_cap_in_block = 1;
    for (uint32_t o = 0; o < len; o += 4) { /* PCI GART: pages are scattered */
        uint32_t v;

        if (!rage128_pm4_bus_read(dev, vm + o, &v)) {
            rage128_cap_in_block = 0;
            return 0;
        }
        memcpy(dst + o, &v, (len - o >= 4) ? 4u : (len - o));
    }
    rage128_cap_in_block = 0;
    rage128_cap_bus_block(dev, vm, dst, len);
    return 1;
}

/* Write counterpart of rage128_guest_ram_readl: one dword stored straight
   into the ram array. The data written this way is surface data, such
   as a depth buffer, which the guest never executes, so no recompiled
   code page needs invalidating, and the store touches no CPU-thread
   state. Aperture addresses go through dma_bm_write instead. Returns 0
   for an address past the end of RAM. */
static inline int
rage128_guest_ram_writel(uint32_t phys, uint32_t val)
{
    if ((uint64_t) phys + 4 > ((uint64_t) mem_size << 10))
        return 0;
    memcpy(&ram[phys], &val, 4);
    return 1;
}

/* Write counterpart of rage128_pm4_bus_read, one dword. The address is
   resolved exactly as the read resolves it (the AGP aperture through
   dma_bm_write, or a lookup in the on-chip table), so the write lands
   in the guest page the matching read came from. Called only on the
   thread executing packets, never from a render worker: dma_bm_write
   invalidates recompiled code over the range it writes. Returns 0 when
   bus mastering is blocked, AGP_BASE is 0, or the table entry cannot be
   read. */
static int
rage128_pm4_bus_write(rage128_t *dev, uint32_t bus_addr, uint32_t val)
{
    uint32_t table = dev->pci_gart_page & 0xfffff000;
    uint32_t pte;
    uint32_t phys;

    if (!rage128_pm4_bus_master_ok(dev))
        return 0;

    if (!rage128_gart_walk(dev)) {
        /* Through the AGP aperture, with the same AGP_BASE check as the
           read. */
        if (!dev->agp_base)
            return 0;
        dma_bm_write(bus_addr, (const uint8_t *) &val, 4, 4);
        rage128_cap_bus_write(dev, bus_addr, 4);
        return 1;
    }

    if (!rage128_guest_ram_readl(table + ((bus_addr >> 12) & 0x1fff) * 4, &pte))
        return 0;
    phys = (pte & 0xfffff000) | (bus_addr & 0xfff);
    if (!rage128_guest_ram_writel(phys, val)) /* entry is in the aperture, as in the read */
        dma_bm_write(phys, (const uint8_t *) &val, 4, 4);
    rage128_cap_bus_write(dev, bus_addr, 4);
    return 1;
}

/* Write counterpart of rage128_pm4_bus_read_block: `len` bytes from `src`
   to card address `vm`, resolved the same way as the read; for example,
   a depth surface held in AGP memory is written back to the guest this
   way when a batch completes. With the on-chip table the copy goes dword by dword so each
   page is looked up as the read looked it up. Depth rows are padded to
   whole dwords, so a partial last dword is not expected, but the tail
   copy handles one. Called only on the thread executing packets.
   Returns 1 when the whole block was written, 0 on any failed write. */
int
rage128_pm4_bus_write_block(rage128_t *dev, uint32_t vm, const uint8_t *src, uint32_t len)
{
    if (!rage128_pm4_bus_master_ok(dev))
        return 0;
    if (!rage128_gart_walk(dev)) { /* AGP: aperture is linear in bus space */
        uint32_t bus = rage128_pm4_vm_addr(dev, vm);

        if (!dev->agp_base)
            return 0;
        dma_bm_write(bus, src, len, 4);
        rage128_cap_bus_write(dev, bus, len);
        return 1;
    }
    /* A bit 25 surface goes to the chipset aperture, as in the read: a
       depth surface read through the aperture has to be written back
       through it. Same AGP_BASE condition. */
    if ((vm & 0x02000000u) && dev->agp_base) {
        uint32_t bus = dev->agp_base + (vm & 0x01ffffffu);

        dma_bm_write(bus, src, len, 4);
        rage128_cap_bus_write(dev, bus, len);
        return 1;
    }
    for (uint32_t o = 0; o < len; o += 4) { /* PCI GART: pages are scattered */
        uint32_t v = 0;

        memcpy(&v, src + o, (len - o >= 4) ? 4u : (len - o));
        if (!rage128_pm4_bus_write(dev, vm + o, v))
            return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Indirect-buffer splice.

   An indirect buffer is submitted from the ring by a type-0 packet that
   writes PM4_IW_INDOFF and PM4_IW_INDSIZE; the PM4_IW_INDSIZE write
   starts the transfer (SDK: Indirect Buffer, p. 5-15 / PDF 111). The executor here
   runs behind the pump by up to a FIFO's worth of work, so a body read
   only when the walker reaches it can pick up whatever the guest has
   written into the buffer since it submitted it.

   The pump runs inside the guest's write to PM4_BUFFER_DL_WPTR, so the
   body is copied there, as part of taking in the submit packet, while
   the guest is stopped and the buffer holds what it submitted. The
   Windows 98 Direct3D driver fills a command buffer, waits on its
   256-entry fence ring, submits the buffer and only then takes a fresh
   one; it does not write a buffer it has already submitted (RE:
   ati3draa.dll @b00cc100, the flush path of driver 4.13.7192).

   The body dwords go into the FIFO right behind their submit packet,
   tagged RAGE128_CCE_TAG_IB, so the walker never reads guest memory for
   a buffer submitted through the ring. For that reason the copy is not
   put off past the WPTR write: when the FIFO is full the pump waits for
   the executor instead of returning. The one exception is a full FIFO
   behind a WAIT_UNTIL flip stall (see rage128_cce_fifo_reserve): that
   body is read from guest memory when the walker reaches it. */

static inline uint32_t
rage128_cce_fifo_space(rage128_t *dev, uint32_t wr)
{
    return RAGE128_CCE_FIFO_DWORDS - (wr - atomic_load(&dev->cce_fifo_rd));
}

/* Wait for the executor to free `need` FIFO dwords, so a body is never
   fetched later than the WPTR write that submitted it. Everything up to
   the last packet boundary is already published, so the executor has
   work to drain without the half-built submit packet the caller is
   holding back. A WAIT_UNTIL flip stall is not broken to make room: the
   work queued behind it is the next frame, and released early it would
   draw into the buffer still on display. A stalled executor cannot
   drain, and the vblank that releases it is driven from this same
   thread, so the wait gives up at once and the caller takes its
   fallback.

   Blocking here stops emulated time. The wait is bounded, about 250 ms
   of 1 ms waits, because it runs inside a guest MMIO write and a device
   bug must not freeze the machine. When it runs out, a splice leaves
   the body to be read from guest memory later, and a ring fetch resumes
   from the pump timer. Running out means the FIFO
   is too small for the workload, which is not expected with 2 MB
   against buffers of about 50 KB, so it is logged. The flip-stall exit
   is not logged: it is expected under load. Returns 1 when it left
   through that exit. CPU thread. */
static int
rage128_cce_fifo_reserve(rage128_t *dev, uint32_t wr, uint32_t need)
{
    int draining;
    int waited  = 0;
    int stalled = 0;

    if (rage128_cce_fifo_space(dev, wr) >= need)
        return 0;
    /* A drain wait in progress (it pumps) has already broken any flip
       stall. Outside one, only the pacer hold gives way to this wait
       (cce_room_req). */
    draining = atomic_load(&dev->cce_drain_req);
    atomic_store(&dev->cce_room_req, 1);
    while (dev->cce_thread_run && rage128_cce_fifo_space(dev, wr) < need
           && waited < 250) {
        if (!draining && atomic_load(&dev->cce_flip_stalled)) {
            stalled = 1;
            break;
        }
        thread_reset_event(dev->cce_idle_event);
        if (rage128_cce_fifo_space(dev, wr) >= need)
            break;
        thread_set_event(dev->cce_wake_event);
        thread_wait_event(dev->cce_idle_event, 1);
        waited++;
    }
    atomic_store(&dev->cce_room_req, 0);
    if (rage128_cce_fifo_space(dev, wr) < need && waited >= 250)
        pclog("[r128 pm4] IB splice gave up waiting for FIFO space: need=%u\n", need);
    return stalled;
}

/* Copy one indirect buffer into the FIFO behind its submit packet and
   return the new write index. Every body dword carries the submit
   packet's ring read pointer, so the retire pointer (and with it the
   host's read-pointer copy and PM4_BUFFER_DL_RPTR) passes the submit
   only once the whole buffer has executed.

   A buffer whose body submits another buffer is not spliced again: the
   inner body would have to sit inside this one's tagged run, which the
   walker, reading one run in order, cannot consume. The walker reads
   that inner body from guest memory. No such submit appears in captured
   traffic, so the "IB walk unspliced" log line in
   rage128_pm4_run_indirect marks something new rather than noise. */
static uint32_t
rage128_pm4_splice_ib(rage128_t *dev, uint32_t wr, uint32_t off, uint32_t n,
                      uint32_t rptr)
{
    int prev_fetch;
    int stalled;

    if (!n)
        return wr;
    /* At most half the FIFO, so the reserve wait below can always be
       met: the caller holds back no more than a 3-dword submit packet,
       and draining what is already published frees at least this much. */
    if (n > RAGE128_CCE_FIFO_DWORDS / 2) {
        pclog("[r128 pm4] IB too large to splice: off=%08x n=%u\n", off, n);
        return wr;
    }
    stalled = rage128_cce_fifo_reserve(dev, wr, n);
    if (rage128_cce_fifo_space(dev, wr) < n) {
        /* Flip stall, timeout or shutdown: the walker reads the body
           from guest memory. Only the flip stall is expected, so that
           case marks the submit's size dword (the one just copied, not
           yet published) with RAGE128_CCE_TAG_LIVE for the walker. */
        if (stalled) {
            dev->cce_fifo_tag[(wr - 1) & RAGE128_CCE_FIFO_MASK] = RAGE128_CCE_TAG_LIVE;
            dev->pump_stall_live++;
        }
        return wr;
    }

    /* These are fetches, not execution, so they are kept out of the
       capture (rage128_ring_fetch); the walker records each body dword
       when it pops it, which keeps the capture in execution order. */
    prev_fetch         = rage128_ring_fetch;
    rage128_ring_fetch = 1;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v;

        if (!rage128_pm4_bus_read(dev, rage128_pm4_vm_addr(dev, off + i * 4), &v)) {
            pclog("[r128 pm4] IB splice dead bus: off=%08x n=%u at=%u\n", off, n, i);
            rage128_ring_fetch = prev_fetch;
            return wr - i; /* nothing is published yet: drop the partial body */
        }
        dev->cce_fifo[wr & RAGE128_CCE_FIFO_MASK]      = v;
        dev->cce_fifo_rptr[wr & RAGE128_CCE_FIFO_MASK] = rptr;
        dev->cce_fifo_tag[wr & RAGE128_CCE_FIFO_MASK]  = RAGE128_CCE_TAG_IB;
        wr++;
    }
    rage128_ring_fetch = prev_fetch;
    return wr;
}

/* Pump: copy ring dwords into the FIFO, splicing each submitted indirect
   buffer in behind its submit packet. CPU thread only: the WPTR write,
   the pump timer, or a drain wait. dev->pm4_rptr is the fetch pointer
   and moves as dwords are copied; the guest sees the retire pointer
   instead (rage128_pm4_reg_read), which the pump writes to the host's
   read-pointer copy afterward. Returns 1 when the ring is fully
   fetched, 0 when the FIFO is full (the timer is re-armed), -1 on a
   failed bus read (bus mastering blocked or a bad GART entry; the next
   WPTR write retries). */
/* ------------------------------------------------------------------ */
static int
rage128_pm4_pump(rage128_t *dev)
{
    uint32_t mask;
    uint32_t wptr;
    uint32_t wr;
    int      copied = 0;
    int      result = 1;
    int      prev_fetch;

    /* Fetch only in a bus-master mode (rage128_pm4_ring_bm in
       vid_ati_rage128.h, which explains why). In a PIO mode the packets
       come through PM4_FIFO_DATA_EVEN / ODD, not from memory. A ring
       reset with the mode still set is handled by the backward-WPTR test
       in rage128_pm4_reg_write. */
    if (!rage128_pm4_ring_bm(dev))
        return 1;

    mask = rage128_pm4_ring_mask(dev);
    wptr = dev->pm4_wptr & mask;
    wr   = atomic_load(&dev->cce_fifo_wr);

    /* Ring fetches are kept out of the capture (see rage128_cap_bus_read).
       The flag is saved and restored rather than cleared so that a pump
       entered from inside another cannot leave it wrong. */
    prev_fetch         = rage128_ring_fetch;
    rage128_ring_fetch = 1;

    while (dev->pm4_rptr != wptr) {
        uint32_t v;
        uint32_t need;

        /* Four dwords of room: a submit packet is header, address and
           size, and it must not be split by stopping on a full FIFO,
           because the body is spliced at its last dword and the whole
           packet is published together. Inside a submit packet only the
           dwords it still owes are asked for: its header was taken with
           room for them, and stopping there would publish it split.

           The pump waits for room rather than leaving the rest to the
           pump timer. A fetch resumed by the timer runs after this WPTR
           write has returned and the guest has run on, so the buffer it
           would copy may already have been reused, which is what the
           splice exists to prevent. The wait is bounded, so a
           pathological case ends in a late fetch and a log line rather
           than a hang. Behind a flip stall the wait returns at once and
           the fetch resumes from the pump timer. */
        need = dev->pump_ib_state ? 3 - dev->pump_ib_state : 4;
        if (rage128_cce_fifo_space(dev, wr) < need) {
            static int backpressure_logged = 0;

            if (backpressure_logged < 8) {
                backpressure_logged++;
                rage128_log("[r128 pm4] ring fetch stalled on FIFO space (guest held)\n");
            }
            rage128_cce_fifo_reserve(dev, wr, need);
            if (rage128_cce_fifo_space(dev, wr) < need) {
                result = 0;
                break;
            }
        }
        if (!rage128_pm4_bus_read(dev,
                                  rage128_pm4_vm_addr(dev, dev->pm4_buffer_offset + dev->pm4_rptr * 4),
                                  &v)) {
            result = -1;
            break;
        }
        dev->pm4_rptr                             = (dev->pm4_rptr + 1) & mask;
        dev->cce_fifo[wr & RAGE128_CCE_FIFO_MASK] = v;
        /* The ring read pointer just past this dword: once the executor
           has run the packet holding it, the ring is retired up to here. */
        dev->cce_fifo_rptr[wr & RAGE128_CCE_FIFO_MASK] = dev->pm4_rptr;
        dev->cce_fifo_tag[wr & RAGE128_CCE_FIFO_MASK]  = 0;
        wr++;
        copied = 1;

        if (dev->pump_frame_rem) {
            dev->pump_frame_rem--;
            if (dev->pump_ib_state == 1) {
                /* Rounded as the PM4_IW_INDOFF write will round it, 24:0
                   with bits 2:0 zero, so the splice reads the body from
                   where the walk's register value points. */
                dev->pump_ib_addr  = v & 0x01fffff8u;
                dev->pump_ib_state = 2;
            } else if (dev->pump_ib_state == 2) {
                dev->pump_ib_state = 0;
                /* masked as the PM4_IW_INDSIZE write will mask it, so the
                   splice and the walk agree on the length */
                wr = rage128_pm4_splice_ib(dev, wr, dev->pump_ib_addr,
                                           v & RAGE128_PM4_IW_INDSIZE_MASK, dev->pm4_rptr);
            }
        } else {
            switch (RAGE128_PM4_TYPE(v)) {
                case 0:
                    dev->pump_frame_rem = RAGE128_PM4_COUNT(v);
                    if (v == 0x000101ce) /* PM4_IW_INDOFF and PM4_IW_INDSIZE: a buffer submit
                                            (CCE_PACKET0(R128_PM4_IW_INDOFF, 1), linux r128
                                            DRM r128_state.c r128_cce_dispatch_indirect) */
                        dev->pump_ib_state = 1;
                    break;
                case 1:
                    dev->pump_frame_rem = 2;
                    break;
                case 3:
                    dev->pump_frame_rem = RAGE128_PM4_COUNT(v);
                    break;
                default: /* type 2 filler: no payload */
                    break;
            }
        }
        /* Publish only outside a submit packet, so the executor never
           reaches the PM4_IW_INDSIZE write before the body behind it is
           in the FIFO. */
        if (!dev->pump_ib_state)
            atomic_store(&dev->cce_fifo_wr, wr);
    }

    rage128_ring_fetch = prev_fetch;
    atomic_store(&dev->cce_fifo_wr, wr);

    if (copied)
        thread_set_event(dev->cce_wake_event);

    /* Write the read pointer to the host's copy at PM4_BUFFER_DL_RPTR_ADDR.
       The value is the retire pointer, not the fetch pointer: the
       written-back pointer "reflects where parser is" in the ring (CCE 3D
       supplement, PM4_BUFFER_WM_CNTL, PM4_BUFFER_CNTL_WB_WM). The
       Windows 2000 driver has been observed reusing a command buffer
       once this pointer has passed it, so a fetch pointer here lets it
       overwrite buffers not yet drawn. The executor advances
       cce_retire_rptr; the write itself stays on the CPU thread. The
       supplement calls the address a
       32 MB AGP/PCI pointer, but the Windows drivers have been observed
       programming a host physical address beyond the 32 MB GART range,
       so it is written there directly. */
    {
        uint32_t retire = atomic_load(&dev->cce_retire_rptr);

        /* The write-back is a bus-master write too. While mastering is
           blocked cce_shadow_last is left alone, so a later pump (the
           re-enable kick) retries. PM4_BUFFER_CNTL bit 27, "Never update
           RPTR in system memory" (CCE 3D supplement, PM4_BUFFER_CNTL),
           holds the write-back as well: it is for drivers that poll
           PM4_BUFFER_DL_RPTR instead, and the Linux DRM sets it while
           the CCE runs (linux r128 DRM r128_cce.c r128_do_cce_start).
           cce_shadow_last is left alone under the bit too, so the first
           pump after the driver clears it publishes the held pointer. */
        if (dev->pm4_rptr_addr && !(dev->pm4_buffer_cntl & 0x08000000u)
            && retire != dev->cce_shadow_last
            && rage128_pm4_bus_master_ok(dev)) {
            dev->cce_shadow_last = retire;
            dma_bm_write(dev->pm4_rptr_addr, (const uint8_t *) &retire, 4, 4);
        }

        /* Re-arm the timer while there is unfetched work (result 0) or
           the executor is still behind the fetch pointer, so the retire
           pointer keeps reaching the host's copy after the guest stops
           submitting. The test uses the same `retire` value that was
           written: with a second load the executor could finish in
           between, the test would see the ring done, no pump would be
           scheduled, and the host's copy would stay at the older value
           while the guest polls it. */
        if ((result == 0 || retire != dev->pm4_rptr)
            && !timer_is_enabled(&dev->cce_pump_timer))
            timer_set_delay_u64(&dev->cce_pump_timer, 100 * TIMER_USEC);
    }

    return result;
}

static void
rage128_pm4_pump_timer(void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint64_t   t0  = dev->slice_prof ? rage128_now_ns() : 0;

    rage128_pm4_pump(dev);
    if (dev->slice_prof)
        dev->pump_win_ns += rage128_now_ns() - t0;
}

/* Re-enable kick: a ring set up while bus mastering was blocked has no
   pending WPTR write or timer to restart it, so the writes that enable
   mastering (PCI COMMAND, BUS_CNTL) call this to pump. CPU thread only. */
void
rage128_pm4_kick(rage128_t *dev)
{
    rage128_pm4_pump(dev);
}

/* GEN_RESET_CNTL.SOFT_RESET_GUI pulse (RRG: GEN_RESET_CNTL, p. 3-200 /
   PDF 218), which both open-source drivers issue to recover the engine
   (xf86-video-r128 r128_accel.c R128EngineReset; linux r128 DRM
   r128_cce.c r128_do_engine_reset). Pending CCE work is abandoned: the
   executor is stopped, fetched but unexecuted FIFO dwords and any
   partial indirect-buffer state are dropped, and the retire pointer is
   set to the fetch pointer. The ring registers, PM4_BUFFER_CNTL among
   them, keep their values. The CCE 3D supplement's PM4_BUFFER_CNTL
   entry says the register "is reset with SOFT_GUI_RESET", but the
   Windows 2000 driver has been observed writing the ring size into it
   before its reset pulses and afterward changing only the mode, by
   reading the register and writing it back with the mode bits
   replaced. A pulse that cleared the size would leave that driver a
   2-dword ring that never fetches its commands, so the register is
   modeled as surviving the pulse. CPU thread only: a write from the
   command stream cannot stop its own executor. */
void
rage128_pm4_gui_reset(rage128_t *dev)
{
    if (rage128_on_cce_thread)
        return;
    /* Done whether or not the executor looks busy, and it waits for
       cce_parked rather than for cce_executing to drop: that flag also
       drops between packets, so an
       executor with FIFO dwords still queued would read as idle here and
       then go on running the work the pulse abandons. cce_parked is set
       by the executor itself once it has stopped touching the FIFO read
       index and the retire pointers. */
    if (dev->cce_thread) {
        atomic_store(&dev->cce_parked, 0);
        atomic_store(&dev->cce_abort, 1);
        thread_set_event(dev->cce_wake_event);
        /* A WAIT_UNTIL flip stall waits for this thread to latch the
           flip. The cce_abort test in its loop is what ends it; the event
           only saves it waiting out its 1 ms timeout. */
        thread_set_event(dev->cce_flip_event);
        thread_reset_event(dev->cce_idle_event);
        while (dev->cce_thread_run && !atomic_load(&dev->cce_parked))
            thread_wait_event(dev->cce_idle_event, 1);
    }
    atomic_store(&dev->cce_fifo_rd, atomic_load(&dev->cce_fifo_wr));
    atomic_store(&dev->cce_hung, 0);
    dev->pm4_ind_busy    = 0;
    dev->pm4_ind_pending = 0;
    dev->pump_frame_rem  = 0;
    dev->pump_ib_state   = 0;
    /* The executor is parked, so its own cce_retire_pending is reset
       too; otherwise the next packet would publish the abandoned
       position over the new read pointer. */
    atomic_store(&dev->cce_retire_rptr, dev->pm4_rptr);
    dev->cce_retire_pending = dev->pm4_rptr;
    /* Dropped here, before the abort is released, rather than by the
       caller: once released, the executor wakes into its idle-path
       rage128_raster_flush, which would draw the batch being dropped.
       The same goes for an armed host-data operation, which a resumed 2D
       packet would otherwise keep feeding. PM4_VC_DEBUG_CONFIG is "reset
       with SOFT_RESET_GUI (all fields default to zero)" (RRG:
       PM4_VC_DEBUG_CONFIG, p. 3-221 / PDF 239). */
    rage128_raster_abandon(dev);
    rage128_gpu_abandon(dev); /* the GPU backend's deferred batch */
    dev->hostdata_active     = 0;
    dev->hostdata_ndw        = 0;
    dev->pm4_vc_debug_config = 0;
    /* Released only now: every store above has to be done before the
       executor may look at the FIFO again. */
    if (dev->cce_thread) {
        atomic_store(&dev->cce_abort, 0);
        thread_set_event(dev->cce_wake_event);
    }
}

/* Whether a register write lands in the CCE's own fetch-control block,
   which a packet in the command stream may not write. The drivers
   program the ring registers, PM4_BUFFER_OFFSET (0x700) through
   PM4_BUFFER_DL_WPTR_DELAY (0x718), and PM4_MICRO_CNTL (0x7fc) by MMIO
   only, so a packet aimed at them is a parse that has lost its place and
   is reading other memory as packets. The refusal is not documented
   for these registers; it is modeled so that a lost parse cannot point
   the fetch at memory that is not a ring and run away.
   PM4_VC_DEBUG_CONFIG (0x7a4) is documented as not writable by the
   parser, "PIO only" (RRG: PM4_VC_DEBUG_CONFIG, p. 3-221 / PDF 239).
   PM4_VC_FPU_SETUP (0x71c), right after the ring registers, is a 3D
   setup register that the drivers do write through the ring (linux r128
   DRM r128_state.c r128_emit_setup; xf86-video-r128 r128_exa_render.c
   COMPOSITE_SETUP), so the range stops at 0x718. */
static int
rage128_pm4_reg_in_fetch_block(uint32_t reg, uint32_t nregs)
{
    for (uint32_t i = 0; i < nregs; i++) {
        uint32_t r = reg + i * 4;

        if ((r >= RAGE128_PM4_BUFFER_OFFSET && r <= RAGE128_PM4_BUFFER_DL_WPTR_DELAY)
            || r == RAGE128_PM4_MICRO_CNTL
            || r == RAGE128_PM4_VC_DEBUG_CONFIG)
            return 1;
    }
    return 0;
}

/* Realtime pacer (pace_hold_us in vid_ati_rage128.h): sleep off the
   current per-flip hold in 1 ms steps, so a drain request, a request
   for FIFO room, an abort or shutdown ends it within 1 ms. It runs at
   each flip, a CRTC_OFFSET write executed on the CCE thread
   (vid_ati_rage128_display.c). Short holds spread over packets only add
   latency that the driver's queued work hides, so the guest never waits
   and its rate does not drop; a hold of frame size uses up what the
   driver has queued and makes it wait. CCE thread only. */
void
rage128_pace_consume(rage128_t *dev)
{
    int      hold = atomic_load(&dev->pace_hold_us);
    int      held = 0;
    uint64_t t0   = rage128_now_ns();

    while (dev->cce_thread_run
           && held < hold
           && !atomic_load(&dev->cce_drain_req)
           && !atomic_load(&dev->cce_room_req)
           && !atomic_load(&dev->cce_abort)) {
        plat_delay_ms(1);
        held += 1000;
    }
    if (held) {
        atomic_fetch_add(&dev->pace_sleep_ns, rage128_now_ns() - t0);
        atomic_fetch_add(&dev->pace_sleep_ask_ns, (uint64_t) held * 1000u);
    }
}

/* Wait until the engine is idle: ring fetched, FIFO drained, executor
   between packets. CPU thread only; called before the CPU touches
   engine state directly. Drivers wait for idle themselves by polling
   GUI_STAT (xf86-video-r128 r128_accel.c R128WaitForIdle); this wait
   keeps an access that skipped the poll from racing the executor. It
   sets cce_drain_req so a WAIT_UNTIL flip stall on the CCE thread gives
   way; the flip itself still latches at the next vblank. */
int (*rage128_harness_drain_hook)(rage128_t *dev);

void
rage128_pm4_drain_wait(rage128_t *dev)
{
    if (!dev->cce_thread || rage128_on_cce_thread)
        return;
    if (!rage128_pm4_active(dev)) {
        /* Executor idle: draw any deferred 3D batch so the caller's
           direct access sees the finished pixels. The flush is safe here
           because the executor is not submitting. */
        rage128_raster_flush(dev);
        return;
    }

    atomic_store(&dev->cce_drain_req, 1);
    thread_set_event(dev->cce_flip_event); /* break a flip stall */
    for (long iter = 0;; iter++) {
        int pumped;

        /* Offline replay: the ring dwords that complete a packet the
           executor is waiting on come from the capture stream, which
           only this thread reads, so the replay harness feeds them here. */
        if (rage128_harness_drain_hook) {
            int h = rage128_harness_drain_hook(dev);

            if (h == 1)
                continue;
            if (h == 2) { /* stream has no more dwords for this packet */
                atomic_store(&dev->cce_abort, 1);
                thread_set_event(dev->cce_wake_event);
                while (atomic_load(&dev->cce_executing))
                    thread_wait_event(dev->cce_idle_event, 1);
                atomic_store(&dev->cce_abort, 0);
                break;
            }
        }
        thread_reset_event(dev->cce_idle_event);
        pumped = rage128_pm4_pump(dev);

        if (!rage128_pm4_active(dev))
            break;
        /* Hung engine with the executor stopped: it will never go idle.
           Give up with the engine busy, as on a failed bus read. */
        if (atomic_load(&dev->cce_hung) && !atomic_load(&dev->cce_executing))
            break;
        if (pumped == -1
            && atomic_load(&dev->cce_fifo_rd) == atomic_load(&dev->cce_fifo_wr)) {
            /* Failed ring read mid-stream (bus mastering blocked or a bad
               GART entry): nothing more will arrive. An executor waiting
               mid-packet for more dwords is aborted, then the wait gives
               up with the engine still busy until the driver reprograms
               it (modeled). An executor that is not waiting for dwords
               is still running a packet, such as an indirect buffer read
               from guest memory, which does not depend on the ring, so
               it is left to finish. cce_starved is loaded last, as in
               the test below. */
            if (atomic_load(&dev->cce_executing)) {
                if (!atomic_load(&dev->cce_starved)) {
                    thread_set_event(dev->cce_wake_event);
                    thread_wait_event(dev->cce_idle_event, 1);
                    continue;
                }
                atomic_store(&dev->cce_abort, 1);
                thread_set_event(dev->cce_wake_event);
                while (atomic_load(&dev->cce_executing))
                    thread_wait_event(dev->cce_idle_event, 1);
                atomic_store(&dev->cce_abort, 0);
            }
            break;
        }
        /* Executor stuck on a packet that can never complete: the ring is
           fully fetched and the FIFO empty, yet it is still mid-packet,
           because a header's count is larger than everything the ring
           holds and its blocking rage128_cce_get never returns. The
           packet is aborted rather than hanging the emulated CPU. The
           Linux DRM notes that stopping a busy CCE drops the triangles
           still in flight (linux r128 DRM r128_cce.c r128_cce_stop); the
           abort here drops the stuck packet the same way. The iter test
           avoids a false hit while the executor is still being scheduled;
           a real hang never makes progress. cce_starved tells that wait
           apart from a packet still running with nothing queued behind
           it, such as an indirect buffer read from guest memory, which an
           abort would cut short. It is loaded last: the pop clears it
           before the read index moves, so a set flag seen after an empty
           FIFO cannot belong to such a walk. */
        if (iter >= 8
            && atomic_load(&dev->cce_executing)
            && atomic_load(&dev->cce_fifo_rd) == atomic_load(&dev->cce_fifo_wr)
            && (!rage128_pm4_ring_bm(dev) /* PIO mode: no ring to wait on */
                || dev->pm4_rptr == (dev->pm4_wptr & rage128_pm4_ring_mask(dev)))
            && atomic_load(&dev->cce_starved)) {
            atomic_store(&dev->cce_abort, 1);
            thread_set_event(dev->cce_wake_event);
            while (atomic_load(&dev->cce_executing))
                thread_wait_event(dev->cce_idle_event, 1);
            atomic_store(&dev->cce_abort, 0);
            break;
        }
        thread_set_event(dev->cce_wake_event);
        thread_wait_event(dev->cce_idle_event, 1);
    }
    /* Engine idle: draw any deferred 3D batch. */
    rage128_raster_flush(dev);
    atomic_store(&dev->cce_drain_req, 0);
}

/* The display moved something a WAIT_UNTIL stall waits on: vsync latched
   a pending CRTC_OFFSET, or the raster crossed a CRTC_GUI_TRIG_VLINE
   window edge (vid_ati_rage128_display.c). */
void
rage128_pm4_flip_notify(rage128_t *dev)
{
    if (dev->cce_flip_event)
        thread_set_event(dev->cce_flip_event);
}

/* Queue a direct CPU register write behind the engine's pending work as
   a made-up one-register type-0 packet. On the chip, host writes to GUI
   registers go through the command FIFO: GUI_STAT.GUI_FIFOCNT counts its
   free entries (RRG: GUI_STAT, p. 3-244 / PDF 262), and the X driver
   waits for free entries before its register writes (xf86-video-r128
   r128_accel.c R128WaitForFifoFunction). WAIT_UNTIL stalls that FIFO
   (RRG: WAIT_UNTIL, p. 3-239 / PDF 257), so a WAIT_UNTIL written by
   MMIO holds later draws behind a pending flip, and queuing the write
   keeps that order. The register file queues only WAIT_UNTIL this way.
   CPU thread only. Returns 0, and
   the caller drains and writes directly instead, when the offset does
   not fit the header, when unfetched ring dwords could not be fetched
   first (the queued write must not jump ahead of them) or when the
   FIFO is full. */
int
rage128_pm4_enqueue_write(rage128_t *dev, uint32_t off, uint32_t val)
{
    uint32_t rd;
    uint32_t wr;

    if (!dev->cce_thread)
        return 0;
    /* The header's BASE_INDEX is the dword index of the register in
       bits 10:0 (SDK: Type-0 CCE Packet, p. F-3 / PDF 293), and the
       parser decodes it with that width, so only a dword-aligned offset
       up to 0x1ffc can be queued. Anything else is refused before the
       FIFO changes: masking a larger offset into the field would queue
       a write to a different register. */
    if ((off & 3) != 0 || off > 0x1ffc)
        return 0;
    if (dev->pm4_rptr != (dev->pm4_wptr & rage128_pm4_ring_mask(dev))
        && rage128_pm4_pump(dev) != 1)
        return 0;
    rd = atomic_load(&dev->cce_fifo_rd);
    wr = atomic_load(&dev->cce_fifo_wr);
    if (RAGE128_CCE_FIFO_DWORDS - (wr - rd) < 2)
        return 0;
    dev->cce_fifo[wr & RAGE128_CCE_FIFO_MASK]       = (off >> 2) & 0x7ff;
    dev->cce_fifo[(wr + 1) & RAGE128_CCE_FIFO_MASK] = val;
    /* A queued MMIO write is not ring traffic, so it carries no ring read
       pointer (0xffffffff) and moves nothing on retire. The tags are
       written too: a slot keeps whatever the previous pass through the
       FIFO left in it, often an indirect-buffer tag in a Direct3D
       session, and a slot still tagged that way would be popped as body
       past the end of a buffer. */
    dev->cce_fifo_rptr[wr & RAGE128_CCE_FIFO_MASK]       = 0xffffffffu;
    dev->cce_fifo_rptr[(wr + 1) & RAGE128_CCE_FIFO_MASK] = 0xffffffffu;
    dev->cce_fifo_tag[wr & RAGE128_CCE_FIFO_MASK]        = 0;
    dev->cce_fifo_tag[(wr + 1) & RAGE128_CCE_FIFO_MASK]  = 0;
    atomic_store(&dev->cce_fifo_wr, wr + 2);
    thread_set_event(dev->cce_wake_event);
    return 1;
}

/* Packets written by the host in a PIO mode. The SDK writes them in
   pairs, the first dword to PM4_FIFO_DATA_EVEN and the second to
   PM4_FIFO_DATA_ODD, and pads an odd count with a type-2 packet (SDK:
   Memory Apertures, Table 2-10, p. 2-22 / PDF 38; SDK: Ring Buffer
   Server, p. 5-13 / PDF 109). Modeled as: the EVEN write is held, and
   the ODD write queues both. The pair carries no ring position and is
   captured as J dwords, not as a register write. Only a host write
   reaches the ports here; a packet in the command stream that writes
   them is not taken. */
int
rage128_pm4_fifo_data_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask)
{
    uint32_t wr;
    int      odd = (off == RAGE128_PM4_FIFO_DATA_ODD);

    if ((off != RAGE128_PM4_FIFO_DATA_EVEN && !odd) || !rage128_pm4_pio(dev)
        || !dev->cce_thread || rage128_on_cce_thread || rage128_in_indirect)
        return 0;
    dev->pm4_fifo_data[odd] = (dev->pm4_fifo_data[odd] & ~mask) | val;
    if (!odd)
        return 1;
    wr = atomic_load(&dev->cce_fifo_wr);
    rage128_cce_fifo_reserve(dev, wr, 2);
    if (rage128_cce_fifo_space(dev, wr) < 2) {
        pclog("[r128 pm4] PIO pair dropped: CCE FIFO full\n");
        return 1;
    }
    for (int i = 0; i < 2; i++) {
        dev->cce_fifo[(wr + i) & RAGE128_CCE_FIFO_MASK]      = dev->pm4_fifo_data[i];
        dev->cce_fifo_rptr[(wr + i) & RAGE128_CCE_FIFO_MASK] = 0xffffffffu;
        dev->cce_fifo_tag[(wr + i) & RAGE128_CCE_FIFO_MASK]  = 0;
    }
    atomic_store(&dev->cce_fifo_wr, wr + 2);
    thread_set_event(dev->cce_wake_event);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Executor. CCE thread: pop dwords from the FIFO and run them as
   packets. The pump copies ring dwords unchanged, so the packet format
   is the ring's. */
/* ------------------------------------------------------------------ */

/* Blocking dword pop: a packet can run ahead of the pump in the middle
   of its body, and the FIFO is then refilled from the pump timer or the
   drain loop. Returns 0 only on shutdown or an abort; a drain in
   progress means more data is coming. The abort is checked at the pop
   as well as inside the empty-FIFO wait, because a reset pulse arrives
   with FIFO dwords still queued behind the current packet: the X
   driver's recovery pulses the reset when its FIFO or idle wait times
   out (xf86-video-r128 r128_accel.c R128WaitForFifoFunction,
   R128WaitForIdle), which is exactly when work is outstanding. Checked
   only in the wait, the executor would run all the queued work instead
   of dropping it. Stopping here leaves nothing behind: rd does not move
   past the unconsumed body, and the pulse sets rd = wr once the executor
   has parked. The next dword after an abort is parsed as a new header,
   so the dropped packet is not resumed; after the pulse the drivers
   reset the ring and start again with whole packets. */
static int
rage128_cce_get(rage128_t *dev, uint32_t *val)
{
    uint32_t rd = atomic_load(&dev->cce_fifo_rd);

    while (rd == atomic_load(&dev->cce_fifo_wr)) {
        if (!dev->cce_thread_run)
            return 0;
        if (atomic_load(&dev->cce_abort))
            return 0;
        atomic_store(&dev->cce_starved, 1);
        /* Waiting for the rest of a packet can last as long as the guest
           takes to write it; scanout's submit request is answered here
           rather than after the packet (see rage128_gpu_scan_serve). */
        if (dev->gpu)
            rage128_gpu_scan_serve(dev);
        thread_wait_event(dev->cce_wake_event, 1);
        thread_reset_event(dev->cce_wake_event);
    }
    if (atomic_load(&dev->cce_abort))
        return 0;
    atomic_store(&dev->cce_starved, 0);
    *val = dev->cce_fifo[rd & RAGE128_CCE_FIFO_MASK];
    /* Read before rd moves: a full FIFO reuses the slot at once. */
    dev->cce_pop_tag = dev->cce_fifo_tag[rd & RAGE128_CCE_FIFO_MASK];
    /* Keep this dword's ring position as the pending retire point,
       skipping queued MMIO writes (0xffffffff). cce_retire_rptr is set
       from it only after the whole packet has run, so a draw's buffer is
       not reported done to the host's read-pointer copy before it has
       been drawn. */
    {
        uint32_t rr = dev->cce_fifo_rptr[rd & RAGE128_CCE_FIFO_MASK];
        if (rr != 0xffffffffu)
            dev->cce_retire_pending = rr;
        if (dev->cap_file) {
            fprintf(dev->cap_file, "%c %08x\n",
                    (rr != 0xffffffffu) ? 'D' : 'J', *val);
            rage128_cap_tick(dev);
        }
    }
    atomic_store(&dev->cce_fifo_rd, rd + 1);
    return 1;
}

/* Pop one spliced indirect-buffer dword, or return 0 when the next FIFO
   dword is not body. Never blocks: a body is published together with its
   submit packet, so "not body" means the buffer was never spliced (an
   MMIO submit, an oversized buffer, a failed read), not that it is late.
   `addr` is the dword's card address, captured here rather than at
   splice time so the capture stays in execution order. CCE thread. */
static int
rage128_cce_get_ib(rage128_t *dev, uint32_t *val, uint32_t addr)
{
    uint32_t rd = atomic_load(&dev->cce_fifo_rd);

    if (rd == atomic_load(&dev->cce_fifo_wr)
        || dev->cce_fifo_tag[rd & RAGE128_CCE_FIFO_MASK] != RAGE128_CCE_TAG_IB)
        return 0;
    *val = dev->cce_fifo[rd & RAGE128_CCE_FIFO_MASK];
    atomic_store(&dev->cce_fifo_rd, rd + 1);
    if (dev->cap_file)
        rage128_cap_run_add(dev, rage128_pm4_vm_addr(dev, addr), *val);
    return 1;
}

/* Whether a type-3 2D operation needs the deferred 3D batch drawn first.
   2D operations draw into the same surfaces, so they do, except
   SET_SCISSORS (0x1e; SDK: Summary of the CEE Packets, Table 4-13,
   p. F-10 / PDF 300) and NOP, which draw nothing and would only break
   the batch up. Queued draws keep a copy of their scissor, so a
   SET_SCISSORS cannot change one already queued. The Windows 2000
   Direct3D driver has been observed sending SET_SCISSORS for every draw
   block, about 900 times a second. */
static int
rage128_2d_op_flushes(uint32_t hdr)
{
    uint32_t op = RAGE128_PM4_T3_OPCODE(hdr);

    return op != 0x1e && op != RAGE128_PM4_OP_NOP;
}

/* Execute one packet whose header has been popped. */
static void
rage128_cce_packet(rage128_t *dev, uint32_t hdr)
{
    uint32_t  count;
    uint32_t *pl = dev->cce_pl;

    switch (RAGE128_PM4_TYPE(hdr)) {
        case 0: /* register writes starting at BASE_INDEX [10:0], a dword index */
            {
                uint32_t reg = RAGE128_PM4_T0_REG(hdr);

                count = RAGE128_PM4_COUNT(hdr);
                for (uint32_t i = 0; i < count; i++) {
                    uint32_t v;

                    if (!rage128_cce_get(dev, &v))
                        return;
                    /* ONE_REG_WR, header bit 15: every body dword goes to the
                       same register instead of to consecutive ones (SDK:
                       Type-0 CCE Packet, p. F-3 / PDF 293). The Windows 98 2D
                       driver has been observed setting it on every type-0
                       header, and it streams palette entries to PALETTE_DATA
                       this way. */
                    uint32_t treg = (hdr & 0x8000) ? reg : reg + i * 4;
                    /* A write to the CCE's own fetch registers is refused (see
                       rage128_pm4_reg_in_fetch_block). The dword is still
                       consumed above, so the parse stays aligned; only the
                       register write is dropped. */
                    if (!rage128_pm4_reg_in_fetch_block(treg, 1))
                        rage128_reg_poke(dev, treg, v);
                }
                break;
            }

        case 1: /* two register writes, to the dword indices REG_INDEX1
                   [10:0] and REG_INDEX2 [21:11]; the registers need not
                   be adjacent (SDK: Type 1 CCE Packet, p. F-5 / PDF 295).
                   The Linux DRM sends SETUP_CNTL and PM4_VC_FPU_SETUP
                   this way (linux r128 DRM r128_state.c r128_emit_setup). */
            for (uint32_t i = 0; i < 2; i++) {
                uint32_t v;
                uint32_t treg = i ? RAGE128_PM4_T1_REG1(hdr)
                                  : RAGE128_PM4_T1_REG0(hdr);

                if (!rage128_cce_get(dev, &v))
                    return;
                /* The same refusal as in the type-0 case. Each 11-bit
                   index reaches byte offset 0x1ffc, so a stray type-1
                   header can name the ring registers directly. Captured
                   Windows 98 ring traffic contains no type-1 packet at
                   all, so under that driver every one seen is a parse
                   that has lost its place. */
                if (!rage128_pm4_reg_in_fetch_block(treg, 1))
                    rage128_reg_poke(dev, treg, v);
            }
            break;

        case 2: /* filler */
            break;

        case 3:
            {
                uint32_t n;

                count = RAGE128_PM4_COUNT(hdr);
                n     = count > RAGE128_PM4_MAX_PAYLOAD ? RAGE128_PM4_MAX_PAYLOAD : count;
                for (uint32_t i = 0; i < count; i++) {
                    uint32_t v;

                    if (!rage128_cce_get(dev, &v))
                        return;
                    if (i < n)
                        pl[i] = v;
                }
                /* The MPEG opcode first, then the 3D opcodes, then the 2D
                   dispatcher, which draws any deferred 3D batch first unless
                   rage128_2d_op_flushes says it need not. */
                if (rage128_mpeg_packet3(dev, hdr, pl, n))
                    break;
                if (!rage128_3d_packet3(dev, hdr, pl, n)) {
                    if (rage128_2d_op_flushes(hdr)) {
                        rage128_raster_flush(dev);
                        if (dev->gpu)
                            rage128_gpu_flush(dev, RAGE128_PM4_T3_OPCODE(hdr));
                    }
                    rage128_2d_packet3(dev, hdr, pl, n);
                }
                break;
            }

        default:
            break;
    }
}

static void
rage128_cce_thread(void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

#ifdef __APPLE__
    /* One tier below the emulation thread (see rb_worker_thread). */
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif
    rage128_on_cce_thread = 1;

    int did_work = 0; /* a packet completed since the last idle transition */

    while (dev->cce_thread_run) {
        uint32_t hdr;

        /* Marked busy before the FIFO is checked, so a status read never
           sees idle between popping a packet's last dword and finishing
           the packet. */
        atomic_store(&dev->cce_executing, 1);
        /* Abort pending: start no further packet and set cce_parked.
           cce_executing drops between packets on its own, so a resetting
           CPU thread that waited only on it could store cce_fifo_rd and
           the retire pointers while this thread is about to pop the next
           packet. Once cce_parked is set, those belong to the CPU thread. */
        if (atomic_load(&dev->cce_abort)) {
            atomic_store(&dev->cce_executing, 0);
            atomic_store(&dev->cce_parked, 1);
            thread_set_event(dev->cce_idle_event);
            thread_wait_event(dev->cce_wake_event, 1);
            thread_reset_event(dev->cce_wake_event);
            continue;
        }
        atomic_store(&dev->cce_parked, 0);
        /* Hung engine: start nothing more. Only the reset pulse, through
           the abort above, clears it. */
        if (atomic_load(&dev->cce_hung)) {
            atomic_store(&dev->cce_executing, 0);
            thread_set_event(dev->cce_idle_event);
            thread_wait_event(dev->cce_wake_event, -1);
            thread_reset_event(dev->cce_wake_event);
            continue;
        }
        /* Between packets: hand over the pending GPU segment if scanout
           asked for it. Ahead of the out-of-work test, so a request made
           during the last packet is answered before the executor sleeps,
           and one whose wake ended a sleep is answered right here. */
        if (dev->gpu)
            rage128_gpu_scan_serve(dev);
        if (atomic_load(&dev->cce_fifo_rd) == atomic_load(&dev->cce_fifo_wr)) {
            /* Out of submitted work: draw any deferred 3D batch now, so
               the finished frame is in VRAM before scanout shows it. This
               covers clients that flip without a WAIT_UNTIL. It runs on
               the CCE thread, so it cannot race a submit. */
            rage128_raster_flush(dev);
            /* GUI_IDLE_INT event, GEN_INT_STATUS bit 19 (RRG:
               GEN_INT_STATUS, p. 3-196 / PDF 214): set only when
               the engine goes from busy to idle, so a spurious wake does
               not set again a bit the driver has acknowledged. It is set
               before cce_executing drops, so a status read that sees idle
               also sees the event. The CPU thread folds it into
               GEN_INT_STATUS. */
            if (did_work) {
                did_work = 0;
                atomic_store(&dev->gui_idle_event, 1);
            }
            atomic_store(&dev->cce_executing, 0);
            thread_set_event(dev->cce_idle_event);
            if (dev->ftl_en) {
                dev->ftl_idle_t0 = rage128_now_ns();
                if (dev->cce_busy_t0)
                    dev->cce_busy_ns += dev->ftl_idle_t0 - dev->cce_busy_t0;
                dev->cce_busy_t0 = 0;
            }
            /* Out of work and about to sleep: use the idle time to wait
               for the GPU to finish the frame scanout is about to read,
               so the emulation thread's per-line wait does not have to.
               Clients that flip get this wait at the CRTC_OFFSET write;
               clients that draw into the displayed buffer have no other
               point for it. It comes after idle is published, so a status
               read that sees idle is not held behind a GPU frame, and
               before the sleep, so new ring work still finds the wake
               event set. */
            rage128_scanout_fence_idle(dev);
            thread_wait_event(dev->cce_wake_event, -1);
            thread_reset_event(dev->cce_wake_event);
            if (dev->ftl_en && dev->ftl_idle_t0) {
                uint64_t t = rage128_now_ns();

                dev->ftl_idle_ns += t - dev->ftl_idle_t0;
                dev->ftl_idle_t0 = 0;
                dev->cce_busy_t0 = t;
            }
            continue;
        }
        if (rage128_cce_get(dev, &hdr)) {
            rage128_cce_packet(dev, hdr);
            did_work = 1;
        }
        /* Publish the retire pointer only after the packet has run. A
           type-3 draw pops all its body dwords before its handler runs,
           so a retire pointer advanced at each pop would report the
           buffer done while the draw is still pending, and the Windows
           2000 driver has been observed reusing and presenting such a
           buffer early. */
        if (!atomic_load(&dev->cce_hung)) /* a hung submit never retires */
            atomic_store(&dev->cce_retire_rptr, dev->cce_retire_pending);
        atomic_store(&dev->cce_executing, 0);
    }
    rage128_cap_run_flush(dev); /* this thread's pending capture run */
}

void
rage128_pm4_thread_init(rage128_t *dev)
{
    dev->cce_fifo      = calloc(RAGE128_CCE_FIFO_DWORDS, sizeof(uint32_t));
    dev->cce_fifo_rptr = calloc(RAGE128_CCE_FIFO_DWORDS, sizeof(uint32_t));
    dev->cce_fifo_tag  = calloc(RAGE128_CCE_FIFO_DWORDS, sizeof(uint8_t));
    atomic_store(&dev->cce_retire_rptr, 0);
    dev->cce_retire_pending = 0;
    atomic_store(&dev->cce_batch_pending, 0);
    dev->cce_shadow_last   = 0xffffffffu;
    dev->cce_pl            = calloc(RAGE128_PM4_MAX_PAYLOAD, sizeof(uint32_t));
    dev->ind_pl            = calloc(RAGE128_PM4_MAX_PAYLOAD, sizeof(uint32_t));
    dev->vc_bundle.idx     = calloc(4 + 32768, sizeof(uint32_t));
    dev->vc_bundle.pending = 0;
    dev->pump_frame_rem    = 0;
    dev->pump_ib_state     = 0;
    dev->pump_stall_live   = 0;
    dev->cce_pop_tag       = 0;
    atomic_store(&dev->cce_fifo_rd, 0);
    atomic_store(&dev->cce_fifo_wr, 0);
    atomic_store(&dev->cce_executing, 0);
    atomic_store(&dev->cce_drain_req, 0);
    atomic_store(&dev->cce_room_req, 0);
    atomic_store(&dev->cce_flip_stalled, 0);
    atomic_store(&dev->cce_abort, 0);
    atomic_store(&dev->cce_parked, 0);
    atomic_store(&dev->cce_starved, 0);
    atomic_store(&dev->cce_hung, 0);
    atomic_store(&dev->gui_idle_event, 0);
    atomic_store(&dev->pace_hold_us, 0);
    dev->pace_wall_ms_last = 0;
    atomic_store(&dev->pace_3d_seen, 0);
    /* Command-stream capture for offline replay: one file for the whole
       session, opened before any packet can flow, so no dump of the
       register state is needed at its start. */
    atomic_store(&dev->cap_recs, 0);
    if (!dev->cap_file) {
        const char *cap = getenv("RAGE128_CAPTURE");

        if (cap && *cap) {
            /* Each chip of a multi-chip board gets its own file: two
               chips opening one path with "w" write through separate
               stdio buffers and leave neither stream replayable. A
               .zst path streams through zstd (the replay reads .zst
               natively); the tag goes before the extension. */
            char   path[4096];
            size_t n   = strlen(cap);
            int    zst = n > 4 && !strcmp(cap + n - 4, ".zst");

            if (dev->cap_tag && zst)
                snprintf(path, sizeof(path), "%.*s-%s.zst", (int) (n - 4), cap, dev->cap_tag);
            else if (dev->cap_tag)
                snprintf(path, sizeof(path), "%s-%s", cap, dev->cap_tag);
            else
                snprintf(path, sizeof(path), "%s", cap);
            dev->cap_pipe = 0;
#ifndef _WIN32
            if (zst && !strchr(path, '\'')) {
                /* A GUI launch carries only the system PATH, so the usual
                   package locations are also tried by full path. */
                static const char *const zstds[] = {
                    "zstd", "/opt/homebrew/bin/zstd", "/usr/local/bin/zstd", "/usr/bin/zstd"
                };
                char cmd[4300];

                for (size_t i = 0; i < sizeof(zstds) / sizeof(zstds[0]) && !dev->cap_file; i++) {
                    snprintf(cmd, sizeof(cmd), "%s --version >/dev/null 2>&1", zstds[i]);
                    if (system(cmd) != 0)
                        continue;
                    snprintf(cmd, sizeof(cmd), "%s -q -f -T2 -o '%s'", zstds[i], path);
                    dev->cap_file = popen(cmd, "w");
                    dev->cap_pipe = dev->cap_file != NULL;
                }
            }
#endif
            if (!dev->cap_file) {
                /* No zstd on this host (or Windows): plain text under
                   the plain name, so the replay does not try to
                   decompress it. */
                if (zst) {
                    size_t pn = strlen(path);

                    path[pn - 4] = '\0';
                }
                dev->cap_file = fopen(path, "w");
            }
            rage128_log("RAGE128 capture: %s%s%s\n", path, dev->cap_pipe ? " (zstd)" : "",
                        dev->cap_file ? "" : " (open FAILED)");
        }
    }
    timer_add(&dev->cce_pump_timer, rage128_pm4_pump_timer, dev, 0);
    dev->cce_wake_event = thread_create_event();
    dev->cce_idle_event = thread_create_event();
    dev->cce_flip_event = thread_create_event();
    dev->cce_thread_run = 1;
    dev->cce_thread     = thread_create_named(rage128_cce_thread, dev, "rage128_cce");
}

void
rage128_pm4_thread_close(rage128_t *dev)
{
    if (!dev->cce_thread)
        return;
    if (dev->pump_stall_live)
        rage128_log("[r128 pm4] close: %u IB bodies left to a live read behind a flip stall (expected)\n",
                    dev->pump_stall_live);
    dev->cce_thread_run = 0;
    thread_set_event(dev->cce_wake_event);
    thread_set_event(dev->cce_flip_event);
    thread_wait(dev->cce_thread);
    dev->cce_thread = NULL;
    thread_destroy_event(dev->cce_wake_event);
    thread_destroy_event(dev->cce_idle_event);
    thread_destroy_event(dev->cce_flip_event);
    dev->cce_wake_event = dev->cce_idle_event = dev->cce_flip_event = NULL;
    free(dev->cce_fifo);
    dev->cce_fifo = NULL;
    free(dev->cce_fifo_rptr);
    dev->cce_fifo_rptr = NULL;
    free(dev->cce_fifo_tag);
    dev->cce_fifo_tag = NULL;
    free(dev->cce_pl);
    dev->cce_pl = NULL;
    free(dev->ind_pl);
    dev->ind_pl = NULL;
    free(dev->vc_bundle.idx);
    dev->vc_bundle.idx = NULL;
    if (dev->cap_file) {
#ifndef _WIN32
        if (dev->cap_pipe)
            pclose(dev->cap_file);
        else
#endif
            fclose(dev->cap_file);
        dev->cap_file = NULL;
        dev->cap_pipe = 0;
    }
}

/* Indirect buffer execution. The ring carries a type-0 packet writing
   two dwords, the buffer address to PM4_IW_INDOFF and its length in
   dwords to PM4_IW_INDSIZE; the size write starts a linear parse of the
   buffer, with no wrap (SDK: Indirect Buffer, p. 5-15 / PDF 111).
   The Linux DRM submits its vertex and blit buffers this way (linux r128
   DRM r128_state.c r128_cce_dispatch_indirect). The packet format is the
   ring's. This runs on whichever thread wrote PM4_IW_INDSIZE: the CCE
   thread for ring traffic, or the CPU thread for a direct MMIO write,
   for which rage128_pm4_reg_write drains the executor first. The bus
   reads are usable from either thread. */
static void
rage128_pm4_run_indirect(rage128_t *dev)
{
    if (dev->pm4_ind_busy) {
        /* A submit from inside a running walk: the buffer being walked
           wrote PM4_IW_INDOFF and PM4_IW_INDSIZE. The registers already
           hold the new pair; mark it pending and the walk runs it when
           it finishes. Refusing it would drop the submitted buffer, and
           the walk works from its own copy of the pair, so the new
           values cannot redirect the rest of the current walk. */
        dev->pm4_ind_pending = 1;
        return;
    }
    dev->pm4_ind_busy   = 1;
    rage128_in_indirect = 1;

    /* The executor has just popped the submit's size dword, which the
       pump tags RAGE128_CCE_TAG_LIVE when it left the body to be read
       from guest memory behind a flip stall. A walk chained on below is
       a different submit and does not inherit the tag. */
    int stall_live = rage128_on_cce_thread && dev->cce_pop_tag == RAGE128_CCE_TAG_LIVE;

    /* Cycle detection over the chained pairs, see the chain test below. */
    uint32_t loop_off = 0;
    uint32_t loop_n   = 0;
    uint32_t loop_pow = 0; /* 0 = not armed */
    uint32_t loop_lam = 0;

chain:;
    /* Copy the pair: writes to PM4_IW_INDOFF and PM4_IW_INDSIZE from the
       packets this walk runs must not change the walk itself. */
    uint32_t off = dev->pm4_iw_indoff;
    uint32_t n   = dev->pm4_iw_indsize;
    uint32_t pos = 0;

    /* A ring submit arrives with its body already spliced into the FIFO
       behind the submit packet, so the walk keeps popping tagged dwords.
       A direct MMIO submit has no body there and reads guest memory.
       That is safe because it runs on the CPU thread, inside the guest's
       own write, so the guest cannot change the buffer during the read. */
    int spliced = 0;
    {
        uint32_t rd = atomic_load(&dev->cce_fifo_rd);

        spliced = rage128_on_cce_thread
            && rd != atomic_load(&dev->cce_fifo_wr)
            && dev->cce_fifo_tag[rd & RAGE128_CCE_FIFO_MASK] == RAGE128_CCE_TAG_IB;
    }

    if (!spliced && rage128_on_cce_thread) {
        static int unspliced_logged = 0;

        /* A ring submit normally arrives with its body already copied.
           One that does not reads guest memory now, which is what the
           splice exists to avoid, so it is logged unless the pump tagged
           it as the expected flip-stall case. */
        if (!stall_live && unspliced_logged < 8) {
            unspliced_logged++;
            pclog("[r128 pm4] IB walk unspliced (live read): off=%08x n=%u\n", off, n);
        }
    }
    stall_live = 0;

    dev->pm4_ind_pending = 0;

    /* A reset pulse abandons the rest of the body (the packet already
       running completes). The discard loop below still removes it from
       the FIFO, so none of it is parsed as ring packets, and
       rage128_pm4_gui_reset resets the FIFO once the executor parks. */
    while (pos < n && !atomic_load(&dev->cce_abort)) {
        uint32_t  hdr;
        uint32_t  count;
        uint32_t *pl = dev->ind_pl;

        /* Between two packets of the buffer, so a long buffer, or a
           chain that never returns to the ring loop, still answers
           scanout's request (see rage128_gpu_scan_serve). */
        if (dev->gpu)
            rage128_gpu_scan_serve(dev);
        if (spliced) {
            if (!rage128_cce_get_ib(dev, &hdr, off + pos * 4))
                break;
        } else if (!rage128_pm4_bus_read(dev, rage128_pm4_vm_addr(dev, off + pos * 4), &hdr)) {
            pclog("[r128 pm4] IB dead bus: addr=%08x pos=%u size=%u\n", off, pos, n);
            break;
        }
        pos++;

        switch (RAGE128_PM4_TYPE(hdr)) {
            case 0:
                {
                    uint32_t reg = RAGE128_PM4_T0_REG(hdr);

                    count = RAGE128_PM4_COUNT(hdr);
                    for (uint32_t i = 0; i < count && pos < n; i++, pos++) {
                        uint32_t v;

                        if (spliced) {
                            if (!rage128_cce_get_ib(dev, &v, off + pos * 4))
                                break;
                        } else if (!rage128_pm4_bus_read(dev, rage128_pm4_vm_addr(dev, off + pos * 4), &v))
                            break;
                        uint32_t treg = (hdr & 0x8000) ? reg : reg + i * 4;
                        if (!rage128_pm4_reg_in_fetch_block(treg, 1)) /* fetch registers refused */
                            rage128_reg_poke(dev, treg, v);
                    }
                    break;
                }

            case 1:
                for (uint32_t i = 0; i < 2 && pos < n; i++, pos++) {
                    uint32_t v;
                    uint32_t treg = i ? RAGE128_PM4_T1_REG1(hdr)
                                      : RAGE128_PM4_T1_REG0(hdr);

                    if (spliced) {
                        if (!rage128_cce_get_ib(dev, &v, off + pos * 4))
                            break;
                    } else if (!rage128_pm4_bus_read(dev, rage128_pm4_vm_addr(dev, off + pos * 4), &v))
                        break;
                    if (!rage128_pm4_reg_in_fetch_block(treg, 1)) /* fetch registers refused */
                        rage128_reg_poke(dev, treg, v);
                }
                break;

            case 2: /* filler: pads the buffer to an even dword count */
                break;

            case 3:
                {
                    uint32_t m;
                    uint32_t filled = 0;

                    count = RAGE128_PM4_COUNT(hdr);
                    m     = count > RAGE128_PM4_MAX_PAYLOAD ? RAGE128_PM4_MAX_PAYLOAD : count;
                    for (uint32_t i = 0; i < count && pos < n; i++, pos++) {
                        uint32_t v = 0;

                        if (spliced) {
                            if (!rage128_cce_get_ib(dev, &v, off + pos * 4))
                                break;
                        } else
                            rage128_pm4_bus_read(dev, rage128_pm4_vm_addr(dev, off + pos * 4), &v);
                        if (i < m)
                            pl[i] = v;
                        filled = i + 1;
                    }
                    /* A packet stops at the end of the buffer: the SDK and
                       the Linux DRM pad a buffer to an even length with a
                       type-2 packet (SDK: Indirect Buffer, p. 5-15 / PDF 111;
                       linux r128 DRM r128_state.c r128_cce_dispatch_indirect),
                       so a body running past the end is malformed. Only the
                       dwords read are passed on, never the rest of ind_pl
                       left from the previous packet. */
                    if (m > filled)
                        m = filled;
                    if (rage128_mpeg_packet3(dev, hdr, pl, m))
                        break;
                    /* As on the ring path: a 2D operation draws into the same
                       surfaces, so any deferred 3D batch is drawn first, or a
                       present blit or clear would land ahead of it. */
                    if (!rage128_3d_packet3(dev, hdr, pl, m)) {
                        if (rage128_2d_op_flushes(hdr)) {
                            rage128_raster_flush(dev);
                            if (dev->gpu)
                                rage128_gpu_flush(dev, RAGE128_PM4_T3_OPCODE(hdr));
                        }
                        rage128_2d_packet3(dev, hdr, pl, m);
                    }
                    break;
                }

            default:
                break;
        }
    }

    /* A body dword this walk did not consume would be parsed next as a
       ring packet and run a second time, so the rest of the tagged run
       is discarded: after an abort, after a walk that stopped before the
       end of the body, and on any path that got here without reading
       from the splice at all. Only the executor may move the FIFO read
       index. */
    if (rage128_on_cce_thread) {
        uint32_t junk;

        while (rage128_cce_get_ib(dev, &junk, off + pos * 4))
            pos++;
    }

    /* A submit arrived while this walk ran: the registers hold the new
       pair, so run it now, one buffer after the other, unless a reset
       pulse abandons it too. A pending flag left by the pulse is cleared
       by rage128_pm4_gui_reset and on entry to the next walk. */
    if (dev->pm4_ind_pending && !atomic_load(&dev->cce_abort)) {
        /* A chain that submits itself again is a hung engine: it stays
           busy until the reset pulse, with no made-up limit on its
           length. While the CPU thread is blocked (draining, or running
           this walk itself) guest memory cannot change, so a pair seen
           twice proves the chain never ends. The test only counts pairs
           walked entirely under that condition, and looks for a repeat
           by keeping one saved pair and replacing it at power-of-two
           steps (Brent's cycle detection). A body that the device itself
           rewrites to end its own loop would be cut short; no client is
           known to do that. */
        if (!rage128_on_cce_thread || atomic_load(&dev->cce_drain_req)) {
            uint32_t noff = dev->pm4_iw_indoff;
            uint32_t nn   = dev->pm4_iw_indsize;

            if (!loop_pow) {
                loop_off = noff;
                loop_n   = nn;
                loop_pow = 1;
                loop_lam = 0;
            } else if (noff == loop_off && nn == loop_n) {
                pclog("[r128 pm4] IB chain re-submits itself (off=%08x n=%u): engine hung until reset\n",
                      noff, nn);
                atomic_store(&dev->cce_hung, 1);
                goto out;
            } else if (++loop_lam == loop_pow) {
                loop_off = noff;
                loop_n   = nn;
                loop_pow <<= 1;
                loop_lam = 0;
            }
        } else
            loop_pow = 0;
        goto chain;
    }

out:
    rage128_cap_run_flush(dev); /* the pending capture run of a CPU-thread walk */
    rage128_in_indirect = 0;
    dev->pm4_ind_busy   = 0;
}

int
rage128_pm4_reg_read(rage128_t *dev, uint32_t off, uint32_t *val)
{
    switch (off) {
        case RAGE128_PM4_BUFFER_OFFSET:
            *val = dev->pm4_buffer_offset;
            return 1;
        case RAGE128_PM4_BUFFER_CNTL:
            *val = dev->pm4_buffer_cntl;
            return 1;
        case RAGE128_PM4_BUFFER_WM_CNTL:
            *val = dev->pm4_wm_cntl;
            return 1;
        case RAGE128_PM4_BUFFER_DL_RPTR_ADDR:
            *val = dev->pm4_rptr_addr;
            return 1;
        case RAGE128_PM4_BUFFER_DL_RPTR:
            /* The retire pointer, the same value the host's copy gets
               (see the write-back in rage128_pm4_pump). The fetch pointer
               runs up to a FIFO ahead of the executor, and the Windows 98
               driver has been observed reading this register to decide
               which indirect buffers it may reuse; answering with the
               fetch pointer lets it rewrite buffers not yet walked. */
            *val = atomic_load(&dev->cce_retire_rptr);
            return 1;
        case RAGE128_PM4_BUFFER_DL_WPTR:
            /* The written pointer, without PM4_BUFFER_DL_DONE, which
               reads as 0 (CCE 3D supplement, PM4_BUFFER_DL_WPTR; the
               write path clears it). The Windows 98 driver has been
               observed using this value for its ring free-space count
               and stopping its submits when extra bits are set. */
            *val = dev->pm4_wptr;
            return 1;
        case RAGE128_PM4_BUFFER_DL_WPTR_DELAY:
            *val = dev->pm4_wptr_delay;
            return 1;
        case RAGE128_PM4_VC_FPU_SETUP:
            *val = dev->t3d.fpu_setup;
            return 1;
        case RAGE128_PM4_VC_DEBUG_CONFIG:
            *val = dev->pm4_vc_debug_config;
            return 1;
        case RAGE128_PM4_VC_STAT:
            *val = 0; /* read-only vertex walker status, modeled as 0
                         (RRG: PM4_VC_STAT, p. 3-221 / PDF 239) */
            return 1;
        case RAGE128_PM4_STAT:
            /* CCE status (CCE 3D supplement, PM4_STAT). PM4_BUSY and
               GUI_ACTIVE are set while rage128_pm4_active reports work,
               whose ring term counts only while the CCE is in a
               bus-master mode. The Windows 2000 driver has been observed
               stopping the CCE during a mode set with a packet still
               unfetched and then waiting, with no timeout, for idle; a
               stopped CCE reads idle here. The Linux DRM waits for both
               busy bits clear and PM4_FIFOCNT at its full packet-region
               size (linux r128 DRM r128_cce.c r128_do_cce_idle). */
            /* PM4_FIFOCNT, "number of free CCE FIFO entries": the size
               of the mode's packet region (rage128_pm4_fifo_dwords) less
               the dwords queued and not yet popped, whatever the busy bits
               say, so a PIO client waiting for room cannot deadlock an
               executor waiting mid-packet. The pump fetches as far ahead
               as it can, where the chip refills by watermark
               (PM4_BUFFER_WM_CNTL), so during a run of work this reads 0. */
            {
                uint32_t room = rage128_pm4_fifo_dwords(dev);
                uint32_t used = atomic_load(&dev->cce_fifo_wr)
                    - atomic_load(&dev->cce_fifo_rd);

                *val = (used < room) ? room - used : 0;
                if (rage128_pm4_active(dev))
                    *val |= RAGE128_PM4_STAT_BUSY | RAGE128_PM4_STAT_GUI_ACTIVE;
            }
            return 1;
        case RAGE128_PM4_MICROCODE_ADDR:
            *val = dev->pm4_microcode_addr;
            return 1;
        case RAGE128_PM4_MICROCODE_RADDR:
        case RAGE128_PM4_MICROCODE_DATAH:
        case RAGE128_PM4_MICROCODE_DATAL:
            /* The microcode RAM is not modeled: packets are run by C
               code, not by the loaded microcode. The drivers load it and
               have not been observed reading it back. */
            *val = 0;
            return 1;
        case RAGE128_PM4_CMDFIFO_ADDR:
            *val = 0;
            return 1;
        case RAGE128_PM4_MICRO_CNTL:
            *val = dev->pm4_micro_cntl;
            return 1;
        case RAGE128_PM4_IW_INDOFF:
            *val = dev->pm4_iw_indoff;
            return 1;
        case RAGE128_PM4_IW_INDSIZE:
            *val = dev->pm4_iw_indsize;
            return 1;
        case RAGE128_PCI_GART_PAGE:
            *val = dev->pci_gart_page;
            return 1;
        default:
            break;
    }
    return 0;
}

int
rage128_pm4_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask)
{
    /* A direct CPU write that reconfigures the ring fetch drains the
       executor first: the ring registers and PM4_MICRO_CNTL (the set in
       rage128_pm4_reg_in_fetch_block) and PCI_GART_PAGE.
       PM4_BUFFER_DL_WPTR is the submit doorbell and pumps instead. Other
       registers in the 0x700-0x7fc block do not drain, among them
       PM4_VC_DEBUG_CONFIG and the PM4_MICROCODE_DATAH and
       PM4_MICROCODE_DATAL store, which the Windows 2000 Direct3D driver
       has been observed writing about 130 times a second in its fill
       path. A drain blocks the CPU thread until the rasterizer has
       finished, and those registers do not move the ring or change
       drawing, so draining for them would only tie emulated time to
       drawing time. Writes from the command stream skip this: there the
       executor is the one writing. */
    /* PM4_VC_DEBUG_CONFIG is in the fetch-block set only so that the
       parser refuses it (PIO only, RRG: PM4_VC_DEBUG_CONFIG, p. 3-221 /
       PDF 239); a host write does not drain. The executor reads its
       PM4_VC_DONT_START and PM4_VC_NO_OUTPUT bits when a draw runs, so
       an unordered host write takes effect at the next draw. The
       Windows XP Direct3D driver has been observed writing it about 700
       times a second right after its flip and WAIT_UNTIL; a drain there
       would break the flip stall and let the next frame's clear land in
       the displayed buffer. */
    /* PM4_IW_INDOFF and PM4_IW_INDSIZE drain too: a direct submit runs
       the walker on this thread, and the walker's state (pm4_ind_busy,
       ind_pl) and every handler it calls belong to one thread at a time,
       so the executor has to be idle first. */
    if (!rage128_on_cce_thread
        && off != RAGE128_PM4_BUFFER_DL_WPTR
        && off != RAGE128_PM4_VC_DEBUG_CONFIG
        && (off == RAGE128_PCI_GART_PAGE
            || off == RAGE128_PM4_IW_INDOFF
            || off == RAGE128_PM4_IW_INDSIZE
            || rage128_pm4_reg_in_fetch_block(off, 1)))
        rage128_pm4_drain_wait(dev);

#define MERGE(field) ((field) = ((field) & ~mask) | (val & mask))
    switch (off) {
        case RAGE128_PM4_BUFFER_OFFSET:
            MERGE(dev->pm4_buffer_offset);
            return 1;
        case RAGE128_PM4_BUFFER_CNTL:
            MERGE(dev->pm4_buffer_cntl);
            return 1;
        case RAGE128_PM4_BUFFER_WM_CNTL:
            MERGE(dev->pm4_wm_cntl);
            return 1;
        case RAGE128_PM4_BUFFER_DL_RPTR_ADDR:
            MERGE(dev->pm4_rptr_addr);
            /* The pump writes the host's copy only when the retire
               pointer differs from cce_shadow_last, its record of the
               last value it wrote. A new address names memory that has
               not received that value, so the shadow is dropped to a
               value no 22:0 pointer can equal and the next pump writes
               the copy whether or not the pointer has moved. */
            dev->cce_shadow_last = 0xffffffffu;
            return 1;
        case RAGE128_PM4_BUFFER_DL_RPTR:
            MERGE(dev->pm4_rptr);
            /* The read pointer is a 22:0 field (CCE 3D supplement,
               PM4_BUFFER_DL_RPTR): bits 31:23 of a write are dropped
               here, before the value reaches the fetch pointer and the
               retire pointers below, so the pump, the readback and the
               host's copy all carry the field value. */
            dev->pm4_rptr &= 0x007fffffu;
            /* Written when the ring is set up or reset (linux r128 DRM
               r128_cce.c r128_cce_init_ring_buffer, r128_do_cce_reset).
               The retire pointer follows, or a read would return the old
               ring's last position, and the pump's packet framing starts
               over with the ring. cce_retire_pending follows as well:
               the next queued MMIO write carries no ring position, and
               the executor would otherwise publish the old ring's last
               retire point over the value just written. */
            atomic_store(&dev->cce_retire_rptr, dev->pm4_rptr);
            dev->cce_retire_pending = dev->pm4_rptr;
            dev->pump_frame_rem     = 0;
            dev->pump_ib_state      = 0;
            return 1;
        case RAGE128_PM4_BUFFER_DL_WPTR:
            {
                uint32_t wmask = rage128_pm4_ring_mask(dev);
                uint32_t owptr = dev->pm4_wptr & wmask;

                MERGE(dev->pm4_wptr);
                /* The write pointer is a 22:0 field with bits 30:23 reserved
                   (CCE 3D supplement, PM4_BUFFER_DL_WPTR), so a write's
                   bits 31:23 are dropped before the value is stored, read
                   back or compared with the fetch pointer. Bit 31,
                   PM4_BUFFER_DL_DONE, tells the CCE that the driver is
                   waiting for idle; it is write-only and reads as 0. The
                   Linux DRM sets it on the current pointer to flush (linux
                   r128 DRM r128_cce.c r128_do_cce_flush). The Windows 98
                   driver has been observed waiting until the host's
                   read-pointer copy equals the PM4_BUFFER_DL_WPTR it reads
                   back, which never happens if bit 31 reads back set. */
                dev->pm4_wptr &= 0x007fffffu;
                /* A WPTR move that is shorter going backward than forward is a
                   ring reset made without stopping the CCE, not a submit: no
                   driver posts more than half a ring in one write. The
                   Linux DRM resets the ring by writing WPTR = 0 and then
                   RPTR = 0 (linux r128 DRM r128_cce.c r128_do_cce_reset),
                   and its r128_cce_reset ioctl, which the X driver's lockup
                   recovery issues (xf86-video-r128 R128CCE_RESET), does
                   that without clearing the FIFO mode. Fetching the forward
                   gap would run the whole old ring again, so the fetch
                   pointer is moved to the new WPTR and the pump's packet
                   framing is dropped. */
                if (rage128_pm4_ring_bm(dev)) {
                    uint32_t nwptr = dev->pm4_wptr & wmask;

                    if (((owptr - nwptr) & wmask) < ((nwptr - owptr) & wmask)) {
                        dev->pm4_rptr       = nwptr;
                        dev->pump_frame_rem = 0;
                        dev->pump_ib_state  = 0;
                    }
                }
                rage128_pm4_pump(dev);
                return 1;
            }
        case RAGE128_PM4_BUFFER_DL_WPTR_DELAY:
            /* On the chip a WPTR write first goes to a staging register
               and reaches the fetch engine only after this delay, so the
               engine does not fetch too early (RRG:
               PM4_BUFFER_DL_WPTR_DELAY, p. 3-218 / PDF 236). The pump
               fetches at the WPTR write itself, so the value is stored
               and the delay is not modeled. */
            MERGE(dev->pm4_wptr_delay);
            return 1;
        case RAGE128_PM4_VC_FPU_SETUP:
            MERGE(dev->t3d.fpu_setup);
            return 1;
        case RAGE128_PM4_VC_DEBUG_CONFIG:
            MERGE(dev->pm4_vc_debug_config);
            return 1;
        case RAGE128_PM4_VC_STAT:
            return 1; /* RO */
        case RAGE128_PM4_MICROCODE_ADDR:
            MERGE(dev->pm4_microcode_addr);
            dev->pm4_microcode_addr &= 0xff;
            return 1;
        case RAGE128_PM4_MICROCODE_RADDR:
            return 1;
        case RAGE128_PM4_MICROCODE_DATAH:
            return 1; /* the microcode RAM is not modeled */
        case RAGE128_PM4_MICROCODE_DATAL:
            /* A PM4_MICROCODE_DATAH then PM4_MICROCODE_DATAL write fills
               one 37-bit entry, and the address moves to the next entry
               after the low half. The CCE 3D supplement gives the 8-bit
               address and the two halves (PM4_MICROCODE_ADDR,
               PM4_MICROCODE_DATAH, PM4_MICROCODE_DATAL) but not the
               increment; the Linux DRM sets the address once and writes
               all 256 entries as high, low pairs (linux r128 DRM
               r128_cce.c r128_cce_load_microcode). */
            dev->pm4_microcode_addr = (dev->pm4_microcode_addr + 1) & 0xff;
            return 1;
        case RAGE128_PM4_CMDFIFO_ADDR:
            return 1; /* RO */
        case RAGE128_PM4_MICRO_CNTL:
            MERGE(dev->pm4_micro_cntl);
            return 1;
        case RAGE128_PM4_IW_INDOFF:
            MERGE(dev->pm4_iw_indoff);
            /* The buffer offset is a 24:0 field whose bits 2:0 are
               hardwired to zero (CCE 3D supplement, PM4_IW_INDOFF), so
               a submit at an unaligned offset walks the buffer from the
               offset rounded down to 8 bytes. The pump rounds its own
               copy of the address the same way when it splices a ring
               submit's body, so the spliced dwords are the ones a walk
               from this register would read. */
            dev->pm4_iw_indoff &= 0x01fffff8u;
            return 1;
        case RAGE128_PM4_IW_INDSIZE:
            /* The size write starts the indirect buffer (SDK: Indirect
               Buffer, p. 5-15 / PDF 111). A submit packet writes
               PM4_IW_INDOFF first and PM4_IW_INDSIZE second. */
            MERGE(dev->pm4_iw_indsize);
            dev->pm4_iw_indsize &= RAGE128_PM4_IW_INDSIZE_MASK;
            rage128_pm4_run_indirect(dev);
            return 1;
        case RAGE128_WAIT_UNTIL:
            /* EVENT_CRTC_OFFSET [0]: "stall cmdfifo" until the display
               has started showing the last CRTC_OFFSET written, that is
               until a pending flip has latched (RRG: WAIT_UNTIL,
               p. 3-239 / PDF 257). The Windows 98 Direct3D driver has been
               observed writing 1 here. Both the command stream and
               direct MMIO writes reach this point on the CCE thread
               (MMIO writes are queued as packets by
               rage128_pm4_enqueue_write), so the stall holds all later
               engine work behind the flip. A write that arrives on the
               CPU thread instead (a partial-dword write, or one the
               queue could not take) comes after a drain, with an empty
               engine and nothing to hold back. A drain wait or a reset
               pulse breaks the stall; the flip still latches at the next
               vblank. */
            if (rage128_on_cce_thread && (val & mask & 1)) {
                /* Present: draw the deferred batch into the back buffer
                   before scanout takes the flip. */
                rage128_raster_flush(dev);
                if (dev->gpu)
                    rage128_gpu_present(dev);
                thread_reset_event(dev->cce_flip_event);
                atomic_store(&dev->cce_flip_stalled, 1);
                while (dev->cce_thread_run && !atomic_load(&dev->cce_drain_req)
                       && !atomic_load(&dev->cce_abort)
                       && dev->crtc_offset_pending && !dev->crtc_offset_lock)
                    thread_wait_event(dev->cce_flip_event, 1);
                atomic_store(&dev->cce_flip_stalled, 0);
            }
            /* EVENT_RE_CRTC_VLINE [1], EVENT_FE_CRTC_VLINE [2] and
               EVENT_CRTC_VLINE [3]: stall until CRTC_GUI_TRIG_VLINE has a
               rising edge, has a falling edge, or is 1 (RRG: WAIT_UNTIL,
               p. 3-239 / PDF 257). An edge has to happen after the wait
               begins; the display publishes the level and counts the
               edges once per scanline. The chip stalls on the AND of all
               set events; here the flip wait above runs first, then this
               one. The other events, bits 11:4 (bus-master channel idle,
               EVENT_CMDFIFO, EVENT_OV0_FLIP), are taken as already met.
               Same exits as the flip stall, and the same flush on entry:
               the stall holds back commands still in the FIFO, not work
               the engine has already accepted. */
            if (rage128_on_cce_thread && (val & mask & 0xe)) {
                uint32_t ev    = val & mask;
                unsigned rise0 = atomic_load(&dev->vline_rise_seq);
                unsigned fall0 = atomic_load(&dev->vline_fall_seq);

                rage128_raster_flush(dev);
                if (dev->gpu)
                    rage128_gpu_present(dev);
                thread_reset_event(dev->cce_flip_event);
                atomic_store(&dev->cce_flip_stalled, 1);
                while (dev->cce_thread_run && !atomic_load(&dev->cce_drain_req)
                       && !atomic_load(&dev->cce_abort)
                       && (((ev & 2) && atomic_load(&dev->vline_rise_seq) == rise0)
                           || ((ev & 4) && atomic_load(&dev->vline_fall_seq) == fall0)
                           || ((ev & 8) && !atomic_load(&dev->vline_in_window))))
                    thread_wait_event(dev->cce_flip_event, 1);
                atomic_store(&dev->cce_flip_stalled, 0);
            }
            return 1;
        case RAGE128_FLUSH_1:
        case RAGE128_FLUSH_2:
        case RAGE128_FLUSH_3:
        case RAGE128_FLUSH_4:
        case RAGE128_FLUSH_5:
        case RAGE128_FLUSH_6:
        case RAGE128_FLUSH_7:
            /* FLUSH_1 to FLUSH_7: "Block FIFO'd writes until level n
               engines are idle" (RRG: FLUSH_1, pp. 3-234-3-235 /
               PDF 252-253). Nothing to do here: the executor runs one
               packet at a time, and deferred draws keep a copy of their
               state when queued, so a later state write cannot change a
               queued draw. Everything that does need the engine idle
               flushes for itself (type-3 and register-started 2D
               operations, WAIT_UNTIL, drains, the idle flush when the
               FIFO empties). Drawing the batch on every FLUSH_n would
               cost about 1 ms per write, and the Windows 2000 and NT 4
               drivers have been observed writing one for every draw, 600
               to 900 times a second. */
            return 1;
        case RAGE128_PCI_GART_PAGE:
            /* PCI_GART_DIS [0] and the table base [31:12] are the only
               fields; bits 11:1 are reserved (RRG: PCI_GART_PAGE,
               pp. 3-207-3-208 / PDF 225-226) and modeled as reading 0, so
               a byte or word write cannot leave a value the register
               cannot hold. */
            MERGE(dev->pci_gart_page);
            dev->pci_gart_page &= 0xfffff001u;
            return 1;
        default:
            break;
    }
#undef MERGE
    return 0;
}
