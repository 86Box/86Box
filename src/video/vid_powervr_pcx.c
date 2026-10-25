/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NEC / VideoLogic PowerVR PCX1 and PCX2 (Matrox m3D,
 *          VideoLogic Apocalypse 3D/3Dx/5D, NEC 3DEngine) PCI 3D
 *          accelerator.
 *
 *          Models the interface the Windows 9x drivers rely on
 *          (VSGL.VXD 4.x): PCI identity, the BAR0 register file with its
 *          fog / TLB / divider-table arrays, the BAR1 texture memory
 *          window, the STARTRENDER strobe and the end-of-render
 *          interrupt. PCX2 frames are drawn by vid_powervr_pcx_render.c
 *          on a worker thread and copied to SOFADDR when the render
 *          completes; PCX1 frames complete without drawing.
 *
 *          Register names, offsets and defaults come from the PowerVR
 *          Series 1 driver source released by Imagination Technologies
 *          under the MIT licence (pcx/hwregs.h, pcx/hwregs.c,
 *          win32/brdsetup.c) and from the register descriptor table in
 *          every 4.x VSGL.VXD.
 *
 *          PCI configuration values and register reset values were read
 *          from a Matrox m3D (PCX2, REVISION 1) on 2026-10-17.
 *
 *          Frame dumps (config "dump_frames") write, per STARTRENDER:
 *            header   pcx_dump_hdr_t (little endian, 64 bytes)
 *            regs     1024 x uint32  (BAR0 dwords 0x000-0x3FF)
 *            isp      tlb_entries pages of page_bytes, read through the TLB
 *            object   object_bytes from OBJECT_OFFSET (PCX1 only)
 *            texmem   texmem_bytes from the start of BAR1
 *
 * Authors: Zvi Rosen and contributors.
 *
 *          Copyright 2026 Zvi Rosen.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <stdatomic.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/path.h>
#include <86box/video.h>
#include <86box/thread.h>
#include "vid_powervr_pcx_tables.h"
#include "vid_powervr_pcx_render.h"

/* PCI identity. */
#define PCX_VENDOR_NEC      0x1033
#define PCX1_DEVICE_ID      0x002a
#define PCX2_DEVICE_ID      0x0046

/* Measured on a Matrox m3D: class 048000, both BARs 32-bit memory,
   prefetchable; BAR0 64 KB, BAR1 4 MB. */
#define PCX_PCI_CLASS       0x04
#define PCX_PCI_SUBCLASS    0x80
#define PCX_PCI_PROGIF      0x00
#define PCX_BAR0_SIZE       0x00010000 /* register file (only 4 KB decoded here) */
#define PCX_BAR1_SIZE       0x00400000 /* 4 MB texture memory window */
#define PCX_BAR_FLAGS       0x00000008 /* memory, 32-bit, prefetchable */

/* Register values after power-on, before any driver write (m3D). */
#define PCX_RESET_TMEM_SETUP   0x377777ff
#define PCX_RESET_TMEM_REFRESH 0x00000700
#define PCX_RESET_GPPORT       0x0000f616

#define PCX_TEXMEM_SIZE     0x00400000

/* Register file: DWORD indices into BAR0 (pcx/hwregs.h). */
#define PCX_ID              0x000
#define PCX_REVISION        0x001
#define PCX_SOFTRESET       0x002
#define PCX_INTSTATUS       0x003
#define PCX_INTMASK         0x004
#define PCX_STARTRENDER     0x005
#define PCX_FOGAMOUNT       0x006
#define PCX_OBJECT_OFFSET   0x007
#define PCX_PAGE_CTRL       0x008
#define PCX_ISP_BASE        0x00a
#define PCX_PREC_BASE       0x00b
#define PCX_TMEM_SETUP      0x00c
#define PCX_TMEM_REFRESH    0x00d
#define PCX_FOGCOL          0x00e
#define PCX_CAMERA          0x00f
#define PCX_PACKMODE        0x010
#define PCX_ARBMODE         0x011
#define PCX_LSTRIDE         0x012
#define PCX_SOFADDR         0x013
#define PCX_XCLIP           0x014
#define PCX_ABORTADDR       0x015
#define PCX_GPPORT          0x016
#define PCX_IEEEFP          0x018 /* PCX2 */
#define PCX_BILINEAR        0x019 /* PCX2 */
#define PCX_PCI21COMP       0x01b /* PCX2 */
#define PCX_CLKSELECT       0x01c /* PCX2 */
#define PCX_FASTFOG         0x01d /* PCX2 */
#define PCX_POWERDOWN       0x01e /* PCX2 */
#define PCX_MEMTEST_DATA    0x07d
#define PCX_MEMTEST_MODE    0x07e
#define PCX_MEMTEST_RES     0x07f
#define PCX_FOG_TABLE       0x080 /* 128 entries */
#define PCX_TLB             0x100 /* 256-dword window, 128 entries implemented */
#define PCX_TLB_ENTRIES     128
#define PCX_TLB_MASK        0x00000fff /* 12-bit page number; bits 31:24 from PAGE_CTRL */
#define PCX_DIVIDER_TABLE   0x200 /* 512 entries; RAM on PCX1, ROM on PCX2 */
#define PCX_REG_COUNT       0x400

#define PCX_INT_EOR         0x00000002 /* INTSTATUS bit 1: end of render */
/* Measured on the m3D (pcx2replay): after a frame INTSTATUS reads 0x7, the
   interrupt line stays asserted until INTSTATUS is read, and that read
   clears all three bits. Bits 0 and 2 are not named in any driver. */
#define PCX_INT_FRAME_DONE  0x00000007

/* Implemented bits of the plain registers, measured on a PCX2 (m3D) by
   writing all ones and reading back. Registers not listed are stored as
   written (not measured, or with side effects that kept them out of the
   test). A zero mask means the register reads back 0 whatever is written. */
static const struct {
    uint8_t  idx;
    uint32_t mask;
} pcx2_write_masks[] = {
    { 0x06, 0x0000001f }, /* FOGAMOUNT */
    { 0x07, 0xfffffffd }, /* OBJECT_OFFSET (bit 1 not stored) */
    { 0x08, 0x000003ff }, /* PAGE_CTRL: [7:0] address bits 31:24, [9:8] page size */
    { 0x09, 0x00000000 },
    { 0x0a, 0x000fffff }, /* ISP_BASE */
    { 0x0b, 0x000fffff }, /* PREC_BASE */
    { 0x0e, 0x01ffffff }, /* FOGCOL */
    { 0x0f, 0x000007ff }, /* CAMERA */
    { 0x10, 0x000000ff }, /* PACKMODE */
    { 0x12, 0x00001ffc }, /* LSTRIDE */
    { 0x13, 0xfffffffc }, /* SOFADDR */
    { 0x14, 0x13ff13ff }, /* XCLIP: enable bits 28/12, right 25:16, left 9:0 */
    { 0x15, 0x00000000 }, /* ABORTADDR (reads 0 when idle) */
    { 0x17, 0x00000000 },
    { 0x18, 0x0000000f }, /* IEEEFP */
    { 0x19, 0x00000007 }, /* BILINEAR */
    { 0x1a, 0x00000000 },
    { 0x1d, 0x000001ff }, /* FASTFOG */
};

enum {
    PCX_CHIP_PCX1 = 1,
    PCX_CHIP_PCX2 = 2
};

/* How the interrupt is acknowledged. Both drivers only ever READ
   INTSTATUS, so read-to-clear is the working assumption until a
   hardware trace settles it. */
enum {
    PCX_ACK_READ_CLEARS   = 0,
    PCX_ACK_WRITE1_CLEARS = 1,
    PCX_ACK_RESET_CLEARS  = 2
};

enum {
    PCX_LOG_OFF     = 0,
    PCX_LOG_SUMMARY = 1, /* start-up, problems, and speed every 100 frames */
    PCX_LOG_EVENTS  = 2, /* init writes, every render and interrupt */
    PCX_LOG_ALL     = 3  /* every register access incl. tables and TLB */
};

typedef struct pcx_dump_hdr_t {
    char     magic[8];      /* "PCXDUMP1" */
    uint32_t version;       /* 1 */
    uint32_t frame;
    uint32_t chip;          /* 1 = PCX1, 2 = PCX2 */
    uint32_t page_bytes;    /* ISP TLB page size from PAGE_CTRL */
    uint32_t tlb_entries;
    uint32_t object_bytes;
    uint32_t texmem_bytes;
    uint32_t pci_command;
    uint32_t reserved[6];
} pcx_dump_hdr_t;

/* Worst case for the runs of one row: every other pixel written. */
#define PCX_ROW_SPANS ((PCXR_MAX_W + 1) / 2)
#define PCX_MAX_SPANS (PCXR_MAX_H * PCX_ROW_SPANS)

/* What the render helpers do when started: draw tiles, or pack rows. */
enum { PCX_PHASE_TILES, PCX_PHASE_PACK };

typedef struct pcx_t {
    int      chip;
    uint16_t device_id;

    uint8_t  pci_slot;
    uint8_t  irq_state;
    uint8_t  pci_regs[256];
    uint32_t bar0;
    uint32_t bar1;

    mem_mapping_t reg_mapping;
    mem_mapping_t tex_mapping;

    uint32_t regs[PCX_REG_COUNT];
    uint8_t *tex_mem;

    uint32_t int_status;
    int      in_reset;
    int      render_busy;
    uint32_t tlb_hiwater;  /* highest TLB entry ever written + 1. The Windows
                              drivers rewrite the TLB every frame; the DOS
                              SGL driver (Tomb Raider) writes it once. */

    pc_timer_t render_timer;

    /* Renderer (PCX2): runs on its own thread between STARTRENDER and the
       end-of-render interrupt; output is written to SOFADDR on the
       emulation thread when the frame completes. */
    thread_t      *render_thread;
    event_t       *render_go;
    event_t       *render_finished;
    /* Helpers draw every n-th tile alongside the render thread. */
#define PCX_MAX_HELPERS 15
    int            n_helpers;
    struct pcx_helper_t {
        struct pcx_t *pcx;
        int           index;
        thread_t     *thread;
        event_t      *go, *done;
    } helper[PCX_MAX_HELPERS];
    pcxr_job_t    *rjob;
    volatile int   render_quit;
    int            render_pending;
    atomic_int     render_ready;   /* set by the render thread when the frame is drawn */
    double         render_late_us; /* emulated time the end of render was held back */
    int            render_discard;
    uint32_t      *isp_buf;
    uint32_t      *rgb;
    uint32_t       job_fog[128];
    pcxr_frame_t   job;
    pcxr_result_t  job_res;
    uint32_t       job_sofaddr, job_stride, job_packmode;
    double         job_start_ms, job_ms; /* host time spent on the frame (render thread) */
    /* Output prepared by the render thread: the frame packed to PACKMODE,
       and the runs of written pixels to bus-master to SOFADDR. */
    uint8_t       *out_buf; /* row y's bytes start at y * PCXR_MAX_W * 4 */
    struct pcx_span_t {
        uint32_t addr, off, len;
    }             *spans;   /* row y's runs start at y * PCX_ROW_SPANS */
    uint16_t      *row_spans; /* number of runs in each row */
    int            pack_x0, pack_x1, pack_y0, pack_y1; /* area to pack; empty if y1 < y0 */
    int            phase;     /* what the helpers do next: draw tiles or pack rows */

    /* Configuration. */
    uint32_t revision_value;
    uint32_t id_value;
    uint8_t  pci_revision;
    uint16_t subsys_vendor;
    uint16_t subsys_id;
    int      render_delay_us;
    int      ack_mode;
    int      log_level;
    int      debug_tile;
    int      dump_limit;
    int      dump_from;
    int      dump_object_kb;

    /* Statistics. */
    double   stat_render_ms, stat_render_max_ms, stat_wait_ms, stat_output_ms, stat_late_ms;
    uint32_t stat_frames;
    uint32_t frames;
    uint32_t dumps;
    uint32_t tex_writes_during_render;
} pcx_t;

static const char *
pcx_reg_name(uint32_t idx)
{
    static const char *names[0x20] = {
        "ID", "REVISION", "SOFTRESET", "INTSTATUS", "INTMASK", "STARTRENDER", "FOGAMOUNT", "OBJECT_OFFSET",
        "PAGE_CTRL", "REG_09", "ISP_BASE", "PREC_BASE", "TMEM_SETUP", "TMEM_REFRESH", "FOGCOL", "CAMERA",
        "PACKMODE", "ARBMODE", "LSTRIDE", "SOFADDR", "XCLIP", "ABORTADDR", "GPPORT", "REG_17",
        "IEEEFP", "BILINEAR", "REG_1A", "PCI21COMP", "CLKSELECT", "FASTFOG", "POWERDOWN", "REG_1F"
    };

    if (idx < 0x20)
        return names[idx];
    if (idx >= PCX_MEMTEST_DATA && idx <= PCX_MEMTEST_RES)
        return "MEMTEST";
    if (idx >= PCX_FOG_TABLE && idx < PCX_TLB)
        return "FOG_TABLE";
    if (idx >= PCX_TLB && idx < PCX_DIVIDER_TABLE)
        return "TLB";
    if (idx >= PCX_DIVIDER_TABLE && idx < PCX_REG_COUNT)
        return "DIVIDER_TABLE";
    return "UNKNOWN";
}

static void
pcx_log(pcx_t *pcx, int level, const char *fmt, ...)
{
    va_list ap;

    if (pcx->log_level < level)
        return;

    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}

/* Host time in milliseconds, for the speed summary. */
static double
pcx_now_ms(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

/* ---------------------------------------------------------------------- */
/* Interrupts                                                              */
/* ---------------------------------------------------------------------- */

static void
pcx_update_irq(pcx_t *pcx)
{
    if (pcx->int_status & pcx->regs[PCX_INTMASK])
        pci_set_irq(pcx->pci_slot, PCI_INTA, &pcx->irq_state);
    else
        pci_clear_irq(pcx->pci_slot, PCI_INTA, &pcx->irq_state);
}

/* ---------------------------------------------------------------------- */
/* Rendering                                                               */
/* ---------------------------------------------------------------------- */

static uint32_t
pcx_isp_page_bytes(const pcx_t *pcx)
{
    /* PAGE_CTRL bits 8-9: 00 = 4 KB, 01 = 8 KB, 11 = 16 KB (VxD
       ProgramRenderRegs). 10 is never written by the drivers. */
    switch ((pcx->regs[PCX_PAGE_CTRL] >> 8) & 3) {
        case 0:
            return 0x1000;
        case 1:
            return 0x2000;
        default:
            return 0x4000;
    }
}

/* Host physical address of ISP page i. The driver writes phys >> 12 to
   each TLB entry and phys[0] >> 24 to PAGE_CTRL[7:0]; the PCX2 keeps only
   12 bits per entry, so all pages share address bits 31:24 (one 16 MB
   window). On PCX1 the full entry is used until it is measured. */
static uint32_t
pcx_tlb_addr(const pcx_t *pcx, uint32_t i)
{
    uint32_t e = pcx->regs[PCX_TLB + i];

    if (pcx->chip == PCX_CHIP_PCX2)
        return ((pcx->regs[PCX_PAGE_CTRL] & 0xff) << 24) | ((e & PCX_TLB_MASK) << 12);
    return e << 12;
}

static int
pcx_bus_master_enabled(const pcx_t *pcx)
{
    return !!(pcx->pci_regs[PCI_REG_COMMAND_L] & PCI_COMMAND_L_BM);
}

/* Debug aid: draws a 32x32 square at the top-left of the target surface,
   cycling colour each frame, to prove SOFADDR / LSTRIDE / PACKMODE land
   in the 2D card's frame buffer. */
static void
pcx_draw_debug_tile(pcx_t *pcx)
{
    static const uint32_t colours[4] = { 0x00ff00ff, 0x0000ffff, 0x00ffff00, 0x00ff8000 };
    uint32_t              rgb        = colours[pcx->frames & 3];
    uint32_t              base       = pcx->regs[PCX_SOFADDR];
    uint32_t              stride     = pcx->regs[PCX_LSTRIDE];
    int                   bpp;
    uint8_t               line[32 * 4];
    uint32_t              pix;

    switch (pcx->regs[PCX_PACKMODE] & 3) {
        case 0: /* 32 bpp */
            bpp = 4;
            pix = rgb;
            break;
        case 1: /* 24 bpp */
            bpp = 3;
            pix = rgb;
            break;
        case 2: /* 16 bpp 565 */
            bpp = 2;
            pix = ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f);
            break;
        default: /* 15 bpp 555 */
            bpp = 2;
            pix = ((rgb >> 9) & 0x7c00) | ((rgb >> 6) & 0x03e0) | ((rgb >> 3) & 0x001f);
            break;
    }

    for (int x = 0; x < 32; x++)
        for (int b = 0; b < bpp; b++)
            line[x * bpp + b] = (pix >> (8 * b)) & 0xff;

    for (int y = 0; y < 32; y++)
        dma_bm_write(base + y * stride, line, 32 * bpp, 1);
}

static void
pcx_dump_frame(pcx_t *pcx)
{
    char           dir[1024];
    char           fn[1024];
    char           name[64];
    pcx_dump_hdr_t hdr;
    FILE          *f;
    uint8_t       *buf;
    uint32_t       page_bytes = pcx_isp_page_bytes(pcx);
    uint32_t       obj_bytes  = 0;

    path_append_filename(dir, usr_path, "powervr_dumps");
    plat_dir_create(dir);
    snprintf(name, sizeof(name), "frame_%06u.bin", pcx->frames);
    path_append_filename(fn, dir, name);

    f = plat_fopen(fn, "wb");
    if (f == NULL) {
        pclog("PowerVR: cannot create dump %s\n", fn);
        return;
    }

    if ((pcx->chip == PCX_CHIP_PCX1) && pcx_bus_master_enabled(pcx))
        obj_bytes = pcx->dump_object_kb * 1024;

    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "PCXDUMP1", 8);
    hdr.version      = 1;
    hdr.frame        = pcx->frames;
    hdr.chip         = pcx->chip;
    hdr.page_bytes   = page_bytes;
    hdr.tlb_entries  = pcx_bus_master_enabled(pcx) ? pcx->tlb_hiwater : 0;
    hdr.object_bytes = obj_bytes;
    hdr.texmem_bytes = PCX_TEXMEM_SIZE;
    hdr.pci_command  = pcx->pci_regs[PCI_REG_COMMAND_L] | (pcx->pci_regs[PCI_REG_COMMAND_H] << 8);
    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(pcx->regs, sizeof(uint32_t), PCX_REG_COUNT, f);

    buf = malloc(page_bytes > obj_bytes ? page_bytes : obj_bytes);
    if (buf != NULL) {
        for (uint32_t i = 0; i < hdr.tlb_entries; i++) {
            dma_bm_read(pcx_tlb_addr(pcx, i), buf, page_bytes, 4);
            fwrite(buf, 1, page_bytes, f);
        }
        if (obj_bytes) {
            dma_bm_read(pcx->regs[PCX_OBJECT_OFFSET], buf, obj_bytes, 4);
            fwrite(buf, 1, obj_bytes, f);
        }
        free(buf);
    }
    fwrite(pcx->tex_mem, 1, PCX_TEXMEM_SIZE, f);
    fclose(f);

    pcx->dumps++;
    pcx_log(pcx, PCX_LOG_SUMMARY, "PowerVR: dumped frame %u to %s (%u TLB pages of %u bytes)\n",
            pcx->frames, fn, hdr.tlb_entries, page_bytes);
}

static void pcx_pack_rows(pcx_t *pcx, int first, int step);

static void
pcx_render_helper(void *priv)
{
    struct pcx_helper_t *h   = (struct pcx_helper_t *) priv;
    pcx_t               *pcx = h->pcx;

    for (;;) {
        thread_wait_event(h->go, -1);
        thread_reset_event(h->go);
        if (pcx->render_quit)
            break;
        if (pcx->phase == PCX_PHASE_TILES)
            pcxr_render_tiles(pcx->rjob, pcx->rgb, h->index, pcx->n_helpers + 1);
        else
            pcx_pack_rows(pcx, h->index, pcx->n_helpers + 1);
        thread_set_event(h->done);
    }
}


/* The area of the frame to pack, after the tiles are drawn: the rows and
   columns the tiles cover, inside XCLIP. */
static void
pcx_pack_area(pcx_t *pcx)
{
    const pcxr_result_t *r  = &pcx->job_res;
    uint32_t             xc = pcx->job.xclip;
    int                  x0, x1;

    pcx->pack_y0 = 0;
    pcx->pack_y1 = -1;
    if (r->error || r->max_x < 0 || r->max_y < 0)
        return;

    x0 = r->min_x;
    x1 = r->max_x;
    if ((xc & (1u << 12)) && x0 < (int) (xc & 0x3ff) - 1)
        x0 = (int) (xc & 0x3ff) - 1;
    if ((xc & (1u << 28)) && x1 > (int) ((xc >> 16) & 0x3ff) - 1)
        x1 = (int) ((xc >> 16) & 0x3ff) - 1;
    if (x1 >= PCXR_MAX_W)
        x1 = PCXR_MAX_W - 1;
    if (x0 > x1)
        return;
    pcx->pack_x0 = x0;
    pcx->pack_x1 = x1;
    pcx->pack_y0 = r->min_y;
    pcx->pack_y1 = (r->max_y < PCXR_MAX_H) ? r->max_y : PCXR_MAX_H - 1;
}

/* Render threads, after the tiles: pack the pixels the chip wrote into the
   PACKMODE format and list the runs that go out to the frame buffer, so
   the emulation thread only has to issue the bus-master writes. Rows
   pack_y0 + first, + step, ... ; each row has its own place in out_buf
   and spans, so the threads share the rows between them. */
static void
pcx_pack_rows(pcx_t *pcx, int first, int step)
{
    int bpp = ((pcx->job_packmode & 3) == 0) ? 4 : ((pcx->job_packmode & 3) == 1) ? 3 : 2;

    for (int y = pcx->pack_y0 + first; y <= pcx->pack_y1; y += step) {
        const uint32_t    *src  = &pcx->rgb[y * PCXR_MAX_W];
        uint8_t           *out  = &pcx->out_buf[(uint32_t) y * PCXR_MAX_W * 4];
        struct pcx_span_t *sp   = &pcx->spans[(uint32_t) y * PCX_ROW_SPANS];
        uint32_t           off  = 0;
        int                n    = 0;
        int                x    = pcx->pack_x0;
        const int          x1   = pcx->pack_x1;

        /* Only pixels the chip wrote go out; the masking plane leaves the
           frame buffer as it was. Each run of written pixels is one
           bus-master write. */
        while (x <= x1) {
            int start;
            while (x <= x1 && !(src[x] & PCXR_WRITTEN))
                x++;
            if (x > x1)
                break;
            start = x;
            while (x <= x1 && (src[x] & PCXR_WRITTEN))
                x++;
            sp[n].addr = pcx->job_sofaddr + y * pcx->job_stride + start * bpp;
            sp[n].off  = (uint32_t) y * PCXR_MAX_W * 4 + off;
            sp[n].len  = pcxr_pack_run(pcx->job_packmode, src, start, x - start, y, out + off);
            off += sp[n].len;
            n++;
        }
        pcx->row_spans[y] = (uint16_t) n;
    }
}

/* Start the helpers on the current phase, do share 0 here, and wait for
   them. */
static void
pcx_run_phase(pcx_t *pcx, int phase)
{
    pcx->phase = phase;
    for (int i = 0; i < pcx->n_helpers; i++)
        thread_set_event(pcx->helper[i].go);
    if (phase == PCX_PHASE_TILES)
        pcxr_render_tiles(pcx->rjob, pcx->rgb, 0, pcx->n_helpers + 1);
    else
        pcx_pack_rows(pcx, 0, pcx->n_helpers + 1);
    for (int i = 0; i < pcx->n_helpers; i++) {
        thread_wait_event(pcx->helper[i].done, -1);
        thread_reset_event(pcx->helper[i].done);
    }
}

static void
pcx_render_thread(void *priv)
{
    pcx_t *pcx = (pcx_t *) priv;

    for (;;) {
        thread_wait_event(pcx->render_go, -1);
        thread_reset_event(pcx->render_go);
        if (pcx->render_quit) {
            for (int i = 0; i < pcx->n_helpers; i++)
                thread_set_event(pcx->helper[i].go);
            break;
        }
        pcx->job_start_ms = pcx_now_ms();
        memset(pcx->rgb, 0, PCXR_MAX_W * PCXR_MAX_H * sizeof(uint32_t));
        if (pcxr_prepare(pcx->rjob, &pcx->job, &pcx->job_res) > 0)
            pcx_run_phase(pcx, PCX_PHASE_TILES);
        pcx_pack_area(pcx);
        if (pcx->pack_y1 >= pcx->pack_y0)
            pcx_run_phase(pcx, PCX_PHASE_PACK);
        pcx->job_ms = pcx_now_ms() - pcx->job_start_ms;
        atomic_store(&pcx->render_ready, 1);
        thread_set_event(pcx->render_finished);
    }
}

/* Wait for a frame the renderer is still working on. */
static void
pcx_render_wait(pcx_t *pcx)
{
    if (!pcx->render_pending)
        return;
    thread_wait_event(pcx->render_finished, -1);
    thread_reset_event(pcx->render_finished);
    pcx->render_pending = 0;
}

/* STARTRENDER: gather the frame's inputs and hand them to the renderer. */
static void
pcx_render_frame(pcx_t *pcx)
{
    uint32_t page_bytes = pcx_isp_page_bytes(pcx);

    if ((pcx->chip != PCX_CHIP_PCX2) || !pcx_bus_master_enabled(pcx) || !pcx->render_thread)
        return; /* PCX1 parameter formats are not modelled yet */

    pcx_render_wait(pcx);

    /* The ISP parameter store, through the TLB, in virtual order. */
    for (uint32_t i = 0; i < PCX_TLB_ENTRIES; i++) {
        uint32_t pa  = pcx_tlb_addr(pcx, i);
        uint8_t *dst = (uint8_t *) pcx->isp_buf + i * page_bytes;
        /* Pages in system RAM are copied directly (as the Rage 128's PM4
           engine does); anything else goes through the bus. */
        if ((uint64_t) pa + page_bytes <= ((uint64_t) mem_size << 10))
            memcpy(dst, &ram[pa], page_bytes);
        else
            dma_bm_read(pa, dst, page_bytes, 4);
    }

    memcpy(pcx->job_fog, &pcx->regs[PCX_FOG_TABLE], sizeof(pcx->job_fog));
    memset(&pcx->job, 0, sizeof(pcx->job));
    pcx->job.isp           = pcx->isp_buf;
    pcx->job.isp_words     = PCX_TLB_ENTRIES * page_bytes / 4;
    pcx->job.tex           = (const uint32_t *) pcx->tex_mem;
    pcx->job.tex_words     = PCX_TEXMEM_SIZE / 4;
    pcx->job.object_offset = pcx->regs[PCX_OBJECT_OFFSET];
    pcx->job.fog_shift     = pcx->regs[PCX_FOGAMOUNT];
    pcx->job.fog_colour    = pcx->regs[PCX_FOGCOL];
    pcx->job.camera        = pcx->regs[PCX_CAMERA];
    pcx->job.xclip         = pcx->regs[PCX_XCLIP];
    pcx->job.bilinear      = pcx->regs[PCX_BILINEAR];
    pcx->job.fog_table     = pcx->job_fog;
    pcx->job_sofaddr       = pcx->regs[PCX_SOFADDR];
    pcx->job_stride        = pcx->regs[PCX_LSTRIDE];
    pcx->job_packmode      = pcx->regs[PCX_PACKMODE];

    pcx->render_pending = 1;
    pcx->render_discard = 0;
    pcx->render_late_us = 0.0;
    atomic_store(&pcx->render_ready, 0);
    thread_set_event(pcx->render_go);
}

/* End of render: write the finished frame into the frame buffer.

   The runs were packed on the render thread; only the bus-master writes
   happen here. They go through the 2D card's linear frame buffer handlers,
   which charge the emulated CPU for each access (video timings). On the
   real board these writes are the PCX2 mastering the PCI bus while the CPU
   keeps running, so the CPU's cycle count is put back afterwards rather
   than billing it for transfers it never made (as vid_chips_69000.c does
   for its blitter's own frame buffer accesses). */
static void
pcx_render_output(pcx_t *pcx)
{
    int orig_cycles = cycles;

    if (pcx->job_res.error)
        pcx_log(pcx, PCX_LOG_SUMMARY, "PowerVR: frame %u: malformed tile list (error %d)\n",
                pcx->frames, pcx->job_res.error);

    for (int y = pcx->pack_y0; y <= pcx->pack_y1; y++) {
        const struct pcx_span_t *s = &pcx->spans[(uint32_t) y * PCX_ROW_SPANS];
        for (int i = 0; i < pcx->row_spans[y]; i++, s++)
            dma_bm_write(s->addr, &pcx->out_buf[s->off], s->len, 4);
    }

    cycles = orig_cycles;
}

/* How often the end of render looks again for a frame the host has not
   finished, in emulated microseconds. */
#define PCX_RENDER_POLL_US 250.0

static void
pcx_render_done(void *priv)
{
    pcx_t *pcx = (pcx_t *) priv;

    /* If the host has not finished drawing the frame yet, the card is
       simply still busy: hold the end of render back in emulated time
       rather than stalling the emulated PC until the host catches up.
       Stalling froze the guest's clocks, and the catch-up afterwards
       could drop timer interrupts, so games that count them ran in slow
       motion while reporting a normal frame rate. */
    if (pcx->render_pending && !atomic_load(&pcx->render_ready)) {
        pcx->render_late_us += PCX_RENDER_POLL_US;
        timer_on_auto(&pcx->render_timer, PCX_RENDER_POLL_US);
        return;
    }

    if (pcx->render_pending) {
        double t0 = pcx_now_ms(), t1;
        pcx_render_wait(pcx);
        t1 = pcx_now_ms();
        if (!pcx->render_discard)
            pcx_render_output(pcx);
        /* Speed summary: how long the renderer took, how long emulation
           stalled waiting for it (0 when the host keeps up), and how long
           the frame buffer writes held up the emulation thread. */
        pcx->stat_wait_ms += t1 - t0;
        pcx->stat_output_ms += pcx_now_ms() - t1;
        pcx->stat_late_ms += pcx->render_late_us / 1000.0;
        pcx->stat_render_ms += pcx->job_ms;
        if (pcx->job_ms > pcx->stat_render_max_ms)
            pcx->stat_render_max_ms = pcx->job_ms;
        if (++pcx->stat_frames == 100) {
            pcx_log(pcx, PCX_LOG_SUMMARY,
                    "PowerVR: frames %u-%u: render %.1f ms average (%.1f max) on %d thread(s), "
                    "emulation waited %.1f ms and wrote output %.2f ms per frame; "
                    "emulated render time %.1f ms + %.1f ms held back for the host\n",
                    pcx->frames - 99, pcx->frames, pcx->stat_render_ms / 100.0, pcx->stat_render_max_ms,
                    pcx->n_helpers + 1, pcx->stat_wait_ms / 100.0, pcx->stat_output_ms / 100.0,
                    pcx->render_delay_us / 1000.0, pcx->stat_late_ms / 100.0);
            pcx->stat_frames = 0;
            pcx->stat_render_ms = pcx->stat_render_max_ms = pcx->stat_wait_ms = pcx->stat_output_ms = pcx->stat_late_ms = 0.0;
        }
    }
    if (pcx->debug_tile && pcx_bus_master_enabled(pcx)) {
        int orig_cycles = cycles;
        pcx_draw_debug_tile(pcx);
        cycles = orig_cycles;
    }

    pcx->render_busy = 0;
    pcx->int_status |= PCX_INT_FRAME_DONE;
    pcx_update_irq(pcx);

    pcx_log(pcx, PCX_LOG_EVENTS, "PowerVR: frame %u end of render, IRQ %s\n",
            pcx->frames, pcx->irq_state ? "raised" : "masked");
}

static void
pcx_start_render(pcx_t *pcx)
{
    pcx->frames++;

    if (pcx->render_busy)
        pcx_log(pcx, PCX_LOG_EVENTS, "PowerVR: STARTRENDER while frame %u still busy\n", pcx->frames - 1);
    if (pcx->in_reset)
        pcx_log(pcx, PCX_LOG_EVENTS, "PowerVR: STARTRENDER while SOFTRESET is held\n");
    if (!pcx_bus_master_enabled(pcx))
        pcx_log(pcx, PCX_LOG_EVENTS, "PowerVR: STARTRENDER with bus mastering disabled\n");

    pcx_log(pcx, PCX_LOG_EVENTS,
            "PowerVR: frame %u start: SOFADDR=%08x LSTRIDE=%u PACKMODE=%02x XCLIP=%08x "
            "OBJECT_OFFSET=%08x PAGE_CTRL=%08x TLB=%u FOG=%u FOGCOL=%08x CAMERA=%x BILINEAR=%x\n",
            pcx->frames, pcx->regs[PCX_SOFADDR], pcx->regs[PCX_LSTRIDE], pcx->regs[PCX_PACKMODE],
            pcx->regs[PCX_XCLIP], pcx->regs[PCX_OBJECT_OFFSET], pcx->regs[PCX_PAGE_CTRL],
            pcx->tlb_hiwater, pcx->regs[PCX_FOGAMOUNT], pcx->regs[PCX_FOGCOL], pcx->regs[PCX_CAMERA],
            pcx->regs[PCX_BILINEAR]);

    if ((pcx->frames >= (uint32_t) pcx->dump_from) && (pcx->dumps < (uint32_t) pcx->dump_limit))
        pcx_dump_frame(pcx);

    pcx_render_frame(pcx);

    pcx->render_busy = 1;
    timer_on_auto(&pcx->render_timer, (double) pcx->render_delay_us);
}

static void
pcx_soft_reset(pcx_t *pcx, int assert)
{
    if (assert) {
        pcx->in_reset = 1;
        if (pcx->render_busy) {
            /* The VxD's timeout handler resets a hung render this way. */
            timer_disable(&pcx->render_timer);
            pcx_render_wait(pcx);
            pcx->render_discard = 1;
            pcx->render_busy    = 0;
            pcx_log(pcx, PCX_LOG_EVENTS, "PowerVR: SOFTRESET aborted frame %u\n", pcx->frames);
        }
        if (pcx->ack_mode == PCX_ACK_RESET_CLEARS) {
            pcx->int_status = 0;
            pcx_update_irq(pcx);
        }
    } else
        pcx->in_reset = 0;
}

/* ---------------------------------------------------------------------- */
/* BAR0: register file                                                     */
/* ---------------------------------------------------------------------- */

static uint32_t
pcx_reg_read(pcx_t *pcx, uint32_t idx)
{
    uint32_t val;

    switch (idx) {
        case PCX_ID:
            val = pcx->id_value;
            break;
        case PCX_REVISION:
            val = pcx->revision_value;
            break;
        case PCX_GPPORT:
            /* Measured: reads 0xf616 even straight after the driver writes
               0, so the read side is input pins, not the written latch. */
            val = PCX_RESET_GPPORT;
            break;
        case PCX_INTSTATUS:
            val = pcx->int_status;
            if (pcx->ack_mode == PCX_ACK_READ_CLEARS && val) {
                pcx->int_status = 0;
                pcx_update_irq(pcx);
            }
            break;
        default:
            val = pcx->regs[idx];
            break;
    }

    if ((pcx->log_level >= PCX_LOG_ALL) || ((pcx->log_level >= PCX_LOG_EVENTS) && (idx == PCX_REVISION || idx == PCX_ID)))
        pclog("PowerVR: read  %-13s [%03x] = %08x\n", pcx_reg_name(idx), idx, val);

    return val;
}

static void
pcx_reg_write(pcx_t *pcx, uint32_t idx, uint32_t val)
{
    int table = (idx >= PCX_FOG_TABLE);

    /* At the events level, individual writes are logged only until the
       first frame (the init sequence); after that each frame is
       summarised by pcx_start_render(). */
    if ((pcx->log_level >= PCX_LOG_ALL) || ((pcx->log_level >= PCX_LOG_EVENTS) && !table && (pcx->frames == 0)))
        pclog("PowerVR: write %-13s [%03x] = %08x\n", pcx_reg_name(idx), idx, val);

    if ((pcx->chip == PCX_CHIP_PCX1) && (idx >= PCX_IEEEFP) && (idx < 0x20))
        pcx_log(pcx, PCX_LOG_SUMMARY, "PowerVR: PCX2-only register %s written on PCX1\n", pcx_reg_name(idx));

    if (pcx->chip == PCX_CHIP_PCX2) {
        if ((idx >= PCX_FOG_TABLE) && (idx < PCX_TLB))
            return; /* ROM */
        if (idx >= PCX_DIVIDER_TABLE)
            return; /* ROM */
        if (idx >= PCX_TLB + PCX_TLB_ENTRIES && idx < PCX_DIVIDER_TABLE)
            return; /* not implemented, reads 0 */
        if ((idx >= PCX_TLB) && (idx < PCX_TLB + PCX_TLB_ENTRIES))
            val &= PCX_TLB_MASK;
        for (unsigned i = 0; i < sizeof(pcx2_write_masks) / sizeof(pcx2_write_masks[0]); i++)
            if (pcx2_write_masks[i].idx == idx)
                val &= pcx2_write_masks[i].mask;
    }

    switch (idx) {
        case PCX_ID:
        case PCX_REVISION:
            break; /* read-only */

        case PCX_SOFTRESET:
            pcx->regs[idx] = val;
            pcx_soft_reset(pcx, val & 1);
            break;

        case PCX_INTSTATUS:
            if (pcx->ack_mode == PCX_ACK_WRITE1_CLEARS) {
                pcx->int_status &= ~val;
                pcx_update_irq(pcx);
            }
            break;

        case PCX_INTMASK:
            pcx->regs[idx] = val;
            pcx_update_irq(pcx);
            break;

        case PCX_STARTRENDER:
            pcx->regs[idx] = val;
            pcx_start_render(pcx);
            break;

        default:
            pcx->regs[idx] = val;
            if ((idx >= PCX_TLB) && (idx < PCX_DIVIDER_TABLE) && (idx - PCX_TLB + 1 > pcx->tlb_hiwater))
                pcx->tlb_hiwater = idx - PCX_TLB + 1;
            break;
    }
}

static uint32_t
pcx_bar0_readl(uint32_t addr, void *priv)
{
    pcx_t   *pcx = (pcx_t *) priv;
    uint32_t off = (addr - pcx->bar0) & (PCX_BAR0_SIZE - 1);

    if (off >= PCX_REG_COUNT * 4) {
        pcx_log(pcx, PCX_LOG_ALL, "PowerVR: read beyond register file at BAR0+%05x\n", off);
        return 0xffffffff;
    }
    return pcx_reg_read(pcx, off >> 2);
}

static uint16_t
pcx_bar0_readw(uint32_t addr, void *priv)
{
    return pcx_bar0_readl(addr & ~3, priv) >> ((addr & 2) * 8);
}

static uint8_t
pcx_bar0_readb(uint32_t addr, void *priv)
{
    return pcx_bar0_readl(addr & ~3, priv) >> ((addr & 3) * 8);
}

static void
pcx_bar0_writel(uint32_t addr, uint32_t val, void *priv)
{
    pcx_t   *pcx = (pcx_t *) priv;
    uint32_t off = (addr - pcx->bar0) & (PCX_BAR0_SIZE - 1);

    if (off >= PCX_REG_COUNT * 4) {
        pcx_log(pcx, PCX_LOG_SUMMARY, "PowerVR: write beyond register file at BAR0+%05x = %08x\n", off, val);
        return;
    }
    pcx_reg_write(pcx, off >> 2, val);
}

/* Partial writes are merged into the dword; the drivers only ever use
   32-bit accesses. */
static void
pcx_bar0_writew(uint32_t addr, uint16_t val, void *priv)
{
    pcx_t   *pcx   = (pcx_t *) priv;
    uint32_t off   = (addr - pcx->bar0) & (PCX_BAR0_SIZE - 1);
    uint32_t shift = (addr & 2) * 8;
    uint32_t idx   = off >> 2;

    if (off >= PCX_REG_COUNT * 4)
        return;
    pcx_reg_write(pcx, idx, (pcx->regs[idx] & ~(0xffffU << shift)) | ((uint32_t) val << shift));
}

static void
pcx_bar0_writeb(uint32_t addr, uint8_t val, void *priv)
{
    pcx_t   *pcx   = (pcx_t *) priv;
    uint32_t off   = (addr - pcx->bar0) & (PCX_BAR0_SIZE - 1);
    uint32_t shift = (addr & 3) * 8;
    uint32_t idx   = off >> 2;

    if (off >= PCX_REG_COUNT * 4)
        return;
    pcx_reg_write(pcx, idx, (pcx->regs[idx] & ~(0xffU << shift)) | ((uint32_t) val << shift));
}

/* ---------------------------------------------------------------------- */
/* BAR1: texture memory                                                    */
/* ---------------------------------------------------------------------- */

static void
pcx_tex_write_check(pcx_t *pcx)
{
    /* The SDK says texture memory must not be written during a render. */
    if (pcx->render_busy && (pcx->tex_writes_during_render++ == 0))
        pcx_log(pcx, PCX_LOG_SUMMARY, "PowerVR: texture memory written during render (frame %u)\n", pcx->frames);
}

static uint8_t
pcx_bar1_readb(uint32_t addr, void *priv)
{
    const pcx_t *pcx = (pcx_t *) priv;

    return pcx->tex_mem[(addr - pcx->bar1) & (PCX_TEXMEM_SIZE - 1)];
}

static uint16_t
pcx_bar1_readw(uint32_t addr, void *priv)
{
    const pcx_t *pcx = (pcx_t *) priv;

    return *(uint16_t *) &pcx->tex_mem[(addr - pcx->bar1) & (PCX_TEXMEM_SIZE - 2)];
}

static uint32_t
pcx_bar1_readl(uint32_t addr, void *priv)
{
    const pcx_t *pcx = (pcx_t *) priv;

    return *(uint32_t *) &pcx->tex_mem[(addr - pcx->bar1) & (PCX_TEXMEM_SIZE - 4)];
}

static void
pcx_bar1_writeb(uint32_t addr, uint8_t val, void *priv)
{
    pcx_t *pcx = (pcx_t *) priv;

    pcx_tex_write_check(pcx);
    pcx->tex_mem[(addr - pcx->bar1) & (PCX_TEXMEM_SIZE - 1)] = val;
}

static void
pcx_bar1_writew(uint32_t addr, uint16_t val, void *priv)
{
    pcx_t *pcx = (pcx_t *) priv;

    pcx_tex_write_check(pcx);
    *(uint16_t *) &pcx->tex_mem[(addr - pcx->bar1) & (PCX_TEXMEM_SIZE - 2)] = val;
}

static void
pcx_bar1_writel(uint32_t addr, uint32_t val, void *priv)
{
    pcx_t *pcx = (pcx_t *) priv;

    pcx_tex_write_check(pcx);
    *(uint32_t *) &pcx->tex_mem[(addr - pcx->bar1) & (PCX_TEXMEM_SIZE - 4)] = val;
}

/* ---------------------------------------------------------------------- */
/* PCI configuration space                                                 */
/* ---------------------------------------------------------------------- */

static void
pcx_recalc_mapping(pcx_t *pcx)
{
    int mem_on = !!(pcx->pci_regs[PCI_REG_COMMAND_L] & PCI_COMMAND_L_MEM);

    mem_mapping_disable(&pcx->reg_mapping);
    mem_mapping_disable(&pcx->tex_mapping);

    if (mem_on && pcx->bar0)
        mem_mapping_set_addr(&pcx->reg_mapping, pcx->bar0, PCX_BAR0_SIZE);
    if (mem_on && pcx->bar1)
        mem_mapping_set_addr(&pcx->tex_mapping, pcx->bar1, PCX_BAR1_SIZE);

    pcx_log(pcx, PCX_LOG_EVENTS, "PowerVR: memory %s, BAR0=%08x BAR1=%08x\n",
            mem_on ? "on" : "off", pcx->bar0, pcx->bar1);
}

static uint8_t
pcx_pci_read(int func, int addr, UNUSED(int len), void *priv)
{
    const pcx_t *pcx = (pcx_t *) priv;

    if (func)
        return 0xff;

    switch (addr) {
        case 0x10 ... 0x13:
            return ((pcx->bar0 | PCX_BAR_FLAGS) >> ((addr & 3) * 8)) & 0xff;
        case 0x14 ... 0x17:
            return ((pcx->bar1 | PCX_BAR_FLAGS) >> ((addr & 3) * 8)) & 0xff;
        default:
            return pcx->pci_regs[addr & 0xff];
    }
}

static void
pcx_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    pcx_t   *pcx = (pcx_t *) priv;
    uint32_t shift;

    if (func)
        return;

    switch (addr) {
        case PCI_REG_COMMAND_L:
            /* Memory space, bus master, parity response. */
            pcx->pci_regs[addr] = val & (PCI_COMMAND_L_MEM | PCI_COMMAND_L_BM | PCI_COMMAND_L_PARITY);
            pcx_recalc_mapping(pcx);
            break;
        case PCI_REG_COMMAND_H:
            pcx->pci_regs[addr] = val & PCI_COMMAND_H_SERR;
            break;
        case 0x07:
            /* Status: write 1 to clear the error bits. */
            pcx->pci_regs[addr] &= ~(val & 0xf9);
            break;
        case 0x0c: /* cache line size (the driver sets it on PCX2) */
        case 0x0d: /* latency timer (the driver sets 0x40 if 0) */
        case 0x3c: /* interrupt line */
            pcx->pci_regs[addr] = val;
            break;
        case 0x10 ... 0x13:
            shift     = (addr & 3) * 8;
            pcx->bar0 = (pcx->bar0 & ~(0xffU << shift)) | ((uint32_t) val << shift);
            pcx->bar0 &= ~(PCX_BAR0_SIZE - 1);
            pcx_recalc_mapping(pcx);
            break;
        case 0x14 ... 0x17:
            shift     = (addr & 3) * 8;
            pcx->bar1 = (pcx->bar1 & ~(0xffU << shift)) | ((uint32_t) val << shift);
            pcx->bar1 &= ~(PCX_BAR1_SIZE - 1);
            pcx_recalc_mapping(pcx);
            break;
        default:
            break;
    }
}

static void
pcx_pci_init_regs(pcx_t *pcx)
{
    uint16_t subvendor = pcx->subsys_vendor;
    uint16_t subsys    = pcx->subsys_id;

    memset(pcx->pci_regs, 0, sizeof(pcx->pci_regs));
    pcx->pci_regs[0x00] = PCX_VENDOR_NEC & 0xff;
    pcx->pci_regs[0x01] = PCX_VENDOR_NEC >> 8;
    pcx->pci_regs[0x02] = pcx->device_id & 0xff;
    pcx->pci_regs[0x03] = pcx->device_id >> 8;
    pcx->pci_regs[0x06] = 0xa0; /* status 02a0: 66 MHz capable, fast back-to-back */
    pcx->pci_regs[0x07] = 0x02; /* medium DEVSEL timing */
    pcx->pci_regs[0x08] = pcx->pci_revision;
    pcx->pci_regs[0x09] = PCX_PCI_PROGIF;
    pcx->pci_regs[0x0a] = PCX_PCI_SUBCLASS;
    pcx->pci_regs[0x0b] = PCX_PCI_CLASS;
    pcx->pci_regs[0x2c] = subvendor & 0xff;
    pcx->pci_regs[0x2d] = subvendor >> 8;
    pcx->pci_regs[0x2e] = subsys & 0xff;
    pcx->pci_regs[0x2f] = subsys >> 8;
    pcx->pci_regs[0x3d] = PCI_INTA;
    pcx->pci_regs[0x3e] = 0x00; /* MIN_GNT */
    pcx->pci_regs[0x3f] = 0x00; /* MAX_LAT */
    pcx->bar0           = 0;
    pcx->bar1           = 0;
}

/* ---------------------------------------------------------------------- */
/* Device                                                                  */
/* ---------------------------------------------------------------------- */

static void
pcx_reg_defaults(pcx_t *pcx)
{
    memset(pcx->regs, 0, sizeof(pcx->regs));
    pcx->regs[PCX_ID]           = pcx->id_value;       /* mirrored for dumps */
    pcx->regs[PCX_REVISION]     = pcx->revision_value;
    pcx->regs[PCX_TMEM_SETUP]   = PCX_RESET_TMEM_SETUP;
    pcx->regs[PCX_TMEM_REFRESH] = PCX_RESET_TMEM_REFRESH;
    pcx->regs[PCX_GPPORT]       = PCX_RESET_GPPORT;
    if (pcx->chip == PCX_CHIP_PCX2) {
        /* Built-in tables (read-only on the measured card). The TLB powers
           up with random 12-bit values; zero is used here. */
        memcpy(&pcx->regs[PCX_FOG_TABLE], pcx2_fog_rom, sizeof(pcx2_fog_rom));
        memcpy(&pcx->regs[PCX_DIVIDER_TABLE], pcx2_divider_rom, sizeof(pcx2_divider_rom));
    }
}

static void
pcx_reset(void *priv)
{
    pcx_t *pcx = (pcx_t *) priv;

    timer_disable(&pcx->render_timer);
    pcx_render_wait(pcx);
    pcx_reg_defaults(pcx);
    pcx->int_status  = 0;
    pcx->in_reset    = 0;
    pcx->render_busy = 0;
    pcx->tlb_hiwater = 0;
    pcx_update_irq(pcx);
    pcx_pci_init_regs(pcx);
    pcx_recalc_mapping(pcx);
}

static void *
pcx_init(const device_t *info)
{
    pcx_t *pcx = calloc(1, sizeof(pcx_t));

#ifdef USE_POWERVR_PCX_DEV
    /* Developer settings (cmake -D POWERVR_PCX_DEV=ON). */
    pcx->chip           = device_get_config_int("chip");
    pcx->revision_value = (pcx->chip == PCX_CHIP_PCX2) ? device_get_config_int("revision") : 0;
    pcx->render_delay_us = device_get_config_int("render_delay");
    pcx->ack_mode       = device_get_config_int("int_ack");
    pcx->debug_tile     = device_get_config_int("debug_tile");
    pcx->pci_revision   = device_get_config_int("pci_revision");
    pcx->subsys_vendor  = device_get_config_hex16("subsys_vendor");
    pcx->subsys_id      = device_get_config_hex16("subsys_id");
#else
    /* The Matrox m3D as measured. */
    pcx->chip           = PCX_CHIP_PCX2;
    pcx->revision_value = 1;
    pcx->render_delay_us = 7600;
    pcx->ack_mode       = PCX_ACK_READ_CLEARS;
    pcx->debug_tile     = 0;
    pcx->pci_revision   = 1;
    pcx->subsys_vendor  = 0;
    pcx->subsys_id      = 0;
#endif
    pcx->device_id      = (pcx->chip == PCX_CHIP_PCX1) ? PCX1_DEVICE_ID : PCX2_DEVICE_ID;
    pcx->id_value       = ((uint32_t) PCX_VENDOR_NEC << 16) | pcx->device_id; /* m3D reads 0x10330046 */
    pcx->log_level      = device_get_config_int("log_level");
    pcx->dump_limit     = device_get_config_int("dump_frames");
    pcx->dump_from      = device_get_config_int("dump_from");
    pcx->dump_object_kb = 256;

    pcx->tex_mem = calloc(1, PCX_TEXMEM_SIZE);
    if (pcx->chip == PCX_CHIP_PCX2) {
        pcx->isp_buf         = calloc(PCX_TLB_ENTRIES, 0x4000);
        pcx->rgb             = calloc(PCXR_MAX_W * PCXR_MAX_H, sizeof(uint32_t));
        pcx->out_buf         = malloc(PCXR_MAX_W * PCXR_MAX_H * 4);
        pcx->spans           = malloc(PCX_MAX_SPANS * sizeof(struct pcx_span_t));
        pcx->row_spans       = calloc(PCXR_MAX_H, sizeof(uint16_t));
        pcx->render_go       = thread_create_event();
        pcx->render_finished = thread_create_event();
        pcx->rjob            = pcxr_job_create();
        pcx->n_helpers       = device_get_config_int("render_threads") - 1;
        if (pcx->n_helpers < 0)
            pcx->n_helpers = 0;
        if (pcx->n_helpers > PCX_MAX_HELPERS)
            pcx->n_helpers = PCX_MAX_HELPERS;
        for (int i = 0; i < pcx->n_helpers; i++) {
            pcx->helper[i].pcx    = pcx;
            pcx->helper[i].index  = i + 1;
            pcx->helper[i].go     = thread_create_event();
            pcx->helper[i].done   = thread_create_event();
            pcx->helper[i].thread = thread_create(pcx_render_helper, &pcx->helper[i]);
        }
        pcx->render_thread   = thread_create(pcx_render_thread, pcx);
    }
    pcx_reg_defaults(pcx);

    pcx_pci_init_regs(pcx);
    pci_add_card(PCI_ADD_NORMAL, pcx_pci_read, pcx_pci_write, pcx, &pcx->pci_slot);

    mem_mapping_add(&pcx->reg_mapping, 0, 0,
                    pcx_bar0_readb, pcx_bar0_readw, pcx_bar0_readl,
                    pcx_bar0_writeb, pcx_bar0_writew, pcx_bar0_writel,
                    NULL, MEM_MAPPING_EXTERNAL, pcx);
    mem_mapping_add(&pcx->tex_mapping, 0, 0,
                    pcx_bar1_readb, pcx_bar1_readw, pcx_bar1_readl,
                    pcx_bar1_writeb, pcx_bar1_writew, pcx_bar1_writel,
                    NULL, MEM_MAPPING_EXTERNAL, pcx);
    mem_mapping_disable(&pcx->reg_mapping);
    mem_mapping_disable(&pcx->tex_mapping);

    timer_add(&pcx->render_timer, pcx_render_done, pcx, 0);

    pclog("PowerVR: %s (1033:%04x) added, REVISION=%u, interrupt ack mode %d\n",
          info->name, pcx->device_id, pcx->revision_value, pcx->ack_mode);

    return pcx;
}

static void
pcx_close(void *priv)
{
    pcx_t *pcx = (pcx_t *) priv;

    pclog("PowerVR: %u frames rendered, %u dumped\n", pcx->frames, pcx->dumps);
    if (pcx->render_thread) {
        pcx_render_wait(pcx);
        pcx->render_quit = 1;
        thread_set_event(pcx->render_go);
        thread_wait(pcx->render_thread);
        for (int i = 0; i < pcx->n_helpers; i++) {
            thread_wait(pcx->helper[i].thread);
            thread_destroy_event(pcx->helper[i].go);
            thread_destroy_event(pcx->helper[i].done);
        }
        thread_destroy_event(pcx->render_go);
        thread_destroy_event(pcx->render_finished);
        pcxr_job_destroy(pcx->rjob);
    }
    free(pcx->isp_buf);
    free(pcx->rgb);
    free(pcx->out_buf);
    free(pcx->spans);
    free(pcx->row_spans);
    free(pcx->tex_mem);
    free(pcx);
}

static const device_config_t pcx_config[] = {
  // clang-format off
#ifdef USE_POWERVR_PCX_DEV
    {
        .name           = "chip",
        .description    = "Chip",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = PCX_CHIP_PCX2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "PCX2 (Matrox m3D, Apocalypse 3Dx/5D)", .value = PCX_CHIP_PCX2 },
            { .description = "PCX1 (Apocalypse 3D)",                 .value = PCX_CHIP_PCX1 },
            { .description = ""                                                             }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "revision",
        .description    = "PCX2 REVISION register",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "1 (PCX2_002, Matrox m3D)", .value = 1 },
            { .description = "3 (PCX2_003)",             .value = 3 },
            { .description = ""                         }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "pci_revision",
        .description    = "PCI revision ID",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { .min = 0, .max = 255, .step = 1 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "subsys_vendor",
        .description    = "Subsystem vendor ID",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "subsys_id",
        .description    = "Subsystem ID",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "int_ack",
        .description    = "Interrupt acknowledge",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = PCX_ACK_READ_CLEARS,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Reading INTSTATUS clears it (measured)", .value = PCX_ACK_READ_CLEARS   },
            { .description = "Writing 1 to INTSTATUS",      .value = PCX_ACK_WRITE1_CLEARS },
            { .description = "SOFTRESET clears it",         .value = PCX_ACK_RESET_CLEARS  },
            { .description = ""                                                            }
        },
        .bios           = { { 0 } }
    },
#endif
    {
        .name           = "render_threads",
        .description    = "Render threads",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 3,
        .file_filter    = NULL,
        .spinner        = { .min = 1, .max = PCX_MAX_HELPERS + 1, .step = 1 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
#ifdef USE_POWERVR_PCX_DEV
    {
        .name           = "render_delay",
        .description    = "Render time (microseconds)",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 7600, /* 640x480 SGLTri frame on the m3D: 7.6 ms */
        .file_filter    = NULL,
        .spinner        = { .min = 10, .max = 30000, .step = 100 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "debug_tile",
        .description    = "Debug: draw a marker tile at the frame origin",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
#endif
    {
        .name           = "log_level",
        .description    = "Logging",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = PCX_LOG_SUMMARY,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Off",                              .value = PCX_LOG_OFF     },
            { .description = "Summary and speed every 100 frames", .value = PCX_LOG_SUMMARY },
            { .description = "Every render and interrupt",       .value = PCX_LOG_EVENTS  },
            { .description = "Every register access",            .value = PCX_LOG_ALL     },
            { .description = ""                                                     }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "dump_frames",
        .description    = "Dump N frames to powervr_dumps",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { .min = 0, .max = 999, .step = 1 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "dump_from",
        .description    = "Start dumping at frame",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { .min = 1, .max = 30000, .step = 100 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t powervr_pcx_device = {
    .name          = "PowerVR PCX1/PCX2",
    .internal_name = "powervr_pcx",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = pcx_init,
    .close         = pcx_close,
    .reset         = pcx_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pcx_config
};
