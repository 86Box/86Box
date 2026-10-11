/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- device shell.
 *
 *          PCI configuration space and the three BARs (linear
 *          framebuffer, I/O registers, memory-mapped registers), the
 *          register dispatch that hands each offset to the block that
 *          owns it, chip reset, and the device definitions and options
 *          for the AGP and PCI boards.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 VR / RAGE 128 GL Graphics
 *              Controller Specifications", GCS-C04100 Rev 0.05, November
 *              1998 (the board straps). Cited as "GCS: ...".
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#define HAVE_STDARG_H
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/plat.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include "cpu.h"
#include <86box/vid_ati_rage128.h>
#include <86box/vid_ati_rage128_regs.h>

#ifdef ENABLE_RAGE128_LOG
int rage128_do_log = ENABLE_RAGE128_LOG;

void
rage128_log(const char *fmt, ...)
{
    va_list ap;

    if (rage128_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#endif

/* Bus clocks per CPU access, which video_inform turns into CPU cycles.
   The framebuffer and register read handlers charge them: a read stalls
   the CPU for the whole bus transaction, and that cost is what limits
   how fast a driver can poll a status register. */
static video_timings_t timing_rage128 = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };

static void rage128_vga_decode_update(rage128_t *dev);

/* ------------------------------------------------------------------ */
/* Linear framebuffer (BAR0).                                          */
/* ------------------------------------------------------------------ */

/* SURF0_PITCHSEL code (the same for surfaces 1-3) to pitch in bytes
   (RRG: SURFACE0_INFO, p. 3-228 / PDF 246). Code 0 is "Linear/No
   translation"; the guide lists no pitch for codes 20-31, and the device
   treats them as no translation too. */
static const uint16_t rage128_surf_pitch[32] = {
    /* clang-format off */
       0,   64,  128,  256,  512, 1024, 2048, 4096,
     640, 1280, 2560, 5120, 1600, 3200, 6400,  832,
    1664, 3328, 1920, 3840,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0
    /* clang-format on */
};

/* Translate a CPU aperture offset through the SURFACE0-3 windows. Inside
   an enabled window the offset is read as a linear (x, y) at the
   window's pitch and moved to the tiled offset r128_tile_off gives, the
   layout the engine draws in, so the CPU sees a tiled surface as a
   linear one. The guide lists the window registers and pitch codes but
   does not describe the translation; the tiled target is modeled. A
   window covers [LOWER, UPPER): drivers are observed to program UPPER
   as LOWER plus the surface size. Outside every window the offset
   passes through unchanged.

   *head receives how many of the len bytes fall in the first byte's
   64-byte tile column. The translation is modeled on bus data phases,
   which are aligned dwords and never cross a column, so a misaligned
   CPU word or dword that does cross one must be delivered as separately
   translated pieces, not as one store. */
static uint32_t
rage128_surf_xlate(rage128_t *dev, uint32_t addr, uint32_t len, uint32_t *head)
{
    *head = len;
    for (int n = 0; n < 4; n++) {
        uint32_t pitch = rage128_surf_pitch[dev->surf_info[n] & RAGE128_SURF_INFO_MASK];
        uint32_t off;
        uint32_t col;

        if (!pitch || addr < dev->surf_lower[n] || addr >= dev->surf_upper[n])
            continue;
        off = addr - dev->surf_lower[n];
        col = (off % pitch) & 63u;
        if (col + len > 64u)
            *head = 64u - col;
        return (dev->surf_lower[n]
                + r128_tile_off(off % pitch, off / pitch, pitch))
            & dev->vram_mask;
    }
    return addr;
}

static uint8_t rage128_lfb_readb(uint32_t addr, void *priv);
static void    rage128_lfb_writeb(uint32_t addr, uint8_t val, void *priv);

/* Framebuffer access profiler, active only with ftl_en. Counts one
   access by width, by region and by 64 KB bucket, and returns a start
   timestamp for one access in eight (the timed sample), else 0. The
   region is found by comparing the untranslated card offset with the
   scanout, destination and Z buffer bases, each taken as one
   scanout-sized extent. Bucket counts only grow, because the interval
   printer reports differences between two readings; the last-hit index
   skips the search for the common sequential walk. */
static uint64_t
rage128_lfbt_enter(rage128_t *dev, uint32_t off, uint32_t len, int wr)
{
    rage128_lfbt_t *t    = &dev->lfbt;
    const svga_t   *svga = &dev->svga;
    uint32_t        ext  = (svga->dispend > 0)
                ? (uint32_t) svga->rowoffset * 8u * (uint32_t) svga->dispend
                : 0;
    uint32_t        key  = off >> 16;
    unsigned        r;

    if (t->in_split)
        return 0;
    t->n[wr][len >> 1]++;
    if (off - dev->crtc_offset < ext)
        r = 0;
    else if (off - dev->dst_offset < ext)
        r = 1;
    else if (off - (dev->t3d.z_offset & 0x3fffffffu) < ext)
        r = 2;
    else
        r = 3;
    t->reg[r]++;
    if (t->bkt_used && t->bkt_key[t->bkt_last] == key)
        t->bkt_cnt[t->bkt_last]++;
    else {
        uint32_t i;

        for (i = 0; i < t->bkt_used; i++)
            if (t->bkt_key[i] == key)
                break;
        if (i == t->bkt_used) {
            if (i >= RAGE128_LFBT_BKTS) {
                t->bkt_dropped++;
                goto stamp;
            }
            t->bkt_key[i] = key;
            t->bkt_cnt[i] = 0;
            t->bkt_used++;
        }
        t->bkt_cnt[i]++;
        t->bkt_last = i;
    }
stamp:
    t->sample = ((++t->total) & 7u) == 0;
    return t->sample ? rage128_now_ns() : 0;
}

static void
rage128_lfbt_leave(rage128_t *dev, uint64_t t0, uint64_t t1, uint64_t t2, uint64_t t3)
{
    dev->lfbt.sampled++;
    dev->lfbt.xlate_ns += t1 - t0;
    dev->lfbt.barrier_ns += t2 - t1;
    dev->lfbt.store_ns += t3 - t2;
    dev->lfbt.sample = 0;
}

/* A word or dword that straddles a tile column goes byte by byte through
   the byte handlers, so each byte is translated on its own and reports
   its own address to synctel, the GPU barrier and changedvram. */
static uint32_t
rage128_lfb_read_split(uint32_t addr, uint32_t len, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint32_t   val = 0;

    dev->lfbt.in_split = 1;
    for (uint32_t i = 0; i < len; i++)
        val |= (uint32_t) rage128_lfb_readb(addr + i, priv) << (8 * i);
    dev->lfbt.in_split = 0;
    return val;
}

static void
rage128_lfb_write_split(uint32_t addr, uint32_t val, uint32_t len, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    dev->lfbt.in_split = 1;
    for (uint32_t i = 0; i < len; i++)
        rage128_lfb_writeb(addr + i, (uint8_t) (val >> (8 * i)), priv);
    dev->lfbt.in_split = 0;
}

static uint8_t
rage128_lfb_readb(uint32_t addr, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint64_t   t0 = 0, t1 = 0, t2 = 0;
    uint8_t    v;

    addr = (addr - dev->lfb_base) & dev->vram_mask;
    if (dev->ftl_en)
        t0 = rage128_lfbt_enter(dev, addr, 1, 0);
    if (dev->surf_xlate_on) {
        uint32_t head;
        addr = rage128_surf_xlate(dev, addr, 1, &head);
    }
    if (t0)
        t1 = rage128_now_ns();
    if (dev->synctel)
        rage128_synctel_cpu_read(dev, addr, 1);
    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, addr, 1, 0);
    if (t0)
        t2 = rage128_now_ns();
    cycles -= dev->svga.monitor->mon_video_timing_read_b;
    v = dev->svga.vram[addr];
    if (t0)
        rage128_lfbt_leave(dev, t0, t1, t2, rage128_now_ns());
    return v;
}

/* Bus cost of a len-byte aperture read; with a constant len it compiles
   to one field load. */
static inline __attribute__((always_inline)) int
rage128_lfb_read_timing(const rage128_t *dev, uint32_t len)
{
    const monitor_t *mon = dev->svga.monitor;

    return (len == 4) ? mon->mon_video_timing_read_l
        : (len == 2)  ? mon->mon_video_timing_read_w
                      : mon->mon_video_timing_read_b;
}

/* Word and dword aperture access, one body per direction. The
   width-named handlers below call it with a constant len, so each
   compiles to the same code as a hand-written handler. An access whose
   head does not cover the whole width straddles a tile column and goes
   byte by byte. */
static inline __attribute__((always_inline)) uint32_t
rage128_lfb_read_n(uint32_t addr, void *priv, uint32_t len)
{
    rage128_t *dev = (rage128_t *) priv;
    uint32_t   off = (addr - dev->lfb_base) & dev->vram_mask;
    uint64_t   t0 = 0, t1 = 0, t2 = 0;
    uint32_t   v;

    if (dev->ftl_en)
        t0 = rage128_lfbt_enter(dev, off, len, 0);
    if (dev->surf_xlate_on) {
        uint32_t head;
        uint32_t xa = rage128_surf_xlate(dev, off, len, &head);
        if (head < len) {
            if (dev->ftl_en)
                dev->lfbt.split++;
            if (t0)
                t1 = rage128_now_ns();
            v = rage128_lfb_read_split(addr, len, priv);
            if (t0)
                rage128_lfbt_leave(dev, t0, t1, t1, rage128_now_ns());
            return v;
        }
        addr = xa;
    } else
        addr = off;
    if (t0)
        t1 = rage128_now_ns();
    if (dev->synctel)
        rage128_synctel_cpu_read(dev, addr, len);
    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, addr, len, 0);
    if (t0)
        t2 = rage128_now_ns();
    cycles -= rage128_lfb_read_timing(dev, len);
    if (len == 2)
        v = *(uint16_t *) &dev->svga.vram[addr];
    else
        v = *(uint32_t *) &dev->svga.vram[addr];
    if (t0)
        rage128_lfbt_leave(dev, t0, t1, t2, rage128_now_ns());
    return v;
}

static uint16_t
rage128_lfb_readw(uint32_t addr, void *priv)
{
    return (uint16_t) rage128_lfb_read_n(addr, priv, 2);
}

static uint32_t
rage128_lfb_readl(uint32_t addr, void *priv)
{
    return rage128_lfb_read_n(addr, priv, 4);
}

static void
rage128_lfb_writeb(uint32_t addr, uint8_t val, void *priv)
{
    rage128_t *dev  = (rage128_t *) priv;
    svga_t    *svga = &dev->svga;
    uint64_t   t0 = 0, t1 = 0, t2 = 0;

    addr = (addr - dev->lfb_base) & dev->vram_mask;
    if (dev->ftl_en)
        t0 = rage128_lfbt_enter(dev, addr, 1, 1);
    if (dev->surf_xlate_on) {
        uint32_t head;
        addr = rage128_surf_xlate(dev, addr, 1, &head);
    }
    if (t0)
        t1 = rage128_now_ns();
    if (dev->synctel)
        rage128_synctel_cpu_write(dev, addr, 1);
    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, addr, 1, 1);
    if (t0)
        t2 = rage128_now_ns();
    svga->vram[addr]              = val;
    svga->changedvram[addr >> 12] = svga->monitor->mon_changeframecount;
    if (t0)
        rage128_lfbt_leave(dev, t0, t1, t2, rage128_now_ns());
}

static inline __attribute__((always_inline)) void
rage128_lfb_write_n(uint32_t addr, uint32_t val, void *priv, uint32_t len)
{
    rage128_t *dev  = (rage128_t *) priv;
    svga_t    *svga = &dev->svga;
    uint32_t   off  = (addr - dev->lfb_base) & dev->vram_mask;
    uint64_t   t0 = 0, t1 = 0, t2 = 0;

    if (dev->ftl_en)
        t0 = rage128_lfbt_enter(dev, off, len, 1);
    if (dev->surf_xlate_on) {
        uint32_t head;
        uint32_t xa = rage128_surf_xlate(dev, off, len, &head);
        if (head < len) {
            if (dev->ftl_en)
                dev->lfbt.split++;
            if (t0)
                t1 = rage128_now_ns();
            rage128_lfb_write_split(addr, val, len, priv);
            if (t0)
                rage128_lfbt_leave(dev, t0, t1, t1, rage128_now_ns());
            return;
        }
        addr = xa;
    } else
        addr = off;
    if (t0)
        t1 = rage128_now_ns();
    if (dev->synctel)
        rage128_synctel_cpu_write(dev, addr, len);
    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, addr, len, 1);
    if (t0)
        t2 = rage128_now_ns();
    if (len == 2)
        *(uint16_t *) &svga->vram[addr] = (uint16_t) val;
    else
        *(uint32_t *) &svga->vram[addr] = val;
    svga->changedvram[addr >> 12] = svga->monitor->mon_changeframecount;
    if (t0)
        rage128_lfbt_leave(dev, t0, t1, t2, rage128_now_ns());
}

static void
rage128_lfb_writew(uint32_t addr, uint16_t val, void *priv)
{
    rage128_lfb_write_n(addr, val, priv, 2);
}

static void
rage128_lfb_writel(uint32_t addr, uint32_t val, void *priv)
{
    rage128_lfb_write_n(addr, val, priv, 4);
}

/* Byte-swapped views of the framebuffer aperture, selected by
   CONFIG_CNTL.APER_0_ENDIAN: 1 is "16 bpp swapping", 2 is "32 bpp
   swapping" (RRG: CONFIG_CNTL, p. 3-10 / PDF 28, which gives no more
   detail). The swap is modeled as a byte address permutation that holds
   for every access width: aperture byte n is card byte n ^ 1 in 16 bpp
   mode and n ^ 3 in 32 bpp mode. An access aligned within one 16- or
   32-bit unit is one stored access plus a byte reverse; a misaligned one
   is split into narrower pieces. These handlers are installed only while
   a swap is selected, so the unswapped handlers carry no mode test. */
static uint8_t
rage128_lfb_readb_sw16(uint32_t addr, void *priv)
{
    return rage128_lfb_readb(addr ^ 1, priv);
}

static uint16_t
rage128_lfb_readw_sw16(uint32_t addr, void *priv)
{
    if (addr & 1)
        return (uint16_t) (rage128_lfb_readb_sw16(addr, priv)
                           | ((uint16_t) rage128_lfb_readb_sw16(addr + 1, priv) << 8));
    return (uint16_t) __builtin_bswap16(rage128_lfb_readw(addr, priv));
}

static uint32_t
rage128_lfb_readl_sw16(uint32_t addr, void *priv)
{
    return (uint32_t) rage128_lfb_readw_sw16(addr, priv)
        | ((uint32_t) rage128_lfb_readw_sw16(addr + 2, priv) << 16);
}

static void
rage128_lfb_writeb_sw16(uint32_t addr, uint8_t val, void *priv)
{
    rage128_lfb_writeb(addr ^ 1, val, priv);
}

static void
rage128_lfb_writew_sw16(uint32_t addr, uint16_t val, void *priv)
{
    if (addr & 1) {
        rage128_lfb_writeb_sw16(addr, (uint8_t) val, priv);
        rage128_lfb_writeb_sw16(addr + 1, (uint8_t) (val >> 8), priv);
        return;
    }
    rage128_lfb_writew(addr, (uint16_t) __builtin_bswap16(val), priv);
}

static void
rage128_lfb_writel_sw16(uint32_t addr, uint32_t val, void *priv)
{
    rage128_lfb_writew_sw16(addr, (uint16_t) val, priv);
    rage128_lfb_writew_sw16(addr + 2, (uint16_t) (val >> 16), priv);
}

static uint8_t
rage128_lfb_readb_sw32(uint32_t addr, void *priv)
{
    return rage128_lfb_readb(addr ^ 3, priv);
}

static uint16_t
rage128_lfb_readw_sw32(uint32_t addr, void *priv)
{
    if (addr & 1)
        return (uint16_t) (rage128_lfb_readb_sw32(addr, priv)
                           | ((uint16_t) rage128_lfb_readb_sw32(addr + 1, priv) << 8));
    return (uint16_t) __builtin_bswap16(rage128_lfb_readw(addr ^ 2, priv));
}

static uint32_t
rage128_lfb_readl_sw32(uint32_t addr, void *priv)
{
    if (addr & 3)
        return (uint32_t) rage128_lfb_readw_sw32(addr, priv)
            | ((uint32_t) rage128_lfb_readw_sw32(addr + 2, priv) << 16);
    return __builtin_bswap32(rage128_lfb_readl(addr, priv));
}

static void
rage128_lfb_writeb_sw32(uint32_t addr, uint8_t val, void *priv)
{
    rage128_lfb_writeb(addr ^ 3, val, priv);
}

static void
rage128_lfb_writew_sw32(uint32_t addr, uint16_t val, void *priv)
{
    if (addr & 1) {
        rage128_lfb_writeb_sw32(addr, (uint8_t) val, priv);
        rage128_lfb_writeb_sw32(addr + 1, (uint8_t) (val >> 8), priv);
        return;
    }
    rage128_lfb_writew(addr ^ 2, (uint16_t) __builtin_bswap16(val), priv);
}

static void
rage128_lfb_writel_sw32(uint32_t addr, uint32_t val, void *priv)
{
    if (addr & 3) {
        rage128_lfb_writew_sw32(addr, (uint16_t) val, priv);
        rage128_lfb_writew_sw32(addr + 2, (uint16_t) (val >> 16), priv);
        return;
    }
    rage128_lfb_writel(addr, __builtin_bswap32(val), priv);
}

/* ------------------------------------------------------------------ */
/* AGP image window (BAR0 upper half = Linear Aperture 1).             */
/* ------------------------------------------------------------------ */
/* The upper 32 MB of card address space is the AGP image: card offset
   0x02000000 + n maps to AGP_BASE + n[24:0] in system memory (RRG:
   DST_OFFSET, p. 3-137 / PDF 155; the image starts at AGP_APER_OFFSET,
   RRG: AGP_APER_OFFSET, p. 3-187 / PDF 205). A CPU access to it, through
   BAR0's upper half or MM_DATA, becomes one bus-master access of the
   same width through dma_bm_*. It needs bus mastering allowed (PCI
   BUS_MASTER_EN set, BUS_CNTL.BUS_MASTER_DIS clear) and AGP_BASE
   programmed; otherwise it reaches no bus, so a read returns all-ones
   and a write is dropped (modeled). The on-chip PCI GART is not
   consulted here: the guide does not say that CPU accesses to the image
   use it. Scanout never reads this half, because CRTC_OFFSET and
   CUR_OFFSET are 25-bit fields (RRG: CRTC_OFFSET, p. 3-70 / PDF 88;
   RRG: CUR_OFFSET, p. 3-81 / PDF 99). */
static int
rage128_agp_image_read(rage128_t *dev, uint32_t off, uint8_t *dst, uint32_t len)
{
    if (!rage128_pm4_bus_master_ok(dev) || !dev->agp_base)
        return 0;
    dma_bm_read(dev->agp_base + (off & 0x01ffffffu), dst, len, (int) len);
    return 1;
}

static int
rage128_agp_image_write(rage128_t *dev, uint32_t off, const uint8_t *src, uint32_t len)
{
    if (!rage128_pm4_bus_master_ok(dev) || !dev->agp_base)
        return 0;
    dma_bm_write(dev->agp_base + (off & 0x01ffffffu), src, len, (int) len);
    return 1;
}

/* Each read handler starts from all-ones of its own width, so an access
   that reaches no bus returns exactly that. */
static inline __attribute__((always_inline)) void
rage128_agp_read_n(uint32_t addr, void *priv, void *dst, uint32_t len)
{
    rage128_t *dev = (rage128_t *) priv;

    rage128_agp_image_read(dev, addr - dev->lfb_base, (uint8_t *) dst, len);
    cycles -= rage128_lfb_read_timing(dev, len);
}

static inline __attribute__((always_inline)) void
rage128_agp_write_n(uint32_t addr, void *priv, const void *src, uint32_t len)
{
    rage128_t *dev = (rage128_t *) priv;

    rage128_agp_image_write(dev, addr - dev->lfb_base, (const uint8_t *) src, len);
}

static uint8_t
rage128_agp_readb(uint32_t addr, void *priv)
{
    uint8_t v = 0xff;

    rage128_agp_read_n(addr, priv, &v, 1);
    return v;
}

static uint16_t
rage128_agp_readw(uint32_t addr, void *priv)
{
    uint16_t v = 0xffff;

    rage128_agp_read_n(addr, priv, &v, 2);
    return v;
}

static uint32_t
rage128_agp_readl(uint32_t addr, void *priv)
{
    uint32_t v = 0xffffffffu;

    rage128_agp_read_n(addr, priv, &v, 4);
    return v;
}

static void
rage128_agp_writeb(uint32_t addr, uint8_t val, void *priv)
{
    rage128_agp_write_n(addr, priv, &val, 1);
}

static void
rage128_agp_writew(uint32_t addr, uint16_t val, void *priv)
{
    rage128_agp_write_n(addr, priv, &val, 2);
}

static void
rage128_agp_writel(uint32_t addr, uint32_t val, void *priv)
{
    rage128_agp_write_n(addr, priv, &val, 4);
}

/* Byte-swapped views of the AGP image window, selected by
   CONFIG_CNTL.APER_1_ENDIAN: the same byte permutation as the
   APER_0_ENDIAN views above (aperture byte n is card byte n ^ 1 or
   n ^ 3), built over the plain handlers so the swap is applied before
   the bus transaction. */
static uint8_t
rage128_agp_readb_sw16(uint32_t addr, void *priv)
{
    return rage128_agp_readb(addr ^ 1, priv);
}

static uint16_t
rage128_agp_readw_sw16(uint32_t addr, void *priv)
{
    if (addr & 1)
        return (uint16_t) (rage128_agp_readb_sw16(addr, priv)
                           | ((uint16_t) rage128_agp_readb_sw16(addr + 1, priv) << 8));
    return (uint16_t) __builtin_bswap16(rage128_agp_readw(addr, priv));
}

static uint32_t
rage128_agp_readl_sw16(uint32_t addr, void *priv)
{
    return (uint32_t) rage128_agp_readw_sw16(addr, priv)
        | ((uint32_t) rage128_agp_readw_sw16(addr + 2, priv) << 16);
}

static void
rage128_agp_writeb_sw16(uint32_t addr, uint8_t val, void *priv)
{
    rage128_agp_writeb(addr ^ 1, val, priv);
}

static void
rage128_agp_writew_sw16(uint32_t addr, uint16_t val, void *priv)
{
    if (addr & 1) {
        rage128_agp_writeb_sw16(addr, (uint8_t) val, priv);
        rage128_agp_writeb_sw16(addr + 1, (uint8_t) (val >> 8), priv);
        return;
    }
    rage128_agp_writew(addr, (uint16_t) __builtin_bswap16(val), priv);
}

static void
rage128_agp_writel_sw16(uint32_t addr, uint32_t val, void *priv)
{
    rage128_agp_writew_sw16(addr, (uint16_t) val, priv);
    rage128_agp_writew_sw16(addr + 2, (uint16_t) (val >> 16), priv);
}

static uint8_t
rage128_agp_readb_sw32(uint32_t addr, void *priv)
{
    return rage128_agp_readb(addr ^ 3, priv);
}

static uint16_t
rage128_agp_readw_sw32(uint32_t addr, void *priv)
{
    if (addr & 1)
        return (uint16_t) (rage128_agp_readb_sw32(addr, priv)
                           | ((uint16_t) rage128_agp_readb_sw32(addr + 1, priv) << 8));
    return (uint16_t) __builtin_bswap16(rage128_agp_readw(addr ^ 2, priv));
}

static uint32_t
rage128_agp_readl_sw32(uint32_t addr, void *priv)
{
    if (addr & 3)
        return (uint32_t) rage128_agp_readw_sw32(addr, priv)
            | ((uint32_t) rage128_agp_readw_sw32(addr + 2, priv) << 16);
    return __builtin_bswap32(rage128_agp_readl(addr, priv));
}

static void
rage128_agp_writeb_sw32(uint32_t addr, uint8_t val, void *priv)
{
    rage128_agp_writeb(addr ^ 3, val, priv);
}

static void
rage128_agp_writew_sw32(uint32_t addr, uint16_t val, void *priv)
{
    if (addr & 1) {
        rage128_agp_writeb_sw32(addr, (uint8_t) val, priv);
        rage128_agp_writeb_sw32(addr + 1, (uint8_t) (val >> 8), priv);
        return;
    }
    rage128_agp_writew(addr ^ 2, (uint16_t) __builtin_bswap16(val), priv);
}

static void
rage128_agp_writel_sw32(uint32_t addr, uint32_t val, void *priv)
{
    if (addr & 3) {
        rage128_agp_writew_sw32(addr, (uint16_t) val, priv);
        rage128_agp_writew_sw32(addr + 2, (uint16_t) (val >> 16), priv);
        return;
    }
    rage128_agp_writel(addr, __builtin_bswap32(val), priv);
}

/* ------------------------------------------------------------------ */
/* MMIO register block (BAR2).                                         */
/* ------------------------------------------------------------------ */
static uint8_t rage128_pci_read(int func, int addr, int len, void *priv);
static void    rage128_endian_apply(rage128_t *dev);

/* Interrupt line. A GEN_INT_CNTL enable bit "allows corresponding status
   bit to generate an interrupt signal to the system" (RRG: GEN_INT_CNTL,
   p. 3-193 / PDF 211), so INTA is asserted while any enabled source has
   its GEN_INT_STATUS bit set and released once none has. PCI interrupts
   are level-sensitive, so this is re-evaluated after every change to
   either register. pci_set_irq and pci_clear_irq drive INTA through the
   irq_state shadow, as vid_ati_mach64.c does. */
void
rage128_gen_int_update(rage128_t *dev)
{
    if (dev->gen_int_cntl & dev->gen_int_status & RAGE128_GIC_SUPPORTED)
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    else
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
}

/* Move a pending busy-to-idle event of the engine into GUI_IDLE_INT,
   GEN_INT_STATUS bit 19 (RRG: GEN_INT_STATUS, p. 3-196 / PDF 214; the
   guide does not describe the event, and raising it on the engine's
   busy-to-idle edge is modeled). The engine code only sets
   gui_idle_event, from whichever thread it runs on; this runs on the
   CPU thread, so pci_set_irq is never called from another thread. It
   runs on every GEN_INT_STATUS read and from the vertical blank and
   sync callbacks, so an enabled interrupt is raised within a frame even
   if the driver never polls. */
static void
rage128_gen_int_fold_gui_idle(rage128_t *dev)
{
    if (atomic_exchange(&dev->gui_idle_event, 0)) {
        dev->gen_int_status |= RAGE128_GIS_GUI_IDLE;
        rage128_gen_int_update(dev);
    }
}

/* Latch CRTC_VBLANK_INT, "Vertical blank started since last cleared",
   and re-evaluate INTA. Called from the svga core's vblank-start hook:
   vertical blank starts a front porch before vertical sync, so the two
   events are latched separately. The bit stays set until the driver
   writes 1 to CRTC_VBLANK_INT_AK (RRG: GEN_INT_STATUS, p. 3-193 /
   PDF 211). */
void
rage128_gen_int_vblank(rage128_t *dev)
{
    rage128_gen_int_fold_gui_idle(dev);
    dev->gen_int_status |= RAGE128_GIS_VBLANK;
    rage128_gen_int_update(dev);
}

/* Latch CRTC_VSYNC_INT, "Vertical sync started since last cleared",
   from the svga core's vsync callback (RRG: GEN_INT_STATUS, p. 3-194 /
   PDF 212). */
void
rage128_gen_int_vsync(rage128_t *dev)
{
    rage128_gen_int_fold_gui_idle(dev);
    dev->gen_int_status |= RAGE128_GIS_VSYNC;
    rage128_gen_int_update(dev);
}

/* AMCGPIO pin levels, what AMCGPIO_Y reads. A pad whose MASK and EN bits
   are both set drives its own A bit; every other pad shows the level
   supplied from outside in amcgpio_in, which is 0 on a single card with
   nothing wired to the pads. */
uint32_t
rage128_amcgpio_pins(const rage128_t *dev)
{
    uint32_t drv = dev->amcgpio_mask & dev->amcgpio_en;

    return ((dev->amcgpio_a & drv) | (dev->amcgpio_in & ~drv)) & RAGE128_AMCGPIO_PINS;
}

/* Register file read. Reached directly through the memory-mapped BAR2,
   through the I/O BAR for offsets 0x0000-0x00ff (RRG: Description of
   Mapped Memory Apertures, Table 2-1, p. 2-5 / PDF 15), and indirectly
   through MM_INDEX and MM_DATA. Registers the device does not implement
   read 0. */
static uint32_t
rage128_reg_read32(rage128_t *dev, uint32_t off)
{
    off &= 0x3ffc;

    if (dev->ftl_en) {
        /* Count an indexed read under the register it reaches, not
           under MM_DATA. */
        uint32_t bin = off;

        if (off == RAGE128_MM_DATA && !(dev->mm_index & RAGE128_MM_INDEX_MM_APER))
            bin = dev->mm_index & 0x3ffc;
        if (bin == RAGE128_PM4_BUFFER_DL_RPTR || bin == RAGE128_PM4_BUFFER_DL_WPTR
            || bin == RAGE128_GUI_STAT)
            dev->tel_polls++;
        dev->tel_reads[bin >> 2]++;
        if (atomic_load_explicit(&dev->ftl_gap, memory_order_relaxed))
            dev->ftl_reads[bin >> 2]++;
    }

    switch (off) {
        case RAGE128_MM_INDEX:
            return dev->mm_index;
        case RAGE128_MM_DATA:
            /* With MM_INDEX.MM_APER set, MM_DATA reaches linear aperture 0
               at offset MM_ADDR instead of the register file (RRG:
               MM_INDEX, p. 3-191 / PDF 209). The offset is split at bit
               25 as on every engine path: below it is local VRAM, above
               it the AGP image, reached through the chipset aperture. */
            if (dev->mm_index & RAGE128_MM_INDEX_MM_APER) {
                uint32_t a = dev->mm_index & RAGE128_MM_INDEX_MM_ADDR;
                if (r128_card_is_agp(a)) {
                    uint32_t v = 0xffffffffu;
                    rage128_agp_image_read(dev, a, (uint8_t *) &v, 4);
                    return v;
                }
                a &= dev->vram_mask;
                if (dev->synctel)
                    rage128_synctel_cpu_read(dev, a, 4);
                if (dev->gpu)
                    rage128_gpu_cpu_barrier(dev, a, 4, 0);
                return *(uint32_t *) &dev->svga.vram[a];
            }
            /* Otherwise MM_DATA reads the register at MM_ADDR; MM_DATA
               pointing at itself reads 0. */
            if ((dev->mm_index & 0x3ffc) == RAGE128_MM_DATA)
                return 0x00000000;
            return rage128_reg_read32(dev, dev->mm_index);
        case RAGE128_BIOS_0_SCRATCH:
        case RAGE128_BIOS_1_SCRATCH:
        case RAGE128_BIOS_2_SCRATCH:
        case RAGE128_BIOS_3_SCRATCH:
            return dev->bios_scratch[(off - RAGE128_BIOS_0_SCRATCH) >> 2];

        /* Chip-core configuration registers. The guide's page for each
           is cited next to its constant in vid_ati_rage128_regs.h. */
        case RAGE128_GEN_INT_CNTL:
            return dev->gen_int_cntl;
        case RAGE128_GEN_INT_STATUS:
            /* "Read shows current states" (RRG: GEN_INT_STATUS,
               p. 3-196 / PDF 214): a pending engine-idle event is
               folded in first. */
            rage128_gen_int_fold_gui_idle(dev);
            return dev->gen_int_status;
        case RAGE128_BUS_CNTL:
            return dev->bus_cntl;
        case RAGE128_BUS_CNTL1:
            return dev->bus_cntl1;
        case RAGE128_GPIO_MONID:
            {
                /* The stored pad bits, with the two DDC input bits
                   (MONID2_Y for the clock, MONID1_Y for the data) read live
                   from the I2C bus. */
                uint32_t v = dev->gpio_monid
                    & ~(RAGE128_GPIO_MONID_Y_DDC_CLK | RAGE128_GPIO_MONID_Y_DDC_DAT);

                if (i2c_gpio_get_scl(dev->i2c))
                    v |= RAGE128_GPIO_MONID_Y_DDC_CLK;
                if (i2c_gpio_get_sda(dev->i2c))
                    v |= RAGE128_GPIO_MONID_Y_DDC_DAT;
                return v;
            }
        case RAGE128_AMCGPIO_MASK:
        case RAGE128_AMCGPIO_MASK_MIR:
            return dev->amcgpio_mask;
        case RAGE128_AMCGPIO_A:
        case RAGE128_AMCGPIO_A_MIR:
            return dev->amcgpio_a;
        case RAGE128_AMCGPIO_EN:
        case RAGE128_AMCGPIO_EN_MIR:
            return dev->amcgpio_en;
        case RAGE128_AMCGPIO_Y:
        case RAGE128_AMCGPIO_Y_MIR:
            return rage128_amcgpio_pins(dev);
        case RAGE128_CONFIG_CNTL:
            /* CFG_ATI_REV_ID [19:16] is the read-only chip revision: 1 for
               device 0x5046 (PF), 3 for device 0x5245 (RE). The guide's
               default is 0, but the Windows 98 driver needs a revision
               its ASIC table lists (RE: ati2vxaa.vxd @7fc4); the reasoning
               for each value is next to RAGE128_CFG_ATI_REV_PF in
               vid_ati_rage128_regs.h. */
            return (dev->config_cntl & ~0x000f0000)
                | (dev->pci_device_id == RAGE128_PCI_DEVICE_RE
                       ? RAGE128_CFG_ATI_REV_RE
                       : RAGE128_CFG_ATI_REV_PF);
        case RAGE128_CONFIG_APER_0_BASE:
            /* Software is told to find the linear apertures through these
               registers (RRG: Description of Mapped Memory Apertures,
               p. 2-6 / PDF 16), so they follow BAR0 rather than read 0.
               APER_0_BASE is [31:26] (RRG: CONFIG_APER_0_BASE, p. 3-12 /
               PDF 30), and BAR0 is already 64 MB aligned. */
            return dev->lfb_base & RAGE128_APER_0_BASE_MASK;
        case RAGE128_CONFIG_APER_1_BASE:
            /* APER_1_BASE is [31:25], and "Bit 0 of this field is
               hardwired to ONE" means register bit 25, not bit 0 (RRG:
               CONFIG_APER_1_BASE, pp. 3-12-3-13 / PDF 30-31). With
               APER_0_BASE 64 MB aligned, that puts aperture 1 at aperture
               0 plus 32 MB, the upper half of BAR0. The guide calls the
               two apertures identical copies of the framebuffer and AGP
               image (RRG: Description of Mapped Memory Apertures, p. 2-6 /
               PDF 16); the device maps that upper half as the AGP image
               window only, which is where AGP_APER_OFFSET (32 MB) places
               the image within aperture 0. */
            return (dev->lfb_base & RAGE128_APER_1_BASE_MASK) | R128_CARD_AGP_HALF;
        case RAGE128_CONFIG_APER_SIZE:
            /* 32 MB whatever the fitted VRAM: the field's default, with
               bits 24:0 hardwired to zero (RRG: CONFIG_APER_SIZE,
               p. 3-202 / PDF 220). */
            return RAGE128_CONFIG_APER_SIZE_VAL;
        case RAGE128_CONFIG_REG_1_BASE:
            /* The second register aperture is identical to the first and
               "functions in all systems" (RRG: CONFIG_REG_1_BASE,
               p. 3-202 / PDF 220). REG_1_BASE is [31:13] and the low bit
               of the field, register bit 13, is hardwired to one, so with
               BAR2 16 KB aligned it reads BAR2 plus 8 KB: the upper copy
               in BAR2. */
            return (dev->mmio_base & RAGE128_REG_1_BASE_MASK)
                | (RAGE128_REG_APER_MASK + 1);
        case RAGE128_CONFIG_REG_APER_SIZE:
            return 0x00002000; /* RRG: CONFIG_REG_APER_SIZE, p. 3-202 / PDF 220 */
        case RAGE128_CONFIG_MEMSIZE_EMB:
            /* "Reserved for future use", default 0; the chip has no
               embedded memory (RRG: CONFIG_MEMSIZE_EMBEDDED, p. 3-203 /
               PDF 221). */
            return 0x00000000;
        case RAGE128_BM_CHUNK_0_VAL:
            return dev->bm_chunk_val[0];
        case RAGE128_BM_CHUNK_1_VAL:
            return dev->bm_chunk_val[1];
        case RAGE128_BM_QUEUE_FREE_STATUS:
            /* The reset value: every queue's free count at its default and
               no transfer active (RRG: BM_QUEUE_FREE_STATUS,
               pp. 3-223-3-224 / PDF 241-242). The device has no bus-master
               queues, so it always reads this. */
            return RAGE128_BM_QUEUE_FREE_IDLE;
        case RAGE128_CONFIG_XSTRAP:
            {
                /* Read-only copies of the pin straps latched at reset (RRG:
                   CONFIG_XSTRAP, p. 3-11 / PDF 29). Each pin strap has an
                   internal pull-down except add_in_card, which has a pull-up
                   (GCS: External Straps, Table 4-7, pp. 4-32-4-33 /
                   PDF 80-81), so a strap leaves its default only where the
                   board pulls the pin the other way. VGA_DISABLE is set on a
                   board installed as a secondary card. The bus mode comes
                   from BUS_CLK_SEL and BUSTYPE (GCS: External Straps,
                   Table 4-8, p. 4-33 / PDF 81): the AGP board is modeled as
                   mode 4, PLL clock and AGP, with both at 0; the PCI board as
                   mode 1, reference clock and PCI, with BUS_CLK_SEL 1.
                   ADDIN_CARD is set when the chip has a BIOS ROM of its own;
                   a chip without one, such as the second chip in
                   vid_ati_rage128_maxx.c, comes up as a motherboard
                   implementation. */
                uint32_t straps = 0;
                if (dev->vga_disabled)
                    straps |= RAGE128_XSTRAP_VGA_DISABLE;
                if (!dev->is_agp)
                    straps |= RAGE128_XSTRAP_BUS_CLK_SEL;
                if (dev->has_bios_rom)
                    straps |= RAGE128_XSTRAP_ADDIN_CARD;
                return straps;
            }
        case RAGE128_CONFIG_BONDS:
            /* Read-only bond options, every field defaulting to 0 (RRG:
               CONFIG_BONDS, pp. 3-11-3-12 / PDF 29-30). */
            return 0x00000000;
        case RAGE128_GEN_RESET_CNTL:
            return dev->gen_reset_cntl;
        case RAGE128_GEN_STATUS:
            /* Read-only MPP bus status (RRG: GEN_STATUS, p. 3-201 /
               PDF 219). The device has no MPP bus, so it reads idle, 0. */
            return 0x00000000;
        case RAGE128_CONFIG_MEMSIZE:
            return dev->config_memsize;
        case RAGE128_TEST_DEBUG_CNTL:
            return dev->test_debug_cntl;
        case RAGE128_TEST_DEBUG_MUX:
            return dev->test_debug_mux;
        case RAGE128_HW_DEBUG:
            return dev->hw_debug;
        case RAGE128_HOST_PATH_CNTL:
            return dev->host_path_cntl;
        case RAGE128_MEM_CNTL:
            /* MEM_CTLR_STATUS, MEM_SEQNCR_STATUS and MEM_ARBITER_STATUS
               [22:20] are read-only busy bits (RRG: MEM_CNTL, p. 3-112 /
               PDF 130). The emulated memory controller is never busy, so
               they read 0. */
            return dev->mem_cntl & ~RAGE128_MEM_CNTL_RO_MASK;
        case RAGE128_EXT_MEM_CNTL:
            return dev->ext_mem_cntl;
        case RAGE128_MEM_ADDR_CONFIG:
            return dev->mem_addr_config;
        case RAGE128_MEM_INTF_CNTL:
            return dev->mem_intf_cntl;
        case RAGE128_MEM_STR_CNTL:
            return dev->mem_str_cntl;
        case RAGE128_MEM_INIT_LAT_TIMER:
            return dev->mem_init_lat_timer;
        case RAGE128_MEM_SDRAM_MODE_REG:
            return dev->mem_sdram_mode_reg;
        case RAGE128_PAD_CTLR_STRENGTH:
            return dev->pad_ctlr_strength;
        case RAGE128_PC_MISC_CTL:
            return dev->pc_misc_ctl;
        case RAGE128_VIDEOMUX_CNTL:
            return dev->videomux_cntl;
        case RAGE128_SURFACE_DELAY:
            return dev->surface_delay;
        case RAGE128_AGP_BASE:
            return dev->agp_base;
        case RAGE128_AGP_CNTL:
            return dev->agp_cntl;
        case RAGE128_AGP_APER_OFFSET:
            /* Read-only 32 MB, bits 24:0 hardwired to zero (RRG:
               AGP_APER_OFFSET, p. 3-187 / PDF 205): the AGP image starts
               at card address bit 25, the split every engine path tests
               with r128_card_is_agp. */
            return R128_CARD_AGP_HALF;
        case RAGE128_AGP_CNTL_B:
            return dev->agp_cntl_b;
        case RAGE128_GUI_DEBUG0:
            return dev->gui_debug0;
        case RAGE128_GUI_STAT:
            /* Read-only engine status (RRG: GUI_STAT, p. 3-244 / PDF 262).
               It reads GUI_ACTIVE and PM4_BUSY while the CCE thread holds
               fetched work it has not executed, GUI_ACTIVE alone while
               the ring is drained but the optional GPU backend is still
               drawing, and idle otherwise. GUI_ACTIVE is the OR of the
               engine busy bits, so it stays set until the last pixels
               are written, and a driver's idle poll waits for that in
               guest time. GUI_FIFOCNT always reads 64 free entries. */
            if (rage128_pm4_active(dev))
                return RAGE128_GUI_STAT_BUSY;
            return rage128_gpu_engine_busy(dev) ? RAGE128_GUI_STAT_ACTIVE
                                                : RAGE128_GUI_STAT_IDLE;
        case RAGE128_PC_GUI_MODE:
            return dev->pc_gui_mode;
        default:
            break;
    }

    /* SURFACE0-3 CPU translation windows: SURFACE0_LOWER_BOUND,
       SURFACE0_UPPER_BOUND and SURFACE0_INFO at 0x0b04-0x0b0c, and the
       same three for each further surface 0x10 higher. The first
       dword of a group is SURFACE_DELAY for surface 0 (handled above) and
       not in the guide for the others. */
    if (off >= RAGE128_SURFACE0_LOWER_BOUND && off <= RAGE128_SURFACE3_INFO) {
        uint32_t n = (off - 0x0b00) >> 4;

        switch (off & 0x0c) {
            case 0x04:
                return dev->surf_lower[n];
            case 0x08:
                return dev->surf_upper[n];
            case 0x0c:
                return dev->surf_info[n];
            default:
                break;
        }
    }

    /* A CPU read of engine state returns the current value and does not
       wait for the CCE thread to drain. Drivers poll scratch and status
       registers while the engine runs; a drain here would hold the CPU
       behind the whole ring and defeat the WAIT_UNTIL flip throttle,
       which lets the CPU run ahead of a stalled ring. A single-dword read
       that races the CCE thread sees either the old or the new value. */

    /* Display block: PLL / CRTC / DAC / palette. */
    {
        uint32_t v;

        if (rage128_display_reg_read(dev, off, &v))
            return v;
        /* OV0 hardware video overlay (vid_ati_rage128_ov0.c). */
        if (rage128_ov0_reg_read(dev, off, &v))
            return v;
        /* DVD subpicture block (vid_ati_rage128_ov0.c). */
        if (rage128_subpic_reg_read(dev, off, &v))
            return v;
        /* 2D GUI engine context (vid_ati_rage128_2d.c). */
        if (rage128_2d_reg_read(dev, off, &v))
            return v;
        /* PM4/CCE command processor + PCI GART (vid_ati_rage128_pm4.c). */
        if (rage128_pm4_reg_read(dev, off, &v))
            return v;
        /* 3D engine CCE context (vid_ati_rage128_3d.c). */
        if (rage128_3d_reg_read(dev, off, &v))
            return v;
        /* MPEG-2 macroblock assist (vid_ati_rage128_mpeg.c). */
        if (rage128_mpeg_reg_read(dev, off, &v))
            return v;
    }

    /* Read-only copy of PCI configuration space (RRG: Description of
       Mapped Memory Apertures, Table 2-1, p. 2-5 / PDF 15). */
    if (off >= RAGE128_CONFIG_MIRROR_BASE && off <= RAGE128_CONFIG_MIRROR_END) {
        uint32_t base = off - RAGE128_CONFIG_MIRROR_BASE;
        return rage128_pci_read(0, base, 1, dev)
            | (rage128_pci_read(0, base + 1, 1, dev) << 8)
            | (rage128_pci_read(0, base + 2, 1, dev) << 16)
            | ((uint32_t) rage128_pci_read(0, base + 3, 1, dev) << 24);
    }

    return 0x00000000;
}

/* One open-drain MONID pad: it pulls its line low only when its MASK and
   EN bits are set and its A bit is 0; otherwise the line floats to the
   board's pull-up. */
static inline int
rage128_monid_pad_low(uint32_t monid, uint32_t mask_bit, uint32_t en_bit,
                      uint32_t a_bit)
{
    return (monid & mask_bit) && (monid & en_bit) && !(monid & a_bit);
}

/* Writes the capture stream's R record for a register that
   rage128_reg_write handles before its main capture tap. Every arm of
   the first switch (except MM_DATA in register mode, whose indexed
   write records itself) and the SURFACE window code call it, so each
   direct CPU write is in the stream exactly once. It has the same guard
   as the main tap: writes made by the CCE stream or by an
   indirect-buffer parse are already in the stream. It is called after
   the write lands, except that BUS_CNTL records before its ring kick and
   GEN_RESET_CNTL after its reset (see each site). */
static void
rage128_cap_early_reg(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask)
{
    if (dev->cap_file && !rage128_on_cce_thread && !rage128_in_indirect) {
        fprintf(dev->cap_file, "R %04x %08x %08x\n", off, val, mask);
        rage128_cap_tick(dev);
    }
}

/* Register file write. val is already shifted to its byte lanes and mask
   selects the lanes written. The guide allows byte and word access to
   the display and configuration registers but lists the GUI and
   multimedia registers as dword-only (RRG: Accessing Bytes, Words, and
   Dwords, Table 2-2, p. 2-7 / PDF 17); the device accepts a partial
   write to any register and merges it by mask. */
static void
rage128_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask)
{
    off &= 0x3ffc;
    val &= mask;

    /* A write to the CCE and GUI engine registers from 0x1000 up (RRG:
       Description of Mapped Memory Apertures, Table 2-1, p. 2-5 / PDF 15)
       or to the PM4 block at 0x0700-0x07ff starts the deferred GPU
       backend. Display, PLL and CRTC registers sit below, so the video
       BIOS POST and mode sets do not start it. */
    if (dev->gpu_defer && (off >= 0x1000 || (off >= 0x0700 && off < 0x0800)))
        rage128_gpu_lazy_init(dev, off);

    switch (off) {
        case RAGE128_AGP_BASE:
            /* The system bus address of the chipset's AGP aperture, the
               base of the AGP image (RRG: DST_OFFSET, p. 3-137 / PDF 155).
               The X driver writes it with the chipset's aperture base and
               then turns the on-chip PCI GART off (xf86-video-r128
               r128_dri.c R128DRIAgpInit). */
            dev->agp_base = ((dev->agp_base & ~mask) | val) & RAGE128_AGP_BASE_MASK;
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_AGP_CNTL:
            dev->agp_cntl = ((dev->agp_cntl & ~mask) | val) & RAGE128_AGP_CNTL_MASK;
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_AGP_APER_OFFSET:
            rage128_cap_early_reg(dev, off, val, mask);
            return; /* read-only */
        case RAGE128_MM_INDEX:
            dev->mm_index = (dev->mm_index & ~mask) | val;
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_MM_DATA:
            /* With MM_INDEX.MM_APER set, MM_DATA writes linear aperture 0
               at MM_ADDR instead of the register file (RRG: MM_INDEX,
               p. 3-191 / PDF 209), split at bit 25 as on a read. */
            if (dev->mm_index & RAGE128_MM_INDEX_MM_APER) {
                uint32_t a = dev->mm_index & RAGE128_MM_INDEX_MM_ADDR;
                if (r128_card_is_agp(a)) {
                    /* A byte or word write enables only some byte
                       lanes; send only those bytes, so system memory
                       sees the same byte enables. */
                    if (mask == 0xffffffffu)
                        rage128_agp_image_write(dev, a, (const uint8_t *) &val, 4);
                    else
                        for (uint32_t i = 0; i < 4; i++)
                            if (mask & (0xffu << (i * 8))) {
                                uint8_t b = (uint8_t) (val >> (i * 8));
                                rage128_agp_image_write(dev, a + i, &b, 1);
                            }
                    rage128_cap_early_reg(dev, off, val, mask);
                    return;
                }
                a &= dev->vram_mask;
                uint32_t *p = (uint32_t *) &dev->svga.vram[a];
                if (dev->synctel)
                    rage128_synctel_cpu_write(dev, a, 4);
                if (dev->gpu)
                    rage128_gpu_cpu_barrier(dev, a, 4, 1);
                *p                             = (*p & ~mask) | (val & mask);
                dev->svga.changedvram[a >> 12] = dev->svga.monitor->mon_changeframecount;
                rage128_cap_early_reg(dev, off, val, mask);
                return;
            }
            /* Register mode is not recorded here: the indexed write
               records itself, and a replay of both would apply it twice. */
            if ((dev->mm_index & 0x3ffc) == RAGE128_MM_DATA)
                return;
            rage128_reg_write(dev, dev->mm_index, val, mask);
            return;
        case RAGE128_BIOS_0_SCRATCH:
        case RAGE128_BIOS_1_SCRATCH:
        case RAGE128_BIOS_2_SCRATCH:
        case RAGE128_BIOS_3_SCRATCH:
            {
                uint32_t *r = &dev->bios_scratch[(off - RAGE128_BIOS_0_SCRATCH) >> 2];
                *r          = (*r & ~mask) | val;
                rage128_cap_early_reg(dev, off, val, mask);
                return;
            }

            /* Chip-core configuration registers: read/write storage that
               keeps the guide's read-only fields read-only. The page for each
               register is cited next to its constant in
               vid_ati_rage128_regs.h. */
#define MERGE(field) ((field) = ((field) & ~mask) | val)
        case RAGE128_BUS_CNTL:
            MERGE(dev->bus_cntl);
            /* BUS_MSTR_RESET [1] and BUS_FLUSH_BUF [2] are write-only
               strobes (RRG: BUS_CNTL, p. 3-106 / PDF 124) and read back 0.
               The device has no bus-master queues for them to act on. */
            dev->bus_cntl &= ~RAGE128_BUS_CNTL_WO_MASK;
            /* Recorded before the kick: the dwords the kick releases
               must follow this record in the capture stream. */
            rage128_cap_early_reg(dev, off, val, mask);
            /* Clearing BUS_MASTER_DIS lets a ring armed while bus
               mastering was blocked start fetching. Such a ring has no
               pending write pointer update to restart it, so the ring
               pump is kicked here; the pump is CPU-thread only, so a
               write from the CCE stream does not kick. */
            if (dev->cce_thread && !rage128_on_cce_thread)
                rage128_pm4_kick(dev);
            return;
        case RAGE128_BUS_CNTL1:
            MERGE(dev->bus_cntl1);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_GEN_INT_CNTL:
            MERGE(dev->gen_int_cntl);
            rage128_gen_int_update(dev);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_GEN_INT_STATUS:
            /* "Write of 1 clears states" (RRG: GEN_INT_STATUS, p. 3-196 /
               PDF 214): each _AK bit written as 1 clears its status bit,
               and a 0 leaves it alone. INTA is re-evaluated afterward. */
            dev->gen_int_status &= ~(val & mask & RAGE128_GIS_ACK_MASK);
            rage128_gen_int_update(dev);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_GPIO_MONID:
            {
                /* MONID pads 2 (clock) and 1 (data) drive the VGA
                   connector's DDC2 I2C lines as open-drain outputs (RRG:
                   GPIO_MONID, pp. 3-196-3-197 / PDF 214-215; pad use from
                   xf86-video-r128 r128_output.c R128SetupConnectors and
                   R128DisplayDDCConnected). After each write the I2C lines
                   are set from the two pads. */
                MERGE(dev->gpio_monid);
                i2c_gpio_set(dev->i2c,
                             rage128_monid_pad_low(dev->gpio_monid,
                                                   RAGE128_GPIO_MONID_MASK_DDC_CLK,
                                                   RAGE128_GPIO_MONID_EN_DDC_CLK,
                                                   RAGE128_GPIO_MONID_A_DDC_CLK)
                                 ? 0
                                 : 1,
                             rage128_monid_pad_low(dev->gpio_monid,
                                                   RAGE128_GPIO_MONID_MASK_DDC_DAT,
                                                   RAGE128_GPIO_MONID_EN_DDC_DAT,
                                                   RAGE128_GPIO_MONID_A_DDC_DAT)
                                 ? 0
                                 : 1);
                rage128_cap_early_reg(dev, off, val, mask);
                return;
            }
        case RAGE128_AMCGPIO_MASK:
        case RAGE128_AMCGPIO_MASK_MIR:
        case RAGE128_AMCGPIO_A:
        case RAGE128_AMCGPIO_A_MIR:
        case RAGE128_AMCGPIO_EN:
        case RAGE128_AMCGPIO_EN_MIR:
        case RAGE128_AMCGPIO_Y:
        case RAGE128_AMCGPIO_Y_MIR:
            {
                /* Three latches (MASK, A, EN) of 26 pads each; Y is the pin
                   readback, so a write to it is dropped. A board wired to the
                   pads is told after the latch is updated, so it sees the
                   write as one change. */
                uint32_t *r;

                switch (off) {
                    case RAGE128_AMCGPIO_MASK:
                    case RAGE128_AMCGPIO_MASK_MIR:
                        r = &dev->amcgpio_mask;
                        break;
                    case RAGE128_AMCGPIO_A:
                    case RAGE128_AMCGPIO_A_MIR:
                        r = &dev->amcgpio_a;
                        break;
                    case RAGE128_AMCGPIO_EN:
                    case RAGE128_AMCGPIO_EN_MIR:
                        r = &dev->amcgpio_en;
                        break;
                    default:
                        rage128_cap_early_reg(dev, off, val, mask);
                        return;
                }
                *r = ((*r & ~mask) | (val & mask)) & RAGE128_AMCGPIO_PINS;
                rage128_cap_early_reg(dev, off, val, mask);
                if (dev->amcgpio_notify)
                    dev->amcgpio_notify(dev, dev->amcgpio_priv);
                return;
            }
        case RAGE128_CONFIG_CNTL:
            MERGE(dev->config_cntl);
            dev->config_cntl &= ~0x000f0000; /* CFG_ATI_REV_ID is read-only */
            rage128_vga_decode_update(dev);
            rage128_endian_apply(dev); /* APER_0_ENDIAN, APER_1_ENDIAN, APER_REG_ENDIAN */
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_CONFIG_XSTRAP:
        case RAGE128_CONFIG_BONDS:
        case RAGE128_GEN_STATUS:
            rage128_cap_early_reg(dev, off, val, mask);
            return; /* read-only */
        case RAGE128_GEN_RESET_CNTL:
            {
                /* Soft resets per block (RRG: GEN_RESET_CNTL, pp. 3-200-3-201
                   / PDF 218-219); the value reads back as written. Both open
                   drivers pulse SOFT_RESET_GUI to recover a hung engine
                   (linux r128 DRM r128_cce.c r128_do_engine_reset;
                   xf86-video-r128 r128_accel.c R128EngineReset). On its 0 to
                   1 edge the device abandons pending engine work: the CCE
                   executor is parked, fetched but unexecuted FIFO dwords and
                   a partly parsed indirect buffer are dropped, an armed host
                   data transfer is canceled and a deferred raster batch is
                   discarded. Register contents are kept, except
                   PM4_VC_DEBUG_CONFIG, the one register the guide says "is
                   reset with SOFT_RESET_GUI (all fields default to zero)"
                   (RRG: PM4_VC_DEBUG_CONFIG, p. 3-221 / PDF 239). The other
                   reset bits are stored only. */
                uint32_t old = dev->gen_reset_cntl;

                MERGE(dev->gen_reset_cntl);
                if (!(old & RAGE128_GEN_SOFT_RESET_GUI)
                    && (dev->gen_reset_cntl & RAGE128_GEN_SOFT_RESET_GUI)) {
                    /* All of it is done in rage128_pm4_gui_reset, which keeps
                       the executor parked for the whole sequence and releases
                       it only after the last of these stores has landed. */
                    rage128_pm4_gui_reset(dev);
                }
                /* Recorded after the reset: the reset parks the executor and
                   empties the FIFO, so every dword taken before the pulse is
                   already ahead of this record, and only this thread can
                   refill the FIFO. */
                rage128_cap_early_reg(dev, off, val, mask);
                return;
            }
        case RAGE128_CONFIG_MEMSIZE:
            /* Bits 20:0 are hardwired to zero, a 2 MB granularity (RRG:
               CONFIG_MEMSIZE, p. 3-12 / PDF 30). */
            MERGE(dev->config_memsize);
            dev->config_memsize &= RAGE128_CONFIG_MEMSIZE_MASK;
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_TEST_DEBUG_CNTL:
            /* Selects the test mode whose signals TEST_DEBUG_MUX picks
               (RRG: TEST_DEBUG_MUX, p. 3-132 / PDF 150). The device has
               no test modes, so it is stored only. */
            MERGE(dev->test_debug_cntl);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_TEST_DEBUG_MUX:
            MERGE(dev->test_debug_mux);
            /* Keep the defined fields: TEST_DEBUG_MUX [3:0],
               TEST_DEBUG_BANK [5:4], TEST_DEBUG_CLK [12:8] and
               TEST_DEBUG_CLK_INV [15] (RRG: TEST_DEBUG_MUX,
               pp. 3-132-3-133 / PDF 150-151). */
            dev->test_debug_mux &= 0x00009f3f;
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_HW_DEBUG:
            MERGE(dev->hw_debug);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_HOST_PATH_CNTL:
            MERGE(dev->host_path_cntl);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_MEM_CNTL:
            MERGE(dev->mem_cntl);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_EXT_MEM_CNTL:
            MERGE(dev->ext_mem_cntl);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_MEM_ADDR_CONFIG:
            MERGE(dev->mem_addr_config);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_MEM_INTF_CNTL:
            MERGE(dev->mem_intf_cntl);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_MEM_STR_CNTL:
            MERGE(dev->mem_str_cntl);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_MEM_INIT_LAT_TIMER:
            MERGE(dev->mem_init_lat_timer);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_MEM_SDRAM_MODE_REG:
            /* A 0 to 1 transition of MEM_SDRAM_RESET [31] starts the SDRAM
               reset sequence (RRG: MEM_SDRAM_MODE_REG, p. 3-206 /
               PDF 224). The emulated SDRAM needs no setup, so the bit is
               only stored and reads back as written. */
            MERGE(dev->mem_sdram_mode_reg);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_PAD_CTLR_STRENGTH:
            MERGE(dev->pad_ctlr_strength);
            /* PAD_N_STRENGTH_READ_BACK and PAD_P_STRENGTH_READ_BACK [7:0],
               PAD_TEST_OUT [17] and PAD_DUMMY_OUT [18] are read-only (RRG:
               PAD_CTLR_STRENGTH, p. 3-206 / PDF 224) and read 0 here. */
            dev->pad_ctlr_strength &= ~0x000600ff;
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_PC_MISC_CTL:
            MERGE(dev->pc_misc_ctl);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_VIDEOMUX_CNTL:
            MERGE(dev->videomux_cntl);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_SURFACE_DELAY:
            MERGE(dev->surface_delay);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_AGP_CNTL_B:
            MERGE(dev->agp_cntl_b);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_GUI_DEBUG0:
            MERGE(dev->gui_debug0);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_GUI_STAT:
            rage128_cap_early_reg(dev, off, val, mask);
            return; /* read-only */
        case RAGE128_PC_GUI_MODE:
            MERGE(dev->pc_gui_mode);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_BM_CHUNK_0_VAL:
        case RAGE128_BM_CHUNK_1_VAL:
            MERGE(dev->bm_chunk_val[(off - RAGE128_BM_CHUNK_0_VAL) >> 2]);
            rage128_cap_early_reg(dev, off, val, mask);
            return;
        case RAGE128_CONFIG_APER_0_BASE:
        case RAGE128_CONFIG_APER_1_BASE:
        case RAGE128_CONFIG_APER_SIZE:
        case RAGE128_CONFIG_REG_1_BASE:
        case RAGE128_CONFIG_REG_APER_SIZE:
        case RAGE128_CONFIG_MEMSIZE_EMB:
        case RAGE128_BM_QUEUE_FREE_STATUS:
            rage128_cap_early_reg(dev, off, val, mask);
            return; /* read-only */
#undef MERGE

        default:
            break;
    }

    /* SURFACE0-3 CPU translation windows, laid out as in the read path.
       A write to any of the twelve recomputes surf_xlate_on, which lets
       the framebuffer handlers skip translation while no window has a
       pitch. */
    if (off >= RAGE128_SURFACE0_LOWER_BOUND && off <= RAGE128_SURFACE3_INFO
        && (off & 0x0c) != 0) {
        uint32_t  n = (off - 0x0b00) >> 4;
        uint32_t *r;
        uint32_t  m;

        switch (off & 0x0c) {
            case 0x04:
                r = &dev->surf_lower[n];
                m = RAGE128_SURF_BOUND_MASK;
                break;
            case 0x08:
                r = &dev->surf_upper[n];
                m = RAGE128_SURF_BOUND_MASK;
                break;
            default:
                r = &dev->surf_info[n];
                m = RAGE128_SURF_INFO_MASK;
                break;
        }
        *r = ((*r & ~mask) | val) & m;

        dev->surf_xlate_on = 0;
        for (n = 0; n < 4; n++)
            if (rage128_surf_pitch[dev->surf_info[n] & RAGE128_SURF_INFO_MASK])
                dev->surf_xlate_on = 1;
        rage128_cap_early_reg(dev, off, val, mask);
        return;
    }

    /* The GUI registers at 0x1400-0x1fff are FIFOed: on the chip a write
       to them goes through the command FIFO behind earlier commands
       (RRG: Memory Mapping, Figure 2-1, p. 2-4 / PDF 14). The device
       keeps that order by draining the CCE thread's pending work first
       and then applying the write directly, on the CPU thread. Queuing
       the write to the CCE thread instead would let the CPU thread and
       the CCE thread change engine state at the same time. Writes made
       by the CCE stream skip this, because they are already executing in
       order on the CCE thread. */
    if (off >= 0x1400 && off < 0x2000 && !rage128_on_cce_thread && dev->cce_thread) {
        /* WAIT_UNTIL is the one exception. Its EVENT_CRTC_OFFSET stall
           holds the command FIFO until the last CRTC_OFFSET written is
           being displayed (RRG: WAIT_UNTIL, p. 3-239 / PDF 257), and it
           has to run on the CCE thread so that later ring draws wait
           behind a pending page flip while the CPU keeps running. The
           Windows 98 Direct3D driver is observed to present frames this
           way. A partial write, or a full queue, falls back to the drain
           and a direct write. */
        if (off == RAGE128_WAIT_UNTIL && mask == 0xffffffffu
            && rage128_pm4_enqueue_write(dev, off, val))
            return;
        rage128_pm4_drain_wait(dev);
    }

    /* PM4_FIFO_DATA_EVEN / PM4_FIFO_DATA_ODD: a write accepted as PIO
       input to the CCE is recorded in the capture as J dwords when it
       executes, so it is not recorded here. */
    if (rage128_pm4_fifo_data_write(dev, off, val, mask))
        return;

    /* Capture tap: direct CPU writes only. Writes from the CCE stream run
       on the CCE thread, and an indirect-buffer parse sets
       rage128_in_indirect on whichever thread runs it; both are already
       in the stream as D or J dwords. The parse test has to be the
       thread-local rage128_in_indirect: the shared pm4_ind_busy is also
       set while the CCE thread parses, and testing it would drop CPU
       writes made during that time. The tap sits after the drain, so the
       record order matches execution order, and after the WAIT_UNTIL
       queue path, so a queued write is not captured twice. */
    if (dev->cap_file && !rage128_on_cce_thread && !rage128_in_indirect) {
        fprintf(dev->cap_file, "R %04x %08x %08x\n", off, val, mask);
        rage128_cap_tick(dev);
    }

    /* Display block: PLL / CRTC / DAC / palette. */
    if (rage128_display_reg_write(dev, off, val, mask))
        return;
    /* OV0 hardware video overlay (vid_ati_rage128_ov0.c). */
    if (rage128_ov0_reg_write(dev, off, val, mask))
        return;
    /* DVD subpicture block (vid_ati_rage128_ov0.c). */
    if (rage128_subpic_reg_write(dev, off, val, mask))
        return;
    /* 2D GUI engine context (vid_ati_rage128_2d.c). */
    if (rage128_2d_reg_write(dev, off, val, mask))
        return;
    /* PM4/CCE command processor + PCI GART (vid_ati_rage128_pm4.c). */
    if (rage128_pm4_reg_write(dev, off, val, mask))
        return;
    /* 3D engine CCE context (vid_ati_rage128_3d.c). */
    if (rage128_3d_reg_write(dev, off, val, mask))
        return;
    /* MPEG-2 macroblock assist (vid_ati_rage128_mpeg.c). */
    if (rage128_mpeg_reg_write(dev, off, val, mask))
        return;

    if (off >= RAGE128_CONFIG_MIRROR_BASE && off <= RAGE128_CONFIG_MIRROR_END)
        return; /* read-only mirror */
}

/* A PM4 type-0 packet can write any register, so the packet executor
   gets the same full register-file entry the CPU uses. */
void
rage128_reg_poke(rage128_t *dev, uint32_t off, uint32_t val)
{
    rage128_reg_write(dev, off, val, 0xffffffffu);
}

/* --- MMIO BAR2: direct register access, BAR offset = register. ---
   With R128_MMIO_PROF set, each access's wall time is added to
   mmr_win_ns (reads) or mmw_win_ns (writes), to measure how much of the
   emulation thread's time the register file takes. A write's time
   includes whatever it triggers, such as a ring pump after a write
   pointer update or an engine drain. The I/O BAR is not timed: drivers
   are observed to stop using it after initialization. */

/* BAR2 is 16 KB and holds two identical 8 KB register apertures (RRG:
   Description of Mapped Memory Apertures, p. 2-5 / PDF 15; RRG:
   CONFIG_REG_APER_SIZE, p. 3-202 / PDF 220), so bit 13 of the BAR offset
   selects a copy, not a register. The fold is done here rather than in
   rage128_reg_read32 and rage128_reg_write because those also take
   MM_INDEX addresses, which are register offsets, not BAR offsets. */
#define RAGE128_REG_OFF(dev, addr) (((addr) - (dev)->mmio_base) & RAGE128_REG_APER_MASK)

static uint8_t
rage128_mmio_readb(uint32_t addr, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint32_t   off = RAGE128_REG_OFF(dev, addr);
    uint64_t   t0  = dev->mmio_prof ? rage128_now_ns() : 0;
    uint8_t    r   = rage128_reg_read32(dev, off) >> ((off & 3) * 8);

    if (dev->mmio_prof)
        dev->mmr_win_ns += rage128_now_ns() - t0;
    cycles -= dev->svga.monitor->mon_video_timing_read_b;
    return r;
}

static uint16_t
rage128_mmio_readw(uint32_t addr, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint32_t   off = RAGE128_REG_OFF(dev, addr);
    uint64_t   t0  = dev->mmio_prof ? rage128_now_ns() : 0;
    uint16_t   r   = rage128_reg_read32(dev, off) >> ((off & 2) * 8);

    if (dev->mmio_prof)
        dev->mmr_win_ns += rage128_now_ns() - t0;
    cycles -= dev->svga.monitor->mon_video_timing_read_w;
    return r;
}

static uint32_t
rage128_mmio_readl(uint32_t addr, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint64_t   t0  = dev->mmio_prof ? rage128_now_ns() : 0;
    uint32_t   r   = rage128_reg_read32(dev, RAGE128_REG_OFF(dev, addr));

    if (dev->mmio_prof)
        dev->mmr_win_ns += rage128_now_ns() - t0;
    cycles -= dev->svga.monitor->mon_video_timing_read_l;
    return r;
}

static void
rage128_mmio_writeb(uint32_t addr, uint8_t val, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint32_t   off = RAGE128_REG_OFF(dev, addr);
    uint32_t   sh  = (off & 3) * 8;
    uint64_t   t0  = dev->mmio_prof ? rage128_now_ns() : 0;

    rage128_reg_write(dev, off, (uint32_t) val << sh, 0xffu << sh);
    if (dev->mmio_prof)
        dev->mmw_win_ns += rage128_now_ns() - t0;
}

static void
rage128_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint32_t   off = RAGE128_REG_OFF(dev, addr);
    uint32_t   sh  = (off & 2) * 8;
    uint64_t   t0  = dev->mmio_prof ? rage128_now_ns() : 0;

    rage128_reg_write(dev, off, (uint32_t) val << sh, 0xffffu << sh);
    if (dev->mmio_prof)
        dev->mmw_win_ns += rage128_now_ns() - t0;
}

static void
rage128_mmio_writel(uint32_t addr, uint32_t val, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint64_t   t0  = dev->mmio_prof ? rage128_now_ns() : 0;

    rage128_reg_write(dev, RAGE128_REG_OFF(dev, addr), val, 0xffffffffu);
    if (dev->mmio_prof)
        dev->mmw_win_ns += rage128_now_ns() - t0;
}

/* Byte-swapped view of the register aperture, selected by
   CONFIG_CNTL.APER_REG_ENDIAN, whose only swap mode is "32 bpp swapping"
   (RRG: CONFIG_CNTL, p. 3-10 / PDF 28). It uses the same 32-bit byte
   permutation as the framebuffer views, but keeps the access width: one
   CPU access is one register access, so a register's read or write side
   effects happen exactly once. */
static uint8_t
rage128_mmio_readb_sw(uint32_t addr, void *priv)
{
    return rage128_mmio_readb(addr ^ 3, priv);
}

static uint16_t
rage128_mmio_readw_sw(uint32_t addr, void *priv)
{
    return (uint16_t) __builtin_bswap16(rage128_mmio_readw(addr ^ 2, priv));
}

static uint32_t
rage128_mmio_readl_sw(uint32_t addr, void *priv)
{
    return __builtin_bswap32(rage128_mmio_readl(addr, priv));
}

static void
rage128_mmio_writeb_sw(uint32_t addr, uint8_t val, void *priv)
{
    rage128_mmio_writeb(addr ^ 3, val, priv);
}

static void
rage128_mmio_writew_sw(uint32_t addr, uint16_t val, void *priv)
{
    rage128_mmio_writew(addr ^ 2, (uint16_t) __builtin_bswap16(val), priv);
}

static void
rage128_mmio_writel_sw(uint32_t addr, uint32_t val, void *priv)
{
    rage128_mmio_writel(addr, __builtin_bswap32(val), priv);
}

/* Install the handler sets the CONFIG_CNTL endian fields select (RRG:
   CONFIG_CNTL, p. 3-10 / PDF 28). APER_1_ENDIAN applies to the AGP image
   window, because the device maps linear aperture 1 as BAR0's upper
   half, the address CONFIG_APER_1_BASE reports. Value 3 is not defined
   and leaves an aperture unswapped. The I/O BAR is never swapped: the
   field names the register aperture, and the guide says nothing of I/O
   space. */
static void
rage128_endian_apply(rage128_t *dev)
{
    switch (dev->config_cntl & 3) {
        case 1:
            mem_mapping_set_handler(&dev->lfb_mapping,
                                    rage128_lfb_readb_sw16, rage128_lfb_readw_sw16, rage128_lfb_readl_sw16,
                                    rage128_lfb_writeb_sw16, rage128_lfb_writew_sw16, rage128_lfb_writel_sw16);
            break;
        case 2:
            mem_mapping_set_handler(&dev->lfb_mapping,
                                    rage128_lfb_readb_sw32, rage128_lfb_readw_sw32, rage128_lfb_readl_sw32,
                                    rage128_lfb_writeb_sw32, rage128_lfb_writew_sw32, rage128_lfb_writel_sw32);
            break;
        default: /* 0 = little endian; 3 undefined, kept unswapped */
            mem_mapping_set_handler(&dev->lfb_mapping,
                                    rage128_lfb_readb, rage128_lfb_readw, rage128_lfb_readl,
                                    rage128_lfb_writeb, rage128_lfb_writew, rage128_lfb_writel);
            break;
    }
    switch ((dev->config_cntl >> 2) & 3) {
        case 1:
            mem_mapping_set_handler(&dev->agp_mapping,
                                    rage128_agp_readb_sw16, rage128_agp_readw_sw16, rage128_agp_readl_sw16,
                                    rage128_agp_writeb_sw16, rage128_agp_writew_sw16, rage128_agp_writel_sw16);
            break;
        case 2:
            mem_mapping_set_handler(&dev->agp_mapping,
                                    rage128_agp_readb_sw32, rage128_agp_readw_sw32, rage128_agp_readl_sw32,
                                    rage128_agp_writeb_sw32, rage128_agp_writew_sw32, rage128_agp_writel_sw32);
            break;
        default:
            mem_mapping_set_handler(&dev->agp_mapping,
                                    rage128_agp_readb, rage128_agp_readw, rage128_agp_readl,
                                    rage128_agp_writeb, rage128_agp_writew, rage128_agp_writel);
            break;
    }
    if (dev->config_cntl & (1 << 4))
        mem_mapping_set_handler(&dev->mmio_mapping,
                                rage128_mmio_readb_sw, rage128_mmio_readw_sw, rage128_mmio_readl_sw,
                                rage128_mmio_writeb_sw, rage128_mmio_writew_sw, rage128_mmio_writel_sw);
    else
        mem_mapping_set_handler(&dev->mmio_mapping,
                                rage128_mmio_readb, rage128_mmio_readw, rage128_mmio_readl,
                                rage128_mmio_writeb, rage128_mmio_writew, rage128_mmio_writel);
}

/* --- I/O BAR1: the register file's offsets 0x0000-0x00ff, the same
   registers as in BAR2 (RRG: Description of Mapped Memory Apertures,
   Table 2-1, p. 2-5 / PDF 15). --- */
static uint32_t
rage128_io_inl(uint16_t port, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint32_t   v   = rage128_reg_read32(dev, (uint32_t) (port - dev->io_base) & 0xff);
    return v;
}

static uint8_t
rage128_io_inb(uint16_t port, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint32_t   off = (uint32_t) (port - dev->io_base) & 0xff;
    uint8_t    v   = rage128_reg_read32(dev, off) >> ((off & 3) * 8);
    return v;
}

static uint16_t
rage128_io_inw(uint16_t port, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint32_t   off = (uint32_t) (port - dev->io_base) & 0xff;
    uint16_t   v   = rage128_reg_read32(dev, off) >> ((off & 2) * 8);
    return v;
}

static void
rage128_io_outl(uint16_t port, uint32_t val, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    rage128_reg_write(dev, (uint32_t) (port - dev->io_base) & 0xff, val, 0xffffffffu);
}

static void
rage128_io_outb(uint16_t port, uint8_t val, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint32_t   off = (uint32_t) (port - dev->io_base) & 0xff;
    uint32_t   sh  = (off & 3) * 8;
    rage128_reg_write(dev, off, (uint32_t) val << sh, 0xffu << sh);
}

static void
rage128_io_outw(uint16_t port, uint16_t val, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;
    uint32_t   off = (uint32_t) (port - dev->io_base) & 0xff;
    uint32_t   sh  = (off & 2) * 8;
    rage128_reg_write(dev, off, (uint32_t) val << sh, 0xffffu << sh);
}

static void
rage128_io_remove(rage128_t *dev)
{
    if (dev->io_mapped) {
        io_removehandler(dev->io_mapped, RAGE128_IO_SIZE,
                         rage128_io_inb, rage128_io_inw, rage128_io_inl,
                         rage128_io_outb, rage128_io_outw, rage128_io_outl, dev);
        dev->io_mapped = 0;
    }
}

static void
rage128_io_set(rage128_t *dev)
{
    rage128_io_remove(dev);
    if ((dev->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO) && dev->io_base) {
        io_sethandler(dev->io_base, RAGE128_IO_SIZE,
                      rage128_io_inb, rage128_io_inw, rage128_io_inl,
                      rage128_io_outb, rage128_io_outw, rage128_io_outl, dev);
        dev->io_mapped = dev->io_base;
    }
}

/* Legacy VGA I/O decode, on only while CONFIG_CNTL.CFG_VGA_IO_DIS is
   clear and the VGA_DISABLE strap is off (RRG: CONFIG_CNTL, p. 3-10 /
   PDF 28). The 3Bx monochrome range follows miscout bit 0. The svga core
   also installs that range itself on a 3C2 write, so the removal covers
   all of 3A0-3DF; otherwise a monochrome handler could survive a
   disable. */
static void
rage128_vga_decode_update(rage128_t *dev)
{
    io_removehandler(0x03a0, 0x0040,
                     rage128_vga_in, NULL, NULL,
                     rage128_vga_out, NULL, NULL, dev);
    if (!dev->vga_disabled && !(dev->config_cntl & RAGE128_CFG_VGA_IO_DIS)) {
        if (!(dev->svga.miscout & 0x01))
            io_sethandler(0x03a0, 0x0020,
                          rage128_vga_in, NULL, NULL,
                          rage128_vga_out, NULL, NULL, dev);
        io_sethandler(0x03c0, 0x0020,
                      rage128_vga_in, NULL, NULL,
                      rage128_vga_out, NULL, NULL, dev);
    }
}

/* ------------------------------------------------------------------ */
/* Aperture mapping.                                                   */
/* ------------------------------------------------------------------ */
static void
rage128_update_mappings(rage128_t *dev)
{
    if (!(dev->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)) {
        mem_mapping_disable(&dev->lfb_mapping);
        mem_mapping_disable(&dev->agp_mapping);
        mem_mapping_disable(&dev->mmio_mapping);
        return;
    }

    if (dev->lfb_base) {
        /* The fitted VRAM is mapped at the bottom of the 64 MB BAR0. The
           upper 32 MB is the AGP image window, whose handlers forward to
           the chipset's AGP aperture; until AGP_BASE is programmed they
           reach no bus. */
        mem_mapping_set_addr(&dev->lfb_mapping, dev->lfb_base, dev->vram_size);
        mem_mapping_set_addr(&dev->agp_mapping, dev->lfb_base + R128_CARD_AGP_HALF,
                             R128_CARD_AGP_HALF);
    } else {
        mem_mapping_disable(&dev->lfb_mapping);
        mem_mapping_disable(&dev->agp_mapping);
    }

    if (dev->mmio_base)
        mem_mapping_set_addr(&dev->mmio_mapping, dev->mmio_base, RAGE128_MMIO_SIZE);
    else
        mem_mapping_disable(&dev->mmio_mapping);
}

/* ------------------------------------------------------------------ */
/* PCI configuration space.                                            */
/* ------------------------------------------------------------------ */
static uint8_t rage128_pci_read_reg(int addr, const rage128_t *dev);

static uint8_t
rage128_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    const rage128_t *dev = (rage128_t *) priv;
    uint8_t          val = rage128_pci_read_reg(addr, dev);

    return val;
}

static uint8_t
rage128_pci_read_reg(int addr, const rage128_t *dev)
{
    switch (addr) {
        case PCI_REG_VENDOR_ID_L:
            return RAGE128_PCI_VENDOR & 0xff;
        case PCI_REG_VENDOR_ID_H:
            return RAGE128_PCI_VENDOR >> 8;
        case PCI_REG_DEVICE_ID_L:
            return dev->pci_device_id & 0xff;
        case PCI_REG_DEVICE_ID_H:
            return dev->pci_device_id >> 8;
        case PCI_REG_COMMAND:
            /* AD_STEPPING [7] is read-only 1 (RRG: COMMAND, p. 3-3 /
               PDF 21). */
            return dev->pci_regs[PCI_REG_COMMAND] | 0x80;
        case PCI_REG_COMMAND_H:
            /* Of the high byte only FAST_B2B_EN [9] is read-write;
               SERR_EN [8] and the bits above are read-only 0 (RRG:
               COMMAND, p. 3-3 / PDF 21). */
            return dev->pci_regs[PCI_REG_COMMAND_H] & PCI_COMMAND_H_FAST_B2B;
        case PCI_REG_STATUS_L:
            /* CAP_LIST, PCI_66_EN and FAST_BACK_CAPABLE, all read-only
               with default 1 (RRG: STATUS, pp. 3-3-3-4 / PDF 21-22). The
               guide does not describe PCI_66_EN; the device clears it on
               the PCI board, modeled as a 33 MHz part, and keeps the
               default on the AGP board. */
            if (dev->is_agp)
                return PCI_STATUS_L_CAPAB | PCI_STATUS_L_66MHZ | PCI_STATUS_L_FAST_B2B;
            return PCI_STATUS_L_CAPAB | PCI_STATUS_L_FAST_B2B;
        case PCI_REG_STATUS_H:
            return PCI_DEVSEL_MEDIUM; /* DEVSEL_TIMING = 01 (RRG: STATUS, p. 3-4 / PDF 22) */
        case PCI_REG_CACHELINE_SIZE:
        case PCI_REG_LATENCY_TIMER:
            /* Read/write, 8 bits, default 0 (RRG: CACHE_LINE and LATENCY,
               p. 3-5 / PDF 23). */
            return dev->pci_regs[addr];
        case PCI_REG_REVISION:
            return 0x00;
        case PCI_REG_PROG_IF:
            return 0x00;
        case PCI_REG_SUBCLASS:
            /* The guide's SUB_CLASS has one field, SUB_CLASS_INF [7],
               with default 1 and no description (RRG: SUB_CLASS, p. 3-5 /
               PDF 23). The device ties that bit to the VGA_DISABLE strap:
               a primary board reads 0x00, a VGA-compatible controller,
               and a VGA_DISABLE-strapped board reads 0x80, other display
               controller, the PCI class code for a display device that
               is not a VGA, so a BIOS assigns it resources instead of
               skipping it as a second VGA. The tie to the strap is
               modeled. */
            return dev->vga_disabled ? 0x80 : 0x00;
        case PCI_REG_CLASS:
            return 0x03; /* display controller */
        case 0x0e:
            return 0x00; /* header type */
        /* BAR0: the 64 MB linear framebuffer aperture, lower 32 MB local
           VRAM and upper 32 MB the AGP image window. PREFETCH_EN [3]
           reads 1 and MEM_BASE decodes bits 31:26 (RRG: MEM_BASE,
           p. 3-6 / PDF 24). */
        case PCI_REG_BAR0_BYTE0:
            return 0x08;
        case PCI_REG_BAR0_BYTE1:
        case PCI_REG_BAR0_BYTE2:
            return 0x00;
        case PCI_REG_BAR0_BYTE3:
            return (dev->lfb_base >> 24) & 0xff;
        /* BAR1: the 256-byte I/O block; BLOCK_IO_BIT reads 1 and IO_BASE
           decodes bits 31:8 (RRG: IO_BASE, p. 3-7 / PDF 25). */
        case PCI_REG_BAR1_BYTE0:
            return 0x01;
        case PCI_REG_BAR1_BYTE1:
            return (dev->io_base >> 8) & 0xff;
        case PCI_REG_BAR1_BYTE2:
            return (dev->io_base >> 16) & 0xff;
        case PCI_REG_BAR1_BYTE3:
            return (dev->io_base >> 24) & 0xff;
        /* BAR2: the 16 KB memory-mapped register block; REG_BASE decodes
           bits 31:14 (RRG: REG_BASE, p. 3-7 / PDF 25). */
        case 0x18:
            return 0x00;
        case 0x19:
            return (dev->mmio_base >> 8) & 0xff;
        case 0x1a:
            return (dev->mmio_base >> 16) & 0xff;
        case 0x1b:
            return (dev->mmio_base >> 24) & 0xff;
        /* ADAPTER_ID, the subsystem vendor and id: read-only here (RRG:
           ADAPTER_ID, p. 3-7 / PDF 25), loaded from the ROM straps at
           reset and by writes to the ADAPTER_ID_W alias at 0x4c. */
        case 0x2c:
            return dev->pci_subsys_vendor & 0xff;
        case 0x2d:
            return dev->pci_subsys_vendor >> 8;
        case 0x2e:
            return dev->pci_subsys_id & 0xff;
        case 0x2f:
            return dev->pci_subsys_id >> 8;
        /* Expansion ROM BAR (RRG: BIOS_ROM, p. 3-8 / PDF 26). With no
           flash behind the chip's ROM interface it is hardwired to zero:
           a size probe then reads back 0, which is how a PCI device says
           it has no expansion ROM, so firmware neither maps nor runs one
           for it. */
        case PCI_REG_ROM_BAR_BYTE0:
            return dev->has_bios_rom ? (dev->pci_regs[PCI_REG_ROM_BAR_BYTE0] & 0x01) : 0x00;
        case PCI_REG_ROM_BAR_BYTE1:
            return 0x00;
        case PCI_REG_ROM_BAR_BYTE2:
            return dev->has_bios_rom ? dev->pci_regs[PCI_REG_ROM_BAR_BYTE2] : 0x00;
        case PCI_REG_ROM_BAR_BYTE3:
            return dev->has_bios_rom ? dev->pci_regs[PCI_REG_ROM_BAR_BYTE3] : 0x00;
        case PCI_REG_CAPS_PTR:
            /* AGP board: the AGP capability at 0x50, whose NEXT_PTR leads
               to the power management capability at 0x5c, the end of the
               list (RRG: CAPABILITIES_ID, p. 3-9 / PDF 27; RRG:
               PMI_NXT_CAP_PTR, p. 3-189 / PDF 207). PCI board: the list
               starts at 0x5c and has no AGP capability. The guide gives
               CAP_PTR a default of 0 (RRG: CAPABILITIES_PTR, p. 3-8 /
               PDF 26); the values here follow lspci listings of real
               boards, cited next to RAGE128_PCI_CAP_PTR in
               vid_ati_rage128_regs.h. */
            return dev->is_agp ? RAGE128_PCI_CAP_PTR : RAGE128_AGP_NEXT_PTR;
        case PCI_REG_INT_LINE:
            return dev->pci_regs[PCI_REG_INT_LINE];
        case PCI_REG_INT_PIN:
            return PCI_INTA;
        case PCI_REG_MIN_GRANT:
            return 0x08; /* RRG: MIN_GRANT, p. 3-9 / PDF 27 */
        case PCI_REG_MAX_LAT:
            return 0x00; /* RRG: MAX_LATENCY, p. 3-9 / PDF 27 */

        /* AGP capability, AGP board only; the PCI board has no AGP block
           and this range reads 0. ID, next pointer and revision: RRG:
           CAPABILITIES_ID, pp. 3-9-3-10 / PDF 27-28. Status and command:
           RRG: AGP_STATUS and AGP_COMMAND, p. 3-185 / PDF 203. */
        case 0x50:
            return dev->is_agp ? RAGE128_AGP_CAP_ID : 0x00;
        case 0x51:
            return dev->is_agp ? RAGE128_AGP_NEXT_PTR : 0x00;
        case 0x52:
            return dev->is_agp ? RAGE128_AGP_REV : 0x00;
        case 0x53:
            return 0x00;
        case 0x54:
        case 0x55:
        case 0x56:
        case 0x57:
            if (!dev->is_agp)
                return 0x00;
            return RAGE128_AGP_STATUS >> ((addr - 0x54) << 3);
        case 0x58:
        case 0x59:
        case 0x5a:
        case 0x5b:
            if (!dev->is_agp)
                return 0x00;
            /* SBA_EN [9] is read-only 1 (RRG: AGP_COMMAND, p. 3-185 /
               PDF 203). */
            return (dev->pci_regs[addr] | (RAGE128_AGP_COMMAND_SBA_EN >> ((addr - 0x58) << 3))) & 0xff;

        /* Power management capability (RRG: PMI_CAP_ID to PMI_DATA,
           pp. 3-189-3-190 / PDF 207-208). */
        case 0x5c:
            return RAGE128_PMI_CAP_ID;
        case 0x5d:
            return 0x00; /* end of chain */
        case 0x5e:
            return RAGE128_PMI_PMC & 0xff;
        case 0x5f:
            return RAGE128_PMI_PMC >> 8;
        case 0x60:
            /* PMI_PMCSR_REG: only PMI_POWER_STATE [1:0] is read/write;
               the rest reads 0. */
            return dev->pci_regs[addr] & 0x03;
        case 0x61:
        case 0x62:
        case 0x63:
            return 0x00; /* PME and data fields, and PMI_DATA, read 0 */

        default:
            return 0x00;
    }
}

static void
rage128_pci_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    switch (addr) {
        case PCI_REG_COMMAND:
            /* The writable bits of the low byte: IO_ACCESS_EN,
               MEM_ACCESS_EN, BUS_MASTER_EN and PAL_SNOOP_EN (RRG:
               COMMAND, pp. 3-2-3-3 / PDF 20-21). */
            dev->pci_regs[PCI_REG_COMMAND] = val & 0x27;
            rage128_update_mappings(dev);
            rage128_io_set(dev);
            /* Setting BUS_MASTER_EN lets a ring armed while bus
               mastering was blocked start fetching. */
            if (dev->cce_thread)
                rage128_pm4_kick(dev);
            break;
        case PCI_REG_COMMAND_H:
            /* FAST_B2B_EN [9] is the only writable bit of the high byte
               (RRG: COMMAND, p. 3-3 / PDF 21). It changes no decode, so
               the mappings and the executor are left alone. */
            dev->pci_regs[PCI_REG_COMMAND_H] = val & PCI_COMMAND_H_FAST_B2B;
            break;
        case PCI_REG_CACHELINE_SIZE:
        case PCI_REG_LATENCY_TIMER:
            /* Read/write, 8 bits (RRG: CACHE_LINE and LATENCY, p. 3-5 /
               PDF 23). */
            dev->pci_regs[addr] = val;
            break;
        case PCI_REG_BAR0_BYTE3:
            dev->lfb_base = ((uint32_t) val << 24) & ~(RAGE128_LFB_SIZE - 1u);
            rage128_update_mappings(dev);
            break;
        case PCI_REG_BAR1_BYTE1:
            dev->io_base = (dev->io_base & 0xffff00ff) | ((uint32_t) val << 8);
            dev->io_base &= 0xffffff00;
            rage128_io_set(dev);
            break;
        case PCI_REG_BAR1_BYTE2:
            dev->io_base = (dev->io_base & 0xff00ffff) | ((uint32_t) val << 16);
            dev->io_base &= 0xffffff00;
            rage128_io_set(dev);
            break;
        case PCI_REG_BAR1_BYTE3:
            dev->io_base = (dev->io_base & 0x00ffffff) | ((uint32_t) val << 24);
            dev->io_base &= 0xffffff00;
            rage128_io_set(dev);
            break;
        case 0x19:
            dev->mmio_base = (dev->mmio_base & 0xffff00ff) | ((uint32_t) val << 8);
            dev->mmio_base &= 0xffffc000;
            rage128_update_mappings(dev);
            break;
        case 0x1a:
            dev->mmio_base = (dev->mmio_base & 0xff00ffff) | ((uint32_t) val << 16);
            dev->mmio_base &= 0xffffc000;
            rage128_update_mappings(dev);
            break;
        case 0x1b:
            dev->mmio_base = (dev->mmio_base & 0x00ffffff) | ((uint32_t) val << 24);
            dev->mmio_base &= 0xffffc000;
            rage128_update_mappings(dev);
            break;
        case PCI_REG_ROM_BAR_BYTE0:
        case PCI_REG_ROM_BAR_BYTE2:
        case PCI_REG_ROM_BAR_BYTE3:
            /* BIOS_ROM bits 16:1 are reserved and BIOS_BASE_ADDR is
               31:17 (RRG: BIOS_ROM, p. 3-8 / PDF 26), so bit 16, byte 2
               bit 0, is not stored. An all-ones size probe reads back
               0xfffe0000 in the address bits, a 128 KB decode, and the
               base is 128 KB aligned. The mapped window stays 64 KB, the
               size of the ROM image. */
            if (!dev->has_bios_rom)
                return;
            dev->pci_regs[addr] = (addr == PCI_REG_ROM_BAR_BYTE2) ? (val & 0xfe) : val;
            if (dev->pci_regs[PCI_REG_ROM_BAR_BYTE0] & 0x01) {
                uint32_t romaddr = ((uint32_t) dev->pci_regs[PCI_REG_ROM_BAR_BYTE3] << 24) | ((uint32_t) dev->pci_regs[PCI_REG_ROM_BAR_BYTE2] << 16);
                mem_mapping_set_addr(&dev->bios_rom.mapping, romaddr, RAGE128_ROM_SIZE);
                mem_mapping_enable(&dev->bios_rom.mapping);
            } else
                mem_mapping_disable(&dev->bios_rom.mapping);
            break;
        case PCI_REG_INT_LINE:
            dev->pci_regs[PCI_REG_INT_LINE] = val;
            break;
        case 0x4c:
        case 0x4d:
            /* ADAPTER_ID_W, the write-only alias of the read-only
               ADAPTER_ID at 0x2c (RRG: ADAPTER_ID_W, p. 3-9 / PDF 27). A
               system BIOS uses it to load the subsystem ids of a chip on
               the motherboard, whose straps live in the system BIOS
               (GCS: Standard Boot-up Sequence, configuration 2, p. 4-29 /
               PDF 77). */
            dev->pci_subsys_vendor = (addr == 0x4c)
                ? (dev->pci_subsys_vendor & 0xff00) | val
                : (dev->pci_subsys_vendor & 0x00ff) | ((uint16_t) val << 8);
            break;
        case 0x4e:
        case 0x4f:
            dev->pci_subsys_id = (addr == 0x4e)
                ? (dev->pci_subsys_id & 0xff00) | val
                : (dev->pci_subsys_id & 0x00ff) | ((uint16_t) val << 8);
            break;
        case 0x58:
        case 0x59:
        case 0x5a:
        case 0x5b:
            /* AGP_COMMAND: DATA_RATE [2:0], AGP_EN [8] and RQ_DEPTH
               [31:24] are writable; SBA_EN [9] is read-only and set on
               read (RRG: AGP_COMMAND, p. 3-185 / PDF 203). The PCI board
               has no AGP capability, so writes there are dropped. */
            if (dev->is_agp)
                dev->pci_regs[addr] = val & ((RAGE128_AGP_COMMAND_MASK >> ((addr - 0x58) << 3)) & 0xff);
            break;
        case 0x60:
            /* PMI_POWER_STATE [1:0]: "Writes of unsupported states are
               not accepted", and D2 is the unsupported one (RRG:
               PMI_PMCSR_REG, p. 3-190 / PDF 208; PMI_D2_SUPPORT is 0,
               RRG: PMI_PMC_REG, p. 3-190 / PDF 208). */
            val &= 0x03;
            if (val != RAGE128_PMI_POWER_STATE_D2)
                dev->pci_regs[addr] = val;
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle.                                                          */
/* ------------------------------------------------------------------ */
/* ROM-based straps: right after PCI reset the chip's ROM state machine
   reads the subsystem vendor and id from bytes 0x70-0x73 of the flash
   (GCS: Standard Boot-up Sequence, p. 4-29 / PDF 77; GCS: ROM Based
   Straps, Table 4-9, p. 4-35 / PDF 83). A multi-chip board can pass
   them in through subsys_strap for a chip with no flash of its own.
   With neither, the chip defaults them to 0x1002 and its own device id
   (GCS: Standard Boot-up Sequence, p. 4-30 / PDF 78), and a system BIOS
   may then load them through the ADAPTER_ID_W alias at 0x4c. */
static void
rage128_subsys_straps(rage128_t *dev)
{
    const uint8_t *rom = dev->has_bios_rom ? dev->bios_rom.rom : NULL;

    if (dev->subsys_strap) {
        dev->pci_subsys_vendor = dev->subsys_strap & 0xffff;
        dev->pci_subsys_id     = dev->subsys_strap >> 16;
    } else if (rom && dev->bios_rom.sz >= RAGE128_ROM_STRAP_SUBSYS + 4) {
        rom += RAGE128_ROM_STRAP_SUBSYS;
        dev->pci_subsys_vendor = rom[0] | (rom[1] << 8);
        dev->pci_subsys_id     = rom[2] | (rom[3] << 8);
    } else {
        dev->pci_subsys_vendor = RAGE128_PCI_VENDOR;
        dev->pci_subsys_id     = dev->pci_device_id;
    }
}

/* Reset: the chip-core registers return to the guide's per-field
   defaults (cited next to each constant in vid_ati_rage128_regs.h),
   except where a comment below says otherwise, and every block resets
   its own state. This is the device's reset hook, and init calls it
   once the BIOS ROM is loaded. */
static void
rage128_reset(void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    rage128_subsys_straps(dev);

    /* Quiesce the CCE thread before any block clears the state it
       executes against (no-op before the thread exists). */
    rage128_pm4_drain_wait(dev);

    dev->mm_index = 0;
    memset(dev->bios_scratch, 0, sizeof(dev->bios_scratch));

    /* The power-on contents of the fog table are not documented. It is
       filled with 0xff, which leaves pixels unfogged, until the driver
       uploads a table. */
    memset(dev->t3d.fog_table, 0xff, sizeof(dev->t3d.fog_table));
    dev->fog_table_wr_index = 0;

    /* Every interrupt enable resets to 0 (RRG: GEN_INT_CNTL,
       pp. 3-192-3-193 / PDF 210-211), so INTA is released. GUI_IDLE_INT
       resets to 1 and every other status bit to 0 (RRG: GEN_INT_STATUS,
       pp. 3-193-3-196 / PDF 211-214). */
    dev->gen_int_cntl   = 0;
    dev->gen_int_status = RAGE128_GIS_GUI_IDLE;
    atomic_store(&dev->gui_idle_event, 0);
    rage128_gen_int_update(dev);

    /* The stored read/write configuration bytes return to their
       defaults, all 0: CACHE_LINE and LATENCY (RRG: CACHE_LINE and
       LATENCY, p. 3-5 / PDF 23), AGP_COMMAND (RRG: AGP_COMMAND, p. 3-185 /
       PDF 203) and the PMI_PMCSR_REG power state (RRG: PMI_PMCSR_REG,
       p. 3-190 / PDF 208). */
    dev->pci_regs[PCI_REG_CACHELINE_SIZE] = 0x00;
    dev->pci_regs[PCI_REG_LATENCY_TIMER]  = 0x00;
    dev->pci_regs[0x58]                   = 0x00;
    dev->pci_regs[0x59]                   = 0x00;
    dev->pci_regs[0x5a]                   = 0x00;
    dev->pci_regs[0x5b]                   = 0x00;
    dev->pci_regs[0x60]                   = 0x00;

    /* COMMAND, the three BAR bases and the ROM BAR reset to 0, so the
       card decodes nothing until firmware enables it, and
       INTERRUPT_LINE resets to 0xff (RRG: COMMAND, p. 3-2 / PDF 20;
       RRG: MEM_BASE, IO_BASE and REG_BASE, pp. 3-6-3-7 / PDF 24-25;
       RRG: BIOS_ROM and INTERRUPT_LINE, p. 3-8 / PDF 26). The mapping
       updates after the stores remove the now-disabled decodes. */
    dev->pci_regs[PCI_REG_COMMAND]       = 0x00;
    dev->pci_regs[PCI_REG_COMMAND_H]     = 0x00;
    dev->pci_regs[PCI_REG_INT_LINE]      = 0xff;
    dev->pci_regs[PCI_REG_ROM_BAR_BYTE0] = 0x00;
    dev->pci_regs[PCI_REG_ROM_BAR_BYTE2] = 0x00;
    dev->pci_regs[PCI_REG_ROM_BAR_BYTE3] = 0x00;
    dev->lfb_base                        = 0;
    dev->io_base                         = 0;
    dev->mmio_base                       = 0;
    rage128_update_mappings(dev);
    rage128_io_set(dev);
    if (dev->has_bios_rom)
        mem_mapping_disable(&dev->bios_rom.mapping);

    dev->bus_cntl  = RAGE128_BUS_CNTL_DEFAULT;
    dev->bus_cntl1 = 0x00000000;
    /* CFG_VGA_IO_DIS [9] resets from the VGA_DISABLE strap (RRG:
       CONFIG_CNTL, p. 3-10 / PDF 28). */
    dev->config_cntl = dev->vga_disabled ? RAGE128_CFG_VGA_IO_DIS : 0x00000000;
    rage128_endian_apply(dev); /* endian fields reset to 0: unswapped handlers */
    dev->gen_reset_cntl = 0x00000000;
    /* CONFIG_MEMSIZE resets to the fitted VRAM size, not the guide's
       default of 0 (RRG: CONFIG_MEMSIZE, p. 3-12 / PDF 30). Drivers read
       it for the framebuffer size (xf86-video-r128 r128_driver.c
       R128PreInitConfig), and the video BIOS POST is observed never to
       write it, so it has to come up valid. */
    dev->config_memsize  = dev->vram_size & RAGE128_CONFIG_MEMSIZE_MASK;
    dev->test_debug_cntl = 0x00000000;
    dev->test_debug_mux  = 0x00000000;
    dev->hw_debug        = 0x00000000;
    dev->host_path_cntl  = RAGE128_HOST_PATH_CNTL_DEFAULT;
    dev->mem_cntl        = RAGE128_MEM_CNTL_DEFAULT;
    dev->ext_mem_cntl    = RAGE128_EXT_MEM_CNTL_DEFAULT;
    /* MEM_ADDR_CONFIG, the memory geometry: row, column and bank layout
       in MEM_ADDR_MAPPING [3:0], and MEM_BUS_WIDTH [8], 0 for 64 bits
       and 1 for 128 (RRG: MEM_ADDR_CONFIG, pp. 3-75-3-76 / PDF 93-94).
       The guide's default is 0; the device uses a board-specific reset
       value instead, because the video BIOS POST is observed to change
       only MEM_BUS_WIDTH, and the Windows 2000 miniport looks the POSTed
       value up in its geometry tables (RE: ati2mtaa.sys @408d0, with a
       second table at 40a00). With no matching row it sizes memory as 0
       and Windows 2000 falls back to VGA.
       AGP board, 128-bit, 32 MB: 0x0003012c, the top row of the 128-bit
       table. AGP board with 16 MB: the guide's default, 0.
       PCI board (XPERT 128), 64-bit: 0x0002002c, the 64-bit row that
       holds for both 16 MB and 32 MB; its video BIOS clears bit 8, which
       is already 0. */
    if (dev->is_agp)
        dev->mem_addr_config = (dev->vram_size == 0x02000000) ? 0x0003012c : 0x00000000;
    else
        dev->mem_addr_config = 0x0002002c;
    dev->mem_intf_cntl      = 0x00000000;
    dev->mem_str_cntl       = 0x00000000;
    dev->mem_init_lat_timer = RAGE128_MEM_INIT_LAT_DEFAULT;
    dev->mem_sdram_mode_reg = RAGE128_MEM_SDRAM_MODE_DEFAULT;
    dev->pad_ctlr_strength  = RAGE128_PAD_CTLR_STRENGTH_DEFAULT;
    dev->pc_misc_ctl        = 0x00000000;
    dev->videomux_cntl      = RAGE128_VIDEOMUX_CNTL_DEFAULT;
    dev->surface_delay      = RAGE128_SURFACE_DELAY_DEFAULT;
    memset(dev->surf_lower, 0, sizeof(dev->surf_lower));
    memset(dev->surf_upper, 0, sizeof(dev->surf_upper));
    memset(dev->surf_info, 0, sizeof(dev->surf_info));
    dev->surf_xlate_on       = 0;
    dev->agp_base            = 0x00000000;
    dev->agp_cntl            = RAGE128_AGP_CNTL_DEFAULT;
    dev->agp_cntl_b          = 0x00000000;
    dev->gui_debug0          = 0x00000000;
    dev->pc_gui_mode         = 0x00000000;
    dev->bm_chunk_val[0]     = RAGE128_BM_CHUNK_0_VAL_DEFAULT;
    dev->bm_chunk_val[1]     = RAGE128_BM_CHUNK_1_VAL_DEFAULT;
    dev->pm4_vc_debug_config = 0x00000000;
    /* GPIO_MONID: MONID_A, MONID_EN and MONID_MASK all reset to 0, and
       MONID_EN 0 is input mode (RRG: GPIO_MONID, pp. 3-196-3-197 /
       PDF 214-215), so both DDC lines are released to their pull-ups. */
    dev->gpio_monid = 0x00000000;
    i2c_gpio_set(dev->i2c, 1, 1);
    /* The AMCGPIO latches reset to 0, which leaves every pad an input.
       The board hook (amcgpio_notify) and the input levels (amcgpio_in)
       belong to the board that wired the pads and survive a chip
       reset. */
    dev->amcgpio_mask = 0x00000000;
    dev->amcgpio_a    = 0x00000000;
    dev->amcgpio_en   = 0x00000000;

    rage128_display_reset(dev);
    rage128_ov0_reset(dev);
    rage128_2d_reset(dev);
    rage128_pm4_reset(dev);
    rage128_3d_reset(dev);
    rage128_mpeg_reset(dev);

    rage128_vga_decode_update(dev);
}

/* Create one chip. With cfg NULL it is a single-chip board whose straps
   come from the device flags and instance number; a multi-chip board
   passes each chip's straps in cfg (vid_ati_rage128_maxx.c). */
rage128_t *
rage128_chip_init(const device_t *info, const rage128_chip_cfg_t *cfg)
{
    rage128_t *dev  = calloc(1, sizeof(rage128_t));
    svga_t    *svga = &dev->svga;

    dev->is_agp = !!(info->flags & DEVICE_AGP);
    if (dev->is_agp) {
        dev->pci_device_id = RAGE128_PCI_DEVICE_PF;
        dev->ref_freq_hz   = RAGE128_REF_FREQ_AGP_HZ;
    } else {
        dev->pci_device_id = RAGE128_PCI_DEVICE_RE;
        dev->ref_freq_hz   = RAGE128_REF_FREQ_PCI_HZ;
    }

    /* A board installed as a secondary video card (86Box adds video
       cards as instance 1, 2 and so on) is strapped vga_disable, so it is
       "not recognized as the system's VGA controller, but only as an
       extended mode controller" (GCS: External Straps, p. 4-32 /
       PDF 80): no legacy VGA I/O or A0000-BFFFF decode, and the driver
       reaches it through its BARs only. */
    dev->vga_disabled = cfg ? cfg->vga_disabled : (device_get_instance() > 1);
    dev->has_bios_rom = cfg ? (cfg->rom_path != NULL) : 1;
    if (cfg) {
        dev->subsys_strap = cfg->subsys_strap;
        dev->cap_tag      = cfg->cap_tag;
    }

    /* Fitted local VRAM, 16 or 32 MB, default 32. The chip "supports up
       to 32MB of frame buffer memory" (RRG: Description of Mapped Memory
       Apertures, p. 2-6 / PDF 16), the reach of its 25-bit surface
       offsets. The option changes the backed VRAM and CONFIG_MEMSIZE
       only: BAR0 stays 64 MB (32 MB local, 32 MB AGP image window) and
       CONFIG_APER_SIZE stays 32 MB. */
    {
        int cfg_vram_mb = device_get_config_int("memory");
        if (cfg_vram_mb != 16 && cfg_vram_mb != 32)
            cfg_vram_mb = 32;
        dev->vram_size = (uint32_t) cfg_vram_mb << 20;
    }
    dev->vram_mask = dev->vram_size - 1;

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_rage128);

    svga_init(info, svga, dev, dev->vram_size,
              rage128_recalctimings,
              rage128_vga_in, rage128_vga_out,
              rage128_hwcursor_draw, rage128_overlay_draw);
    svga->vsync_callback = rage128_vsync_callback;
    svga->vblank_start   = rage128_vblank_start;
    svga->conv_16to32    = rage128_conv_16to32; /* 15/16 bpp scanout through the gamma table */
    timer_add(&dev->vline_timer, rage128_vline_timer, dev, 0);
    svga->hwcursor.cur_xsize = 64;
    svga->hwcursor.cur_ysize = 64;

    /* The svga core's ATI panning mode, the one vid_ati_mach64.c also
       sets: the pel-pan count is applied per scanline rather than once
       per frame, so a pan write in mid-frame (Commander Keen 4-6 make
       them) changes the picture from the next line on. The guide does
       not document this; it is modeled after the Mach64. */
    svga->adv_flags |= FLAG_PANNING_ATI;

    /* DDC2 monitor channel. The driver bit-bangs it through GPIO_MONID
       to read the monitor's EDID, which gives it the list of modes and
       refresh rates; without it the driver is observed to fall back to a
       1600x1200 default and to pan modes larger than that. The EDID is
       86Box's shared monitor EDID, the one every DDC-capable card
       presents. */
    dev->i2c = i2c_gpio_init("ddc_ati_rage128");
    dev->ddc = ddc_init(i2c_gpio_get_bus(dev->i2c));

    /* Rasterizer worker pool, size set by the render_threads option. The
       optional GPU backend draws the states it supports and hands the
       rest back to the CPU rasterizer, which must then run in program
       order, so the pool is cut to one thread when the backend is
       requested. */
    dev->render_threads = device_get_config_int("render_threads");
    if (dev->render_threads < 1)
        dev->render_threads = 1;
    if (rage128_gpu_mode() != R128_GPU_OFF && dev->render_threads > 1) {
        rage128_log("RAGE128 GPU: forcing render_threads=1 (was %d)\n",
                    dev->render_threads);
        dev->render_threads = 1;
    }
    /* The capture tap records bus reads in the order the threads issue
       them, and a replay serves them in record order on one thread, so a
       worker's texel fetch interleaved with the CCE thread's ring walk
       would not replay. The pool is cut to one thread while the tap is
       armed. */
    {
        const char *cap = getenv("RAGE128_CAPTURE");

        if (cap && *cap && dev->render_threads > 1) {
            rage128_log("RAGE128 capture: forcing render_threads=1 (was %d)\n",
                        dev->render_threads);
            dev->render_threads = 1;
        }
    }
    rage128_raster_init(dev);

    /* Realtime pacer: any non-zero value of the realtime_pacing option
       means strict, which also covers values from older configs. The
       R128_PACE_BAND environment variable sets how far, in percent,
       emulated time may fall behind real time before the governor acts
       (default 0.5); it is a diagnostic setting, not a user option. */
    dev->pace_enabled = device_get_config_int("realtime_pacing") != 0;
    dev->pace_band_x  = 5;
    {
        const char *pb = getenv("R128_PACE_BAND");

        if (pb && pb[0]) {
            double pct = atof(pb);

            if (pct >= 0.0 && pct <= 100.0)
                dev->pace_band_x = (int) (pct * 10.0 + 0.5);
        }
    }
    {
        const char *mp = getenv("R128_MMIO_PROF");

        dev->mmio_prof = mp && mp[0] && strcmp(mp, "0") != 0;
    }
    {
        const char *sp = getenv("R128_SLICE_PROF");

        dev->slice_prof = sp && sp[0] && strcmp(sp, "0") != 0;
    }
    dev->conv_verify = getenv("R128_CONV_VERIFY") != NULL;
    rage128_log("RAGE128 pacer: %s (band %d.%d%%) mmio-prof %s slice-prof %s\n",
                dev->pace_enabled ? "strict" : "off",
                dev->pace_band_x / 10, dev->pace_band_x % 10,
                dev->mmio_prof ? "on" : "off",
                dev->slice_prof ? "on" : "off");

    /* Span recompiler: a per-machine option, offered only on hosts with a
       code emitter. Elsewhere jit_enabled stays 0 and rage128_jit_init
       leaves the recompiler off. */
#if R128_JIT_HAVE_BACKEND
    dev->jit_enabled = device_get_config_int("recompiler");
#endif
    rage128_jit_init(dev);

    rage128_synctel_init(dev);
    rage128_census_init(dev);

    rage128_gpu_init(dev);

    /* The CCE executor thread is created last. Its first loop iteration
       finds an empty FIFO and flushes, which takes this mutex and reads
       dev->raster, tex_stage and ftl_en; the GPU init above stores the
       last two when it starts at once (R128_GPU_DEFER=0). */
    dev->raster_flush_mtx = thread_create_mutex();

    rage128_pm4_thread_init(dev);

    mem_mapping_add(&dev->lfb_mapping, 0, 0,
                    rage128_lfb_readb, rage128_lfb_readw, rage128_lfb_readl,
                    rage128_lfb_writeb, rage128_lfb_writew, rage128_lfb_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->agp_mapping, 0, 0,
                    rage128_agp_readb, rage128_agp_readw, rage128_agp_readl,
                    rage128_agp_writeb, rage128_agp_writew, rage128_agp_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->mmio_mapping, 0, 0,
                    rage128_mmio_readb, rage128_mmio_readw, rage128_mmio_readl,
                    rage128_mmio_writeb, rage128_mmio_writew, rage128_mmio_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->lfb_mapping);
    mem_mapping_disable(&dev->agp_mapping);
    mem_mapping_disable(&dev->mmio_mapping);

    /* Video BIOS behind the PCI expansion ROM BAR. It is loaded with its
       mapping disabled; the system BIOS maps it through the ROM BAR
       during POST, then shadows and runs it, as for vid_voodoo_banshee.c
       and vid_ati_mach64.c. A chip with no flash of its own has no ROM
       BAR at all. */
    if (dev->has_bios_rom) {
        rom_init(&dev->bios_rom,
                 cfg ? cfg->rom_path : (dev->is_agp ? RAGE128_ROM_PATH_AGP : RAGE128_ROM_PATH_PCI),
                 0xc0000, RAGE128_ROM_SIZE, RAGE128_ROM_SIZE - 1, 0, MEM_MAPPING_EXTERNAL);
        mem_mapping_disable(&dev->bios_rom.mapping);
    }

    /* Reset runs after the ROM is loaded, because it reads the ROM-based
       straps. */
    rage128_reset(dev);

    pci_add_card(dev->is_agp ? PCI_ADD_AGP : PCI_ADD_NORMAL,
                 rage128_pci_read, rage128_pci_write, dev, &dev->pci_slot);

    return dev;
}

static void *
rage128_init(const device_t *info)
{
    return rage128_chip_init(info, NULL);
}

void
rage128_chip_close(rage128_t *dev)
{

    if (dev->pace_wall_ms_tot)
        rage128_log("RAGE128 pace: close: avg emu=%llu.%llu%% over %llus\n",
                    (unsigned long long) (dev->pace_emu_us_tot
                                          / dev->pace_wall_ms_tot / 10),
                    (unsigned long long) (dev->pace_emu_us_tot
                                          / dev->pace_wall_ms_tot % 10),
                    (unsigned long long) (dev->pace_wall_ms_tot / 1000));
    rage128_pm4_thread_close(dev);             /* stop the executor (no more submits) */
    rage128_conv_close(dev);                   /* join the scanout conversion worker */
    rage128_raster_close(dev);                 /* then join the raster workers */
    rage128_jit_close(dev);                    /* all executors stopped -> free the code */
    rage128_synctel_close(dev);                /* all taps quiet -> dump the summary */
    rage128_census_close(dev);                 /* same: every state seen, write the file */
    rage128_gpu_close(dev);                    /* executors stopped -> no GPU work in flight */
    thread_close_mutex(dev->raster_flush_mtx); /* no flusher left */
    dev->raster_flush_mtx = NULL;
    free(dev->tex_stage.arena); /* workers joined -> arenas safe to free */
    free(dev->z_stage.arena);
    free(dev->z_stage.shadow);
    free(dev->c_stage.arena);
    free(dev->c_stage.shadow);
    free(dev->s2d_dst.arena); /* 2D window scratch (op-transient) */
    free(dev->s2d_dst.shadow);
    free(dev->s2d_src.arena);
    free(dev->s2d_src.shadow);
    free(dev->vtx_cache.v); /* submit thread stopped -> cache idle */
    free(dev->vtx_cache.gen);
    rage128_io_remove(dev);
    io_removehandler(0x03a0, 0x0040,
                     rage128_vga_in, NULL, NULL,
                     rage128_vga_out, NULL, NULL, dev);
    ddc_close(dev->ddc);
    i2c_gpio_close(dev->i2c);
    svga_close(&dev->svga);
    free(dev);
}

static void
rage128_close(void *priv)
{
    rage128_chip_close((rage128_t *) priv);
}

static int
rage128_available(void)
{
    return rom_present(RAGE128_ROM_PATH_AGP);
}

static int
rage128_pci_available(void)
{
    return rom_present(RAGE128_ROM_PATH_PCI);
}

void
rage128_chip_speed_changed(rage128_t *dev)
{
    svga_recalctimings(&dev->svga);
}

void
rage128_chip_force_redraw(rage128_t *dev)
{
    dev->svga.fullchange = dev->svga.monitor->mon_changeframecount;
}

void
rage128_chip_reset(rage128_t *dev)
{
    rage128_reset(dev);
}

static void
rage128_speed_changed(void *priv)
{
    rage128_chip_speed_changed((rage128_t *) priv);
}

static void
rage128_force_redraw(void *priv)
{
    rage128_chip_force_redraw((rage128_t *) priv);
}

/* clang-format off */
const device_config_t rage128_config[] = {
#if R128_JIT_HAVE_BACKEND
    {
        .name           = "recompiler",
        .description    = "Dynamic recompiler",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
#endif
#ifdef R128_GPU_HAVE_VULKAN
    /* Absent from the table on hosts built without Vulkan headers: the
       backend is stubbed there, so the option would do nothing. The
       debugging knobs (verify, 2D verify, fold, telemetry) are env-only. */
    {
        .name           = "gpu_raster",
        .description    = "GPU raster backend (uses 1 render thread)",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Off (CPU renderer)", .value = 0 },
            { .description = "On",                 .value = 1 },
            { .description = ""                               }
        },
        .bios           = { { 0 } }
    },
#endif
    {
        .name           = "realtime_pacing",
        .description    = "Realtime pacing",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Off (max fps)",            .value = 0 },
            { .description = "Strict (hold 100% speed)", .value = 1 },
            { .description = ""                                     }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "render_threads",
        .description    = "Render threads",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "1", .value = 1 },
            { .description = "2", .value = 2 },
            { .description = "3", .value = 3 },
            { .description = "4", .value = 4 },
            { .description = "6", .value = 6 },
            { .description = "8", .value = 8 },
            { .description = ""              }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 32,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "16 MB", .value = 16 },
            { .description = "32 MB", .value = 32 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t ati_rage128_pro_device = {
    .name          = "ATI Rage Fury Pro AGP",
    .internal_name = "ati_rage128_pro",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = rage128_init,
    .close         = rage128_close,
    .reset         = rage128_reset,
    .available     = rage128_available,
    .speed_changed = rage128_speed_changed,
    .force_redraw  = rage128_force_redraw,
    .config        = rage128_config
};

/* PCI board: ATI XPERT 128, a Rage 128 GL (device 0x5245) with video
   BIOS part number 113-57403-102. The subsystem id is whatever the
   loaded ROM image straps; that image holds 1002:0008, the id ATI's
   Windows 98 and Windows 2000 INFs list as "Xpert 128" (see
   RAGE128_PCI_VENDOR in vid_ati_rage128_regs.h). */
const device_t ati_xpert128_pci_device = {
    .name          = "ATI Xpert 128 PCI",
    .internal_name = "ati_xpert128_pci",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = rage128_init,
    .close         = rage128_close,
    .reset         = rage128_reset,
    .available     = rage128_pci_available,
    .speed_changed = rage128_speed_changed,
    .force_redraw  = rage128_force_redraw,
    .config        = rage128_config
};
/* clang-format on */
