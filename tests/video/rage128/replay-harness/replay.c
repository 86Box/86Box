/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- offline capture replay harness.
 *
 *          The harness includes the production device shell, so a replay
 *          uses the register handlers and initialization the emulator
 *          uses. The register harness beside it supplies the host-service
 *          stubs; there is no guest and no emulator core.
 *
 *          Record grammar (one record per line, hexadecimal fields):
 *            D <dword>             ring dword consumed by the executor
 *            J <dword>             MMIO-injected dword consumed by the
 *                                  executor
 *            I <addr> <val>        bus dword with its value
 *            R <off> <val> <mask>  direct CPU register write
 *            B <addr> <len> <hex>  bus block with len payload bytes
 *            X <vm> <len>          payload-free bus block, served as
 *                                  all-ones
 *            W <bus> <len>         observed bus write, counted without
 *                                  comparison
 *            A <fields...>         address discriminator, counted without
 *                                  use
 *
 *          D and J dwords enter the command FIFO directly; there is no
 *          guest-memory ring to fetch. Bus reads consume I, B and X
 *          records in order through the harness's bus-master read hook.
 *          Length and payload position decide whether a request matches;
 *          addresses appear in diagnostics but do not gate serving. B
 *          records supply a whole block or successive dwords. X records
 *          carry no data, so their fill cannot establish pixel fidelity.
 *
 *          Two readers keep a register-write drain from blocking its own
 *          bus producer: the main thread feeds commands and register
 *          writes while a bus thread fills a bounded queue on its own.
 *          The queue's entry count and the heap bytes its payloads hold
 *          have separate bounds. Each reader decompresses a .zst capture
 *          through its own zstd child process.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
/* The MinGW C runtime has no POSIX getline, so Windows builds use this
   one. It reads one line, newline included, into a buffer it grows as
   needed, and returns the line's length, or -1 at end of file. fgets
   fills the buffer; a read that stops short of the buffer's end without
   a newline is the last line of the file. Captures are ASCII text, so
   strlen gives the length (a null byte inside a line would cut it). */
static ssize_t
getline(char **line, size_t *cap, FILE *f)
{
    size_t len = 0;

    if (!*line || *cap < 128) {
        char *p = realloc(*line, 128);

        if (!p)
            return -1;
        *line = p;
        *cap  = 128;
    }
    for (;;) {
        if (!fgets(*line + len, (int) (*cap - len), f))
            return len ? (ssize_t) len : -1;
        len += strlen(*line + len);
        if ((*line)[len - 1] == '\n' || len + 1 < *cap)
            return (ssize_t) len;

        char *p = realloc(*line, *cap * 2);

        if (!p)
            return -1;
        *line = p;
        *cap *= 2;
    }
}
#endif

#include "../../../../src/video/vid_ati_rage128.c"

/* CPU read handlers charge bus cycles into cpu_state. The harness has
   no CPU core, so this storage only absorbs the charges. */
cpu_state_t cpu_state;

#include <86box/vid_svga_render.h>

extern int      harness_cfg_memory_mb;
extern int      harness_cfg_render_threads;
extern int      harness_cfg_recompiler;
extern int      harness_log_quiet;
extern uint64_t harness_sysmem_stray;
extern uint64_t harness_sysmem_stray_wr;
extern void     harness_ram_alloc(void);
extern uint8_t (*harness_pci_read)(int func, int addr, int len, void *priv);
extern void (*harness_pci_write)(int func, int addr, int len, uint8_t val, void *priv);
extern void *harness_pci_priv;
extern int (*harness_bm_read_hook)(uint32_t addr, uint8_t *dst, uint32_t len);
extern void (*harness_bm_write_hook)(uint32_t addr, uint32_t len);

/* The bus queue has one producer and one consumer: the bus reader and
   the thread executing a bus walk. Atomic indices publish records and
   release slots. A full queue blocks only the independent bus producer;
   the main thread remains free to feed commands that let the consumer run. */
typedef struct {
    uint32_t addr;
    uint32_t val;
    uint32_t len; /* 4 = I record, otherwise the X/B record's block length */
    uint8_t *blk; /* B record payload (len bytes), null for I and X */
} busrec_t;

#define BQ_CAP (1u << 22) /* maximum queued records */
/* B payloads live on the heap until served; the producer stalls above
   this many outstanding bytes so a texture-heavy capture cannot run the
   reader arbitrarily far ahead of the device. */
#define BQ_BLK_BYTES_CAP (1ull << 30)
static atomic_uint_fast64_t bq_blk_bytes;

static busrec_t     *bq;
static size_t        bq_cap;
static atomic_size_t bq_head; /* consumer */
static atomic_size_t bq_tail; /* producer */
static atomic_int    bq_eof;  /* producer finished its read of the capture */
static atomic_int    bq_drop; /* device done: producer discards instead of blocking */
static uint64_t      stat_bq_dropped;
static atomic_uint_fast64_t bq_served; /* consumer progress, for the drain hook */
static atomic_int    bq_waiting; /* consumer waiting for more producer input */

static void
bq_nap(void)
{
    struct timespec ts = { 0, 100000 }; /* 100 us */

    nanosleep(&ts, NULL);
}

static uint64_t stat_i, stat_x, stat_b, stat_b_bytes, stat_d, stat_j, stat_r, stat_a, stat_w;
static uint64_t nrec; /* pass-2 record counter (shared with replay_drain_hook) */
static uint64_t stat_served, stat_block_served, stat_underrun, stat_mismatch;
static uint64_t stat_block_skipped;
static uint64_t stat_b_short;          /* B records with a truncated payload */
static uint64_t stat_blk_served;       /* B records served whole */
static uint64_t stat_blk_dword_served; /* dwords served out of B records */
static uint32_t blk_pos;               /* consumer cursor inside the head B record */
/* Tail accounting: once the capture is fully served, the device is still
   walking whatever the last records left it pointing at, and every read it
   makes is off the end of the recorded world. Those underruns say nothing
   about fidelity -- they cannot, since no record exists to disagree with --
   so they are counted apart and kept out of the exit status, which
   otherwise reports a clean replay as a failure. */
static atomic_int stream_done;
static uint64_t   stat_underrun_tail;
static uint64_t   tail_budget = 1u << 16;
static uint64_t stat_ring_window; /* ring-window tripwire */
static uint64_t stat_bm_write;
static uint64_t stat_ring_write;  /* bus writes overlapping the shadowed ring */
static uint64_t stat_ring_write_block; /* the >=4KB share: staged writebacks */

static void
bq_push(uint32_t addr, uint32_t val, uint32_t len, uint8_t *blk)
{
    size_t tail = atomic_load_explicit(&bq_tail, memory_order_relaxed);

    while (tail - atomic_load_explicit(&bq_head, memory_order_acquire) >= bq_cap
           || (blk && atomic_load_explicit(&bq_blk_bytes, memory_order_relaxed) > BQ_BLK_BYTES_CAP)) {
        if (atomic_load_explicit(&bq_drop, memory_order_relaxed)) {
            stat_bq_dropped++;
            free(blk);
            return;
        }
        bq_nap();
    }
    if (blk)
        atomic_fetch_add_explicit(&bq_blk_bytes, len, memory_order_relaxed);
    bq[tail % bq_cap] = (busrec_t) { addr, val, len, blk };
    atomic_store_explicit(&bq_tail, tail + 1, memory_order_release);
}

/* Consumer side: the head record is fully served; release it. */
static void
bq_pop(size_t head, const busrec_t *rec)
{
    if (rec->blk) {
        atomic_fetch_sub_explicit(&bq_blk_bytes, rec->len, memory_order_relaxed);
        free(rec->blk);
    }
    blk_pos = 0;
    atomic_store_explicit(&bq_head, head + 1, memory_order_release);
}

/* The first desync is the one that names the defect; later ones are its
   echo. */
static void
mismatch_note(uint32_t addr, uint32_t len, const busrec_t *rec)
{
    if (stat_mismatch == 1)
        printf("first mismatch: device read addr=%08x len=%u, record %s addr=%08x len=%u pos=%u\n",
               addr, len, rec->blk ? "B" : (rec->len == 4 ? "I" : "X"),
               rec->addr, rec->len, blk_pos);
}

/* Serve the next bus record by length and payload position, without an
   address comparison. A length mismatch consumes the record, increments
   stat_mismatch, and supplies all-ones so the executor can continue. */
static int
replay_bm_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
    size_t   head = atomic_load_explicit(&bq_head, memory_order_relaxed);
    busrec_t rec;

    /* An empty queue while the bus thread is still reading means the
       producer is behind: wait for it (it depends on nothing this side
       holds). Empty at end of file means the device asks for a read
       without a corresponding record, from an incomplete stream or
       divergent execution. */
    for (;;) {
        if (head == atomic_load_explicit(&bq_tail, memory_order_acquire)) {
            if (!atomic_load_explicit(&bq_eof, memory_order_acquire)) {
                atomic_store_explicit(&bq_waiting, 1, memory_order_relaxed);
                bq_nap();
                atomic_store_explicit(&bq_waiting, 0, memory_order_relaxed);
                continue;
            }
            /* eof is stored after the last push: re-read the tail once */
            if (head != atomic_load_explicit(&bq_tail, memory_order_acquire))
                continue;
            if (atomic_load_explicit(&stream_done, memory_order_relaxed)) {
                /* Past the budget, stop feeding 0xff: as a PM4 header that
                   is a maximal type-3 packet, and one of the codes it
                   decodes to dispatches another indirect buffer, so the
                   walk re-arms itself and never ends. Zero is a type-0
                   header writing one register, which walks each buffer to
                   its end and dispatches nothing -- the tail terminates. */
                stat_underrun_tail++;
                memset(dst, stat_underrun_tail > tail_budget ? 0x00 : 0xff, len);
                return 1;
            }
            stat_underrun++;
            memset(dst, 0xff, len);
            return 1;
        }
        rec = bq[head % bq_cap];
        /* Skip payload-free X blocks ahead of a dword request, counting each
           skipped block. They supply no recorded values. I and B records
           remain ordered and are never skipped by this path. */
        if (len == 4 && rec.len != 4 && !rec.blk) {
            stat_block_skipped++;
            head++;
            atomic_store_explicit(&bq_head, head, memory_order_release);
            continue;
        }
        break;
    }

    if (len == 4 && rec.len == 4 && !rec.blk) {
        memcpy(dst, &rec.val, 4);
    } else if (len == 4 && rec.blk) {
        /* A B record read dword by dword: the PCI-GART walk gathers a
           block one PTE-resolved dword at a time, and a coalesced run
           of contiguous reads is consumed the same way (a run of one
           dword is a B record of length 4, so the blk test above comes
           first). The record stays at the head until its last byte is
           served. */
        uint32_t n = rec.len - blk_pos;

        if (n > 4)
            n = 4;
        memset(dst, 0xff, 4);
        memcpy(dst, rec.blk + blk_pos, n);
        blk_pos += n;
        stat_served++;
        stat_blk_dword_served++;
        if (blk_pos >= rec.len)
            bq_pop(head, &rec);
        atomic_fetch_add_explicit(&bq_served, 1, memory_order_relaxed);
        return 1;
    } else if (len == rec.len && rec.blk && blk_pos == 0) {
        memcpy(dst, rec.blk, len);
        stat_blk_served++;
    } else if (len == rec.len && !rec.blk) {
        /* X records carry no payload; all-ones models the missing data. */
        memset(dst, 0xff, len);
        stat_block_served++;
    } else {
        stat_mismatch++;
        mismatch_note(addr, len, &rec);
        memset(dst, 0xff, len);
        bq_pop(head, &rec);
        atomic_fetch_add_explicit(&bq_served, 1, memory_order_relaxed);
        return 1;
    }
    stat_served++;
    bq_pop(head, &rec);
    atomic_fetch_add_explicit(&bq_served, 1, memory_order_relaxed);
    return 1;
}

/* The replay has no ring to retire. A sentinel read pointer marks each
   dword as MMIO input, and a zero indirect-buffer tag selects live body
   reads served by the bus hook. FIFO overflow parks in a pending list so
   the command producer cannot block a register-write drain. */
static uint32_t *pending;
static size_t    pending_n, pending_cap;

/* The FIFO is single-producer and the pending list and stream lookahead
   are plain statics, but a drain can run on the scan thread as well as
   the record loop: every feed path takes this first. Recursive (the
   feed paths nest); never held across a device call. */
static pthread_mutex_t feed_lock;
static pthread_t       feed_main_tid;
static int             feed_inflight; /* record loop holds an unapplied record */

static void
pending_add(uint32_t v)
{
    if (pending_n == pending_cap) {
        pending_cap = pending_cap ? pending_cap * 2 : 4096;
        pending     = realloc(pending, pending_cap * sizeof(*pending));
        if (!pending) {
            fprintf(stderr, "replay: out of memory growing the dword list\n");
            exit(2);
        }
    }
    pending[pending_n++] = v;
}

static void
push_dwords(rage128_t *dev, const uint32_t *v, size_t n)
{
    uint32_t wr    = atomic_load(&dev->cce_fifo_wr);
    size_t   space = RAGE128_CCE_FIFO_DWORDS - (wr - atomic_load(&dev->cce_fifo_rd));
    size_t   i     = 0;

    for (; i < n && space; i++, space--) {
        dev->cce_fifo[wr & RAGE128_CCE_FIFO_MASK]      = v[i];
        dev->cce_fifo_rptr[wr & RAGE128_CCE_FIFO_MASK] = 0xffffffffu;
        dev->cce_fifo_tag[wr & RAGE128_CCE_FIFO_MASK]  = 0;
        wr++;
    }
    atomic_store(&dev->cce_fifo_wr, wr);
    if (i)
        thread_set_event(dev->cce_wake_event);
    for (; i < n; i++)
        pending_add(v[i]);
}

/* Drain whatever the FIFO could not take last time, oldest first. */
static void
pending_flush(rage128_t *dev)
{
    size_t done = 0;

    pthread_mutex_lock(&feed_lock);
    if (!pending_n) {
        pthread_mutex_unlock(&feed_lock);
        return;
    }
    {
        uint32_t wr    = atomic_load(&dev->cce_fifo_wr);
        size_t   space = RAGE128_CCE_FIFO_DWORDS - (wr - atomic_load(&dev->cce_fifo_rd));

        for (; done < pending_n && space; done++, space--) {
            dev->cce_fifo[wr & RAGE128_CCE_FIFO_MASK]      = pending[done];
            dev->cce_fifo_rptr[wr & RAGE128_CCE_FIFO_MASK] = 0xffffffffu;
            dev->cce_fifo_tag[wr & RAGE128_CCE_FIFO_MASK]  = 0;
            wr++;
        }
        atomic_store(&dev->cce_fifo_wr, wr);
        if (done)
            thread_set_event(dev->cce_wake_event);
    }
    if (done) {
        memmove(pending, pending + done, (pending_n - done) * sizeof(*pending));
        pending_n -= done;
    }
    pthread_mutex_unlock(&feed_lock);
}

static void
push_dword(rage128_t *dev, uint32_t v)
{
    pthread_mutex_lock(&feed_lock);
    pending_flush(dev);
    if (pending_n)
        pending_add(v);
    else
        push_dwords(dev, &v, 1);
    pthread_mutex_unlock(&feed_lock);
}

/* Register writes use rage128_reg_write for production ordering against
   the executor. Ring control writes are shadowed because the replay feeds
   dwords directly and has no guest-memory ring to pump. Display and overlay
   timing writes are held back by default because the VGA core is stubbed;
   snooped CRTC values supply geometry for the optional scanout threads. */
static uint32_t shadow_buffer_offset, shadow_buffer_cntl;
static uint32_t shadow_agp_base; /* pass-2 AGP_BASE, for the ring-write check */
static int      opt_apply_display;
static int      opt_pick_thread;
static int      opt_scan_thread; /* modeled 60 Hz CRTC beam */
static uint32_t crtc_h_disp;

/* Deferred page flip (--pick-thread): base waiting to be written to
   CRTC_OFFSET through the production register path once the executor is
   idle. Set by a captured R 0224 record or by the --pick-flip cadence;
   fired by pick_flip_pump in the pass-2 loop. */
static uint32_t pick_flip_base_next;
static int      pick_flip_want;
static uint64_t pick_flip_fired, pick_flip_lost;

static uint32_t crtc_offset, crtc_pitch, crtc_gen_cntl, crtc_v_disp;

/* In modeled PCI-GART mode (PCI_GART_PAGE bit 0 clear), in-RAM PTE
   targets bypass the bus hook. Plant a table at the captured base with
   every entry beyond RAM so the device falls back to dma_bm_read and the
   hook supplies the recorded values. --memory must cover the table. */
static uint32_t gart_planted_base = 0xffffffffu;

static void
gart_plant(uint32_t val)
{
    uint32_t base = val & 0xfffff000u;

    if (val & 1)
        return;
    if ((uint64_t) base + 8192u * 4u > ((uint64_t) mem_size << 10)) {
        if (gart_planted_base != base)
            printf("pci-gart     table base %08x is beyond --memory; PCI-GART reads will not be served\n", base);
        gart_planted_base = base;
        return;
    }
    for (uint32_t i = 0; i < 8192u; i++) {
        uint32_t pte = 0x80000000u | (i << 12);

        memcpy(ram + base + i * 4u, &pte, 4);
    }
    if (gart_planted_base != base)
        printf("pci-gart     table planted at %08x (8192 entries, all beyond RAM)\n", base);
    gart_planted_base = base;
}

/* In table mode without a captured BM_CHUNK_0_VAL write, model bus
   routing by setting RAGE128_BM_PTR_FORCE_TO_PCI, RAGE128_BM_PM4_RD_FORCE_TO_PCI,
   and RAGE128_BM_GLOBAL_FORCE_TO_PCI at the first D/J record. These bits
   select the on-chip walk rather than the chipset aperture on the AGP
   device. A captured write retains control of the routing. */
static int bm_chunk_seen;
static int bm_chunk_planted;

static void
bm_chunk_plant(rage128_t *dev)
{
    if (bm_chunk_seen || bm_chunk_planted || (dev->pci_gart_page & 1))
        return;
    bm_chunk_planted = 1;
    rage128_reg_write(dev, RAGE128_BM_CHUNK_0_VAL,
                      dev->bm_chunk_val[0] | RAGE128_BM_PTR_FORCE_TO_PCI
                          | RAGE128_BM_PM4_RD_FORCE_TO_PCI | RAGE128_BM_GLOBAL_FORCE_TO_PCI,
                      0xffffffffu);
    printf("bm-chunk     FORCE_TO_PCI bits planted (capture predates the BM_CHUNK record)\n");
}

static int
reg_is_ring_control(uint32_t off)
{
    return off == RAGE128_PM4_BUFFER_OFFSET || off == RAGE128_PM4_BUFFER_CNTL
        || off == RAGE128_PM4_BUFFER_DL_RPTR || off == RAGE128_PM4_BUFFER_DL_WPTR;
}

static int
reg_is_display(uint32_t off)
{
    /* Chip-core registers inside the 0x0010-0x00ff window: BIOS scratch,
       bus, interrupt, GPIO, config and soft-reset state the device needs.
       CONFIG_MEMSIZE stays with the display family. */
    switch (off) {
        case RAGE128_BIOS_0_SCRATCH:
        case RAGE128_BIOS_1_SCRATCH:
        case RAGE128_BIOS_2_SCRATCH:
        case RAGE128_BIOS_3_SCRATCH:
        case RAGE128_BUS_CNTL:
        case RAGE128_BUS_CNTL1:
        case RAGE128_GEN_INT_CNTL:
        case RAGE128_GEN_INT_STATUS:
        case RAGE128_GPIO_MONID:
        case RAGE128_AMCGPIO_MASK_MIR:
        case RAGE128_AMCGPIO_A_MIR:
        case RAGE128_AMCGPIO_Y_MIR:
        case RAGE128_AMCGPIO_EN_MIR:
        case RAGE128_CONFIG_CNTL:
        case RAGE128_CONFIG_XSTRAP:
        case RAGE128_CONFIG_BONDS:
        case RAGE128_GEN_RESET_CNTL:
        case RAGE128_GEN_STATUS:
            return 0;
        default:
            break;
    }
    /* CRTC/PLL/DAC/palette and the overlay block. */
    return (off >= 0x0010 && off <= 0x00ff) || (off >= 0x0200 && off <= 0x02ff)
        || (off >= 0x0400 && off <= 0x04ff);
}

static void
reg_dispatch(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask)
{
    /* No drain here: rage128_reg_write orders itself against the executor
       for exactly the registers that need it, the same way it does in the
       emulator. Draining on every write instead would be both slower and
       less faithful. */
    pending_flush(dev);

    switch (off) {
        case RAGE128_PM4_BUFFER_OFFSET:
            shadow_buffer_offset = val;
            return;
        case RAGE128_PM4_BUFFER_CNTL:
            shadow_buffer_cntl = val;
            return;
        case RAGE128_AGP_BASE:
            shadow_agp_base = val;
            break; /* still applied to the device */
        case RAGE128_PCI_GART_PAGE:
            gart_plant(val);
            break; /* still applied to the device */
        case RAGE128_BM_CHUNK_0_VAL:
            bm_chunk_seen = 1;
            break; /* still applied to the device */
        case 0x0224: /* CRTC_OFFSET */
            crtc_offset = val;
            break;
        case 0x022c: /* CRTC_PITCH */
            crtc_pitch = val;
            break;
        case 0x0050: /* CRTC_GEN_CNTL */
            crtc_gen_cntl = val;
            break;
        case 0x0208: /* CRTC_V_TOTAL_DISP */
            crtc_v_disp = (val >> 16) & 0x7ff;
            break;
        case 0x0200: /* CRTC_H_TOTAL_DISP */
            crtc_h_disp = ((val >> 16) & 0xff) + 1u;
            break;
        default:
            break;
    }
    /* The stubbed VGA core does not derive scan geometry. The optional
       pick and scan threads need it for snapshot lengths and row reads,
       so mirror the modeled display calculation from snooped CRTC state:
       CRTC_PITCH counts eight pixels and rowoffset counts eight bytes. */
    if ((opt_pick_thread || opt_scan_thread)
        && (off == 0x0050 || off == 0x0208 || off == 0x022c || off == 0x0200)) {
        uint32_t pw = (crtc_gen_cntl >> RAGE128_CRTC_PIX_WIDTH_SHIFT) & 7;
        uint32_t px = crtc_pitch & 0x3ff;

        switch (pw) {
            case 1:  dev->svga.rowoffset = (int) (px >> 1); break;
            case 3:
            case 4:  dev->svga.rowoffset = (int) (px * 2); break;
            case 5:  dev->svga.rowoffset = (int) (px * 3); break;
            case 6:  dev->svga.rowoffset = (int) (px * 4); break;
            default: dev->svga.rowoffset = (int) px; break;
        }
        if (crtc_v_disp)
            dev->svga.dispend = (int) crtc_v_disp + 1;
        if (opt_scan_thread) {
            dev->svga.hdisp = (int) (crtc_h_disp * 8u);
            dev->svga.bpp   = (pw == 6) ? 32 : (pw == 5) ? 24 : (pw >= 3) ? 16 : 8;
            /* the vsync block (snapshot pick, hazard, seed) is gated on
               extended display being enabled; mirror the snooped bit */
            if (off == 0x0050)
                dev->crtc_gen_cntl = (dev->crtc_gen_cntl & ~RAGE128_CRTC_EXT_DISP_EN)
                    | (crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN);
        }
    }
    /* A captured direct-MMIO flip. Not applied here: the flip arm drains
       the executor, and this thread is the executor's only dword source
       (see pick_flip_pump). Deferred to the next idle point instead. */
    if (opt_pick_thread && off == 0x0224 && (mask & 0x01fffff8u)) {
        if (pick_flip_want)
            pick_flip_lost++;
        pick_flip_base_next = val & 0x01fffff8u;
        pick_flip_want      = 1;
    }
    if (reg_is_ring_control(off))
        return;
    if (!opt_apply_display && reg_is_display(off))
        return;
    rage128_reg_write(dev, off, val, mask);
}

static int
hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static uint32_t
hex32(const char *s, const char **end)
{
    uint32_t v = 0;

    while (*s == ' ')
        s++;
    while (1) {
        char c = *s;
        int  d;

        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            break;
        v = (v << 4) | (uint32_t) d;
        s++;
    }
    *end = s;
    return v;
}

/* Ring byte size follows the modeled rage128_pm4_ring_mask rule for
   PM4_BUFFER_CNTL bits [5:0]. The register is shadowed rather than applied
   to the device. A 64-bit byte count preserves encodes that exceed 32 bits. */
static uint64_t
ring_bytes(uint32_t rb_cntl)
{
    uint32_t l2 = rb_cntl & 0x3f;

    return 4ull * ((uint64_t) (uint32_t) ((2ull << l2) - 1) + 1);
}

/* An I or B address inside the shadowed ring window can indicate a
   captured pump fetch. The replay injects ring dwords without fetching
   them, so such bus records shift the sequential serve and trip the gate. */
static void
ring_window_check(uint32_t addr, uint32_t agp_base, uint32_t rb_offset, uint32_t rb_cntl)
{
    uint32_t base;
    uint64_t size;

    if (!rb_cntl)
        return;
    base = agp_base + (rb_offset & 0x01ffffffu);
    size = ring_bytes(rb_cntl);
    if (addr >= base && (uint64_t) addr < base + size)
        stat_ring_window++;
}

/* Count device bus writes overlapping the shadowed CCE ring window.
   The modeled replay treats its command storage as guest-owned, so any
   overlapping write fails the gate. Writes of at least 4096 bytes also
   increment stat_ring_write_block. */
static void
replay_bm_write(uint32_t addr, uint32_t len)
{
    uint32_t base;
    uint64_t size;

    if (!shadow_agp_base || !shadow_buffer_cntl)
        return;
    base = shadow_agp_base + (shadow_buffer_offset & 0x01ffffffu);
    size = ring_bytes(shadow_buffer_cntl);
    if ((uint64_t) addr < base + size && (uint64_t) addr + len > base) {
        stat_ring_write++;
        if (len >= 4096)
            stat_ring_write_block++;
    }
}

/* The optional pick thread models the emulator's scanout consumer. It
   calls rage128_gpu_snap_pick and checksums the returned slot across a
   frame interval. A changed checksum means the producer mutates a slot
   while the modeled display holds it. */
static atomic_int pick_run;
static uint64_t   pick_calls, pick_shows, pick_mutated, pick_len_used, pick_flips;
static uint64_t   pick_flip_every; /* records between synthetic flip requests */

/* Synthetic flips exercise snapshot production when a capture supplies
   few flips. The cadence requests CRTC_OFFSET writes through
   rage128_reg_write, which drains the executor before presenting and
   copying a snapshot. The two bases start at the captured display base
   and a page-aligned buffer one frame above it; the second base falls
   back to the first if the buffer does not fit in VRAM. */
static uint32_t pick_flip_alt_base[2];
static int      pick_flip_alt_ready;

static void
pick_flip_request_synth(rage128_t *dev)
{
    uint32_t len = (uint32_t) dev->svga.rowoffset * 8u
        * ((uint32_t) dev->svga.dispend
           << (dev->svga.interlace ? 1 : 0));

    if (!len)
        return;
    if (!pick_flip_alt_ready) {
        uint32_t b0 = dev->crtc_offset & 0x01fffff8;
        uint32_t b1 = (b0 + len + 4095u) & ~4095u;

        pick_flip_alt_base[0] = b0;
        pick_flip_alt_base[1] =
            ((uint64_t) b1 + len <= dev->vram_size) ? b1 : b0;
        pick_flip_alt_ready = 1;
    }
    if (pick_flip_want)
        pick_flip_lost++;
    pick_flip_base_next = pick_flip_alt_base[(size_t) (pick_flip_fired & 1)];
    pick_flip_want      = 1;
}

/* One-line pushback for the drain-hook lookahead in pass 2. */
static char   *peek_line;
static size_t  peek_cap;
static ssize_t peek_len;
static int     peek_have;

/* Executor parked in its empty-FIFO wait with nothing queued on this
   side: only more records can move it. */
static int
exec_starved(rage128_t *dev)
{
    return atomic_load(&dev->cce_starved) && !pending_n
        && atomic_load(&dev->cce_fifo_rd) == atomic_load(&dev->cce_fifo_wr);
}

/* Drain-wait hook: the write's drain is spinning on this thread while
   the executor is parked mid-packet on an empty FIFO; the dwords it needs
   are the next D/J records. Feed one per call; an R record (or end of file) is
   pushed back and ends the feed. */
static FILE    *drain_stream;
static uint64_t drain_feeds, drain_aborts;

static int
replay_drain_feed(rage128_t *dev)
{
    /* Pending dwords precede unread stream records. Feed them first so
       the drain guard does not abort a starved packet and reinterpret its
       remainder as packet headers or register writes. */
    if (pending_n) {
        size_t before = pending_n;

        pending_flush(dev);
        return pending_n != before ? 1 : 0;
    }
    if (!drain_stream)
        return 0;
    /* Bus records are streamed, so an executor inside an indirect walk
       may be waiting on the bus thread rather than wedged: while it is
       consuming records, or the queue is empty with more to come, keep
       the drain alive (the caller's wedge guard would otherwise abort the
       packet after a few idle passes). */
    {
        static uint64_t last_served;
        uint64_t        served = atomic_load_explicit(&bq_served, memory_order_relaxed);

        if (served != last_served) {
            last_served = served;
            sched_yield();
            return 1;
        }
        if (atomic_load_explicit(&bq_head, memory_order_relaxed)
                == atomic_load_explicit(&bq_tail, memory_order_acquire)
            && !atomic_load_explicit(&bq_eof, memory_order_acquire)) {
            bq_nap();
            return 1;
        }
    }
    if (!exec_starved(dev))
        return 0;
    /* A drain on another thread is not ordered against the stream: it
       must not read past a record the loop has fetched but not applied,
       nor abort on a held one -- the record loop applies it and feeds on. */
    if (!pthread_equal(pthread_self(), feed_main_tid)
        && (peek_have || feed_inflight)) {
        sched_yield();
        return 1;
    }
    if (peek_have) {
        /* Starved with a non-D/J record next: nothing in the stream
           completes this packet. Abort it (the executor drops the
           packet, as on a dead bus) rather than spin forever. */
        drain_aborts++;
        return 2;
    }
    if ((peek_len = getline(&peek_line, &peek_cap, drain_stream)) <= 0)
        return 2;
    if (peek_line[0] == 'D' || peek_line[0] == 'J') {
        const char *q = peek_line + 1;

        nrec++;
        if (peek_line[0] == 'D') stat_d++; else stat_j++;
        push_dword(dev, hex32(q, &q));
        thread_set_event(dev->cce_wake_event);
        drain_feeds++;
        return 1;
    }
    peek_have = 1;
    return 0;
}

static int
replay_drain_hook(rage128_t *dev)
{
    int r;

    pthread_mutex_lock(&feed_lock);
    r = replay_drain_feed(dev);
    pthread_mutex_unlock(&feed_lock);
    return r;
}

/* A flip write drains the executor, but a starved executor needs
   more dwords from this thread. Wait only within a bound, retaining the
   request when the executor needs input. A modeled ring window opened
   by an in-stream PM4_BUFFER_DL_WPTR write has no backing memory to
   fetch; the pump clears that window so it cannot block quiescence. */
static int      pick_flip_stalled;

static void
pick_flip_pump(rage128_t *dev)
{
    uint32_t rd0;

    if (!pick_flip_want)
        return;
    pending_flush(dev);
    rd0 = atomic_load(&dev->cce_fifo_rd);
    /* While the executor remains starved mid-packet, feed more records
       instead of repeating a wait that this thread alone can satisfy. */
    if (pick_flip_stalled) {
        if (rd0 == atomic_load(&dev->cce_fifo_wr)
            && atomic_load(&dev->cce_executing))
            return;
        pick_flip_stalled = 0;
    }
    for (int tries = 0, starved = 0; tries < 200; tries++) {
        struct timespec ts = { 0, 50000 };
        uint32_t        rd;

        pending_flush(dev);
        if (rage128_pm4_ring_bm(dev)
            && dev->pm4_rptr != (dev->pm4_wptr & rage128_pm4_ring_mask(dev))) {
            /* In-stream type-0 writes can arm PM4_BUFFER_CNTL and
               PM4_BUFFER_DL_WPTR even though direct R writes are held back.
               Clear this modeled window: its dwords arrive as D records,
               and a guest-memory pump cannot make progress here. */
            dev->pm4_buffer_cntl = 0;
            dev->pm4_rptr = dev->pm4_wptr = 0;
        }
        if (!pending_n && !rage128_pm4_active(dev)) {
            pick_flip_want = 0;
            pick_flip_fired++;
            rage128_reg_write(dev, 0x0224, pick_flip_base_next, 0x01fffff8u);
            /* Two flips to the other buffer leave newer copies of that base
               beside an older copy of the first base. A lagging scan can
               then pick the oldest slot while the producer selects a
               replacement, exercising snapshot ownership under overlap. */
            if (pick_flip_alt_ready) {
                uint32_t other =
                    (pick_flip_base_next == pick_flip_alt_base[0])
                        ? pick_flip_alt_base[1] : pick_flip_alt_base[0];

                rage128_reg_write(dev, 0x0224, other, 0x01fffff8u);
                rage128_reg_write(dev, 0x0224, other, 0x01fffff8u);
            }
            return;
        }
        rd = atomic_load(&dev->cce_fifo_rd);
        if (rd == atomic_load(&dev->cce_fifo_wr)
            && atomic_load(&dev->cce_executing) && rd == rd0) {
            /* empty FIFO, executor busy, no dword consumed: either
               crunching a just-popped packet (ends on its own, keep
               waiting a little) or starved mid-packet (only more
               records help -- back out, retry at the next one) */
            if (++starved >= 4) {
                pick_flip_stalled = 1;
                return;
            }
        } else {
            starved = 0;
            rd0     = rd;
        }
        thread_set_event(dev->cce_wake_event);
        nanosleep(&ts, NULL);
    }
    /* Engine still busy after a full 10 ms bound: drop this flip. A
       standing retry re-enters the wait on every record and collapses
       the replay to ~1 ms/record; one generous attempt per request
       keeps the stream moving at full speed between flips. */
    pick_flip_want = 0;
    pick_flip_lost++;
}

/* Checksum the modeled displayed surface in eight-byte steps so
   both checks fit around a frame interval. The device checksum uses
   byte steps; this sampler only needs to detect a slot mutation. */
static uint64_t
pick_ck(const uint8_t *p, uint32_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;

    for (uint32_t i = 0; i < n; i += 8) {
        h ^= *(const uint64_t *) (p + i);
        h *= 0x100000001b3ull;
    }
    return h;
}

static void *
pick_thread_fn(void *arg)
{
    rage128_t *dev = (rage128_t *) arg;
    svga_t    *svga = &dev->svga;

    while (atomic_load_explicit(&pick_run, memory_order_acquire)) {
        uint32_t dst = 0, len, want;
        uint64_t seq = 0, a, b;
        struct timespec ts = { 0, 16000000 }; /* one stand-in scan frame */

        len = (uint32_t) svga->rowoffset * 8u
            * ((uint32_t) svga->dispend << (svga->interlace ? 1 : 0));
        len &= ~7u;
        pick_calls++;
        pick_len_used = len;
        {
            static uint32_t last_base = 0xffffffffu;
            uint32_t        b0        = dev->crtc_offset & 0x01fffff8;

            if (b0 != last_base) {
                last_base = b0;
                pick_flips++;
            }
        }
        /* Rotate the requested base independently of the current flip.
           This models a vertical-blank-latched scan that lags the producer
           and can hold an older slot alongside newer copies of the other
           base, exercising the producer replacement path. */
        if (pick_flip_alt_ready)
            want = pick_flip_alt_base[(pick_calls >> 1) & 1];
        else
            want = dev->crtc_offset & 0x01fffff8;
        if (!len || !rage128_gpu_snap_pick(dev, want, &dst, &seq))
            continue;
        pick_shows++;
        if ((uint64_t) dst + len > (uint64_t) dev->vram_size * 2)
            continue;
        /* Hold the slot for a frame without picking again. The pick
           publishes snap_displayed, which protects the slot from the
           producer. A refused second pick clears that ownership in the
           modeled device and would invalidate this mutation check. */
        a = pick_ck(svga->vram + dst, len);
        nanosleep(&ts, NULL);
        b = pick_ck(svga->vram + dst, len);
        if (a != b) {
            pick_mutated++;
            /* the finding itself: the published slot changed under the
               display; keep the evidence bounded */
            if (pick_mutated <= 10)
                fprintf(stderr,
                        "PICK-MUTATED #%llu dst=%08x seq=%llu base=%08x\n",
                        (unsigned long long) pick_mutated, dst,
                        (unsigned long long) seq,
                        dev->crtc_offset & 0x01fffff8);
        }
    }
    return NULL;
}

/* The optional scan thread models a 60 Hz beam using the device's
   horizontal-blank tick, vertical-blank latch, and vsync callback. The
   vertical-blank callback takes a pending base when CRTC_OFFSET_FLIP_CNTL
   in CRTC_OFFSET_CNTL is clear (RRG: CRTC_OFFSET_CNTL, p. 3-72 / PDF 90).
   Each displayed row is copied from the latched base or snapshot. Scoring
   counts rows with at least 95 percent RGB565 pixels equal to 0x4288. */
static atomic_int scan_run;
static uint64_t   scan_frames, scan_bad_frames, scan_bad_rows, scan_max_bad;

static void *
scan_thread_fn(void *arg)
{
    rage128_t *dev  = (rage128_t *) arg;
    svga_t    *svga = &dev->svga;
    uint8_t   *fb   = NULL;
    size_t     fbcap = 0;

    while (atomic_load(&scan_run)) {
        int      dispend = svga->dispend;
        int      hdisp   = svga->hdisp;
        int      bpp     = svga->bpp;
        uint32_t pitch   = (uint32_t) svga->rowoffset * 8u;
        int      vtotal, vsyncstart;
        double   line_ns;
        size_t   rowb;
        struct timespec ts;

        int scannable = !(dispend <= 0 || hdisp <= 0 || !pitch || bpp != 16
                          || dispend > 1200 || hdisp > 1600);
        if (!scannable) {
            static int last_d, last_h, last_p, last_b;
            if (dispend != last_d || hdisp != last_h || (int) pitch != last_p || bpp != last_b) {
                last_d = dispend; last_h = hdisp; last_p = (int) pitch; last_b = bpp;
                pclog("[scan] geometry not scannable: dispend=%d hdisp=%d pitchB=%u bpp=%d gen=%08x (beam runs at 480 lines, no scoring)\n",
                      dispend, hdisp, pitch, bpp, dev->crtc_gen_cntl);
            }
            /* keep the beam (latch + vsync) alive on a default raster so
               a WAIT_UNTIL flip stall can still end the way it does in
               the emulator; just do not read or score rows */
            dispend = 480; hdisp = 640; pitch = 1280;
        }
        vtotal     = dispend + dispend / 11 + 1; /* ~525 for 480 */
        vsyncstart = dispend + 10;
        line_ns    = 16666667.0 / (double) vtotal;
        rowb       = (size_t) hdisp * 2u;
        if (fbcap < rowb * (size_t) dispend) {
            fbcap = rowb * (size_t) dispend;
            fb    = realloc(fb, fbcap);
        }
        svga->dispon = 1;
        for (int line = 0; line < dispend; line++) {
            uint32_t base, addr;

            svga->vc = line;
            svga->displine = line;
            rage128_crtc_offset_hblank_tick(dev);
            base = dev->scanout_snap_cur ? dev->scanout_snap_cur
                                         : (dev->crtc_offset_latched & 0x01fffff8);
            addr = base + (uint32_t) line * pitch;
            if (scannable && (uint64_t) addr + rowb <= (uint64_t) svga->vram_max)
                memcpy(fb + (size_t) line * rowb, svga->vram + addr, rowb);
            else
                memset(fb + (size_t) line * rowb, 0, rowb);
            if ((line & 7) == 7) {
                ts.tv_sec = 0; ts.tv_nsec = (long) (line_ns * 8.0);
                nanosleep(&ts, NULL);
            }
        }
        /* vertical blank: latch at dispend, vsync a few lines later */
        svga->vc = dispend;
        svga->displine = dispend;
        svga->dispon = 0;
        if (svga->vblank_start)
            svga->vblank_start(svga);
        for (int line = dispend; line < vtotal; line++) {
            svga->vc = line;
            svga->displine = line;
            rage128_crtc_offset_hblank_tick(dev);
            if (line == vsyncstart && svga->vsync_callback)
                svga->vsync_callback(svga);
            ts.tv_sec = 0; ts.tv_nsec = (long) line_ns;
            nanosleep(&ts, NULL);
        }
        /* Count rows that meet the modeled color threshold. */
        if (scannable) {
            unsigned bad = 0;
            int      lo = -1, hi = -1;

            for (int line = 0; line < dispend; line++) {
                const uint16_t *px = (const uint16_t *) (fb + (size_t) line * rowb);
                int n = 0;

                for (int x = 0; x < hdisp; x++)
                    n += (px[x] == 0x4288);
                if (n * 20 >= hdisp * 19) { /* at least 95 percent of pixels equal 0x4288 */
                    bad++;
                    if (lo < 0) lo = line;
                    hi = line;
                }
            }
            scan_frames++;
            if (bad) {
                scan_bad_frames++;
                scan_bad_rows += bad;
                if (bad > scan_max_bad) scan_max_bad = bad;
                pclog("[scan] f=%u base=%08x snap=%08x OLIVE rows=%u [%d,%d] of %d\n",
                      dev->frame_count, dev->crtc_offset_latched & 0x01fffff8,
                      dev->scanout_snap_cur, bad, lo, hi, dispend);
            }
        }
    }
    free(fb);
    return NULL;
}

/* Each reader opens a separate stream. A .zst suffix selects a zstd
   decompression child; other paths are read as plain text. */
static int
path_is_zst(const char *path)
{
    size_t n = strlen(path);

    return n > 4 && !strcmp(path + n - 4, ".zst");
}

static FILE *
capture_open(const char *path)
{
    FILE *f;

    if (path_is_zst(path)) {
        char cmd[4096];

#ifdef _WIN32
        /* The Windows command shell expands percent-delimited environment
           variables even inside double quotes, so either character in a
           path is refused. Binary pipe mode preserves carriage returns
           and prevents a 0x1a byte from ending the input stream. */
        if (strpbrk(path, "\"%")) {
            fprintf(stderr, "replay: double quote or %% in .zst path not supported\n");
            return NULL;
        }
        snprintf(cmd, sizeof(cmd), "zstd -dc -q -- \"%s\"", path);
        f = popen(cmd, "rb");
#else
        if (strchr(path, '\'')) {
            fprintf(stderr, "replay: quote in .zst path not supported\n");
            return NULL;
        }
        snprintf(cmd, sizeof(cmd), "zstd -dc -q -- '%s'", path);
        f = popen(cmd, "r");
#endif
    } else
        f = fopen(path, "r");
    if (!f)
        perror(path);
    return f;
}

static void
capture_close(const char *path, FILE *f)
{
    if (path_is_zst(path))
        pclose(f);
    else
        fclose(f);
}

/* The second reader queues I/B/X records in capture order and shadows
   ring registers for the window tripwire without touching device state. */
typedef struct {
    const char *path;
    uint64_t    streamcut;
} bus_src_t;

static void *
bus_thread_fn(void *arg)
{
    const bus_src_t *src = arg;
    FILE            *f   = capture_open(src->path);
    char            *line = NULL;
    size_t           lcap = 0;
    ssize_t          len;
    uint64_t         n = 0;
    uint32_t         agp_base = 0, rb_offset = 0, rb_cntl = 0;

    if (!f)
        exit(2);
    while ((len = getline(&line, &lcap, f)) > 0) {
        const char *p = line + 1;
        uint32_t    a;
        uint32_t    b;

        if (src->streamcut && n >= src->streamcut)
            break;
        n++;
        switch (line[0]) {
            case 'I':
                stat_i++;
                a = hex32(p, &p);
                b = hex32(p, &p);
                ring_window_check(a, agp_base, rb_offset, rb_cntl);
                bq_push(a, b, 4, NULL);
                break;
            case 'X':
                stat_x++;
                a = hex32(p, &p);
                b = hex32(p, &p);
                bq_push(a, 0, b ? b : 4, NULL);
                break;
            case 'B': {
                /* B payloads contain two hex digits per byte. Pad a short or
                   malformed payload with 0xff and increment stat_b_short;
                   the short count itself does not fail the replay gate. */
                uint8_t *blk;
                uint32_t got = 0;

                stat_b++;
                a = hex32(p, &p);
                b = hex32(p, &p);
                if (!b)
                    break;
                ring_window_check(a, agp_base, rb_offset, rb_cntl);
                blk = malloc(b);
                if (!blk) {
                    fprintf(stderr, "replay: out of memory for a %u-byte B record\n", b);
                    exit(2);
                }
                while (*p == ' ')
                    p++;
                for (; got < b; got++) {
                    int hi = hexval(p[0]);
                    int lo = (hi >= 0) ? hexval(p[1]) : -1;

                    if (lo < 0)
                        break;
                    blk[got] = (uint8_t) ((hi << 4) | lo);
                    p += 2;
                }
                if (got < b) {
                    memset(blk + got, 0xff, b - got);
                    stat_b_short++;
                }
                stat_b_bytes += b;
                bq_push(a, 0, b, blk);
                break;
            }
            case 'R':
                a = hex32(p, &p);
                b = hex32(p, &p);
                if (a == RAGE128_AGP_BASE)
                    agp_base = b;
                else if (a == RAGE128_PM4_BUFFER_OFFSET)
                    rb_offset = b;
                else if (a == RAGE128_PM4_BUFFER_CNTL)
                    rb_cntl = b;
                break;
            case 'A':
                stat_a++;
                break;
            case 'W':
                stat_w++;
                break;
            default:
                break;
        }
    }
    free(line);
    capture_close(src->path, f);
    atomic_store_explicit(&bq_eof, 1, memory_order_release);
    return NULL;
}

int
main(int argc, char **argv)
{
    const char *path      = NULL;
    const char *recap     = NULL;
    const char *dumpvram  = NULL;
    uint64_t    streamcut = 0;
    rage128_t  *dev;
    FILE       *f;
    char       *line = NULL;
    size_t      lcap = 0;
    ssize_t     len;

    /* Guest RAM. Only a PCI-GART capture touches it (the page table the
       device walks has to land where the guest put it); AGP-mode captures
       resolve everything through the bus hook. */
    mem_size = 64u << 10;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v"))
            harness_log_quiet = 0;
        else if (!strcmp(argv[i], "--recap") && i + 1 < argc)
            recap = argv[++i];
        else if (!strcmp(argv[i], "--dump-vram") && i + 1 < argc)
            dumpvram = argv[++i];
        else if (!strcmp(argv[i], "--tail-budget") && i + 1 < argc)
            tail_budget = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--streamcut") && i + 1 < argc)
            streamcut = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--apply-display"))
            opt_apply_display = 1;
        else if (!strcmp(argv[i], "--pick-thread"))
            opt_pick_thread = 1;
        else if (!strcmp(argv[i], "--scan-thread"))
            opt_scan_thread = 1;
        else if (!strcmp(argv[i], "--pick-flip") && i + 1 < argc)
            pick_flip_every = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--memory") && i + 1 < argc)
            mem_size = (uint32_t) atoi(argv[++i]) << 10; /* guest RAM, KB */
        else if (!strcmp(argv[i], "--vram") && i + 1 < argc)
            harness_cfg_memory_mb = atoi(argv[++i]);
        else if (argv[i][0] != '-')
            path = argv[i];
        else {
            fprintf(stderr,
                    "usage: replay [-v] [--memory MB] [--vram MB] [--streamcut N] [--tail-budget N]\n"
                    "              [--recap FILE] [--dump-vram FILE]\n"
                    "              [--apply-display] [--pick-thread] [--pick-flip N] [--scan-thread] <capture>\n");
            return 2;
        }
    }
    if (!path) {
        fprintf(stderr, "replay: no capture file given\n");
        return 2;
    }
    f = capture_open(path);
    if (!f)
        return 2;

    harness_ram_alloc();
    harness_cfg_render_threads = 1;
    harness_cfg_recompiler     = 0;
    dev                        = rage128_init(&ati_rage128_pro_device);

    /* Bus mastering as a driver leaves it; the capture carries AGP_BASE
       but not PCI config space. */
    harness_pci_write(0, 0x04, 1, 0x07, harness_pci_priv);
    rage128_reg_write(dev, RAGE128_BUS_CNTL,
                      rage128_reg_read32(dev, RAGE128_BUS_CNTL)
                          & ~RAGE128_BUS_CNTL_BUS_MASTER_DIS,
                      0xffffffffu);

    if (recap) {
        /* Set the recap stream before command input starts so the production
           taps record replay execution in addition to the input stream.
           Injected D dwords appear as J, and bus dwords can coalesce into B
           runs, so record comparison must account for those forms. */
        dev->cap_file = fopen(recap, "w");
        if (!dev->cap_file) {
            perror(recap);
            return 2;
        }
    }

    /* Bus reader: streams the I/B/X records from a second read of the
       capture, ahead of the device by up to BQ_CAP records. */
    pthread_t bus_th;
    bus_src_t bus_src = { path, streamcut };

    bq_cap = BQ_CAP;
    bq     = malloc(bq_cap * sizeof(*bq));
    if (!bq) {
        fprintf(stderr, "replay: out of memory for the bus queue\n");
        return 2;
    }
    if (pthread_create(&bus_th, NULL, bus_thread_fn, &bus_src) != 0) {
        fprintf(stderr, "replay: bus thread create failed\n");
        return 2;
    }

    harness_bm_read_hook  = replay_bm_read;
    harness_bm_write_hook = replay_bm_write;
    {
        pthread_mutexattr_t ma;

        pthread_mutexattr_init(&ma);
        pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&feed_lock, &ma);
        pthread_mutexattr_destroy(&ma);
        feed_main_tid = pthread_self();
    }
    drain_stream              = f;
    rage128_harness_drain_hook = replay_drain_hook;

    pthread_t scan_th;
    if (opt_scan_thread) {
        atomic_store(&scan_run, 1);
        if (pthread_create(&scan_th, NULL, scan_thread_fn, dev) != 0) {
            fprintf(stderr, "replay: scan thread create failed\n");
            return 2;
        }
    }
    pthread_t pick_th;
    if (opt_pick_thread) {
        atomic_store(&pick_run, 1);
        if (pthread_create(&pick_th, NULL, pick_thread_fn, dev) != 0) {
            fprintf(stderr, "replay: pick thread create failed\n");
            return 2;
        }
    }

    for (;;) {
        const char *p;
        uint32_t    a;
        uint32_t    b;

        pthread_mutex_lock(&feed_lock);
        if (peek_have) {
            char  *tl = line;  size_t tc = lcap;
            line = peek_line; lcap = peek_cap; len = peek_len;
            peek_line = tl;   peek_cap = tc;  peek_have = 0;
        } else
            len = getline(&line, &lcap, f);
        feed_inflight = len > 0;
        pthread_mutex_unlock(&feed_lock);
        if (len <= 0)
            break;
        p = line + 1;
        if (streamcut && nrec >= streamcut)
            break;
        nrec++;
        /* Record-index heartbeat on the same stream as pclog, so a
           device-side complaint can be tied to the record that caused
           it. */
        if (!harness_log_quiet && !(nrec % 50000))
            fprintf(stderr, "[replay] nrec=%llu\n", (unsigned long long) nrec);
        if (pick_flip_every && !(nrec % pick_flip_every))
            pick_flip_request_synth(dev);
        if (opt_pick_thread) {
            pick_flip_pump(dev);
            /* progress heartbeat: a full-stream GPU replay can wedge
               mid-run, and the counters must survive that */
            if (!(nrec % 2000000))
                fprintf(stderr,
                        "pick progress nrec=%llu fired=%llu lost=%llu "
                        "shows=%llu mutated=%llu\n",
                        (unsigned long long) nrec,
                        (unsigned long long) pick_flip_fired,
                        (unsigned long long) pick_flip_lost,
                        (unsigned long long) pick_shows,
                        (unsigned long long) pick_mutated);
        }
        switch (line[0]) {
            case 'D':
            case 'J':
                if (line[0] == 'D') stat_d++; else stat_j++;
                bm_chunk_plant(dev);
                pthread_mutex_lock(&feed_lock);
                push_dword(dev, hex32(p, &p));
                feed_inflight = 0;
                pthread_mutex_unlock(&feed_lock);
                break;
            case 'R':
                stat_r++;
                a = hex32(p, &p);
                b = hex32(p, &p);
                pthread_mutex_lock(&feed_lock);
                feed_inflight = 0;
                pthread_mutex_unlock(&feed_lock);
                /* A write that drains while the executor is mid-packet
                   gets its completing dwords from replay_drain_hook. */
                reg_dispatch(dev, a, b, hex32(p, &p));
                break;
            default:
                pthread_mutex_lock(&feed_lock);
                feed_inflight = 0;
                pthread_mutex_unlock(&feed_lock);
                break;
        }
    }
    free(line);
    /* The hook stays installed through the eos pending flush below: with
       the stream closed it only hands over parked dwords. */
    pthread_mutex_lock(&feed_lock);
    feed_inflight = 0;
    drain_stream  = NULL;
    capture_close(path, f);
    pthread_mutex_unlock(&feed_lock);
    atomic_store(&stream_done, 1);

    if (opt_scan_thread) {
        atomic_store(&scan_run, 0);
        pthread_join(scan_th, NULL);
        printf("scan-frames=%llu olive-frames=%llu olive-rows=%llu max-olive-rows-in-frame=%llu (hdisp=%d dispend=%d rowoffset=%d)\n",
               (unsigned long long) scan_frames, (unsigned long long) scan_bad_frames,
               (unsigned long long) scan_bad_rows, (unsigned long long) scan_max_bad,
               dev->svga.hdisp, dev->svga.dispend, dev->svga.rowoffset);
        fflush(stdout);
    }
    if (opt_pick_thread) {
        /* Report snapshot counters before draining so they remain available
           if a starved executor prevents the drain from completing. */
        atomic_store(&pick_run, 0);
        pthread_join(pick_th, NULL);
        printf("pick-calls=%llu pick-shows=%llu pick-mutated=%llu pick-fired=%llu pick-lost=%llu"
               " pick-len=%llu flips=%llu (rowoffset=%d dispend=%d vdisp=%u gpu=%d vram=%u)\n",
               (unsigned long long) pick_calls, (unsigned long long) pick_shows,
               (unsigned long long) pick_mutated,
               (unsigned long long) pick_flip_fired,
               (unsigned long long) pick_flip_lost,
               (unsigned long long) pick_len_used,
               (unsigned long long) pick_flips,
               dev->svga.rowoffset, dev->svga.dispend, crtc_v_disp,
               dev->gpu != NULL, dev->vram_size);
        fflush(stdout);
    }

    /* Clear the modeled ring window before the final drain. In-stream
       type-0 writes can arm PM4_BUFFER_CNTL and PM4_BUFFER_DL_WPTR, unlike
       the direct R writes held back by reg_dispatch. There is no guest
       ring to fetch, so its read pointer cannot catch the write pointer.
       Clearing the window lets only the FIFO and executor determine
       whether command work remains. */
    /* Report only the ring window. FIFO and executor activity also
       contribute to rage128_pm4_active and vary while the CCE thread runs;
       including them here would make this ring diagnostic ambiguous. */
    if (!harness_log_quiet)
        printf("ring at eos  cntl=%08x rptr=%08x wptr=%08x (window %s)\n",
               dev->pm4_buffer_cntl, dev->pm4_rptr, dev->pm4_wptr,
               (rage128_pm4_ring_bm(dev)
                && dev->pm4_rptr != (dev->pm4_wptr & rage128_pm4_ring_mask(dev)))
                   ? "OPEN" : "closed");
    dev->pm4_buffer_cntl = 0;
    dev->pm4_rptr = dev->pm4_wptr = 0;

    /* Everything still parked has to reach the executor before the
       drain, or the tail of the stream is silently dropped. */
    for (int stall = 0; pending_n; ) {
        size_t before = pending_n;

        pending_flush(dev);
        if (pending_n != before) {
            stall = 0;
            continue;
        }
        rage128_pm4_drain_wait(dev);
        /* No progress and the drain returned: the executor is not going to
           take these dwords. Say so and give up rather than spin -- an
           unbounded wait here turns a finished replay into a hang, and
           every counter below is already final. */
        if (++stall >= 64) {
            printf("tail stalled  %zu dword(s) never reached the executor\n",
                   pending_n);
            break;
        }
    }
    rage128_harness_drain_hook = NULL;
    /* Bound the final drain because an unfinished packet can leave
       cce_executing set while the executor waits for input. Progress is a
       FIFO pop or a served bus read; an indirect walk can progress without
       popping FIFO data. Deferred GPU initialization and an empty bus
       queue whose producer has more input can pause both counters under
       host load, so those states receive a longer bounded wait. The CCE
       thread remains parked until the report and VRAM dump finish. */
    {
        uint32_t last    = ~0u;
        uint64_t served  = ~0ull;
        int      wait_ms = 0;

        for (int stall = 0; stall < 64 && rage128_pm4_active(dev); ) {
            uint32_t rd = atomic_load(&dev->cce_fifo_rd);
            uint64_t sv = atomic_load_explicit(&bq_served, memory_order_relaxed);
            int      init_busy = dev->gpu_mode_cfg != R128_GPU_OFF && !dev->gpu
                              && !dev->gpu_defer;
            int      bus_busy  = atomic_load_explicit(&bq_waiting, memory_order_relaxed)
                              && !atomic_load_explicit(&bq_eof, memory_order_acquire);

            if (rd != last || sv != served) {
                last    = rd;
                served  = sv;
                stall   = 0;
                wait_ms = 0;
            } else if ((init_busy || bus_busy) && wait_ms < 600000) {
                wait_ms++;
            } else
                stall++;
            thread_set_event(dev->cce_wake_event);
            plat_delay_ms(1);
        }
    }
    /* The stop itself comes after the report and the VRAM dump: clearing
       cce_thread_run releases a WAIT_UNTIL stall, and the executor then
       runs on (bus reads, draws) while this thread prints, so a counter
       or image taken after it depends on scheduling. Parked here, the
       executor cannot move anything below. */
    int eos_active = rage128_pm4_active(dev);

    if (eos_active)
        printf("engine at eos still active (executing=%d fifo=%u/%u batch=%d)"
               " -- stopping the CCE thread after the report\n",
               atomic_load(&dev->cce_executing),
               atomic_load(&dev->cce_fifo_rd), atomic_load(&dev->cce_fifo_wr),
               atomic_load(&dev->cce_batch_pending));
    rage128_raster_flush(dev);

    /* Device done: whatever the bus thread still holds or has yet to
       parse is unconsumed by definition. Release it and collect. */
    atomic_store(&bq_drop, 1);
    pthread_join(bus_th, NULL);

    stat_bm_write = harness_sysmem_stray;

    printf("records      %llu\n", (unsigned long long) nrec);
    printf("  D ring     %llu\n", (unsigned long long) stat_d);
    printf("  drain-hook feeds %llu aborts %llu\n", (unsigned long long) drain_feeds,
           (unsigned long long) drain_aborts);
    printf("  J inject   %llu\n", (unsigned long long) stat_j);
    printf("  I bus      %llu\n", (unsigned long long) stat_i);
    printf("  X block    %llu\n", (unsigned long long) stat_x);
    printf("  B block    %llu (%llu bytes, %llu short)\n", (unsigned long long) stat_b,
           (unsigned long long) stat_b_bytes, (unsigned long long) stat_b_short);
    printf("  R regwrite %llu\n", (unsigned long long) stat_r);
    if (stat_a)
        printf("  A discrim  %llu   <-- spliced bodies that failed to frame\n",
               (unsigned long long) stat_a);
    if (stat_w)
        printf("  W buswrite %llu\n", (unsigned long long) stat_w);
    printf("bus served   %llu (block %llu, block skipped %llu, payload blocks %llu, payload dwords %llu)\n",
           (unsigned long long) stat_served, (unsigned long long) stat_block_served,
           (unsigned long long) stat_block_skipped, (unsigned long long) stat_blk_served,
           (unsigned long long) stat_blk_dword_served);
    printf("mismatch     %llu\n", (unsigned long long) stat_mismatch);
    printf("underrun     %llu\n", (unsigned long long) stat_underrun);
    printf("underrun tail %llu (post-stream; %llu served as zero)\n",
           (unsigned long long) stat_underrun_tail,
           (unsigned long long) (stat_underrun_tail > tail_budget
                                 ? stat_underrun_tail - tail_budget : 0));
    printf("unconsumed   %llu\n",
           (unsigned long long) (atomic_load(&bq_tail) - atomic_load(&bq_head)
                                 + stat_bq_dropped));
    /* Device->guest writes have nowhere to land: the replay has no guest
       and no AGP aperture behind the addresses the capture used. Reads
       come from the recorded stream, so a nonzero read share here means
       the device mastered an address the capture never recorded. */
    printf("dropped bm   %llu write, %llu read\n",
           (unsigned long long) harness_sysmem_stray_wr,
           (unsigned long long) (stat_bm_write - harness_sysmem_stray_wr));
    printf("ring writes  %llu (block %llu)   (device writes into the CCE ring; must be 0)\n",
           (unsigned long long) stat_ring_write,
           (unsigned long long) stat_ring_write_block);
    if (stat_ring_window)
        printf("ring window WARNING: %llu I records inside the ring window;"
               " this capture is polluted and must be recaptured\n",
               (unsigned long long) stat_ring_window);
    printf("crtc         offset=%08x pitch=%08x gen_cntl=%08x\n",
           crtc_offset, crtc_pitch, crtc_gen_cntl);

    /* The dump reads svga.vram on this thread. On the GPU lane the
       drained stream can still own bytes of it: a pending segment not
       yet submitted, and submitted slots whose mirror copy-back has not
       landed, so the bytes read here would be the image from before
       those draws and blits. The production CPU-read barrier over all of
       local VRAM is what a guest aperture read pays for the same bytes:
       it quiesces the CCE, flushes the pending segment and waits on the
       fences of every slot that stores into the range. A stream that
       ends inside an unfinished packet or a WAIT_UNTIL stall leaves the
       executor parked with cce_executing set, and the barrier's quiesce
       would spin on it forever, or, once the thread is released, let the
       executor run on and move the cut point under the image. That image
       is refused instead of drained: the file is not written and the
       run exits 3, so a caller cannot mistake a pre-barrier image for a
       coherent one. The CPU lane needs neither: its draws finish in
       rage128_raster_flush above. */
    int dump_refused = 0;

    if (dumpvram && dev->gpu) {
        if (eos_active) {
            printf("vram dump refused: engine still active at eos, the GPU"
                   " image is not coherent and a drain here is unbounded\n");
            dump_refused = 1;
        } else {
            rage128_gpu_cpu_barrier(dev, 0, (uint32_t) dev->svga.vram_max, 0);
            printf("vram coherent: CPU-read barrier over %d bytes done\n",
                   dev->svga.vram_max);
        }
    }
    if (dumpvram && !dump_refused) {
        FILE *o = fopen(dumpvram, "wb");

        if (o) {
            fwrite(dev->svga.vram, 1, (size_t) dev->svga.vram_max, o);
            fclose(o);
            printf("vram dumped  %s (%d bytes)\n", dumpvram, dev->svga.vram_max);
        } else {
            perror(dumpvram);
        }
    }

    harness_bm_read_hook  = NULL;
    harness_bm_write_hook = NULL;
    if (eos_active) {
        dev->cce_thread_run = 0;
        thread_set_event(dev->cce_wake_event);
    }
    rage128_close(dev);
    if (stat_mismatch || stat_underrun || stat_ring_window || stat_ring_write)
        return 1;
    return dump_refused ? 3 : 0;
}
