/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Cyrix MediaGX MMX's graphics: the graphics
 *          pipeline (BitBLT and vector engine), its VGA hardware, and the
 *          display controller, with the dot clock, the DACs and the gamma
 *          RAM of the CS5530 it drives.
 *
 *          The MediaGX has no VGA of its own. The VGA registers are SMM
 *          code (VSA's SoftVGA) behind I/O traps; what is hardware is the
 *          VGA memory path of the graphics pipeline, which reads and
 *          writes a 256 KB planar buffer in graphics memory with the VGA's
 *          write modes, rotate, latch and color compare, and the SMI on a
 *          write to displayed VGA memory (VGACTL, VGAM), after which the
 *          handler redraws what the display controller shows. The display
 *          controller scans an 8 or 16 BPP frame buffer, VGA mode 13h's
 *          chain-4 layout or the planar modes; text is drawn by SoftVGA.
 *
 *          MediaGX MMX-Enhanced Processor data book, revision 2.0, 4.4,
 *          4.5 and 5.2; CS5530 data book, revision 4.1, 4.3.5.
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2026 Cacodemon345.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/plat_unused.h>
#include <86box/cyrix_gxm.h>

#ifdef ENABLE_GXM_VIDEO_LOG
int gxm_video_do_log = ENABLE_GXM_VIDEO_LOG;

static void
gxm_video_log(const char *fmt, ...)
{
    va_list ap;

    if (gxm_video_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define gxm_video_log(fmt, ...)
#endif

/* GP_VGA_WRITE (Table 5-5). */
#define VGAW_W3        (1 << 20)
#define VGAW_W2        (1 << 19)

/* GP_VGA_READ. */
#define VGAR_F15       (1 << 15)
#define VGAR_PC4       (1 << 14)
#define VGAR_C4        (1 << 13)
#define VGAR_PB        (1 << 12)
#define VGAR_COE       (1 << 11)
#define VGAR_W2        (1 << 10)
#define VGAR_R2        (1 << 9)
#define VGAR_RM        (1 << 8)

/* GP_RASTER_MODE (Table 4-25). */
#define RM_TB          (1 << 12)
#define RM_ST          (1 << 11)
#define RM_PT          (1 << 10)

/* GP_VECTOR_MODE. */
#define VM_DEST        (1 << 3)
#define VM_DMIN        (1 << 2)
#define VM_DMAJ        (1 << 1)
#define VM_YMAJ        (1 << 0)

/* GP_BLT_MODE. */
#define BM_YREV        (1 << 8)

/* GP_BLT_STATUS. */
#define BS_W2048       (1 << 9)
#define BS_16BPP       (1 << 8)

/* DC_GENERAL_CFG (Table 4-31). */
#define GC_DDCK        (1 << 31)
#define GC_VIDE        (1 << 28)
#define GC_SSLC        (1 << 27)
#define GC_CH4S        (1 << 26)
#define GC_LDBL        (1 << 24)
#define GC_CURE        (1 << 1)
#define GC_DFLE        (1 << 0)

/* DC_TIMING_CFG. */
#define TC_VINT        (1 << 31)
#define TC_VNA         (1 << 30)
#define TC_DNA         (1 << 29)
#define TC_BKRT        (1 << 16)
#define TC_PXDB        (1 << 15)
#define TC_PLNR        (1 << 13)
#define TC_BLNK        (1 << 7)
#define TC_VIEN        (1 << 6)
#define TC_TGEN        (1 << 5)
#define TC_BLKE        (1 << 3)

/* DC_OUTPUT_CFG. */
#define OC_16FMT       (1 << 1)
#define OC_8BPP        (1 << 0)

/* The CS5530's video overlay registers, as dword indexes from F4BAR. */
enum {
    VID_CONFIG = 0,
    VID_X      = 2,
    VID_Y,
    VID_SCALE,
    VID_KEY,
    VID_MASK,
    VID_REGS
};

/* Video Configuration (CS5530 Table 3-71). */
#define VC_16BIT       (1 << 29)
#define VC_XFILT       (1 << 7)
#define VC_YFILT       (1 << 6)
#define VC_CSC_BYPASS  (1 << 5)
#define VC_UPDATE      (1 << 1)
#define VC_ENABLE      (1 << 0)

/* Display Configuration (CS5530 Table 3-75). */
#define DCFG_GFX_BYPASS (1 << 21)
#define DCFG_VID_KEY    (1 << 20)

#define DC_UNLOCK_CODE 0x4758

/* The DC registers, as dword indexes from GX_BASE+8300h. */
enum {
    DC_UNLOCK = 0x00,
    DC_GENERAL_CFG,
    DC_TIMING_CFG,
    DC_OUTPUT_CFG,
    DC_FB_ST_OFFSET,
    DC_CB_ST_OFFSET,
    DC_CURS_ST_OFFSET,
    DC_RSVD_1C,
    DC_VID_ST_OFFSET,
    DC_LINE_DELTA,
    DC_BUF_SIZE,
    DC_RSVD_2C,
    DC_H_TIMING_1,
    DC_H_TIMING_2,
    DC_H_TIMING_3,
    DC_FP_H_TIMING,
    DC_V_TIMING_1,
    DC_V_TIMING_2,
    DC_V_TIMING_3,
    DC_FP_V_TIMING,
    DC_CURSOR_X,
    DC_V_LINE_CNT,
    DC_CURSOR_Y,
    DC_SS_LINE_CMP,
    DC_CURSOR_COLOR,
    DC_RSVD_64,
    DC_BORDER_COLOR,
    DC_RSVD_6C,
    DC_PAL_ADDRESS,
    DC_PAL_DATA,
    DC_DFIFO_DIAG,
    DC_CFIFO_DIAG,
    DC_REGS
};

typedef struct gxm_video_t {
    /* Graphics pipeline. */
    uint32_t dst_xy;      /* 8100h: X in 15:0, Y in 31:16 */
    uint32_t wh;          /* 8104h: width (vector length) in 15:0, height (initial error) in 31:16 */
    uint32_t src_xy;      /* 8108h: X (axial error) in 15:0, Y (diagonal error) in 31:16 */
    uint32_t src_color;   /* 810Ch: color 0 in 15:0, color 1 in 31:16 */
    uint32_t pat_color[2];
    uint32_t pat_data[4];
    uint32_t raster_mode;
    uint32_t vector_mode;
    uint32_t blt_mode;
    uint32_t blt_status;

    /* The VGA hardware. */
    uint32_t vga_write;
    uint32_t vga_read;
    uint32_t vga_base;
    uint32_t vga_latch;
    uint8_t  vga_enables;

    mem_mapping_t vga_mapping[3];

    /* Graphics memory. */
    uint32_t gmem_base;
    uint32_t gmem_size;

    /* Display controller. */
    uint32_t dc[DC_REGS];
    uint32_t palette[0x105];
    uint32_t pal_rgb[0x105];

    /* The CS5530's display interface. */
    double   dotclock;
    uint32_t display_cfg;
    uint32_t gamma[256];
    uint32_t vid[VID_REGS];     /* As written */
    uint32_t vid_win[VID_REGS]; /* The window and scale in use */

    /* Display timing. */
    pc_timer_t timer;
    int        displine;
    int        linepos;
    int        htotal;
    int        hdisp;
    int        vtotal;
    int        vdisp;
    int        vsyncstart;
    int        vsyncend;
    uint64_t   dispontime;
    uint64_t   dispofftime;
    uint32_t   ma;
    int        lineno; /* The frame buffer line, for line doubling */
    int        blink;
    int        output;  /* This display is the one shown */
} gxm_video_t;

void *gxm_video = NULL;

static video_timings_t timing_gxm = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 2, .read_b = 4, .read_w = 4, .read_l = 4 };

static const uint32_t dc_unlocked_regs = (1 << DC_GENERAL_CFG) | (1 << DC_TIMING_CFG) | (1 << DC_OUTPUT_CFG) |
                                         (1 << DC_FB_ST_OFFSET) | (1 << DC_CB_ST_OFFSET) | (1 << DC_CURS_ST_OFFSET) |
                                         (1 << DC_LINE_DELTA) | (1 << DC_BUF_SIZE) | (1 << DC_H_TIMING_1) |
                                         (1 << DC_H_TIMING_2) | (1 << DC_H_TIMING_3) | (1 << DC_FP_H_TIMING) |
                                         (1 << DC_V_TIMING_1) | (1 << DC_V_TIMING_2) | (1 << DC_V_TIMING_3) |
                                         (1 << DC_FP_V_TIMING);

static void gxm_recalctimings(gxm_video_t *dev);

/* Graphics memory, as the graphics pipeline and the display controller
   address it: a 22-bit offset from MC_GBASE_ADD. */
static inline uint8_t *
gxm_gmem(gxm_video_t *dev, uint32_t offset)
{
    static uint8_t dummy[4];

    offset &= 0x003fffff;
    if ((offset + 4) > dev->gmem_size) {
        dummy[0] = dummy[1] = dummy[2] = dummy[3] = 0xff;
        return dummy;
    }

    return &ram[dev->gmem_base + offset];
}

void
gxm_video_set_gmem(void *priv, uint32_t base, uint32_t size)
{
    gxm_video_t *dev = (gxm_video_t *) priv;

    if (dev == NULL)
        return;

    dev->gmem_base = base;
    dev->gmem_size = size;
}

void
gxm_video_gmem_written(UNUSED(void *priv), UNUSED(uint32_t addr), UNUSED(int size))
{
    /* The whole frame is drawn each frame. */
}

/* ----------------------------------------------------------------------
   The ternary raster operation (4.4.5): bit k of the ROP is the output for
   pattern, source and destination bits (k >> 2, k >> 1, k) & 1.
   ---------------------------------------------------------------------- */
static inline uint32_t
gxm_rop(uint8_t rop, uint32_t p, uint32_t s, uint32_t d)
{
    uint32_t out = 0;

    if (rop & 0x01)
        out |= ~p & ~s & ~d;
    if (rop & 0x02)
        out |= ~p & ~s & d;
    if (rop & 0x04)
        out |= ~p & s & ~d;
    if (rop & 0x08)
        out |= ~p & s & d;
    if (rop & 0x10)
        out |= p & ~s & ~d;
    if (rop & 0x20)
        out |= p & ~s & d;
    if (rop & 0x40)
        out |= p & s & ~d;
    if (rop & 0x80)
        out |= p & s & d;

    return out;
}

/* ----------------------------------------------------------------------
   The VGA memory path (5.2.1, 5.2.3). A host byte becomes 32 bits, one for
   each plane, through the write mode, the raster operation unit (pattern =
   the bit mask, source = the write mode's data, destination = the latch;
   SoftVGA loads CAh, 8Ah, EAh or 6Ah into GP_RASTER_MODE for the VGA's
   COPY, AND, OR and XOR), and the plane enables.
   ---------------------------------------------------------------------- */
static uint32_t
gxm_vga_address(gxm_video_t *dev, uint32_t off, int write, uint8_t *planes)
{
    const uint32_t rd    = dev->vga_read;
    const int      chain2 = write ? !!(rd & VGAR_W2) : !!(rd & VGAR_R2);
    uint32_t       addr;

    if (rd & VGAR_F15)
        off &= ~0x8000;

    if (rd & VGAR_C4) {
        *planes = 1 << (off & 3);
        addr    = (rd & VGAR_PC4) ? (off >> 2) : (off & ~3);
    } else if (chain2) {
        *planes = (off & 1) ? 0x0a : 0x05;
        /* The page bit is the VGA's (Miscellaneous Output bit 5): set, the
           low page. SoftVGA sets it in the text modes and draws the cells
           from the even dwords. */
        addr    = (rd & VGAR_COE) ? ((off & ~1) | ((rd & VGAR_PB) ? 0 : 1)) : off;
    } else {
        *planes = 0x0f;
        addr    = off;
    }

    return (addr & 0xffff) << 2;
}

static inline uint32_t
gxm_vga_mem_offset(gxm_video_t *dev, uint32_t addr)
{
    return ((dev->vga_base & 0x3f) << 16) + addr;
}

static uint8_t
gxm_vga_readb_internal(gxm_video_t *dev, uint32_t off)
{
    const uint32_t rd = dev->vga_read;
    uint8_t        planes;
    uint32_t       addr = gxm_vga_address(dev, off, 0, &planes);
    uint8_t        ret;
    int            plane;

    dev->vga_latch = *(uint32_t *) gxm_gmem(dev, gxm_vga_mem_offset(dev, addr));

    if (rd & VGAR_RM) {
        /* Color compare, the planes of CCM against CC. */
        const uint8_t cc  = rd & 0x0f;
        const uint8_t ccm = (rd >> 4) & 0x0f;

        ret = 0xff;
        for (int i = 0; i < 4; i++) {
            if (ccm & (1 << i)) {
                const uint8_t b = dev->vga_latch >> (i << 3);

                ret &= (cc & (1 << i)) ? b : ~b;
            }
        }
        return ret;
    }

    if (rd & VGAR_C4)
        plane = off & 3;
    else if (rd & VGAR_R2)
        plane = ((rd >> 16) & 2) | (off & 1);
    else
        plane = (rd >> 16) & 3;

    return dev->vga_latch >> (plane << 3);
}

static void
gxm_vga_writeb_internal(gxm_video_t *dev, uint32_t off, uint8_t val)
{
    const uint32_t wr = dev->vga_write;
    const uint8_t  rc = (wr >> 16) & 7;
    const uint8_t  sr  = (wr >> 8) & 0x0f;
    const uint8_t  sre = (wr >> 12) & 0x0f;
    uint8_t        planes;
    uint32_t       addr = gxm_vga_address(dev, off, 1, &planes);
    uint8_t        mask = wr & 0xff;
    uint32_t       data = 0;
    uint32_t       pmask;
    uint32_t       out;
    uint8_t       *p;

    val = (val >> rc) | (val << (8 - rc));

    for (int i = 0; i < 4; i++) {
        uint8_t b;

        if (wr & VGAW_W3)
            b = (sr & (1 << i)) ? 0xff : 0x00;
        else if (wr & VGAW_W2)
            b = (val & (1 << i)) ? 0xff : 0x00;
        else if (sre & (1 << i))
            b = (sr & (1 << i)) ? 0xff : 0x00;
        else
            b = val;
        data |= b << (i << 3);
    }
    if (wr & VGAW_W3)
        mask &= val;

    pmask = mask * 0x01010101;
    out   = gxm_rop(dev->raster_mode & 0xff, pmask, data, dev->vga_latch);

    planes &= (wr >> 24) & 0x0f;
    p = gxm_gmem(dev, gxm_vga_mem_offset(dev, addr));
    for (int i = 0; i < 4; i++) {
        if (planes & (1 << i))
            p[i] = out >> (i << 3);
    }
}

/* The SMI on a write to displayed VGA memory: in the decode stage, for the
   whole write (3.11.8, 5.2.3.5). */
static void
gxm_vga_write_smi(UNUSED(gxm_video_t *dev), uint32_t addr, int size, uint32_t val)
{
    int region;

    if (addr < 0xb0000)
        region = 0;
    else if (addr < 0xb8000)
        region = 1;
    else
        region = 2;

    if (!(gxm.vgactl & (1 << region)))
        return;
    if ((region == 0) && !(gxm.vgam & (1 << ((addr >> 11) & 0x1f))))
        return;

    if (!in_smm) {
        cyrix_gxm_smi_status(GXM_PM_SMI_MEM);
        cpu_gxm_smi_mem(addr, size, val);
    }
}

static uint8_t
gxm_vga_read(uint32_t addr, void *priv)
{
    return gxm_vga_readb_internal((gxm_video_t *) priv, addr & 0xffff);
}

static uint16_t
gxm_vga_readw(uint32_t addr, void *priv)
{
    gxm_video_t *dev = (gxm_video_t *) priv;

    return gxm_vga_readb_internal(dev, addr & 0xffff) |
           (gxm_vga_readb_internal(dev, (addr + 1) & 0xffff) << 8);
}

static uint32_t
gxm_vga_readl(uint32_t addr, void *priv)
{
    gxm_video_t *dev = (gxm_video_t *) priv;

    return gxm_vga_readw(addr, dev) | (gxm_vga_readw(addr + 2, dev) << 16);
}

static void
gxm_vga_write(uint32_t addr, uint8_t val, void *priv)
{
    gxm_video_t *dev = (gxm_video_t *) priv;

    gxm_vga_writeb_internal(dev, addr & 0xffff, val);
    gxm_vga_write_smi(dev, addr, 1, val);
}

static void
gxm_vga_writew(uint32_t addr, uint16_t val, void *priv)
{
    gxm_video_t *dev = (gxm_video_t *) priv;

    gxm_vga_writeb_internal(dev, addr & 0xffff, val);
    gxm_vga_writeb_internal(dev, (addr + 1) & 0xffff, val >> 8);
    gxm_vga_write_smi(dev, addr, 2, val);
}

static void
gxm_vga_writel(uint32_t addr, uint32_t val, void *priv)
{
    gxm_video_t *dev = (gxm_video_t *) priv;

    for (int i = 0; i < 4; i++)
        gxm_vga_writeb_internal(dev, (addr + i) & 0xffff, val >> (i << 3));
    gxm_vga_write_smi(dev, addr, 4, val);
}

void
gxm_video_set_vga(void *priv, uint8_t enables)
{
    static const uint32_t base[3] = { 0xa0000, 0xb0000, 0xb8000 };
    static const uint32_t size[3] = { 0x10000, 0x08000, 0x08000 };
    gxm_video_t          *dev     = (gxm_video_t *) priv;

    if (dev == NULL)
        return;

    dev->vga_enables = enables;
    for (int i = 0; i < 3; i++) {
        if (enables & (1 << i))
            mem_mapping_set_addr(&dev->vga_mapping[i], base[i], size[i]);
        else
            mem_mapping_disable(&dev->vga_mapping[i]);
    }
}

/* ----------------------------------------------------------------------
   The BitBLT and vector engine (4.4). Frame buffer addresses are XY: the
   line delta is 1024 or 2048 bytes (GP_BLT_STATUS). The BLT buffers are in
   the scratchpad, at L1_BBx_BASE.
   ---------------------------------------------------------------------- */
/* The widest line: 2048 pixels at 8 bpp with the 2048 byte line delta. */
#define GXM_BLT_LINE_MAX 2048

static inline int
gxm_gp_bpp(gxm_video_t *dev)
{
    return (dev->blt_status & BS_16BPP) ? 2 : 1;
}

static inline uint32_t
gxm_gp_fb_offset(gxm_video_t *dev, int x, int y)
{
    const int pitch = (dev->blt_status & BS_W2048) ? 2048 : 1024;

    return (uint32_t) ((y * pitch) + (x * gxm_gp_bpp(dev)));
}

static uint32_t
gxm_gp_fb_read(gxm_video_t *dev, int x, int y)
{
    const uint8_t *p = gxm_gmem(dev, gxm_gp_fb_offset(dev, x, y));

    return (gxm_gp_bpp(dev) == 2) ? *(uint16_t *) p : *p;
}

static void
gxm_gp_fb_write(gxm_video_t *dev, int x, int y, uint32_t val)
{
    uint8_t *p = gxm_gmem(dev, gxm_gp_fb_offset(dev, x, y));

    if (gxm_gp_bpp(dev) == 2)
        *(uint16_t *) p = val;
    else
        *p = val;
}

static inline uint8_t
gxm_bb_readb(int buf, uint32_t offset)
{
    return gxm.scratchpad[(gxm.bb_base[buf] + offset) & 0x0fff];
}

static uint32_t
gxm_bb_read(gxm_video_t *dev, int buf, uint32_t offset)
{
    if (gxm_gp_bpp(dev) == 2)
        return gxm_bb_readb(buf, offset) | (gxm_bb_readb(buf, offset + 1) << 8);

    return gxm_bb_readb(buf, offset);
}

static inline uint32_t
gxm_gp_color(gxm_video_t *dev, uint16_t color)
{
    return (gxm_gp_bpp(dev) == 2) ? color : (color & 0xff);
}

/* The pattern for a pixel, and whether it is drawn (pattern transparency).
   The pattern hardware keeps no origin: the screen position picks the bit. */
static uint32_t
gxm_gp_pattern(gxm_video_t *dev, int x, int y, int *draw)
{
    const int      row = y & 7;
    const int      col = x & 7;
    const uint16_t c0  = dev->pat_color[0];
    const uint16_t c1  = dev->pat_color[0] >> 16;
    const uint16_t c2  = dev->pat_color[1];
    const uint16_t c3  = dev->pat_color[1] >> 16;
    int            bit;

    *draw = 1;

    switch ((dev->raster_mode >> 8) & 3) {
        default:
        case 0: /* Solid */
            return gxm_gp_color(dev, c0);

        case 1: /* Monochrome: bits 7:0 the first row, bit 7 leftmost */
            bit = (dev->pat_data[row >> 2] >> (((row & 3) << 3) + (7 - col))) & 1;
            if (!bit && (dev->raster_mode & RM_PT))
                *draw = 0;
            return gxm_gp_color(dev, bit ? c1 : c0);

        case 2: { /* Dither: 16 bits a row, the low byte the LSBs */
            const uint16_t r   = dev->pat_data[row >> 1] >> ((row & 1) << 4);
            const int      idx = ((r >> (7 - col)) & 1) | (((r >> (15 - col)) & 1) << 1);
            static const int map[4] = { 0, 1, 2, 3 };
            const uint16_t   c[4]   = { c0, c1, c2, c3 };

            return gxm_gp_color(dev, c[map[idx]]);
        }

        case 3: /* Color: one row of eight pixels, leftmost first */
            if (gxm_gp_bpp(dev) == 2)
                return (dev->pat_data[col >> 1] >> ((col & 1) << 4)) & 0xffff;
            return (dev->pat_data[col >> 2] >> ((col & 3) << 3)) & 0xff;
    }
}

static void
gxm_gp_blt(gxm_video_t *dev)
{
    const int      bpp      = gxm_gp_bpp(dev);
    const uint32_t ones     = (bpp == 2) ? 0xffff : 0xff;
    const uint8_t  rop      = dev->raster_mode & 0xff;
    const int      rs       = dev->blt_mode & 3;
    const int      rd       = (dev->blt_mode >> 2) & 7;
    const int      sm       = (dev->blt_mode >> 6) & 3;
    const int      yrev     = !!(dev->blt_mode & BM_YREV);
    const int      dst_x    = (int16_t) (dev->dst_xy & 0xffff);
    const int      dst_y    = (int16_t) (dev->dst_xy >> 16);
    const int      width    = dev->wh & 0xffff;
    const int      height   = dev->wh >> 16;
    const int      src_x    = (int16_t) (dev->src_xy & 0xffff);
    const int      src_y    = (int16_t) (dev->src_xy >> 16);
    const int      mono     = (sm == 1) || (sm == 3);
    const uint16_t sc0      = dev->src_color;
    const uint16_t sc1      = dev->src_color >> 16;
    uint32_t       key      = 0;
    uint32_t       bb_off;
    static uint32_t src_line[GXM_BLT_LINE_MAX];

    gxm_video_log("GP: BLT %08X ROP %02X raster %08X, (%i, %i) <- (%i, %i), %ix%i\n",
                  dev->blt_mode, rop, dev->raster_mode, dst_x, dst_y, src_x, src_y, width, height);

    /* The color key of a transparent BLT is in the BLT buffer, as destination
       data. */
    if ((dev->raster_mode & RM_TB) && ((rd == 2) || (rd == 3)))
        key = gxm_bb_read(dev, rd & 1, 0);

    /* A BLT buffer source is consumed a line after the other: a color bitmap
       a line of pixels, a monochrome bitmap or a text glyph a line of bytes
       (SoftVGA's 9-dot glyphs are two bytes a line). */
    bb_off = 0;

    for (int line = 0; line < height; line++) {
        const int dy = yrev ? (dst_y - line) : (dst_y + line);
        const int sy = yrev ? (src_y - line) : (src_y + line);
        uint32_t  line_bytes;

        /* A frame buffer source line is read whole, through the BLT buffers,
           before it is written: a copy that overlaps on the same line (the
           tray clock moved right as the tray grows) must not read pixels it
           has already written. */
        if (rs == 1) {
            for (int i = 0; (i < width) && (i < GXM_BLT_LINE_MAX); i++) {
                if (mono) {
                    const int b = src_x + i;

                    src_line[i] = (gxm_gp_fb_read(dev, b >> 3, sy) >> (7 - (b & 7))) & 1;
                } else
                    src_line[i] = gxm_gp_fb_read(dev, src_x + i, sy);
            }
        }

        for (int i = 0; i < width; i++) {
            const int dx = dst_x + i;
            uint32_t  s  = ones;
            uint32_t  d  = ones;
            uint32_t  p;
            int       draw;
            int       bit = 1;

            p = gxm_gp_pattern(dev, dx, dy, &draw);

            switch (rs) {
                case 1: /* Frame buffer */
                    if (i >= GXM_BLT_LINE_MAX)
                        break;
                    if (mono)
                        bit = src_line[i];
                    else
                        s = src_line[i];
                    break;
                case 2: /* BLT buffer 0 */
                case 3: /* BLT buffer 1 */
                    if (mono) {
                        const uint32_t b = (uint32_t) ((bb_off << 3) + i + (src_x & 7));

                        bit = (gxm_bb_readb(rs & 1, b >> 3) >> (7 - (b & 7))) & 1;
                    } else
                        s = gxm_bb_read(dev, rs & 1, bb_off + (i * bpp));
                    break;

                default:
                    break;
            }

            if (mono) {
                if (!bit && (dev->raster_mode & RM_ST))
                    continue;
                s = gxm_gp_color(dev, bit ? sc1 : sc0);
            }

            if (!draw)
                continue;

            if (dev->raster_mode & RM_TB) {
                if (s == key)
                    continue;
            } else if (rd >= 2)
                d = gxm_gp_fb_read(dev, dx, dy);

            gxm_gp_fb_write(dev, dx, dy, gxm_rop(rop, p, s, d) & ones);
        }

        if (mono)
            line_bytes = (width + (src_x & 7) + 7) >> 3;
        else
            line_bytes = width * bpp;
        bb_off += line_bytes;
    }

    /* The master registers not written again take the slave's values on
       (Table 4-21). */
    if (sm == 3)
        dev->dst_xy = (dev->dst_xy & 0xffff0000) | ((dst_x + width) & 0xffff);
    else {
        const int ny = yrev ? (dst_y - height) : (dst_y + height);
        const int sy = yrev ? (src_y - height) : (src_y + height);

        dev->dst_xy = (dev->dst_xy & 0x0000ffff) | ((uint32_t) (ny & 0xffff) << 16);
        dev->src_xy = (dev->src_xy & 0x0000ffff) | ((uint32_t) (sy & 0xffff) << 16);
    }
}

/* A Bresenham vector, error terms given (4.4.1). */
static void
gxm_gp_vector(gxm_video_t *dev)
{
    const uint32_t ones   = (gxm_gp_bpp(dev) == 2) ? 0xffff : 0xff;
    const uint8_t  rop    = dev->raster_mode & 0xff;
    int            x      = (int16_t) (dev->dst_xy & 0xffff);
    int            y      = (int16_t) (dev->dst_xy >> 16);
    const int      length = dev->wh & 0x3fff;
    int            err    = (int16_t) (dev->wh >> 16);
    const int      axial  = (int16_t) (dev->src_xy & 0xffff);
    const int      diag   = (int16_t) (dev->src_xy >> 16);
    const int      majs   = (dev->vector_mode & VM_DMAJ) ? 1 : -1;
    const int      mins   = (dev->vector_mode & VM_DMIN) ? 1 : -1;
    const int      ymaj   = !!(dev->vector_mode & VM_YMAJ);

    gxm_video_log("GP: Vector %08X, (%i, %i) length %i\n", dev->vector_mode, x, y, length);

    for (int i = 0; i < length; i++) {
        uint32_t d = ones;
        uint32_t p;
        int      draw;

        p = gxm_gp_pattern(dev, x, y, &draw);
        if (draw) {
            if (dev->vector_mode & VM_DEST)
                d = gxm_gp_fb_read(dev, x, y);
            gxm_gp_fb_write(dev, x, y, gxm_rop(rop, p, ones, d) & ones);
        }

        if (err >= 0) {
            if (ymaj)
                x += mins;
            else
                y += mins;
            err += diag;
        } else
            err += axial;
        if (ymaj)
            y += majs;
        else
            x += majs;
    }

    dev->dst_xy = (x & 0xffff) | ((uint32_t) (y & 0xffff) << 16);
    dev->wh     = (dev->wh & 0x0000ffff) | ((uint32_t) (err & 0xffff) << 16);
}

uint32_t
gxm_gp_read(uint32_t addr, void *priv)
{
    const gxm_video_t *dev = (gxm_video_t *) priv;
    uint32_t           ret = 0x00000000;

    if (dev == NULL)
        return 0xffffffff;

    switch (addr & 0xfffc) {
        case 0x8100:
            ret = dev->dst_xy;
            break;
        case 0x8104:
            ret = dev->wh;
            break;
        case 0x8108:
            ret = dev->src_xy;
            break;
        case 0x810c:
            ret = dev->src_color;
            break;
        case 0x8110:
        case 0x8114:
            ret = dev->pat_color[(addr >> 2) & 1];
            break;
        case 0x8120 ... 0x812c:
            ret = dev->pat_data[(addr >> 2) & 3];
            break;
        case 0x8140:
            ret = dev->vga_write;
            break;
        case 0x8144:
            ret = dev->vga_read;
            break;
        case 0x8200:
            ret = dev->raster_mode;
            break;
        case 0x8204:
            ret = dev->vector_mode;
            break;
        case 0x8208:
            ret = dev->blt_mode;
            break;
        case 0x820c:
            /* The engine is done at once: never busy nor pending. */
            ret = dev->blt_status & (BS_W2048 | BS_16BPP);
            break;
        case 0x8210:
            /* The base reads back in bits 13:8. */
            ret = (dev->vga_base & 0x3f) << 8;
            break;
        case 0x8214:
            ret = dev->vga_latch;
            break;

        default:
            break;
    }

    return ret;
}

void
gxm_gp_write(uint32_t addr, uint32_t val, uint32_t mask, void *priv)
{
    gxm_video_t *dev = (gxm_video_t *) priv;
    uint32_t    *reg;

    if (dev == NULL)
        return;


    switch (addr & 0xfffc) {
        case 0x8100:
            reg = &dev->dst_xy;
            break;
        case 0x8104:
            reg = &dev->wh;
            break;
        case 0x8108:
            reg = &dev->src_xy;
            break;
        case 0x810c:
            reg = &dev->src_color;
            break;
        case 0x8110:
        case 0x8114:
            reg = &dev->pat_color[(addr >> 2) & 1];
            break;
        case 0x8120 ... 0x812c:
            reg = &dev->pat_data[(addr >> 2) & 3];
            break;
        case 0x8140:
            reg = &dev->vga_write;
            break;
        case 0x8144:
            reg = &dev->vga_read;
            break;
        case 0x8200:
            reg = &dev->raster_mode;
            break;
        case 0x8204:
            dev->vector_mode = ((dev->vector_mode & ~mask) | (val & mask)) & 0x0f;
            gxm_gp_vector(dev);
            return;
        case 0x8208:
            dev->blt_mode = ((dev->blt_mode & ~mask) | (val & mask)) & 0x1df;
            gxm_gp_blt(dev);
            return;
        case 0x820c:
            dev->blt_status = ((dev->blt_status & ~mask) | (val & mask)) & (BS_W2048 | BS_16BPP);
            return;
        case 0x8210:
            /* The base, in 64 KB units: written to bits 5:0, read back from
               13:8. SoftVGA saves the register and writes back what it read,
               so a base in 13:8 is taken as well. */
            dev->vga_base = ((mask & 0x003f) ? (val & 0x3f) : 0) | ((mask & 0x3f00) ? ((val >> 8) & 0x3f) : 0);
            return;
        case 0x8214:
            reg = &dev->vga_latch;
            break;

        default:
            return;
    }

    *reg = (*reg & ~mask) | (val & mask);
}

/* ----------------------------------------------------------------------
   The display controller (4.5).
   ---------------------------------------------------------------------- */
static uint32_t
gxm_pal_to_rgb(uint32_t entry)
{
    const uint8_t r = (entry >> 12) & 0x3f;
    const uint8_t g = (entry >> 6) & 0x3f;
    const uint8_t b = entry & 0x3f;

    return (((r << 2) | (r >> 4)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 2) | (b >> 4));
}

/* The CS5530's gamma RAM, for graphics unless it is bypassed (display
   configuration bit 21). */
static inline uint32_t
gxm_gamma_ram(gxm_video_t *dev, uint32_t rgb)
{
    return (dev->gamma[(rgb >> 16) & 0xff] & 0xff0000) | (dev->gamma[(rgb >> 8) & 0xff] & 0x00ff00) |
           (dev->gamma[rgb & 0xff] & 0x0000ff);
}

static inline uint32_t
gxm_gamma(gxm_video_t *dev, uint32_t rgb)
{
    if (dev->display_cfg & DCFG_GFX_BYPASS)
        return rgb;

    return gxm_gamma_ram(dev, rgb);
}

static inline uint32_t
gxm_rgb16(gxm_video_t *dev, uint16_t p)
{
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if (dev->dc[DC_OUTPUT_CFG] & OC_16FMT) {
        r = (p >> 10) & 0x1f;
        g = (p >> 5) & 0x1f;
        b = p & 0x1f;
        return (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
    }

    r = (p >> 11) & 0x1f;
    g = (p >> 5) & 0x3f;
    b = p & 0x1f;
    return (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}

static int
gxm_display_on(gxm_video_t *dev)
{
    /* The DACs need the display logic on, powered up and not blanked; the
       display controller its FIFO and its timing generator. */
    return ((dev->display_cfg & 0x29) == 0x29) && (dev->dc[DC_GENERAL_CFG] & GC_DFLE) &&
           (dev->dc[DC_TIMING_CFG] & TC_TGEN);
}

static void
gxm_render_cursor(gxm_video_t *dev, uint32_t *line, int y)
{
    const int cx = dev->dc[DC_CURSOR_X] & 0x7ff;
    const int cy = dev->dc[DC_CURSOR_Y] & 0x3ff;
    const int xo = (dev->dc[DC_CURSOR_X] >> 11) & 0x1f;
    const int yo = (dev->dc[DC_CURSOR_Y] >> 11) & 0x1f;
    int       row;

    if (!(dev->dc[DC_GENERAL_CFG] & GC_CURE))
        return;

    /* SoftVGA sets BLNK in text modes for the VGA cursor blink: on for 8 of
       every 16 frames, or 16 of 32 with BKRT. */
    if ((dev->dc[DC_TIMING_CFG] & TC_BLNK) && (dev->blink & ((dev->dc[DC_TIMING_CFG] & TC_BKRT) ? 16 : 8)))
        return;

    row = y - cy;
    if ((row < 0) || ((row + yo) >= 32))
        return;

    /* Two dwords a line: AND mask in the upper word, XOR mask in the lower,
       the leftmost 16 pixels the first dword, the leftmost pixel the MSB. The
       start offset points at the first line shown. */
    for (int half = 0; half < 2; half++) {
        const uint32_t dw = *(uint32_t *) gxm_gmem(dev, (dev->dc[DC_CURS_ST_OFFSET] & 0x3ffffc) + (row << 3) + (half << 2));

        for (int i = 0; i < 16; i++) {
            const int px = (half << 4) + i;
            const int x  = cx + px - xo;
            const int a  = (dw >> (31 - i)) & 1;
            const int xr = (dw >> (15 - i)) & 1;

            if ((px < xo) || (x < 0) || (x >= dev->hdisp))
                continue;

            if (!a)
                line[x] = dev->pal_rgb[0x100 + xr];
            else if (xr)
                line[x] ^= 0x00ffffff;
        }
    }
}

/* ----------------------------------------------------------------------
   The CS5530's video overlay (CS5530 3.8.2, 3.8.3). With VIDE set, the
   display controller streams the video buffer (DC_VID_ST_OFFSET, its
   VID_BUF_SIZE) to the CS5530, which cuts it into source lines of the Video
   Line Size, scales them with its X and Y DDAs, filters them, converts YUV
   4:2:2 to RGB, and shows them in its window where the color key lets them
   through.
   ---------------------------------------------------------------------- */
/* The bytes of Y0, Y1, U and V in a dword, for each Video Input Format. */
static const uint8_t gxm_yuv_order[4][4] = {
    { 1, 3, 0, 2 }, /* U Y0 V Y1 */
    { 2, 0, 3, 1 }, /* Y1 V Y0 U */
    { 0, 2, 1, 3 }, /* Y0 U Y1 V */
    { 0, 2, 3, 1 }, /* Y0 V Y1 U */
};

/* A source pixel's Y, U and V, or with the color space converter bypassed
   its R, G and B (RGB 5:6:5, two pixels a dword like Y0 and Y1). */
static void
gxm_overlay_pixel(gxm_video_t *dev, uint32_t line, int sx, int *c)
{
    const uint32_t cfg = dev->vid[VID_CONFIG];
    const uint32_t dw  = *(uint32_t *) gxm_gmem(dev, line + ((sx >> 1) << 2));

    if (cfg & VC_CSC_BYPASS) {
        const uint16_t p = (sx & 1) ? (dw >> 16) : (dw & 0xffff);

        c[0] = ((p >> 8) & 0xf8) | (p >> 13);
        c[1] = ((p >> 3) & 0xfc) | ((p >> 9) & 0x03);
        c[2] = ((p << 3) & 0xf8) | ((p >> 2) & 0x07);
    } else {
        int fmt = (cfg >> 2) & 3;

        /* The 16-bit interface swaps formats 01 and 10. */
        if ((cfg & VC_16BIT) && ((fmt == 1) || (fmt == 2)))
            fmt ^= 3;

        c[0] = (dw >> (gxm_yuv_order[fmt][sx & 1] << 3)) & 0xff;
        c[1] = (dw >> (gxm_yuv_order[fmt][2] << 3)) & 0xff;
        c[2] = (dw >> (gxm_yuv_order[fmt][3] << 3)) & 0xff;
    }
}

/* Window column dx of a source line: the X DDA from the Initial Buffer Read
   Address, and the 2-tap, 8-phase X filter. */
static void
gxm_overlay_sample(gxm_video_t *dev, uint32_t line, int width, int dx, int *c)
{
    const uint32_t cfg  = dev->vid[VID_CONFIG];
    const uint32_t pos  = (uint32_t) dx * (dev->vid_win[VID_SCALE] & 0x3fff);
    const int      init = (((cfg >> 16) & 0xff) | (((cfg >> 24) & 1) << 8)) << 1;
    const int      ph   = (pos >> 10) & 7;
    int            sx   = init + (int) (pos >> 13);
    int            n[3];

    if (sx >= width)
        sx = width - 1;
    gxm_overlay_pixel(dev, line, sx, c);

    if ((cfg & VC_XFILT) && ph && ((sx + 1) < width)) {
        gxm_overlay_pixel(dev, line, sx + 1, n);
        for (int i = 0; i < 3; i++)
            c[i] = ((c[i] * (8 - ph)) + (n[i] * ph)) >> 3;
    }
}

/* CCIR 601-1, clamped. */
static uint32_t
gxm_csc(int y, int u, int v)
{
    const int yy = 1192 * (y - 16);
    int       r  = (yy + (1634 * (v - 128))) >> 10;
    int       g  = (yy - (833 * (v - 128)) - (400 * (u - 128))) >> 10;
    int       b  = (yy + (2066 * (u - 128))) >> 10;

    r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
    g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
    b = (b < 0) ? 0 : ((b > 255) ? 255 : b);

    return (r << 16) | (g << 8) | b;
}

/* The CS5530's mix of a line: the video window over the graphics, then the
   gamma RAM for whichever stream does not bypass it. */
static void
gxm_output_line(gxm_video_t *dev, uint32_t *line, int y)
{
    const uint32_t cfg   = dev->vid[VID_CONFIG];
    const uint32_t key   = dev->vid[VID_KEY];
    const uint32_t kmask = dev->vid[VID_MASK];
    /* The bits the display controller sends of a graphics pixel, for the key
       compare: 6 a color from the palette, 5 or 6 in 16 BPP. */
    const int      bpp16 = !(dev->dc[DC_OUTPUT_CFG] & OC_8BPP) && !(dev->dc[DC_TIMING_CFG] & TC_PLNR);
    const uint32_t sig   = bpp16 ? ((dev->dc[DC_OUTPUT_CFG] & OC_16FMT) ? 0xf8f8f8 : 0xf8fcf8) : 0xfcfcfc;
    uint32_t       src[2] = { 0, 0 };
    int            width = 0;
    int            yph   = 0;
    int            x0    = 0;
    int            x1    = 0;

    if ((cfg & VC_ENABLE) && (dev->dc[DC_GENERAL_CFG] & GC_VIDE)) {
        /* The window is in sync-relative positions (Table 3-73). */
        const int      hsyncend = (((dev->dc[DC_H_TIMING_3] >> 19) & 0xff) + 1) << 3;
        const int      hadj     = dev->htotal - hsyncend - 13;
        const int      vadj     = dev->vtotal - dev->vsyncend + 1;
        const int      y0       = (int) (dev->vid_win[VID_Y] & 0x7ff) - vadj;
        const int      y1       = (int) ((dev->vid_win[VID_Y] >> 16) & 0x7ff) - vadj;
        const uint32_t dwords   = ((cfg >> 8) & 0xff) | (((cfg >> 27) & 1) << 8);
        const uint32_t buf      = ((dev->dc[DC_BUF_SIZE] >> 16) & 0x3fff) << 6;
        const uint32_t lines    = dwords ? (buf / (dwords << 2)) : 0;

        if ((y >= y0) && (y < y1) && lines) {
            const uint32_t pos  = (uint32_t) (y - y0) * ((dev->vid_win[VID_SCALE] >> 16) & 0x3fff);
            const uint32_t base = dev->dc[DC_VID_ST_OFFSET] & 0x1ffff0;
            uint32_t       sy   = pos >> 13;

            if (sy >= lines)
                sy = lines - 1;
            src[0] = base + (sy * (dwords << 2));
            src[1] = src[0];
            yph    = (pos >> 10) & 7;
            if ((cfg & VC_YFILT) && yph && ((sy + 1) < lines))
                src[1] += dwords << 2;
            else
                yph = 0;

            width = dwords << 1;
            x0    = (int) (dev->vid_win[VID_X] & 0x7ff) - hadj;
            x1    = (int) ((dev->vid_win[VID_X] >> 16) & 0x7ff) - hadj;
        }
    }

    for (int x = 0; x < dev->hdisp; x++) {
        int      c[3];
        int      n[3];
        uint32_t vid;
        int      show;

        if ((x < x0) || (x >= x1)) {
            line[x] = gxm_gamma(dev, line[x]);
            continue;
        }

        gxm_overlay_sample(dev, src[0], width, x - x0, c);
        if (yph) {
            gxm_overlay_sample(dev, src[1], width, x - x0, n);
            for (int i = 0; i < 3; i++)
                c[i] = ((c[i] * (8 - yph)) + (n[i] * yph)) >> 3;
        }
        vid = (cfg & VC_CSC_BYPASS) ? ((c[0] << 16) | (c[1] << 8) | c[2]) : gxm_csc(c[0], c[1], c[2]);

        /* Graphics matching the key let the video through; video matching it
           lets the graphics through. */
        if (dev->display_cfg & DCFG_VID_KEY)
            show = ((vid ^ key) & kmask) != 0;
        else
            show = (((line[x] & sig) ^ key) & kmask) == 0;

        if (show)
            line[x] = (dev->display_cfg & DCFG_GFX_BYPASS) ? gxm_gamma_ram(dev, vid) : vid;
        else
            line[x] = gxm_gamma(dev, line[x]);
    }
}

static void
gxm_render_line(gxm_video_t *dev, int y)
{
    uint32_t *line   = buffer32->line[y];
    const int pxdb   = !!(dev->dc[DC_TIMING_CFG] & TC_PXDB);
    const int bpp16  = !(dev->dc[DC_OUTPUT_CFG] & OC_8BPP);
    const int ch4s   = !!(dev->dc[DC_GENERAL_CFG] & GC_CH4S);
    const int planar = !!(dev->dc[DC_TIMING_CFG] & TC_PLNR);
    /* Blink, 16 of every 32 frames shown (32 of 64 with BKRT). Text: SoftVGA
       draws a blinking character's foreground as 80h | bg << 4 | fg and loads
       the upper palette half with the background colours, so bit 7 is
       ignored while shown. Planar: a pixel has no bit 7, so it is set while
       hidden; SoftVGA's upper half then blinks the pixels with plane 2 set
       and plane 0 clear to colour 0, as mode 0Fh does on a VGA. */
    const int blnk   = !!(dev->dc[DC_TIMING_CFG] & TC_BLNK);
    const int hidden = blnk && (dev->blink & ((dev->dc[DC_TIMING_CFG] & TC_BKRT) ? 32 : 16));
    const int pmask  = (blnk && !hidden) ? 0x7f : 0xff;
    const int pblink = hidden ? 0x80 : 0x00;
    uint32_t  addr   = dev->ma;
    int       x      = 0;

    if (!gxm_display_on(dev)) {
        for (x = 0; x < dev->hdisp; x++)
            line[x] = 0x00000000;
        return;
    }

    while (x < dev->hdisp) {
        uint32_t px[8];
        int      n;

        if (planar) {
            /* Four planes in a dword, eight 4-bit pixels, plane 3 the MSB. */
            const uint32_t dw = *(uint32_t *) gxm_gmem(dev, addr);

            for (int i = 0; i < 8; i++) {
                const int sh = 7 - i;

                px[i] = dev->pal_rgb[pblink | ((dw >> sh) & 1) | (((dw >> (8 + sh)) & 1) << 1) |
                                     (((dw >> (16 + sh)) & 1) << 2) | (((dw >> (24 + sh)) & 1) << 3)];
            }
            n = 8;
            addr += 4;
        } else if (bpp16) {
            px[0] = gxm_rgb16(dev, *(uint16_t *) gxm_gmem(dev, addr));
            n     = 1;
            addr += 2;
        } else {
            px[0] = dev->pal_rgb[*gxm_gmem(dev, addr) & pmask];
            n     = 1;
            /* Chain 4: the pixels are in every fourth dword. */
            addr += (ch4s && ((addr & 3) == 3)) ? 13 : 1;
        }

        for (int i = 0; (i < n) && (x < dev->hdisp); i++) {
            line[x++] = px[i];
            if (pxdb && (x < dev->hdisp))
                line[x++] = px[i];
        }
    }

    gxm_render_cursor(dev, line, y);
    gxm_output_line(dev, line, y);
}

static void
gxm_poll(void *priv)
{
    gxm_video_t *dev = (gxm_video_t *) priv;

    if (!dev->linepos) {
        /* The active part of a line, then the blank. */
        timer_advance_u64(&dev->timer, dev->dispofftime);
        dev->linepos = 1;
        dev->dc[DC_TIMING_CFG] |= TC_DNA;

        if (dev->displine < dev->vdisp) {
            if (dev->displine == 0)
                video_wait_for_buffer();
            if (dev->output)
                gxm_render_line(dev, dev->displine);

            /* The next frame buffer line; line doubling scans each twice. */
            if (!(dev->dc[DC_GENERAL_CFG] & GC_LDBL) || (dev->displine & 1))
                dev->ma += (dev->dc[DC_LINE_DELTA] & 0x3ff) << 2;
            /* The split screen starts over at offset 0. */
            if ((dev->dc[DC_GENERAL_CFG] & GC_SSLC) && (dev->displine == (int) (dev->dc[DC_SS_LINE_CMP] & 0x7ff)))
                dev->ma = 0;
        }
    } else {
        timer_advance_u64(&dev->timer, dev->dispontime);
        dev->linepos = 0;

        dev->displine++;
        if (dev->displine == dev->vdisp) {
            dev->dc[DC_TIMING_CFG] |= TC_VNA;

            if (dev->output) {
                if ((dev->hdisp != xsize) || (dev->vdisp != ysize) || video_force_resize_get()) {
                    xsize = dev->hdisp;
                    ysize = dev->vdisp;
                    if (xsize < 64)
                        xsize = 640;
                    if (ysize < 32)
                        ysize = 480;
                    set_screen_size(xsize, ysize);

                    if (video_force_resize_get())
                        video_force_resize_set(0);
                }

                video_blit_memtoscreen(0, 0, xsize, ysize);
                frames++;

                video_res_x = dev->hdisp;
                video_res_y = dev->vdisp;
                video_bpp   = (dev->dc[DC_OUTPUT_CFG] & OC_8BPP) ? 8 : 16;
            }
            dev->blink++;
        }

        if (dev->displine == dev->vsyncstart) {
            /* The vertical interrupt, for SoftVGA's 3C2h bit 7, and the serial
               packet's VID_IRQ to the CS5530 (6.7). */
            if (dev->dc[DC_TIMING_CFG] & TC_VIEN) {
                dev->dc[DC_TIMING_CFG] |= TC_VINT;
                cyrix_gxm_vid_irq();
            }
        }

        if (dev->displine >= dev->vtotal) {
            dev->displine = 0;
            dev->ma       = dev->dc[DC_FB_ST_OFFSET] & 0x3ffff0;
            dev->dc[DC_TIMING_CFG] &= ~TC_VNA;
            /* Video Register Update: the window and scale take effect
               together, at the frame's start. */
            if (dev->vid[VID_CONFIG] & VC_UPDATE)
                memcpy(&dev->vid_win[VID_X], &dev->vid[VID_X], 3 * sizeof(uint32_t));
            gxm_recalctimings(dev);
        }

        if (dev->displine < dev->vdisp)
            dev->dc[DC_TIMING_CFG] &= ~TC_DNA;
    }
}

static void
gxm_recalctimings(gxm_video_t *dev)
{
    double clock = dev->dotclock;
    double line;

    dev->htotal     = ((((dev->dc[DC_H_TIMING_1] >> 19) & 0xff) + 1) << 3);
    dev->hdisp      = ((((dev->dc[DC_H_TIMING_1] >> 3) & 0xff) + 1) << 3);
    dev->vtotal     = ((dev->dc[DC_V_TIMING_1] >> 16) & 0x7ff) + 1;
    dev->vdisp      = (dev->dc[DC_V_TIMING_1] & 0x7ff) + 1;
    dev->vsyncstart = (dev->dc[DC_V_TIMING_3] & 0x7ff) + 1;
    dev->vsyncend   = ((dev->dc[DC_V_TIMING_3] >> 16) & 0x7ff) + 1;

    /* Until the timing generator runs, a VGA-like 640x480 frame keeps the
       status bits moving. */
    if (!(dev->dc[DC_TIMING_CFG] & TC_TGEN) || (dev->htotal <= dev->hdisp) || (dev->vtotal <= dev->vdisp) ||
        (dev->hdisp > 2048) || (dev->vdisp > 2048)) {
        dev->htotal     = 800;
        dev->hdisp      = 640;
        dev->vtotal     = 525;
        dev->vdisp      = 480;
        dev->vsyncstart = 490;
        dev->vsyncend   = 492;
    }

    /* DCLK_MUL divides the DCLK pin into the internal DOTCLK: 01 halves it,
       10 and 11 pass it, 00 forces it low (the GX1 book; the GXm book's
       1x/2x/4x is wrong, VSA programs twice the dot clock with 01). DDCK
       halves it again. */
    switch ((dev->dc[DC_GENERAL_CFG] >> 6) & 3) {
        case 0:
            clock = 0.0;
            break;
        case 1:
            clock /= 2.0;
            break;
        default:
            break;
    }
    if (dev->dc[DC_GENERAL_CFG] & GC_DDCK)
        clock /= 2.0;
    if (clock < 1000000.0)
        clock = 25175000.0;

    line             = (double) dev->htotal / clock;
    dev->dispontime  = (uint64_t) (((double) dev->hdisp / clock) * 1000000.0 * (double) TIMER_USEC);
    dev->dispofftime = (uint64_t) ((line - ((double) dev->hdisp / clock)) * 1000000.0 * (double) TIMER_USEC);
    if (dev->dispontime < TIMER_USEC / 10)
        dev->dispontime = TIMER_USEC / 10;
    if (dev->dispofftime < TIMER_USEC / 10)
        dev->dispofftime = TIMER_USEC / 10;
}

uint32_t
gxm_dc_read(uint32_t addr, void *priv)
{
    gxm_video_t *dev = (gxm_video_t *) priv;
    const int    reg = (addr >> 2) & 0x3f;
    uint32_t     ret;

    if ((dev == NULL) || (reg >= DC_REGS))
        return 0x00000000;

    switch (reg) {
        case DC_V_LINE_CNT:
            ret = dev->displine & 0x7ff;
            break;
        case DC_PAL_DATA:
            ret = dev->palette[dev->dc[DC_PAL_ADDRESS] & 0x1ff];
            if ((dev->dc[DC_PAL_ADDRESS] & 0x1ff) < 0x104)
                dev->dc[DC_PAL_ADDRESS]++;
            break;
        case DC_RSVD_1C:
        case DC_RSVD_2C:
        case DC_RSVD_64:
        case DC_RSVD_6C:
            ret = 0x00000000;
            break;

        default:
            ret = dev->dc[reg];
            break;
    }

    return ret;
}

void
gxm_dc_write(uint32_t addr, uint32_t val, uint32_t mask, void *priv)
{
    gxm_video_t *dev = (gxm_video_t *) priv;
    const int    reg = (addr >> 2) & 0x3f;
    uint32_t     old;

    if ((dev == NULL) || (reg >= DC_REGS))
        return;

    /* The critical registers write only once unlocked (Table 4-31). */
    if ((dc_unlocked_regs & (1 << reg)) && ((dev->dc[DC_UNLOCK] & 0xffff) != DC_UNLOCK_CODE))
        return;
    /* The timing registers do not, with the timing generator on. */
    if ((reg >= DC_H_TIMING_1) && (reg <= DC_FP_V_TIMING) && (dev->dc[DC_TIMING_CFG] & TC_TGEN))
        return;

    old = dev->dc[reg];
    val = (old & ~mask) | (val & mask);

    gxm_video_log("DC: [W] %04X = %08X\n", addr & 0xffff, val);

    switch (reg) {
        case DC_UNLOCK:
            dev->dc[reg] = val & 0xffff;
            break;
        case DC_TIMING_CFG:
            /* VINT, VNA, DNA, SENS and DDCI read only; VIEN clear clears
               VINT. */
            dev->dc[reg] = (val & 0x000fffff) | (old & (TC_VINT | TC_VNA | TC_DNA));
            if (!(val & TC_VIEN))
                dev->dc[reg] &= ~TC_VINT;
            /* A monitor is on the DACs: SENS high. */
            dev->dc[reg] |= (1 << 28);
            if ((old ^ val) & TC_TGEN)
                gxm_recalctimings(dev);
            break;
        case DC_GENERAL_CFG:
            dev->dc[reg] = val;
            if ((old ^ val) & (GC_DDCK | (3 << 6)))
                gxm_recalctimings(dev);
            break;
        case DC_V_LINE_CNT:
            break;
        case DC_PAL_ADDRESS:
            dev->dc[reg] = val & 0x1ff;
            break;
        case DC_PAL_DATA: {
            const uint32_t idx = dev->dc[DC_PAL_ADDRESS] & 0x1ff;

            if (idx < 0x105) {
                dev->palette[idx] = val & 0x3ffff;
                dev->pal_rgb[idx] = gxm_pal_to_rgb(val);
            }
            if (idx < 0x104)
                dev->dc[DC_PAL_ADDRESS]++;
            break;
        }
        case DC_RSVD_1C:
        case DC_RSVD_2C:
        case DC_RSVD_64:
        case DC_RSVD_6C:
            break;

        default:
            dev->dc[reg] = val;
            break;
    }
}

void
gxm_video_set_dotclock(void *priv, double clock)
{
    gxm_video_t *dev = (gxm_video_t *) priv;

    if (dev == NULL)
        return;

    dev->dotclock = clock;
    gxm_recalctimings(dev);
}

void
gxm_video_set_display(void *priv, uint32_t display_cfg, const uint32_t *gamma)
{
    gxm_video_t *dev = (gxm_video_t *) priv;

    if (dev == NULL)
        return;

    dev->display_cfg = display_cfg;
    if (gamma != NULL)
        memcpy(dev->gamma, gamma, sizeof(dev->gamma));
}

void
gxm_video_set_overlay(void *priv, int reg, uint32_t val)
{
    gxm_video_t *dev = (gxm_video_t *) priv;

    if ((dev == NULL) || (reg < 0) || (reg >= VID_REGS))
        return;

    dev->vid[reg] = val;
    /* The window and scale wait for the next frame with Video Register
       Update. */
    if ((reg >= VID_X) && (reg <= VID_SCALE) && !(dev->vid[VID_CONFIG] & VC_UPDATE))
        dev->vid_win[reg] = val;
}

static void
gxm_video_reset(void *priv)
{
    gxm_video_t *dev = (gxm_video_t *) priv;

    memset(dev->dc, 0x00, sizeof(dev->dc));
    dev->dc[DC_TIMING_CFG] = (1 << 28);
    dev->vga_read          = 0x00000000;
    dev->dst_xy = dev->wh = dev->src_xy = dev->src_color = 0;
    dev->pat_color[0] = dev->pat_color[1] = 0;
    memset(dev->pat_data, 0x00, sizeof(dev->pat_data));
    dev->raster_mode = dev->vector_mode = dev->blt_mode = dev->blt_status = 0;

    dev->displine = 0;
    dev->linepos  = 0;
    gxm_recalctimings(dev);
}

static void
gxm_video_close(void *priv)
{
    gxm_video_t *dev = (gxm_video_t *) priv;

    gxm_video = NULL;
    free(dev);
}

static void
gxm_video_speed_changed(void *priv)
{
    gxm_recalctimings((gxm_video_t *) priv);
}

static void
gxm_video_force_redraw(UNUSED(void *priv))
{
    /* The whole frame is drawn each frame. */
}

static void *
gxm_video_init(UNUSED(const device_t *info))
{
    static const uint32_t base[3] = { 0xa0000, 0xb0000, 0xb8000 };
    gxm_video_t          *dev     = (gxm_video_t *) calloc(1, sizeof(gxm_video_t));

    gxm_video = dev;

    for (int i = 0; i < 3; i++) {
        mem_mapping_add(&dev->vga_mapping[i], base[i], 0,
                        gxm_vga_read, gxm_vga_readw, gxm_vga_readl,
                        gxm_vga_write, gxm_vga_writew, gxm_vga_writel,
                        NULL, MEM_MAPPING_EXTERNAL, dev);
        mem_mapping_disable(&dev->vga_mapping[i]);
    }

    /* The display is the machine's own one, unless a card is used. */
    dev->output = (gfxcard[0] == VID_INTERNAL);
    if (dev->output)
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_gxm);

    for (int i = 0; i < 256; i++)
        dev->gamma[i] = i * 0x010101;

    timer_add(&dev->timer, gxm_poll, dev, 1);

    gxm_video_reset(dev);

    return dev;
}

const device_t cyrix_gxm_video_device = {
    .name          = "Cyrix MediaGX MMX Graphics",
    .internal_name = "cyrix_gxm_video",
    .flags         = 0,
    .local         = 0,
    .init          = gxm_video_init,
    .close         = gxm_video_close,
    .reset         = gxm_video_reset,
    .available     = NULL,
    .speed_changed = gxm_video_speed_changed,
    .force_redraw  = gxm_video_force_redraw,
    .config        = NULL
};
