/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- 2D GUI engine.
 *
 *          C interpreter for the 2D datapath: solid and pattern paints,
 *          screen-to-screen and host-data blits, mono expansion, the
 *          scaler, lines, polyscanlines, color compare and the ROP3
 *          stage. It runs from direct register writes and from CCE
 *          type-3 packets (vid_ati_rage128_pm4.c). Where an optional GPU
 *          backend is present, some ops are resolved here and queued to it
 *          instead of walked on the CPU.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 Software Development Guide",
 *              SDK-G04000 Rev 0.01, August 1999 (the CCE packet
 *              layouts). Cited as "SDK: ...".
 *
 *          [3] ATI Technologies, "RAGE 128 Register Reference Supplement:
 *              Registers for CCE 3D Packets", 1999. Cited as "CCE 3D
 *              supplement" with the register name; it has no page numbers.
 *
 *          [4] ATI Technologies, "RAGE PRO and Derivatives Programmer's
 *              Guide", PRG-215R3-00-10 Rev 1.0, 2000. The Rage 128 guides
 *              leave a few datapath details out (the packed YUV byte
 *              order, the YUV to RGB equations, what DST_X_TILE and
 *              DST_Y_TILE do to the destination position); where this
 *              file takes them from the earlier chip's guide it says so,
 *              cited as "Rage Pro guide, sec. ...".
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
#include <86box/vid_ati_rage128_regs.h>

/* The executors read the datapath registers DP_MIX and DP_DATATYPE, not
   DP_GUI_MASTER_CNTL. Several GMC fields are documented as "Mapped to"
   a DP_MIX or DP_DATATYPE field (RRG: DP_GUI_MASTER_CNTL,
   pp. 3-171-3-174 / PDF 189-192), so rage128_2d_apply_gmc copies them
   there on every GMC write, and a driver that programs DP_MIX or
   DP_DATATYPE directly gets the same result. A GMC read returns the
   value last written. */
/* DP_DATATYPE fields: DP_DST_DATATYPE [3:0], DP_BRUSH_DATATYPE [11:8],
   DP_SRC_DATATYPE [17:16], DP_BYTE_PIX_ORDER [30] (RRG: DP_DATATYPE,
   pp. 3-168-3-170 / PDF 186-188). For example 0x000f0f02 is an 8 bpp
   pseudocolor destination, brush type 15 (no brush) and a color source.
   The GMC carries the destination type at [11:8] and the brush type at
   [7:4] instead; rage128_2d_apply_gmc moves each to its place here. */
static inline int
rage128_dp_dst_dt(const rage128_t *dev)
{
    return dev->dp_datatype & 0xf;
}

static inline int
rage128_dp_brush_dt(const rage128_t *dev)
{
    return (dev->dp_datatype >> 8) & 0xf;
}

static inline int
rage128_dp_src_dt(const rage128_t *dev)
{
    return (dev->dp_datatype >> 16) & 3;
}

static inline int
rage128_dp_lsb_first(const rage128_t *dev)
{
    return (dev->dp_datatype >> 30) & 1;
}

static inline uint8_t
rage128_dp_rop3(const rage128_t *dev)
{
    return (uint8_t) ((dev->dp_mix >> 16) & 0xff);
}

static inline int
rage128_dp_src_source(const rage128_t *dev)
{
    return (dev->dp_mix >> 8) & 7;
}

/* BRUSH_Y_X sets where the screen-aligned 8x8 pattern starts: x [4:0]
   and y [12:8] are the brush alignment, and [20:16] is the start of the
   line pattern pointer (SDK: Table F-1, p. F-16 / PDF 306; the RRG
   names the register only through GMC_LD_BRUSH_Y_X). The X driver
   writes it the same way (xf86-video-r128 r128_accel.c, XAA version,
   R128SubsequentMono8x8PatternFillRect and
   R128SubsequentDashedBresenhamLine). The code adds the alignment to
   the screen coordinate before the mod 8; that direction is modeled,
   not documented. In captured Windows 2000 traffic the display driver
   writes 8 - (origin & 7), always 1..8, so 8 acts as 0. A packet loads
   the register from the dword that follows the brush data when its GMC
   has bit 31 set. */
static inline int
rage128_2d_pat_col(const rage128_t *dev, int x)
{
    return (x + (int) (dev->brush_yx & 0x1f)) & 7;
}

static inline int
rage128_2d_pat_row(const rage128_t *dev, int y)
{
    return (y + (int) ((dev->brush_yx >> 8) & 0x1f)) & 7;
}

/* One pixel of a mono pattern at screen column x. DP_BYTE_PIX_ORDER
   "reverses the pixel order within each byte in monochrome modes"
   (RRG: DP_DATATYPE, p. 3-170 / PDF 188). The code applies it to the
   brush as well as the source: the X driver sets the bit for its
   stipples and dashes and passes the pattern bits LSB-first. So the 8x8
   row byte reads bit c instead of bit 7 - c. The 32x1 line pattern is
   read as a dword, bit 31 first; with the bit set it is read bit 0
   first, which is what the X driver's dashed line expects
   (xf86-video-r128 r128_accel.c, XAA version, R128SetupForDashedLine).
   The guide does not say how the bit applies to a 32x1 pattern. */
static inline int
rage128_2d_pat8_bit(const rage128_t *dev, uint8_t row, int x)
{
    int c = rage128_2d_pat_col(dev, x);

    return rage128_dp_lsb_first(dev) ? (row >> c) & 1 : (row >> (7 - c)) & 1;
}

static inline int
rage128_2d_pat32_bit(const rage128_t *dev, uint32_t pat32, int phase)
{
    return rage128_dp_lsb_first(dev) ? (pat32 >> (phase & 31)) & 1
                                     : (pat32 >> (31 - (phase & 31))) & 1;
}

/* Brush types 8 and 9 are the 32x32 mono pattern (RRG: DP_DATATYPE,
   p. 3-169 / PDF 187). The mono pointer then points at 32 dwords, one
   per row, each read like the 32x1 line pattern, with BRUSH_Y_X taken
   mod 32. The GPU queue paths model only the 8x8 tile and refuse this
   brush. */
static inline int
rage128_2d_brush_32x32(const rage128_t *dev)
{
    int bt = rage128_dp_brush_dt(dev);

    return bt == 8 || bt == 9;
}

/* One mono brush pixel at screen (x, y). */
static inline int
rage128_2d_mono_bit(const rage128_t *dev, const uint8_t *mono, int x, int y)
{
    if (rage128_2d_brush_32x32(dev)) {
        const uint32_t *rows = (const uint32_t *) (const void *) mono;

        return rage128_2d_pat32_bit(dev,
                                    rows[(y + (int) ((dev->brush_yx >> 8) & 0x1f)) & 31],
                                    x + (int) (dev->brush_yx & 0x1f));
    }
    return rage128_2d_pat8_bit(dev, mono[rage128_2d_pat_row(dev, y)], x);
}

/* Bytes per pixel of a destination datatype (GMC_DST_DATATYPE values,
   RRG: DP_GUI_MASTER_CNTL, p. 3-173 / PDF 191). */
static int
rage128_2d_bpp(uint32_t dst_datatype)
{
    switch (dst_datatype) {
        case 3: /* aRGB 1555 */
        case 4: /* RGB 565   */
        case 15:
            return 2;
        case 5: /* 24 bpp RGB */
            return 3;
        case 6:  /* aRGB 8888  */
        case 14: /* aYUV 8888  */
            return 4;
        case 11: /* YUV 422 packed VYUY */
        case 12: /* YUV 422 packed YVYU */
            return 2;
        default: /* 2/7/8/9 are the 8 bpp types */
            return 1;
    }
}

/* Row stride in bytes from a latched pitch (the pitch field times 8).
   The field counts pixels, except at 24 bpp, where it "is programmed in
   bytes*8" (RRG: DST_PITCH, p. 3-137 / PDF 155), as the X driver does
   (xf86-video-r128 r128_accel.c R128EngineInit and r128_exa.c
   R128GetPixmapOffsetPitch). So at 3 bytes per pixel the value is
   already the byte stride and is not multiplied again. X still steps 3
   bytes per pixel. */
static inline uint32_t
rage128_2d_stride(uint32_t pitch, int bpp)
{
    return (bpp == 3) ? pitch : pitch * (uint32_t) bpp;
}

/* Pack 8-bit R, G and B into the destination pixel format. Only the
   gradient fill uses it (rage128_2d_grad_px), whose channels come from
   8.16 fixed-point start and slope values. An 8 bpp destination has no
   RGB packing here, so it gets the brush foreground color. */
static uint32_t
rage128_2d_pack_rgb(rage128_t *dev, int r, int g, int b)
{
    switch (rage128_dp_dst_dt(dev)) {
        case 4: /* RGB 565 */
            return ((uint32_t) (r >> 3) << 11) | ((uint32_t) (g >> 2) << 5) | (uint32_t) (b >> 3);
        case 3: /* aRGB 1555 */
            return 0x8000u | ((uint32_t) (r >> 3) << 10) | ((uint32_t) (g >> 3) << 5) | (uint32_t) (b >> 3);
        case 15: /* aRGB 4444 */
            return 0xf000u | ((uint32_t) (r >> 4) << 8) | ((uint32_t) (g >> 4) << 4) | (uint32_t) (b >> 4);
        case 5: /* 24 bpp */
            return ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
        case 6: /* aRGB 8888 */
            return 0xff000000u | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
        default: /* 8 bpp: keep the brush color */
            return dev->dp_brush_frgd_clr;
    }
}

/* ROP3: evaluate any of the 256 ternary raster ops bitwise on 32-bit
   values. The DP_ROP3 field holds a "Windows 3.1 ROP3 code" (RRG:
   DP_MIX, p. 3-171 / PDF 189), which is the truth table itself: bit
   (p << 2 | s << 1 | d) of the code is the result for that pattern,
   source and destination bit. */
static uint32_t
rage128_rop3(uint8_t rop, uint32_t p, uint32_t s, uint32_t d)
{
    uint32_t r = 0;

    /* clang-format off */
    if (rop & 0x01) r |= ~p & ~s & ~d;
    if (rop & 0x02) r |= ~p & ~s &  d;
    if (rop & 0x04) r |= ~p &  s & ~d;
    if (rop & 0x08) r |= ~p &  s &  d;
    if (rop & 0x10) r |=  p & ~s & ~d;
    if (rop & 0x20) r |=  p & ~s &  d;
    if (rop & 0x40) r |=  p &  s & ~d;
    if (rop & 0x80) r |=  p &  s &  d;
    /* clang-format on */
    return r;
}

/* Fill `count` pixels of `bpp` bytes at `dst` with the pixel `px[0..bpp-1]`.
   memcpy-only (no unaligned word casts -- this builds for arm64) with a
   doubling stamp so a wide fill is O(n) with log-many copies. Caller owns the
   contiguity guard (r128_surf_run). */
static inline void
r128_row_fill(uint8_t *dst, const uint8_t *px, int bpp, int count)
{
    uint32_t total = (uint32_t) count * (uint32_t) bpp;
    uint32_t done;

    if (bpp == 1) {
        memset(dst, px[0], (size_t) count);
        return;
    }
    memcpy(dst, px, (size_t) bpp);
    for (done = (uint32_t) bpp; done < total;) {
        uint32_t chunk = done < total - done ? done : total - done;
        memcpy(dst + done, dst, (size_t) chunk);
        done += chunk;
    }
}

/* Output stage: DP_WRITE_MSK selects which destination bits a 2D op may
   change (RRG: DP_WRITE_MSK, p. 3-171 / PDF 189). The guide does not
   say how the 32-bit mask lines up with pixels narrower than 32 bits.
   The code applies it per pixel: byte b of a pixel uses mask byte
   b & 3, so an 8 bpp pixel always uses the low byte. That matches the X
   driver, which writes the X planemask as is, without repeating it
   across the dword (xf86-video-r128 r128_exa.c R128PrepareSolid), and
   matches the 3D path. */
static inline void
rage128_2d_store(uint8_t *dp, uint8_t v, uint8_t mb)
{
    *dp = (uint8_t) ((v & mb) | (*dp & ~mb));
}

/* Color compare, tested per pixel at the output stage (RRG:
   CLR_CMP_CNTL, pp. 3-178-3-179 / PDF 196-197). CLR_CMP_SRC picks the
   comparators that take part: 0 the destination, 1 the source, 2 both.
   With both, "the final decision is based on the agreement between
   decisions made separately" (SDK: Table F-37, p. F-44 / PDF 334), so
   either one can block the store. That keeps mode 2 working with one
   function left at 0 (always draw): in captured Windows 2000 traffic
   the display driver's color-keyed blits write 0x02000005, mode 2 with
   CLR_CMP_FN_SRC 5 and CLR_CMP_FN_DST 0. Function 4 draws when the
   color equals the key, 5 when it differs, 1 never. (The SDK's table
   describes destination functions 4 and 5 the other way round; the
   code follows the RRG.) Source function 7 writes a source pixel equal
   to the key XORed with the foreground color, DP_SRC_FRGD_CLR cut to
   the pixel width, and writes every other source pixel as it is (SDK:
   Table F-37, p. F-44 / PDF 334: "destPixel = srcPixel XOR
   foregrndColor if srcPixel is equal to" the reference). The reserved
   function codes and CLR_CMP_SRC 3 act as always-draw. The keys are
   compared under CLR_CMP_MSK cut to the pixel width. */
struct r128_ccmp {
    int      src_on, dst_on;
    int      fn_src, fn_dst;
    uint32_t key_src, key_dst;
    uint32_t smask, dmask;
    uint32_t flip;
};

/* has_src: whether the op has a source (memory, host data or the 3D
   pipe). An op without one has no source color to compare, so only the
   destination comparator can take part. Returns nonzero when a
   comparator that takes part can skip or change a store. */
static int
rage128_2d_ccmp_setup(const rage128_t *dev, int has_src, int sbpp, int dbpp,
                      struct r128_ccmp *cc)
{
    int      sel = (dev->clr_cmp_cntl >> 24) & 3;
    int      fs  = dev->clr_cmp_cntl & 7;
    int      fd  = (dev->clr_cmp_cntl >> 8) & 7;
    uint32_t spm = (sbpp >= 4) ? 0xffffffffu : ((1u << (sbpp * 8)) - 1u);
    uint32_t dpm = (dbpp >= 4) ? 0xffffffffu : ((1u << (dbpp * 8)) - 1u);

    cc->fn_src  = (fs == 1 || fs == 4 || fs == 5 || fs == 7) ? fs : 0;
    cc->fn_dst  = (fd == 1 || fd == 4 || fd == 5) ? fd : 0;
    cc->src_on  = has_src && (sel == 1 || sel == 2);
    cc->dst_on  = (sel == 0 || sel == 2);
    cc->smask   = dev->clr_cmp_mask & spm;
    cc->dmask   = dev->clr_cmp_mask & dpm;
    cc->key_src = dev->clr_cmp_clr_src & cc->smask;
    cc->key_dst = dev->clr_cmp_clr_dst & cc->dmask;
    cc->flip    = dev->dp_src_frgd_clr & dpm;
    return (cc->src_on && cc->fn_src != 0) || (cc->dst_on && cc->fn_dst != 0);
}

/* 0 = skip the store, 1 = store the datapath result, 2 = store the
   compared source color XORed with flip (CLR_CMP_FN_SRC 7 on a key
   match). */
static inline int
rage128_2d_ccmp_px(const struct r128_ccmp *cc, uint32_t s, uint32_t d)
{
    int flip = 0;

    if (cc->src_on) {
        int eq = ((s & cc->smask) == cc->key_src);

        if (cc->fn_src == 1 || (cc->fn_src == 4 && !eq)
            || (cc->fn_src == 5 && eq))
            return 0;
        if (cc->fn_src == 7 && eq)
            flip = 1;
    }
    if (cc->dst_on) {
        int eq = ((d & cc->dmask) == cc->key_dst);

        if (cc->fn_dst == 1 || (cc->fn_dst == 4 && !eq)
            || (cc->fn_dst == 5 && eq))
            return 0;
    }
    return flip ? 2 : 1;
}

void
rage128_2d_reset(rage128_t *dev)
{
    dev->dp_gui_master_cntl      = 0;
    dev->sc_top_left             = 0;
    dev->sc_bottom_right         = 0x1fff1fff;
    dev->default_sc_bottom_right = 0x1fff1fff;
    dev->dp_write_mask           = 0xffffffff;
    /* The color compare registers, the mask included, default to 0
       (RRG: CLR_CMP_CNTL, pp. 3-178-3-179 / PDF 196-197). Function 0
       is always draw, so the zero mask has no effect until a
       comparator is enabled. */
    dev->clr_cmp_cntl    = 0;
    dev->clr_cmp_clr_src = 0;
    dev->clr_cmp_clr_dst = 0;
    dev->clr_cmp_mask    = 0;
    /* The AUX1_SC_ENB, AUX2_SC_ENB and AUX3_SC_ENB bits are "set to 0
       on Chip Reset" (RRG: AUX_SC_CNTL, pp. 3-156-3-157 / PDF
       174-175). */
    dev->aux_sc_cntl = 0;
    memset(dev->aux_sc_rect, 0, sizeof(dev->aux_sc_rect));
}

/* DST_PITCH in pixels: the [9:0] field times 8, times the DST_PITCH_ADJ
   [18:17] multiplier (RRG: DST_PITCH, pp. 3-137-3-138 / PDF 155-156).
   The DST_PITCH page says only that the pitch "should be multiplied
   prior to use"; the values used here, 1 = x2 and 2 = x4, are the ones
   the guide gives for SECONDARY_SCALE_PITCH_ADJ (RRG:
   SECONDARY_SCALE_PITCH, p. 3-160 / PDF 178). The multiplier belongs to
   the register, so it applies to every load of the pitch field: a
   DST_PITCH write, a DST_PITCH_OFFSET write and the GMC default reload.
   The value read back and the stride in use then agree. */
static void
rage128_2d_dst_pitch_apply(rage128_t *dev)
{
    uint32_t adj = (dev->dst_pitch_reg >> 17) & 3;

    dev->dst_pitch = (dev->dst_pitch_reg & 0x3ff) * 8u
        * (adj == 1 ? 2u : adj == 2 ? 4u
                                    : 1u);
}

/* Apply a DP_GUI_MASTER_CNTL value: load the defaults its control bits
   ask for, run its clear and set actions, and copy its mapped fields to
   DP_MIX and DP_DATATYPE (RRG: DP_GUI_MASTER_CNTL, pp. 3-171-3-174 /
   PDF 189-192). Every GMC write, from a register write or a packet,
   comes through here. */
static void
rage128_2d_apply_gmc(rage128_t *dev, uint32_t gmc)
{
    dev->dp_gui_master_cntl = gmc;

    /* The gradient values (grad_*) apply only while a scaler-pipe fill is
       set up. A GMC write with GMC_3D_FCN_EN clear drops them, so an old
       gradient cannot reach a later plain fill. */
    if (!(gmc & RAGE128_GMC_3D_FCN_EN)) {
        dev->grad_valid = 0;
        /* GMC_3D_FCN_EN "0 = clear SCALE_3D_FCN, Z_EN, STENCIL_EN"
           (RRG: DP_GUI_MASTER_CNTL, p. 3-174 / PDF 192). The function
           field has two copies, SCALE_3D_FN in SCALE_3D_CNTL [7:6] (CCE
           3D supplement) and in MISC_3D_STATE_CNTL_REG [9:8] (RRG:
           MISC_3D_STATE_CNTL_REG, p. 3-256 / PDF 274); Z_EN [0] and
           STENCIL_EN [3] are in TEX_CNTL_C (CCE 3D supplement). The 3D
           draw path reads these values when the next draw starts, so
           every render path sees the clear. */
        dev->t3d.scale_3d_cntl &= ~0x000000c0u;
        dev->t3d.misc_3d_state_cntl &= ~0x00000300u;
        dev->t3d.tex_cntl &= ~0x00000009u;
    }

    /* DEFAULT_OFFSET is a byte offset in [25:0] with bits 3:0 hardwired
       to zero (RRG: DEFAULT_OFFSET, p. 3-243 / PDF 261). Like DST_OFFSET
       it is a 64 MB card address whose upper half, bit 25 set, maps to
       AGP memory (RRG: DST_OFFSET, p. 3-137 / PDF 155), so the whole
       field is kept. */
    if (!(gmc & RAGE128_GMC_SRC_PITCH_OFFSET_LEAVE)) {
        dev->src_offset = (dev->default_offset & 0x03fffff0);
        dev->src_pitch  = (dev->default_pitch & 0x3ff) * 8;
        /* The reload "SRC_PITCH = DEFAULT_PITCH" copies DEFAULT_TILE
           [16] as well (RRG: DEFAULT_PITCH, p. 3-243 / PDF 261), so a
           tile bit left from an earlier tiled op does not survive it.
           The guide gives the bit no description; carrying it with the
           pitch is modeled. */
        dev->src_pitch_reg = (dev->default_pitch & 0x000103ffu);
    }
    if (!(gmc & RAGE128_GMC_DST_PITCH_OFFSET_LEAVE)) {
        dev->dst_offset    = (dev->default_offset & 0x03fffff0);
        dev->dst_pitch_reg = (dev->default_pitch & 0x000103ffu)
            | (dev->dst_pitch_reg & 0x00060000u); /* DST_PITCH_ADJ kept */
        rage128_2d_dst_pitch_apply(dev);
    }
    if (!(gmc & RAGE128_GMC_SRC_CLIP_LEAVE)) {
        /* "(SRC_SC_RIGHT, SRC_SC_BOTTOM) = (DEFAULT_SC_BOTTOM_RIGHT)"
           (RRG: DP_GUI_MASTER_CNTL, p. 3-172 / PDF 190). */
        dev->src_sc_right  = dev->default_sc_bottom_right & 0x3fff;
        dev->src_sc_bottom = (dev->default_sc_bottom_right >> 16) & 0x3fff;
    }
    if (!(gmc & RAGE128_GMC_DST_CLIP_LEAVE)) {
        dev->sc_top_left     = 0;
        dev->sc_bottom_right = dev->default_sc_bottom_right;
    }
    if (gmc & RAGE128_GMC_WR_MSK_DIS) {
        /* GMC_WR_MSK_DIS: "set DP_WRITE_MSK/CLR_CMP_MSK to 0xffffffff",
           both masks (RRG: DP_GUI_MASTER_CNTL, p. 3-174 / PDF 192). */
        dev->dp_write_mask = 0xffffffff;
        dev->clr_cmp_mask  = 0xffffffff;
    }
    if (gmc & RAGE128_GMC_CLR_CMP_CNTL_DIS)
        /* GMC_CLR_CMP_CNTL_DIS: "clear CLR_CMP_FCN_DST, CLR_CMP_FCN_SRC",
           only the two function fields; CLR_CMP_SRC is kept (RRG:
           DP_GUI_MASTER_CNTL, p. 3-174 / PDF 192). */
        dev->clr_cmp_cntl &= ~0x00000707u;
    if (gmc & RAGE128_GMC_AUX_CLIP_DIS)
        /* GMC_AUX_CLIP_DIS: "clear all AUXn_SC_ENB bits", bits 0, 2 and
           4; the mode bits 1, 3 and 5 are kept (RRG: DP_GUI_MASTER_CNTL,
           p. 3-174 / PDF 192). */
        dev->aux_sc_cntl &= ~RAGE128_AUX_SC_ENB_MASK;

    /* Copy the mapped GMC fields to the registers the executors read:
       GMC_ROP3 and DP_SRC_SOURCE to DP_MIX; the destination, brush and
       source datatypes to DP_DATATYPE [3:0], [11:8] and [17:16];
       GMC_BYTE_PIX_ORDER and GMC_CONVERSION_TEMP to DP_DATATYPE [30] and
       [31] (RRG: DP_GUI_MASTER_CNTL, pp. 3-173-3-174 / PDF 191-192). */
    dev->dp_mix = (dev->dp_mix & ~0x00ff0700u)
        | (gmc & 0x00ff0000u)
        | ((uint32_t) RAGE128_GMC_SRC_SOURCE(gmc) << 8);
    dev->dp_datatype = (dev->dp_datatype & ~0xc0030f0fu)
        | (uint32_t) RAGE128_GMC_DST_DATATYPE(gmc)
        | ((uint32_t) RAGE128_GMC_BRUSH_TYPE(gmc) << 8)
        | ((uint32_t) RAGE128_GMC_SRC_DATATYPE(gmc) << 16)
        | (((gmc >> 14) & 1u) << 30)
        | (((gmc >> 15) & 1u) << 31);

    /* A GMC write sets DP_CNTL DST_X_DIR, DST_Y_DIR and POLY_LINE to 1
       (RRG: DP_CNTL, pp. 3-165-3-166 / PDF 183-184), so a draw after a
       reverse-direction copy runs left to right and top to bottom
       unless DP_CNTL is written again. */
    dev->dp_cntl |= RAGE128_DP_CNTL_DST_X_DIR | RAGE128_DP_CNTL_DST_Y_DIR
        | RAGE128_DP_CNTL_POLY_LINE;
}

/* SRC_PITCH_OFFSET and DST_PITCH_OFFSET: offset [20:0] in 32-byte
   units, pitch [30:21] in units of 8 pixels, tile bit [31] (RRG:
   DST_PITCH_OFFSET, p. 3-137 / PDF 155). Not static: the 3D context
   register DST_PITCH_OFFSET_C uses the same layout. */
void
rage128_2d_set_pitch_offset(rage128_t *dev, int is_dst, uint32_t val)
{
    uint32_t off   = (val & 0x001fffff) << 5;
    uint32_t pitch = ((val >> 21) & 0x3ff) * 8;

    if (is_dst) {
        dev->dst_offset = off;
        /* Keep the DST_PITCH value in step: pitch/8 in [9:0], DST_TILE
           in [16] from packed bit 31. DST_PITCH_ADJ has no field in the
           packed form, so it is kept. */
        dev->dst_pitch_reg = ((val >> 21) & 0x3ff)
            | (((val >> 31) & 1u) << 16)
            | (dev->dst_pitch_reg & 0x00060000u);
        rage128_2d_dst_pitch_apply(dev);
    } else {
        dev->src_offset    = off;
        dev->src_pitch     = pitch;
        dev->src_pitch_reg = ((val >> 21) & 0x3ff)
            | (((val >> 31) & 1u) << 16);
    }
}

/* Scissor decode: SC_TOP_LEFT and SC_BOTTOM_RIGHT hold x (left, right)
   in [13:0] and y (top, bottom) in [29:16] (RRG: SC_TOP_LEFT, p. 3-162
   / PDF 180). Both bounds are inclusive, as the X driver programs them
   (xf86-video-r128 r128_accel.c, XAA version,
   R128SubsequentScanlineCPUToScreenColorExpandFill writes the last
   column and row, x2 - 1 and y + h - 1). */
static void
rage128_2d_clip(rage128_t *dev, int *x0, int *y0, int *x1, int *y1)
{
    /* Each bound is signed 14 bits, "range -8192 to 8191" (RRG:
       SC_LEFT, p. 3-155 / PDF 173), so a negative left or top edge
       clips nothing on that side. */
    *x0 = rage128_sx14(dev->sc_top_left);
    *y0 = rage128_sx14(dev->sc_top_left >> 16);
    *x1 = rage128_sx14(dev->sc_bottom_right);
    *y1 = rage128_sx14(dev->sc_bottom_right >> 16);
}

static void
rage128_2d_mark_dirty(rage128_t *dev, const r128_surf_t *s, uint32_t addr, uint32_t len)
{
    svga_t *svga = &dev->svga;

    if (s->st)
        return; /* staged AGP window -- no local VRAM page was touched */
    /* Walk BY LENGTH, masking each page (aliased/wrapping runs). */
    for (uint32_t o = 0; o < len; o += 0x1000u)
        svga->changedvram[((addr + o) & dev->vram_mask) >> 12] = svga->monitor->mon_changeframecount;
    svga->changedvram[((addr + len - 1u) & dev->vram_mask) >> 12] = svga->monitor->mon_changeframecount;
}

/* Row stride of the latched destination or source surface: the
   programmed pitch, or that pitch cut to whole tile columns when the
   surface is tiled (r128_tile_stride). Every address the executors form
   for these surfaces goes through here, so the cut cannot be applied in
   one place and missed in another. A window of its own that only has
   the same pitch value goes through rage128_2d_span_stride instead:
   only the latched surface itself follows its tile bit. */
static uint32_t
rage128_2d_dst_stride(const rage128_t *dev, int bpp)
{
    return r128_tile_stride(rage128_2d_stride(dev->dst_pitch, bpp),
                            (dev->dst_pitch_reg >> 16) & 1u);
}

static uint32_t
rage128_2d_src_stride(const rage128_t *dev, int bpp)
{
    return r128_tile_stride(rage128_2d_stride(dev->src_pitch, bpp),
                            (dev->src_pitch_reg >> 16) & 1u);
}

/* Latched 2D tile state. The tile bit of the packed *_PITCH_OFFSET form
   [31] and of the DST_PITCH / SRC_PITCH form [16] both land in bit 16 of
   *_pitch_reg, and the GMC default reload copies DEFAULT_TILE there. The
   bit is honored only under the pitch rule the 2D and 3D code share
   (r128_tiled_ok in vid_ati_rage128.h). */
static int
rage128_2d_dst_tiled(rage128_t *dev, int bpp)
{
    return r128_tiled_ok((dev->dst_pitch_reg >> 16) & 1u,
                         rage128_2d_dst_stride(dev, bpp));
}

static int
rage128_2d_src_tiled(rage128_t *dev, int bpp)
{
    return r128_tiled_ok((dev->src_pitch_reg >> 16) & 1u,
                         rage128_2d_src_stride(dev, bpp));
}

/* Either 2D tile bit set: keep the op off the GPU queue and on the CPU
   walk. These queue paths address rows linearly; the CPU walk handles
   tiling through the window layer. The one queue path that handles
   tiling, the solid fill, sends a tiled destination to
   rage128_gpu_2d_fill_tiled before it reaches this test. The test is
   coarser than r128_tiled_ok on purpose: refusing the queue only costs
   speed. Every caller is a queue-eligibility test under dev->gpu, so a
   true result is a refusal and is counted here (the counter does
   nothing without a backend). */
static int
rage128_2d_tiled_any(rage128_t *dev)
{
    int tiled = ((dev->dst_pitch_reg | dev->src_pitch_reg) >> 16) & 1;

    if (tiled)
        rage128_gpu_2d_tiled_skip(dev);
    return tiled;
}

/* A window follows a tile bit only when it is the latched destination
   or source surface itself (base and pitch both match). The scaler's
   own source windows therefore stay linear unless they are the latched
   source surface. This is worked out here rather than passed in, so the
   mapping and the executor's row walk cannot disagree about it. */
static int
rage128_2d_span_tiled(rage128_t *dev, const struct rage128_span_stage *st,
                      uint32_t surf_base, uint32_t pitch_px, int bpp)
{
    return (st == &dev->s2d_dst)
        ? (surf_base == dev->dst_offset && pitch_px == dev->dst_pitch
           && rage128_2d_dst_tiled(dev, bpp))
        : (surf_base == dev->src_offset && pitch_px == dev->src_pitch
           && rage128_2d_src_tiled(dev, bpp));
}

/* The stride that window's rows are addressed at: what the mapping
   stages and what every address formed against it must use. */
static uint32_t
rage128_2d_span_stride(rage128_t *dev, const struct rage128_span_stage *st,
                       uint32_t surf_base, uint32_t pitch_px, int bpp)
{
    return r128_tile_stride(rage128_2d_stride(pitch_px, bpp),
                            rage128_2d_span_tiled(dev, st, surf_base,
                                                  pitch_px, bpp));
}

/* Map the destination or source window for a 2D op that touches rows
   [y0..y1] and columns [x0..x1] (inclusive, after clipping). A local
   surface maps straight onto vram[a & vram_mask]; a surface with bit 25
   set lives in AGP memory and its span is staged (vid_ati_rage128_mem.c).
   A tiled surface stages an untiled copy of the window, so the
   executor's linear row math works unchanged, and the commit writes it
   back through the tile transform. Returns 0 when the span cannot be
   staged: the caller must skip the op, never redirect it onto local
   VRAM. An empty rect maps the local window; it is never read or
   written (the per-pixel clip rejects every pixel), but the caller's
   pattern and phase bookkeeping still runs.
   With a negative scissor a rect can start left of column 0 or above
   row 0. The walk addresses the surface base plus the signed offset, so
   the window opens there. That is modeled: the guide does not say what
   is stored at a negative coordinate. A tiled surface has no transform
   above its origin, so such an op cannot be staged. */
static int
rage128_2d_map_span(rage128_t *dev, r128_surf_t *s, struct rage128_span_stage *st,
                    uint32_t surf_base, uint32_t pitch_px, int bpp,
                    int x0, int y0, int y1, int x1)
{
    uint32_t lo, len, pb;
    int64_t  start;
    int      tiled = rage128_2d_span_tiled(dev, st, surf_base, pitch_px, bpp);

    /* A tiled window is addressed at the truncated stride, so its linear
       image rows are that long too. */
    pb = rage128_2d_span_stride(dev, st, surf_base, pitch_px, bpp);
    if (y1 < y0 || x1 < x0) {
        s->base = dev->svga.vram;
        s->rel  = 0;
        s->mask = dev->vram_mask;
        s->st   = NULL;
        return 1;
    }
    start = (int64_t) y0 * pb + (int64_t) (x0 < 0 ? x0 : 0) * bpp;
    len   = (uint32_t) ((int64_t) y1 * pb + ((int64_t) x1 + 1) * bpp - start);
    if (tiled && start < 0)
        return 0;
    if ((int64_t) surf_base + start < 0) {
        /* The window starts below card address 0. For a local surface
           the walk's stores wrap to the top of local VRAM, so all of
           VRAM is fenced. An AGP surface would wrap onto local VRAM, so
           it cannot be staged. */
        if (r128_card_is_agp(surf_base))
            return 0;
        if (dev->synctel)
            rage128_synctel_2d_span(dev, 0, dev->vram_size, st == &dev->s2d_dst);
        if (dev->gpu)
            rage128_gpu_2d_barrier(dev, 0, dev->vram_size, st == &dev->s2d_dst);
        s->base = dev->svga.vram;
        s->rel  = 0;
        s->mask = dev->vram_mask;
        s->st   = NULL;
        return 1;
    }
    lo = surf_base + (uint32_t) start;
    if (tiled) {
        /* The GPU fence and the synctel access record cover the card
           bytes the tile transform can touch (whole tile rows), not the
           linear copy. The last row comes from the mapped length, not
           from y1: a rect whose right edge runs past the stride spills
           into the next row, which the transform places in a later band
           of tiles. */
        uint32_t yfirst = (uint32_t) start / pb;
        uint32_t ylast  = (uint32_t) ((lo - surf_base + len - 1u) / pb);
        uint32_t tlo    = surf_base + r128_tile_row_start(yfirst, pb);
        uint32_t tlen   = r128_tile_rows_bytes(ylast, pb)
            - r128_tile_row_start(yfirst, pb);

        if (dev->synctel)
            rage128_synctel_2d_span(dev, tlo, tlen, st == &dev->s2d_dst);
        if (dev->gpu)
            rage128_gpu_2d_barrier(dev, tlo, tlen, st == &dev->s2d_dst);
        return r128_surf_map_tiled(dev, s, st, surf_base, pb, lo, len);
    }
    if (dev->synctel)
        rage128_synctel_2d_span(dev, lo, len, st == &dev->s2d_dst);
    /* The op's CPU walk is about to touch this window. If the GPU
       backend has pending work on any of these bytes, the barrier waits
       for it; work elsewhere is left running. An AGP window never
       matches, because the GPU backend writes only local VRAM. */
    if (dev->gpu)
        rage128_gpu_2d_barrier(dev, lo, len, st == &dev->s2d_dst);
    return r128_surf_map(dev, s, st, lo, len);
}

/* The coordinate fields are signed 14 bits, range -8192 to 8191 (RRG:
   DST_X_Y, pp. 3-138-3-139 / PDF 156-157). rage128_sx14, in
   vid_ati_rage128.h, sign-extends them for the 2D and 3D code alike. */

/* Auxiliary scissor test for one pixel, as the 3D path uses it: the
   enabled additive rects form a union the draw is restricted to, and
   each subtractive rect cuts its area out. The main scissor is applied
   separately and always clips. This is how the open-source GL stack
   uses the registers: Mesa sets the main scissor to the drawable (Mesa
   r128 r128_state.c r128UpdateClipping), and the DRM writes each visible
   cliprect as an additive aux rect (linux r128 DRM r128_state.c
   r128_emit_clip_rects). Bounds are inclusive and signed 14 bits.
   Callers test RAGE128_AUX_SC_ENB_MASK first. */
int
rage128_aux_sc_pass(uint32_t cntl, const uint32_t rect[3][4], int x, int y)
{
    int have_add = 0;
    int in_add   = 0;

    for (int i = 0; i < 3; i++) {
        if (!(cntl & (1u << (i * 2))))
            continue;
        int in = x >= rage128_sx14(rect[i][0]) && x <= rage128_sx14(rect[i][1])
            && y >= rage128_sx14(rect[i][2]) && y <= rage128_sx14(rect[i][3]);

        if (cntl & (2u << (i * 2))) {
            if (in)
                return 0; /* subtractive: inside an excluded rect */
        } else {
            have_add = 1;
            if (in)
                in_add = 1;
        }
    }
    return have_add ? in_add : 1;
}

/* The same test for the 2D engine. An additive aux rect combines "with
   other destination SCISSORs with 'OR'" (RRG: AUX_SC_CNTL,
   pp. 3-156-3-157 / PDF 174-175), so it can only add to the area the
   main scissor allows; it never clips a 2D op. Only subtractive rects
   cut. The code does not let an additive rect widen the area past the
   main scissor either. In captured Windows traffic the OpenGL driver
   clears the Z buffer of each Quake 3 frame with an 801x609 2D paint
   while an additive AUX3 rect covering the screen is still enabled;
   rows 600-608 of the paint lie outside that rect and must still be
   cleared. */
static int
rage128_aux_sc_pass_2d(uint32_t cntl, const uint32_t rect[3][4], int x, int y)
{
    for (int i = 0; i < 3; i++) {
        if (!(cntl & (1u << (i * 2))) || !(cntl & (2u << (i * 2))))
            continue;
        if (x >= rage128_sx14(rect[i][0]) && x <= rage128_sx14(rect[i][1])
            && y >= rage128_sx14(rect[i][2]) && y <= rage128_sx14(rect[i][3]))
            return 0; /* inside an enabled subtractive rect */
    }
    return 1;
}

/* Whole-rect 2D test: 1 = every pixel of [x0,x1]x[y0,y1] passes
   rage128_aux_sc_pass_2d (no enabled subtractive rect overlaps it), so
   no pixel inside the rect needs its own aux test. */
static int
rage128_aux_sc_accepts_rect(uint32_t cntl, const uint32_t rect[3][4],
                            int x0, int y0, int x1, int y1)
{
    for (int i = 0; i < 3; i++) {
        if (!(cntl & (1u << (i * 2))) || !(cntl & (2u << (i * 2))))
            continue;
        if (x0 <= rage128_sx14(rect[i][1]) && x1 >= rage128_sx14(rect[i][0])
            && y0 <= rage128_sx14(rect[i][3]) && y1 >= rage128_sx14(rect[i][2]))
            return 0; /* subtractive rect intersects */
    }
    return 1;
}

/* 1 = pixels inside [x0,x1]x[y0,y1] need their own aux test: aux
   scissors are enabled and the whole rect does not pass. The queue
   paths then either split rows into runs (when the stored values do not
   depend on the destination) or, in the read-modify-write plane, set
   the rejected pixels to keep the destination (A = 0xff, B = 0). A
   read-modify-write op must stay one dispatch: if it were split into
   runs and a later run failed, the CPU re-walk would apply the op twice
   to the runs already queued. */
static int
rage128_2d_aux_seg(rage128_t *dev, int x0, int y0, int x1, int y1)
{
    return (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0
        && !rage128_aux_sc_accepts_rect(dev->aux_sc_cntl, dev->aux_sc_rect,
                                        x0, y0, x1, y1);
}

/* Mark scanout-dirty rows for a queued GPU write the CPU never touches. */
static void
rage128_2d_queue_mark(rage128_t *dev, uint32_t addr, uint32_t pitch,
                      uint32_t len, uint32_t rows)
{
    r128_surf_t sm = { .base = dev->svga.vram, .rel = 0, .mask = dev->vram_mask, .st = NULL };

    for (uint32_t r = 0; r < rows; r++)
        rage128_2d_mark_dirty(dev, &sm, addr + r * pitch, len);
}

/* Self-check for the queue paths whose values are worked out on the CPU
   (keyed blit, stretch, line, gradient), active under
   R128_GPU_2D=verify. Before queueing, copy the destination as it was.
   If the whole op queued, save the GPU result, put the old destination
   back and let the op's own CPU walk run; rage128_2d_vq_end then
   compares the two byte for byte at the end of the op. This checks the
   values worked out at queue time (run extents, key and pattern
   phases, ROP folding) against the interpreter, not just the copy to
   the GPU. */
static int
rage128_2d_vq_begin(rage128_t *dev, int op, uint32_t addr, uint32_t pitch,
                    uint32_t rowlen, uint32_t rows)
{
    struct rage128_2d_vq *v = &dev->vq2d;

    if (!rage128_gpu_2d_verifying(dev) || v->pre || !rows || !rowlen
        || (uint64_t) addr + (uint64_t) (rows - 1) * pitch + rowlen
            > (uint64_t) dev->vram_size)
        return 0;
    /* wait for pending GPU writes so the copy is the real old image */
    rage128_gpu_2d_barrier(dev, addr, (rows - 1) * pitch + rowlen, 1);
    v->pre = (uint8_t *) malloc((size_t) rows * rowlen * 2);
    if (!v->pre)
        return 0;
    v->addr   = addr;
    v->pitch  = pitch;
    v->rowlen = rowlen;
    v->rows   = rows;
    v->op     = op;
    v->armed  = 0;
    for (uint32_t r = 0; r < rows; r++)
        memcpy(v->pre + (size_t) r * rowlen,
               dev->svga.vram + addr + (uint64_t) r * pitch, rowlen);
    return 1;
}

static void
rage128_2d_vq_after(rage128_t *dev, int queued)
{
    struct rage128_2d_vq *v   = &dev->vq2d;
    uint8_t              *gpu = v->pre ? v->pre + (size_t) v->rows * v->rowlen
                                       : NULL;

    if (!v->pre)
        return;
    if (!queued) {
        free(v->pre);
        v->pre = NULL;
        return;
    }
    for (uint32_t r = 0; r < v->rows; r++) {
        memcpy(gpu + (size_t) r * v->rowlen,
               dev->svga.vram + v->addr + (uint64_t) r * v->pitch,
               v->rowlen);
        memcpy(dev->svga.vram + v->addr + (uint64_t) r * v->pitch,
               v->pre + (size_t) r * v->rowlen, v->rowlen);
    }
    v->armed = 1;
}

/* Drop an armed compare without judging it (walk aborted early). */
static void
rage128_2d_vq_abort(rage128_t *dev)
{
    struct rage128_2d_vq *v = &dev->vq2d;

    free(v->pre);
    v->pre   = NULL;
    v->armed = 0;
}

static void
rage128_2d_vq_end(rage128_t *dev)
{
    struct rage128_2d_vq *v   = &dev->vq2d;
    uint8_t              *gpu = v->pre ? v->pre + (size_t) v->rows * v->rowlen
                                       : NULL;
    uint64_t              bad = 0;

    if (!v->pre)
        return;
    if (v->armed) {
        for (uint32_t r = 0; r < v->rows; r++)
            for (uint32_t i = 0; i < v->rowlen; i++)
                if (gpu[(size_t) r * v->rowlen + i]
                    != dev->svga.vram[v->addr + (uint64_t) r * v->pitch + i])
                    bad++;
        if (bad)
            pclog("RAGE128 GPU: 2d vq MISMATCH: op=%d addr=%08x pitch=%u "
                  "rowlen=%u rows=%u bad=%llu\n",
                  v->op, v->addr, v->pitch,
                  v->rowlen, v->rows, (unsigned long long) bad);
        rage128_gpu_2d_verify_bad(dev, v->op, bad);
    }
    free(v->pre);
    v->pre   = NULL;
    v->armed = 0;
}

/* Queue one solid rect at the destination's real bytes per pixel on the
   GPU fill queue, and mark the scanout rows dirty if it queued. 0 = the
   queue refused it (the caller walks it on the CPU). A tiled
   destination (the same tile bit rage128_2d_map_span follows for this
   window: dst_offset at dst_pitch) is queued in its tile layout. The
   walk's limits apply here too: a rect that starts above the origin
   cannot be staged (the caller skips it), and a right edge past the
   stride spills into the next row, which only the walk's linear copy
   reproduces, so both go to the CPU. The dirty marks cover whole tile
   rows, the bytes the transform can land in. */
static int
rage128_2d_queue_fill(rage128_t *dev, int x, int y, int w, int h, int bpp)
{
    if (rage128_2d_dst_tiled(dev, bpp)) {
        uint32_t    stride = rage128_2d_dst_stride(dev, bpp);
        uint32_t    xb0    = (uint32_t) x * (uint32_t) bpp;
        uint32_t    xb1    = (uint32_t) (x + w) * (uint32_t) bpp;
        uint32_t    y1     = (uint32_t) (y + h - 1);
        r128_surf_t sm     = { .base = dev->svga.vram, .rel = 0, .mask = dev->vram_mask, .st = NULL };

        if (x < 0 || y < 0 || xb1 > stride
            || !rage128_gpu_2d_fill_tiled(dev, dev->dst_offset, stride, xb0,
                                          xb1, (uint32_t) y, y1,
                                          dev->dp_brush_frgd_clr, bpp)) {
            rage128_gpu_2d_tiled_skip(dev);
            return 0; /* this shape takes the CPU walk */
        }
        rage128_2d_mark_dirty(dev, &sm,
                              dev->dst_offset
                                  + r128_tile_row_start((uint32_t) y, stride),
                              r128_tile_rows_bytes(y1, stride)
                                  - r128_tile_row_start((uint32_t) y, stride));
        return 1;
    }
    if (rage128_2d_tiled_any(dev))
        return 0; /* a source tile bit, or a destination tile bit the
                     stride cannot honor: the CPU walk handles it */
    uint32_t base = dev->dst_offset
        + ((uint32_t) y * rage128_2d_dst_stride(dev, bpp) + (uint32_t) x * (uint32_t) bpp);

    if (!rage128_gpu_2d_fill(dev, base, rage128_2d_dst_stride(dev, bpp),
                             (uint32_t) w * (uint32_t) bpp, (uint32_t) h,
                             dev->dp_brush_frgd_clr, bpp, 0))
        return 0;
    rage128_2d_queue_mark(dev, base, rage128_2d_dst_stride(dev, bpp),
                          (uint32_t) w * (uint32_t) bpp, (uint32_t) h);
    return 1;
}

/* A ROP3 that does not depend on the destination: for every pattern and
   source bit pair the result is the same whether the destination bit is
   0 or 1 (each even bit of the code equals the odd bit above it). The
   stores of such an op depend only on its operands, so if a queue
   attempt fails partway, the CPU re-walk stores the same bytes again
   and nothing needs to be undone. */
static int
rage128_2d_rop_dindep(uint8_t rop)
{
    return ((rop ^ (rop >> 1)) & 0x55) == 0;
}

/* Turn one byte position's ROP3 code, pattern byte, source byte and
   write-mask byte into the A/B pair of the GPU read-modify-write
   kernel, which stores out = (d & A) | (~d & B). That equals
   (rop3(rop, p, s, d) & wm) | (d & ~wm) for every ROP3 code: a ROP3
   works bit by bit, so each result bit is t1 where d is 1 and t0 where
   d is 0, and masked-off bits keep d.
   An op that depends on the destination cannot be repeated safely: a
   CPU re-walk after a partial queue would apply it twice (an XOR would
   undo itself). So every read-modify-write caller queues all or
   nothing: one rage128_gpu_2d_rmw call per op, or, where an op spans
   several runs, all runs checked first and then queued. */
static inline void
rage128_2d_rmw_ab(uint8_t rop, uint8_t p, uint8_t s, uint8_t wm,
                  uint8_t *a, uint8_t *b)
{
    uint8_t t1 = (uint8_t) rage128_rop3(rop, p, s, 0xffu);
    uint8_t t0 = (uint8_t) rage128_rop3(rop, p, s, 0x00u);

    *a = (uint8_t) ((t1 & wm) | (uint8_t) ~wm);
    *b = (uint8_t) (t0 & wm);
}

/* Queue a solid paint under any ROP3 and write mask as one
   read-modify-write dispatch. A and B depend only on the byte's place
   in the pixel, so one row of A/B (row modulus 0) serves every
   destination row. When pixels need their own aux test, the A/B image
   has one row per destination row, and rejected pixels keep the
   destination (A = 0xff, B = 0). The caller has clipped the rect.
   0 = not queued (nothing recorded). */
static int
rage128_2d_queue_fill_rmw(rage128_t *dev, int x, int y, int w, int h,
                          uint8_t rop, int bpp)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    uint32_t pitch = rage128_2d_dst_stride(dev, bpp);
    uint32_t base  = dev->dst_offset
        + ((uint32_t) y * rage128_2d_dst_stride(dev, bpp) + (uint32_t) x * (uint32_t) bpp);
    uint32_t len  = (uint32_t) w * (uint32_t) bpp;
    uint32_t rows = (uint32_t) h;
    uint32_t soff;
    uint8_t *st;
    int      aux;

    if (x < 0 || y < 0) {
        rage128_gpu_2d_qskip(dev, 8, 32);
        return 0;
    }
    aux            = rage128_2d_aux_seg(dev, x, y, x + w - 1, y + h - 1);
    uint32_t arows = aux ? rows : 1u;

    st = (uint8_t *) rage128_gpu_2d_stage(dev, arows * len * 2u, &soff);
    if (!st) {
        rage128_gpu_2d_qskip(dev, 8, 64);
        return 0;
    }
    for (uint32_t r = 0; r < arows; r++)
        for (int c = 0; c < w; c++)
            for (int b = 0; b < bpp; b++) {
                uint32_t i    = r * len + (uint32_t) c * bpp + (uint32_t) b;
                int      lane = b & 3;

                if (aux && !rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x + c, y + (int) r)) {
                    st[i]               = 0xff;
                    st[arows * len + i] = 0;
                    continue;
                }
                rage128_2d_rmw_ab(rop,
                                  (uint8_t) (dev->dp_brush_frgd_clr >> (lane * 8)),
                                  0,
                                  (uint8_t) (dev->dp_write_mask >> (lane * 8)),
                                  &st[i], &st[arows * len + i]);
            }
    if (!rage128_gpu_2d_rmw(dev, base, pitch, len, rows, soff,
                            soff + arows * len,
                            aux ? 0xffffffffu : 0)) {
        rage128_gpu_2d_qskip(dev, 8, 32);
        return 0;
    }
    rage128_2d_queue_mark(dev, base, pitch, len, rows);
    rage128_gpu_2d_qdone(dev, 8);
    if (aux)
        rage128_gpu_2d_qaux(dev, 8);
    return 1;
}

/* Queue a pattern-brush paint under any ROP3 and write mask. The A/B
   image is the 8-row screen-aligned pattern tile (row modulus 7) with a
   value per pixel; clear bits of a leave-alone mono pattern keep the
   destination (A = 0xff, B = 0), so rows need not be split into runs.
   The aux test varies by destination row, so when pixels need it the
   image has one row per destination row, with rejected pixels kept.
   The caller has clipped the rect. */
static int
rage128_2d_queue_pat_rmw(rage128_t *dev, int x, int y, int w, int h,
                         const uint8_t *mono8x8, const uint8_t *col8x8,
                         int mono_la, uint8_t rop, int bpp)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    if (mono8x8 && rage128_2d_brush_32x32(dev))
        return 0;
    uint32_t pitch = rage128_2d_dst_stride(dev, bpp);
    uint32_t base  = dev->dst_offset
        + ((uint32_t) y * rage128_2d_dst_stride(dev, bpp) + (uint32_t) x * (uint32_t) bpp);
    uint32_t len   = (uint32_t) w * (uint32_t) bpp;
    uint32_t prows = (uint32_t) h < 8u ? (uint32_t) h : 8u;
    uint32_t soff;
    uint8_t *st;
    int      aux;

    if (x < 0 || y < 0) {
        rage128_gpu_2d_qskip(dev, 8, 32);
        return 0;
    }
    aux = rage128_2d_aux_seg(dev, x, y, x + w - 1, y + h - 1);
    if (aux)
        prows = (uint32_t) h;
    st = (uint8_t *) rage128_gpu_2d_stage(dev, prows * len * 2u, &soff);
    if (!st) {
        rage128_gpu_2d_qskip(dev, 8, 64);
        return 0;
    }
    for (uint32_t pr = 0; pr < prows; pr++) {
        int py = rage128_2d_pat_row(dev, y + (int) pr);

        for (int c = 0; c < w; c++)
            for (int b = 0; b < bpp; b++) {
                uint32_t i    = pr * len + (uint32_t) c * bpp + (uint32_t) b;
                int      lane = b & 3;
                uint8_t  wm   = (uint8_t) (dev->dp_write_mask >> (lane * 8));
                uint8_t  pb;

                if (aux && !rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x + c, y + (int) pr)) {
                    st[i]               = 0xff;
                    st[prows * len + i] = 0;
                    continue;
                }
                if (mono8x8) {
                    int set = rage128_2d_pat8_bit(dev, mono8x8[py], x + c);

                    if (!set && mono_la) {
                        st[i]               = 0xff;
                        st[prows * len + i] = 0;
                        continue;
                    }
                    pb = (uint8_t) ((set ? dev->dp_brush_frgd_clr
                                         : dev->dp_brush_bkgd_clr)
                                    >> (lane * 8));
                } else
                    pb = col8x8[(uint32_t) (py * 8 + rage128_2d_pat_col(dev, x + c)) * bpp + b];
                rage128_2d_rmw_ab(rop, pb, 0, wm,
                                  &st[i], &st[prows * len + i]);
            }
    }
    if (!rage128_gpu_2d_rmw(dev, base, pitch, len, (uint32_t) h, soff,
                            soff + prows * len, aux ? 0xffffffffu : 7)) {
        rage128_gpu_2d_qskip(dev, 8, 32);
        return 0;
    }
    rage128_2d_queue_mark(dev, base, pitch, len, (uint32_t) h);
    rage128_gpu_2d_qdone(dev, 8);
    if (aux)
        rage128_gpu_2d_qaux(dev, 2);
    return 1;
}

/* The destination-format value of one mono-expanded pixel at screen
   (sx, sy). The source comes from the mono bit, the pattern from the
   brush: solid foreground, or the screen-aligned 8x8 mono or color
   tile, read as rage128_2d_mono_rect's walk reads it (a clear mono cell
   reads the brush background; this path has no pattern leave-alone).
   The destination does not matter: the caller checked that the ROP
   does not use it. */
static inline uint32_t
rage128_2d_mono_pat_v(const rage128_t *dev, uint8_t rop, int bpp, int set,
                      int sx, int sy, const uint8_t *pat8x8,
                      const uint8_t *patcol)
{
    uint32_t s = set ? dev->dp_src_frgd_clr : dev->dp_src_bkgd_clr;
    uint32_t pd;

    if (pat8x8)
        pd = rage128_2d_pat8_bit(dev, pat8x8[rage128_2d_pat_row(dev, sy)], sx)
            ? dev->dp_brush_frgd_clr
            : dev->dp_brush_bkgd_clr;
    else if (patcol) {
        pd = 0;
        memcpy(&pd,
               &patcol[(uint32_t) (rage128_2d_pat_row(dev, sy) * 8
                                   + rage128_2d_pat_col(dev, sx))
                       * (uint32_t) bpp],
               (size_t) bpp);
    } else
        pd = dev->dp_brush_frgd_clr;
    return rage128_rop3(rop, pd, s, 0);
}

/* Queue a mono-expand rect on the GPU: any destination depth, solid or
   8x8 pattern brush, write mask fully open, ROP that does not use the
   destination (the caller checked). Opaque expansion (clear bits write
   the background) stages the final pixels at the destination depth and
   queues one copy. Leave-alone expansion queues one solid fill per run
   of set bits with a solid brush, or one staged copy per run when a
   pattern brush makes the values vary by column. Every stored value
   depends only on the latched operands and is computed here once
   through the ROP. Statistics go to table 0 for a solid brush and
   table 9 for a pattern. 0 = not queueable, run the CPU walk. */
static int
rage128_2d_queue_mono(rage128_t *dev, int x, int y, int w, int h,
                      const uint8_t *bits, uint32_t bitpitch,
                      int use_bkgd, int lsb_first, uint8_t rop, int bpp,
                      const uint8_t *pat8x8, const uint8_t *patcol)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    if (pat8x8 && rage128_2d_brush_32x32(dev))
        return 0;
    uint32_t v_set = rage128_rop3(rop, dev->dp_brush_frgd_clr,
                                  dev->dp_src_frgd_clr, 0);
    uint32_t v_clr = rage128_rop3(rop, dev->dp_brush_frgd_clr,
                                  dev->dp_src_bkgd_clr, 0);
    int      pat   = (pat8x8 || patcol) ? 1 : 0;
    int      qtab  = pat ? 9 : 0;
    int      route = pat ? 8 : 1; /* statistics slot; rage128_gpu_2d_copy
                                     counts a route of 3 or more in table
                                     route + 1 */
    int      cx0, cy0, cx1, cy1, rx0, ry0, rx1, ry1, aux;
    uint32_t base, rowlen, rows, pitch, npx;
    uint64_t end;

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    rx0 = x < cx0 ? cx0 : x;
    ry0 = y < cy0 ? cy0 : y;
    rx1 = x + w - 1 > cx1 ? cx1 : x + w - 1;
    ry1 = y + h - 1 > cy1 ? cy1 : y + h - 1;
    if (rx0 > rx1 || ry0 > ry1)
        return 1; /* fully scissored: the walk would store nothing */
    aux   = rage128_2d_aux_seg(dev, rx0, ry0, rx1, ry1);
    pitch = rage128_2d_dst_stride(dev, bpp);
    base  = dev->dst_offset
        + ((uint32_t) ry0 * rage128_2d_dst_stride(dev, bpp) + rx0 * (uint32_t) bpp);
    npx    = (uint32_t) (rx1 - rx0 + 1);
    rowlen = npx * (uint32_t) bpp;
    rows   = (uint32_t) (ry1 - ry0 + 1);
    end    = (uint64_t) base + (uint64_t) (rows - 1) * pitch + rowlen;
    if (r128_card_is_agp(base) || end > (uint64_t) dev->vram_size
        || (rows > 1 && pitch < rowlen)) {
        rage128_gpu_2d_qskip(dev, qtab, 32);
        return 0;
    }

#define R128_MONO_BIT(i)                           \
    (lsb_first ? (bits[(i) >> 3] >> ((i) & 7)) & 1 \
               : (bits[(i) >> 3] >> (7 - ((i) & 7))) & 1)

#define R128_MONO_AUX(c, r) \
    (!aux || rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, rx0 + (int) (c), ry0 + (int) (r)))

#define R128_MONO_V(set, c, r)                                          \
    (pat ? rage128_2d_mono_pat_v(dev, rop, bpp, (set), rx0 + (int) (c), \
                                 ry0 + (int) (r), pat8x8, patcol)       \
         : ((set) ? v_set : v_clr))

    if (use_bkgd && aux) {
        /* opaque with a per-pixel aux test: one staged copy per run of
           accepted pixels. If a run is refused, the CPU re-walk stores
           the same bytes, so stopping partway needs no undo. */
        uint32_t nruns = 0;

        for (uint32_t r = 0; r < rows; r++) {
            uint32_t rbit = (uint32_t) (ry0 - y + (int) r) * bitpitch
                + (uint32_t) (rx0 - x);
            uint32_t addr = base + r * pitch;
            uint32_t c    = 0;

            while (c < npx) {
                uint32_t s, rlen, soff;
                uint8_t *st;

                while (c < npx && !R128_MONO_AUX(c, r))
                    c++;
                s = c;
                while (c < npx && R128_MONO_AUX(c, r))
                    c++;
                if (c == s)
                    continue;
                if (++nruns > 256) {
                    rage128_gpu_2d_qskip(dev, qtab, 8);
                    return 0;
                }
                rlen = (c - s) * (uint32_t) bpp;
                st   = (uint8_t *) rage128_gpu_2d_stage(dev, rlen, &soff);
                if (!st) {
                    rage128_gpu_2d_qskip(dev, qtab, 64);
                    return 0;
                }
                for (uint32_t k = s; k < c; k++) {
                    uint32_t v = R128_MONO_V(R128_MONO_BIT(rbit + k), k, r);

                    memcpy(st, &v, (size_t) bpp);
                    st += bpp;
                }
                if (!rage128_gpu_2d_copy(dev, addr + s * (uint32_t) bpp,
                                         rlen, rlen, 1, soff, rlen, route))
                    return 0;
                rage128_2d_queue_mark(dev, addr + s * (uint32_t) bpp,
                                      rlen, rlen, 1);
            }
        }
        rage128_gpu_2d_qaux(dev, qtab);
        return 1;
    }

    if (use_bkgd) {
        uint32_t soff;
        uint8_t *st = (uint8_t *) rage128_gpu_2d_stage(dev, rows * rowlen,
                                                       &soff);

        if (!st) {
            rage128_gpu_2d_qskip(dev, qtab, 64);
            return 0;
        }
        for (uint32_t r = 0; r < rows; r++) {
            uint32_t bit = (uint32_t) (ry0 - y + (int) r) * bitpitch
                + (uint32_t) (rx0 - x);

            for (uint32_t c = 0; c < npx; c++, bit++) {
                uint32_t v = R128_MONO_V(R128_MONO_BIT(bit), c, r);

                memcpy(st, &v, (size_t) bpp);
                st += bpp;
            }
        }
        if (!rage128_gpu_2d_copy(dev, base, pitch, rowlen, rows, soff,
                                 rowlen, route))
            return 0;
        rage128_2d_queue_mark(dev, base, pitch, rowlen, rows);
        return 1;
    }

    /* leave-alone: a run refused partway is harmless, because the
       caller's CPU re-walk stores the same bytes. A solid brush gives
       every set pixel the same value (one fill per run); a pattern brush
       varies it by column (one staged copy per run, with the run count
       capped as on the other staged-run paths). */
    {
        uint32_t nruns = 0;

        for (uint32_t r = 0; r < rows; r++) {
            uint32_t rbit = (uint32_t) (ry0 - y + (int) r) * bitpitch
                + (uint32_t) (rx0 - x);
            uint32_t addr = base + r * pitch;
            uint32_t c    = 0;

            while (c < npx) {
                uint32_t s;

                while (c < npx
                       && !(R128_MONO_BIT(rbit + c) && R128_MONO_AUX(c, r)))
                    c++;
                s = c;
                while (c < npx
                       && R128_MONO_BIT(rbit + c) && R128_MONO_AUX(c, r))
                    c++;
                if (c == s)
                    continue;
                if (pat) {
                    uint32_t rlen = (c - s) * (uint32_t) bpp;
                    uint32_t soff;
                    uint8_t *st;

                    if (++nruns > 256) {
                        rage128_gpu_2d_qskip(dev, qtab, 8);
                        return 0;
                    }
                    st = (uint8_t *) rage128_gpu_2d_stage(dev, rlen, &soff);
                    if (!st) {
                        rage128_gpu_2d_qskip(dev, qtab, 64);
                        return 0;
                    }
                    for (uint32_t k = s; k < c; k++) {
                        uint32_t v = R128_MONO_V(1, k, r);

                        memcpy(st, &v, (size_t) bpp);
                        st += bpp;
                    }
                    if (!rage128_gpu_2d_copy(dev, addr + s * (uint32_t) bpp,
                                             rlen, rlen, 1, soff, rlen,
                                             route))
                        return 0;
                    rage128_2d_queue_mark(dev, addr + s * (uint32_t) bpp,
                                          rlen, rlen, 1);
                } else {
                    if (!rage128_gpu_2d_fill(dev, addr + s * (uint32_t) bpp,
                                             rowlen, (c - s) * (uint32_t) bpp,
                                             1, v_set, bpp, 1))
                        return 0;
                    rage128_2d_queue_mark(dev, addr + s * (uint32_t) bpp,
                                          rowlen, (c - s) * (uint32_t) bpp,
                                          1);
                }
            }
        }
    }
#undef R128_MONO_V
#undef R128_MONO_AUX
#undef R128_MONO_BIT
    if (aux)
        rage128_gpu_2d_qaux(dev, qtab);
    return 1;
}

/* Queue a host-data color rect (payload pixels already in destination
   format) as a staged GPU copy, each byte put through the ROP (which
   does not use the destination) at its place in the pixel.
   The payload limit (avail) works as in rage128_2d_host_color_rect:
   pixels whose source bytes lie past the end of the payload are not
   written, so the copy is cut to whole rows plus at most one partial
   row (the cut only moves down the rect, never back up).
   0 = not queueable, run the CPU walk. */
static int
rage128_2d_queue_hostc(rage128_t *dev, int x, int y, int w, int h,
                       const uint8_t *px, uint32_t avail, uint8_t rop,
                       int bpp)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    uint32_t pat    = dev->dp_brush_frgd_clr;
    uint32_t stride = (uint32_t) w * (uint32_t) bpp;
    int      cx0, cy0, cx1, cy1, rx0, ry0, rx1, ry1, aux;
    uint32_t base, rowlen, rows, pitch, ri0, need_full, f;
    uint64_t end;

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    rx0 = x < cx0 ? cx0 : x;
    ry0 = y < cy0 ? cy0 : y;
    rx1 = x + w - 1 > cx1 ? cx1 : x + w - 1;
    ry1 = y + h - 1 > cy1 ? cy1 : y + h - 1;
    if (rx0 > rx1 || ry0 > ry1)
        return 1;
    aux   = rage128_2d_aux_seg(dev, rx0, ry0, rx1, ry1);
    pitch = rage128_2d_dst_stride(dev, bpp);
    base  = dev->dst_offset
        + ((uint32_t) ry0 * rage128_2d_dst_stride(dev, bpp) + rx0 * (uint32_t) bpp);
    rowlen = (uint32_t) (rx1 - rx0 + 1) * (uint32_t) bpp;
    rows   = (uint32_t) (ry1 - ry0 + 1);
    end    = (uint64_t) base + (uint64_t) (rows - 1) * pitch + rowlen;
    if (r128_card_is_agp(base) || end > (uint64_t) dev->vram_size
        || (rows > 1 && pitch < rowlen)) {
        rage128_gpu_2d_qskip(dev, 1, 32);
        return 0;
    }
    ri0 = (uint32_t) (ry0 - y);

    if (aux) {
        /* per-pixel aux test: one staged copy per run of pixels that
           pass and lie inside the payload. A refused run needs no undo,
           since the re-walk stores the same bytes. The payload limit
           only moves forward in scan order: the first pixel past the
           payload ends the whole op, as the walk's goto done does. */
        uint32_t npx   = rowlen / (uint32_t) bpp;
        uint32_t nruns = 0;
        int      out   = 0;

        for (uint32_t r = 0; r < rows && !out; r++) {
            uint32_t addr = base + r * pitch;
            uint64_t roff = (uint64_t) (ri0 + r) * stride
                + (uint64_t) (uint32_t) (rx0 - x) * (uint32_t) bpp;
            uint32_t c = 0;

#define R128_HC_FIT(c)  (roff + ((uint64_t) (c) + 1u) * (uint32_t) bpp <= avail)
#define R128_HC_PASS(c) rage128_aux_sc_pass_2d(dev->aux_sc_cntl,                  \
                                               dev->aux_sc_rect, rx0 + (int) (c), \
                                               ry0 + (int) r)
            while (c < npx) {
                uint32_t s, rlen, soff;
                uint8_t *st;

                while (c < npx && R128_HC_FIT(c) && !R128_HC_PASS(c))
                    c++;
                if (c >= npx || !R128_HC_FIT(c)) {
                    out = c < npx; /* payload exhausted mid-row */
                    break;
                }
                s = c;
                while (c < npx && R128_HC_FIT(c) && R128_HC_PASS(c))
                    c++;
                if (++nruns > 256) {
                    rage128_gpu_2d_qskip(dev, 1, 8);
                    return 0;
                }
                rlen = (c - s) * (uint32_t) bpp;
                st   = (uint8_t *) rage128_gpu_2d_stage(dev, rlen, &soff);
                if (!st) {
                    rage128_gpu_2d_qskip(dev, 1, 64);
                    return 0;
                }
                memcpy(st, px + roff + (uint64_t) s * (uint32_t) bpp, rlen);
                if (rop != 0xcc)
                    for (uint32_t j = 0; j < rlen; j++)
                        st[j] = (uint8_t) rage128_rop3(rop,
                                                       pat >> ((j % (uint32_t) bpp) * 8), st[j], 0);
                if (!rage128_gpu_2d_copy(dev, addr + s * (uint32_t) bpp,
                                         rlen, rlen, 1, soff, rlen, 0))
                    return 0;
                rage128_2d_queue_mark(dev, addr + s * (uint32_t) bpp,
                                      rlen, rlen, 1);
            }
#undef R128_HC_FIT
#undef R128_HC_PASS
        }
        rage128_gpu_2d_qaux(dev, 1);
        return 1;
    }

    /* rows whose rightmost visible pixel's source bytes fit in avail */
    ri0       = (uint32_t) (ry0 - y);
    need_full = ((uint32_t) (rx1 - x) + 1u) * (uint32_t) bpp;
    if ((uint64_t) avail >= (uint64_t) ri0 * stride + need_full) {
        uint64_t fmax = ((uint64_t) avail - need_full) / stride - ri0 + 1;

        f = fmax > rows ? rows : (uint32_t) fmax;
    } else
        f = 0;

    if (f) {
        uint32_t soff;
        uint8_t *st = (uint8_t *) rage128_gpu_2d_stage(dev, f * rowlen, &soff);

        if (!st) {
            rage128_gpu_2d_qskip(dev, 1, 64);
            return 0;
        }
        for (uint32_t r = 0; r < f; r++) {
            const uint8_t *srow = px + (uint64_t) (ri0 + r) * stride
                + (uint32_t) (rx0 - x) * (uint32_t) bpp;
            uint8_t *drow = st + (uint64_t) r * rowlen;

            memcpy(drow, srow, rowlen);
            if (rop != 0xcc) {
                if (bpp == 4) {
                    uint32_t *dw = (uint32_t *) drow;

                    for (uint32_t i = 0; i < rowlen >> 2; i++)
                        dw[i] = rage128_rop3(rop, pat, dw[i], 0);
                } else
                    for (uint32_t i = 0; i < rowlen; i++)
                        drow[i] = (uint8_t) rage128_rop3(rop,
                                                         pat >> ((i % (uint32_t) bpp) * 8), drow[i], 0);
            }
        }
        if (!rage128_gpu_2d_copy(dev, base, pitch, rowlen, f, soff,
                                 rowlen, 0))
            return 0;
        rage128_2d_queue_mark(dev, base, pitch, rowlen, f);
    }
    if (f < rows) {
        /* at most one partial row; anything below it has no payload */
        uint64_t used     = (uint64_t) (ri0 + f) * stride;
        uint32_t px_avail = avail > used
            ? (uint32_t) ((avail - used) / (uint32_t) bpp)
            : 0;
        int      vis_end  = x + (int) px_avail - 1;

        if (vis_end > rx1)
            vis_end = rx1;
        if (px_avail && vis_end >= rx0) {
            uint32_t plen = (uint32_t) (vis_end - rx0 + 1) * (uint32_t) bpp;
            uint32_t soff;
            uint8_t *st = (uint8_t *) rage128_gpu_2d_stage(dev, plen, &soff);

            if (!st) {
                rage128_gpu_2d_qskip(dev, 1, 64);
                return 0;
            }
            memcpy(st, px + used + (uint32_t) (rx0 - x) * (uint32_t) bpp,
                   plen);
            if (rop != 0xcc) {
                if (bpp == 4) {
                    uint32_t *dw = (uint32_t *) st;

                    for (uint32_t i = 0; i < plen >> 2; i++)
                        dw[i] = rage128_rop3(rop, pat, dw[i], 0);
                } else
                    for (uint32_t i = 0; i < plen; i++)
                        st[i] = (uint8_t) rage128_rop3(rop,
                                                       pat >> ((i % (uint32_t) bpp) * 8), st[i], 0);
            }
            if (!rage128_gpu_2d_copy(dev, base + f * pitch, plen, plen, 1,
                                     soff, plen, 0))
                return 0;
            rage128_2d_queue_mark(dev, base + f * pitch, plen, plen, 1);
        }
    }
    return 1;
}

/* Queue a mono-expand rect under any ROP3 and write mask as one
   read-modify-write dispatch, with one A/B row per destination row. A
   set bit uses the source foreground as s; a clear bit uses the source
   background (opaque) or keeps the destination (leave-alone, A = 0xff,
   B = 0). The pattern value per pixel comes from the brush: solid
   foreground, or the screen-aligned 8x8 mono or color tile (a clear
   mono cell reads the brush background, or keeps the destination under
   brush type 1, as the CPU walk does).
   0 = not queued, nothing recorded. */
static int
rage128_2d_queue_mono_rmw(rage128_t *dev, int x, int y, int w, int h,
                          const uint8_t *bits, uint32_t bitpitch,
                          int use_bkgd, int lsb_first, uint8_t rop, int bpp,
                          const uint8_t *pat8x8, const uint8_t *patcol,
                          int pat_la)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    if (pat8x8 && rage128_2d_brush_32x32(dev))
        return 0;
    int      cx0, cy0, cx1, cy1, rx0, ry0, rx1, ry1, aux;
    uint32_t base, len, rows, pitch, npx, soff;
    uint8_t *st;

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    rx0 = x < cx0 ? cx0 : x;
    ry0 = y < cy0 ? cy0 : y;
    rx1 = x + w - 1 > cx1 ? cx1 : x + w - 1;
    ry1 = y + h - 1 > cy1 ? cy1 : y + h - 1;
    if (rx0 > rx1 || ry0 > ry1)
        return 1; /* fully scissored */
    aux   = rage128_2d_aux_seg(dev, rx0, ry0, rx1, ry1);
    pitch = rage128_2d_dst_stride(dev, bpp);
    base  = dev->dst_offset
        + ((uint32_t) ry0 * rage128_2d_dst_stride(dev, bpp) + rx0 * (uint32_t) bpp);
    npx  = (uint32_t) (rx1 - rx0 + 1);
    len  = npx * (uint32_t) bpp;
    rows = (uint32_t) (ry1 - ry0 + 1);
    st   = (uint8_t *) rage128_gpu_2d_stage(dev, rows * len * 2u, &soff);
    if (!st) {
        rage128_gpu_2d_qskip(dev, 8, 64);
        return 0;
    }

#define R128_MONO_BIT(i)                           \
    (lsb_first ? (bits[(i) >> 3] >> ((i) & 7)) & 1 \
               : (bits[(i) >> 3] >> (7 - ((i) & 7))) & 1)

    for (uint32_t r = 0; r < rows; r++) {
        uint32_t bit = (uint32_t) (ry0 - y + (int) r) * bitpitch
            + (uint32_t) (rx0 - x);

        for (uint32_t c = 0; c < npx; c++, bit++) {
            int set  = R128_MONO_BIT(bit);
            int pass = !aux
                || rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect,
                                          rx0 + (int) c, ry0 + (int) r);

            for (int b = 0; b < bpp; b++) {
                uint32_t i    = r * len + c * (uint32_t) bpp + (uint32_t) b;
                int      lane = b & 3;
                uint8_t  pb;

                if (!pass || (!set && !use_bkgd)) {
                    st[i]              = 0xff;
                    st[rows * len + i] = 0;
                    continue;
                }
                if (pat8x8) {
                    int pset = rage128_2d_pat8_bit(dev,
                                                   pat8x8[rage128_2d_pat_row(dev, ry0 + (int) r)],
                                                   rx0 + (int) c);

                    if (!pset && pat_la) { /* leave-alone cell keeps the destination */
                        st[i]              = 0xff;
                        st[rows * len + i] = 0;
                        continue;
                    }
                    pb = (uint8_t) ((pset ? dev->dp_brush_frgd_clr
                                          : dev->dp_brush_bkgd_clr)
                                    >> (lane * 8));
                } else if (patcol)
                    pb = patcol[(uint32_t) (rage128_2d_pat_row(dev, ry0 + (int) r) * 8
                                            + rage128_2d_pat_col(dev, rx0 + (int) c))
                                    * (uint32_t) bpp
                                + (uint32_t) b];
                else
                    pb = (uint8_t) (dev->dp_brush_frgd_clr >> (lane * 8));
                rage128_2d_rmw_ab(rop, pb,
                                  (uint8_t) ((set ? dev->dp_src_frgd_clr
                                                  : dev->dp_src_bkgd_clr)
                                             >> (lane * 8)),
                                  (uint8_t) (dev->dp_write_mask >> (lane * 8)),
                                  &st[i], &st[rows * len + i]);
            }
        }
    }
#undef R128_MONO_BIT
    if (!rage128_gpu_2d_rmw(dev, base, pitch, len, rows, soff,
                            soff + rows * len, 0xffffffffu)) {
        rage128_gpu_2d_qskip(dev, 8, 32);
        return 0;
    }
    rage128_2d_queue_mark(dev, base, pitch, len, rows);
    rage128_gpu_2d_qdone(dev, 8);
    if (aux)
        rage128_gpu_2d_qaux(dev, 0);
    return 1;
}

/* Queue a host-data color rect under any ROP3 and write mask as one
   read-modify-write dispatch. The payload bytes are the source, the
   brush gives the pattern for each byte; pixels whose source bytes lie
   past the end of the payload keep the destination (the CPU walk's
   payload limit, per pixel). 0 = not queued, nothing recorded. */
static int
rage128_2d_queue_hostc_rmw(rage128_t *dev, int x, int y, int w, int h,
                           const uint8_t *px, uint32_t avail, uint8_t rop,
                           int bpp, const uint8_t *pat8x8,
                           const uint8_t *patcol, int pat_la)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    if (pat8x8 && rage128_2d_brush_32x32(dev))
        return 0;
    uint32_t stride = (uint32_t) w * (uint32_t) bpp;
    int      cx0, cy0, cx1, cy1, rx0, ry0, rx1, ry1, aux;
    uint32_t base, len, rows, pitch, soff;
    uint8_t *st;

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    rx0 = x < cx0 ? cx0 : x;
    ry0 = y < cy0 ? cy0 : y;
    rx1 = x + w - 1 > cx1 ? cx1 : x + w - 1;
    ry1 = y + h - 1 > cy1 ? cy1 : y + h - 1;
    if (rx0 > rx1 || ry0 > ry1)
        return 1;
    aux   = rage128_2d_aux_seg(dev, rx0, ry0, rx1, ry1);
    pitch = rage128_2d_dst_stride(dev, bpp);
    base  = dev->dst_offset
        + ((uint32_t) ry0 * rage128_2d_dst_stride(dev, bpp) + rx0 * (uint32_t) bpp);
    len  = (uint32_t) (rx1 - rx0 + 1) * (uint32_t) bpp;
    rows = (uint32_t) (ry1 - ry0 + 1);
    st   = (uint8_t *) rage128_gpu_2d_stage(dev, rows * len * 2u, &soff);
    if (!st) {
        rage128_gpu_2d_qskip(dev, 8, 64);
        return 0;
    }
    for (uint32_t r = 0; r < rows; r++)
        for (uint32_t c = 0; c < len / (uint32_t) bpp; c++) {
            /* per-pixel payload limit, as the walk's goto done */
            uint64_t poff = (uint64_t) (uint32_t) (ry0 - y + (int) r) * stride
                + (uint64_t) ((uint32_t) (rx0 - x) + c) * (uint32_t) bpp;
            int fits = poff + (uint32_t) bpp <= avail;
            int pass = !aux
                || rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect,
                                          rx0 + (int) c, ry0 + (int) r);
            uint32_t       pat   = dev->dp_brush_frgd_clr;
            const uint8_t *patpx = NULL;

            if (pat8x8) {
                int pset = rage128_2d_pat8_bit(dev,
                                               pat8x8[rage128_2d_pat_row(dev, ry0 + (int) r)],
                                               rx0 + (int) c);

                if (!pset && pat_la)
                    pass = 0; /* leave-alone cell keeps the destination */
                pat = pset ? dev->dp_brush_frgd_clr : dev->dp_brush_bkgd_clr;
            } else if (patcol)
                patpx = &patcol[(uint32_t) (rage128_2d_pat_row(dev, ry0 + (int) r) * 8
                                            + rage128_2d_pat_col(dev, rx0 + (int) c))
                                * (uint32_t) bpp];

            for (int b = 0; b < bpp; b++) {
                uint32_t i    = r * len + c * (uint32_t) bpp + (uint32_t) b;
                int      lane = b & 3;

                if (!fits || !pass) {
                    st[i]              = 0xff;
                    st[rows * len + i] = 0;
                    continue;
                }
                rage128_2d_rmw_ab(rop,
                                  patpx ? patpx[b] : (uint8_t) (pat >> (lane * 8)),
                                  px[poff + (uint32_t) b],
                                  (uint8_t) (dev->dp_write_mask >> (lane * 8)),
                                  &st[i], &st[rows * len + i]);
            }
        }
    if (!rage128_gpu_2d_rmw(dev, base, pitch, len, rows, soff,
                            soff + rows * len, 0xffffffffu)) {
        rage128_gpu_2d_qskip(dev, 8, 32);
        return 0;
    }
    rage128_2d_queue_mark(dev, base, pitch, len, rows);
    rage128_gpu_2d_qdone(dev, 8);
    if (aux)
        rage128_gpu_2d_qaux(dev, 1);
    return 1;
}

/* Queue a pattern-brush paint on the GPU: 8x8 mono or color pattern,
   any destination depth, write mask fully open, ROP that does not use
   the destination (the caller checked). Patterns are screen-aligned, so
   destination rows 8 apart get the same pattern row. An opaque mono or
   a color pattern stages at most 8 finished rows and copies each one to
   every eighth row with a source pitch of 0. A leave-alone mono pattern
   queues a solid fill per run of set bits, repeated every 8 rows; a
   dense pattern breaks into many short runs, so the run count is
   capped. A per-pixel aux test turns the 8-row repeats into per-row
   runs. Every stored value depends only on the latched operands and is
   computed here once through the ROP. 0 = not queueable, run the CPU
   walk (a partial queue is harmless: the re-walk stores the same
   bytes). */
static int
rage128_2d_queue_pat(rage128_t *dev, int x, int y, int w, int h,
                     const uint8_t *mono8x8, const uint8_t *col8x8,
                     int mono_la, uint8_t rop, int bpp)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    if (mono8x8 && rage128_2d_brush_32x32(dev))
        return 0;
    uint32_t pitch = rage128_2d_dst_stride(dev, bpp);
    uint32_t base  = dev->dst_offset
        + ((uint32_t) y * rage128_2d_dst_stride(dev, bpp) + (uint32_t) x * (uint32_t) bpp);
    uint32_t rowlen = (uint32_t) w * (uint32_t) bpp;
    uint32_t rows   = (uint32_t) h;
    uint32_t prows  = rows < 8 ? rows : 8;
    uint32_t v_set  = rage128_rop3(rop, dev->dp_brush_frgd_clr, 0, 0);
    uint32_t v_clr  = rage128_rop3(rop, dev->dp_brush_bkgd_clr, 0, 0);
    uint64_t end    = (uint64_t) base + (uint64_t) (rows - 1) * pitch + rowlen;

    if (x < 0 || y < 0 || r128_card_is_agp(base)
        || end > (uint64_t) dev->vram_size
        || (rows > 1 && pitch < rowlen)) {
        rage128_gpu_2d_qskip(dev, 2, 32);
        return 0;
    }

    int aux = rage128_2d_aux_seg(dev, x, y, x + w - 1, y + h - 1);

#define R128_PAT_BIT(prow, c) rage128_2d_pat8_bit(dev, (prow), (x) + (c))
#define R128_PAT_AUX(c, r)                                     \
    rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, \
                           (x) + (c), (y) + (int) (r))

    if (mono8x8 && mono_la && aux) {
        /* leave-alone with a per-pixel aux test: the test varies by
           row, so the every-eighth-row fills cannot be used; fill runs
           row by row instead (a refused run needs no undo) */
        uint32_t nruns = 0;

        for (uint32_t r = 0; r < rows; r++) {
            uint8_t  prow = mono8x8[rage128_2d_pat_row(dev, y + (int) r)];
            uint32_t addr = base + r * pitch;
            int      c    = 0;

            while (c < w) {
                int s;

                while (c < w
                       && !(R128_PAT_BIT(prow, c) && R128_PAT_AUX(c, r)))
                    c++;
                s = c;
                while (c < w && R128_PAT_BIT(prow, c) && R128_PAT_AUX(c, r))
                    c++;
                if (c > s) {
                    uint32_t rl = (uint32_t) (c - s) * (uint32_t) bpp;

                    if (++nruns > 256) {
                        rage128_gpu_2d_qskip(dev, 2, 8);
                        return 0;
                    }
                    if (!rage128_gpu_2d_fill(dev,
                                             addr + (uint32_t) s * (uint32_t) bpp,
                                             rl, rl, 1, v_set, bpp, 2))
                        return 0;
                    rage128_2d_queue_mark(dev,
                                          addr + (uint32_t) s * (uint32_t) bpp,
                                          rl, rl, 1);
                }
            }
        }
        rage128_gpu_2d_qaux(dev, 2);
        return 1;
    }

    if (mono8x8 && mono_la) {
        uint32_t nruns = 0;

        for (uint32_t pr = 0; pr < prows && nruns <= 64; pr++) {
            uint8_t prow = mono8x8[rage128_2d_pat_row(dev, y + (int) pr)];
            int     last = 0;

            for (int c = 0; c < w && nruns <= 64; c++) {
                int bit = R128_PAT_BIT(prow, c);

                nruns += (uint32_t) (bit && !last);
                last = bit;
            }
        }
        if (nruns > 64) {
            rage128_gpu_2d_qskip(dev, 2, 8);
            return 0;
        }
        for (uint32_t pr = 0; pr < prows; pr++) {
            uint32_t nrep = (rows - pr + 7) / 8;
            uint32_t addr = base + pr * pitch;
            uint8_t  prow = mono8x8[rage128_2d_pat_row(dev, y + (int) pr)];
            int      c    = 0;

            while (c < w) {
                int s;

                while (c < w && !R128_PAT_BIT(prow, c))
                    c++;
                s = c;
                while (c < w && R128_PAT_BIT(prow, c))
                    c++;
                if (c > s) {
                    if (!rage128_gpu_2d_fill(dev,
                                             addr + (uint32_t) s * (uint32_t) bpp,
                                             pitch * 8u,
                                             (uint32_t) (c - s) * (uint32_t) bpp,
                                             nrep, v_set, bpp, 2))
                        return 0;
                    rage128_2d_queue_mark(dev,
                                          addr + (uint32_t) s * (uint32_t) bpp,
                                          pitch * 8u,
                                          (uint32_t) (c - s) * (uint32_t) bpp,
                                          nrep);
                }
            }
        }
        return 1;
    }

    /* opaque mono or color pattern: every pixel stored, staged tile */
    {
        uint32_t soff;
        uint8_t *st = (uint8_t *) rage128_gpu_2d_stage(dev, prows * rowlen,
                                                       &soff);

        if (!st) {
            rage128_gpu_2d_qskip(dev, 2, 64);
            return 0;
        }
        for (uint32_t pr = 0; pr < prows; pr++) {
            int py = rage128_2d_pat_row(dev, y + (int) pr);

            for (int c = 0; c < w; c++) {
                uint32_t v;

                if (mono8x8)
                    v = R128_PAT_BIT(mono8x8[py], c) ? v_set : v_clr;
                else {
                    uint32_t pd = 0;

                    memcpy(&pd,
                           &col8x8[(uint32_t) (py * 8 + rage128_2d_pat_col(dev, x + c))
                                   * (uint32_t) bpp],
                           (size_t) bpp);
                    v = rage128_rop3(rop, pd, 0, 0);
                }
                memcpy(st, &v, (size_t) bpp);
                st += bpp;
            }
        }
        if (aux) {
            /* per-pixel aux test: run copies per row from the staged
               tile row (destination row r uses tile row r & 7; prows is
               8 whenever rows is more than 8). The aux test varies by
               row, so the every-eighth-row copies cannot be used. */
            uint32_t nruns = 0;

            for (uint32_t r = 0; r < rows; r++) {
                uint32_t tr   = r & 7;
                uint32_t addr = base + r * pitch;
                int      c    = 0;

                while (c < w) {
                    int s;

                    while (c < w && !R128_PAT_AUX(c, r))
                        c++;
                    s = c;
                    while (c < w && R128_PAT_AUX(c, r))
                        c++;
                    if (c > s) {
                        uint32_t rl = (uint32_t) (c - s) * (uint32_t) bpp;

                        if (++nruns > 256) {
                            rage128_gpu_2d_qskip(dev, 2, 8);
                            return 0;
                        }
                        if (!rage128_gpu_2d_copy(dev,
                                                 addr + (uint32_t) s * (uint32_t) bpp,
                                                 rl, rl, 1,
                                                 soff + tr * rowlen
                                                     + (uint32_t) s * (uint32_t) bpp,
                                                 rl, 2))
                            return 0;
                        rage128_2d_queue_mark(dev,
                                              addr + (uint32_t) s * (uint32_t) bpp,
                                              rl, rl, 1);
                    }
                }
            }
            rage128_gpu_2d_qaux(dev, 2);
            return 1;
        }
        for (uint32_t pr = 0; pr < prows; pr++) {
            uint32_t nrep = (rows - pr + 7) / 8;

            if (!rage128_gpu_2d_copy(dev, base + pr * pitch, pitch * 8u,
                                     rowlen, nrep, soff + pr * rowlen, 0, 2))
                return 0;
            rage128_2d_queue_mark(dev, base + pr * pitch, pitch * 8u,
                                  rowlen, nrep);
        }
        return 1;
    }
#undef R128_PAT_AUX
#undef R128_PAT_BIT
}

/* Fill one rectangle through the current ROP3. Brush types 13 and 15
   paint the solid foreground color; brush types 0 and 1 expand the 8x8
   mono pattern (0 = foreground and background, 1 = foreground and leave
   alone); brush type 10 is an 8x8 color pattern in the destination
   pixel format (RRG: DP_GUI_MASTER_CNTL, p. 3-173 / PDF 191).
   mono8x8 points at the two pattern dwords, byte n holding row n, MSB
   first within the row (the Windows 98 display driver packs rows 0-3
   into the first dword, RE: ati2draa.drv @6f9a, a file offset), or at
   the 32 row dwords of brush types 8 and 9 (rage128_2d_mono_bit).
   col8x8 points at 64 destination-format pixels, row by row. Both are
   NULL for a solid brush. Patterns line up with the screen, offset by
   BRUSH_Y_X. */
static void
rage128_2d_paint_rect(rage128_t *dev, int x, int y, int w, int h,
                      const uint8_t *mono8x8, const uint8_t *col8x8, int mono_la)
{
    int              bpp    = rage128_2d_bpp(rage128_dp_dst_dt(dev));
    uint8_t          rop    = rage128_dp_rop3(dev);
    uint32_t         wmask  = dev->dp_write_mask;
    int              aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    struct r128_ccmp cc;
    int              cca;
    r128_surf_t      sd;
    int              cx0, cy0, cx1, cy1;

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    if (x < cx0) {
        w -= cx0 - x;
        x = cx0;
    }
    if (y < cy0) {
        h -= cy0 - y;
        y = cy0;
    }
    if (x + w - 1 > cx1)
        w = cx1 - x + 1;
    if (y + h - 1 > cy1)
        h = cy1 - y + 1;
    if (w <= 0 || h <= 0)
        return;
    cca = rage128_2d_ccmp_setup(dev, 0, bpp, bpp, &cc);

    /* Fast path: a solid PATCOPY (ROP 0xF0, result = pattern) with the
       write mask fully open fills each row with the brush color. That
       stores the same bytes as the general walk (rop3(0xF0, pb, 0, d) =
       pb, mask byte 0xff). Aux scissors still allow it when the whole
       clipped rect passes them. A partial write mask, a pattern brush,
       another ROP or an active color compare takes the per-byte walk. */
    int aux_ok = !aux_on
        || rage128_aux_sc_accepts_rect(dev->aux_sc_cntl, dev->aux_sc_rect,
                                       x, y, x + w - 1, y + h - 1);
    int fast = aux_ok && wmask == 0xffffffffu && rop == 0xf0
        && !mono8x8 && !col8x8 && !cca;
    uint8_t fpx[4];
    if (fast)
        for (int b = 0; b < bpp; b++)
            fpx[b] = (uint8_t) ((dev->dp_brush_frgd_clr >> ((b & 3) * 8)) & 0xff);

    /* A fast fill goes to the GPU queue at any destination depth; it is
       ordered with the other queued work, so nothing has to be drained
       first. The queue stores the low bytes of the color, as the CPU
       walk does. With synctel on, the CPU path is kept so its access
       record stays complete. */
    if (fast && dev->gpu && !dev->synctel
        && rage128_2d_queue_fill(dev, x, y, w, h, bpp)) {
        if (bpp != 4)
            rage128_gpu_2d_nb(dev, 3);
        return;
    }

    /* Pattern brushes. Under a ROP that does not use the destination,
       every store depends only on the latched operands, so the op is
       queued as repeated tile copies (opaque mono or color) or run fills
       (leave-alone mono) at the destination depth; any other ROP or a
       partial write mask goes through the read-modify-write queue. */
    int pat_q = 0;

    if ((mono8x8 || col8x8) && !cca && dev->gpu && !dev->synctel) {
        int dind = rage128_2d_rop_dindep(rop)
            && wmask == 0xffffffffu;
        int vq = rage128_2d_vq_begin(dev, dind ? 2 : 8,
                                     dev->dst_offset
                                         + ((uint32_t) y * rage128_2d_dst_stride(dev, bpp) + (uint32_t) x * (uint32_t) bpp),
                                     rage128_2d_dst_stride(dev, bpp),
                                     (uint32_t) w * (uint32_t) bpp, (uint32_t) h);
        int ok = dind
            ? rage128_2d_queue_pat(dev, x, y, w, h, mono8x8, col8x8,
                                   mono_la, rop, bpp)
            : rage128_2d_queue_pat_rmw(dev, x, y, w, h, mono8x8,
                                       col8x8, mono_la, rop, bpp);

        if (ok) {
            if (bpp != 4)
                rage128_gpu_2d_nb(dev, 2);
            if (!vq)
                return;
            rage128_2d_vq_after(dev, 1);
            pat_q = 1; /* self-check re-walk: skip the refusal count */
        } else if (vq)
            rage128_2d_vq_after(dev, 0);
    }

    /* A solid paint under another ROP3, a partial write mask or a
       per-pixel aux test is queued as one read-modify-write dispatch
       (pixels the aux test rejects keep the destination). */
    if (!mono8x8 && !col8x8 && !fast && !cca
        && dev->gpu && !dev->synctel) {
        int vq = rage128_2d_vq_begin(dev, 8,
                                     dev->dst_offset
                                         + ((uint32_t) y * rage128_2d_dst_stride(dev, bpp) + (uint32_t) x * (uint32_t) bpp),
                                     rage128_2d_dst_stride(dev, bpp),
                                     (uint32_t) w * (uint32_t) bpp, (uint32_t) h);

        if (rage128_2d_queue_fill_rmw(dev, x, y, w, h, rop, bpp)) {
            if (bpp != 4)
                rage128_gpu_2d_nb(dev, 3);
            if (!vq)
                return;
            rage128_2d_vq_after(dev, 1);
            pat_q = 1;
        } else if (vq)
            rage128_2d_vq_after(dev, 0);
    }

    if (dev->gpu && !dev->synctel && !pat_q) {
        int why = 0;

        if (rop != 0xf0)
            why |= 1;
        if (wmask != 0xffffffffu)
            why |= 2;
        if (!aux_ok)
            why |= 4;
        if (mono8x8 || col8x8)
            why |= 8;
        if (cca)
            why |= 16;
        rage128_gpu_2d_fill_skip(dev, why, rop, wmask);
    }

    if (!rage128_2d_map_span(dev, &sd, &dev->s2d_dst, dev->dst_offset,
                             dev->dst_pitch, bpp, x, y, y + h - 1, x + w - 1)) {
        rage128_2d_vq_abort(dev);
        return;
    }

    for (int row = 0; row < h; row++) {
        uint32_t addr = dev->dst_offset + ((uint32_t) (y + row) * rage128_2d_dst_stride(dev, bpp) + x * bpp);

        if (fast) {
            uint8_t *run = r128_surf_run(&sd, addr, (uint32_t) w * bpp);

            if (run) {
                r128_row_fill(run, fpx, bpp, w);
                rage128_2d_mark_dirty(dev, &sd, addr, (uint32_t) w * bpp);
                continue;
            }
        }
        for (int col = 0; col < w; col++) {
            uint32_t       pat   = dev->dp_brush_frgd_clr;
            const uint8_t *patpx = NULL;
            uint32_t       a     = addr + (uint32_t) col * bpp;

            if (aux_on && !rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x + col, y + row))
                continue;
            if (mono8x8) {
                int set = rage128_2d_mono_bit(dev, mono8x8, x + col, y + row);

                if (!set && mono_la)
                    continue; /* brush type 1: leave alone */
                pat = set ? dev->dp_brush_frgd_clr : dev->dp_brush_bkgd_clr;
            } else if (col8x8)
                patpx = &col8x8[(rage128_2d_pat_row(dev, y + row) * 8
                                 + rage128_2d_pat_col(dev, x + col))
                                * bpp];
            if (cca) {
                uint32_t dpx = 0;

                for (int b = 0; b < bpp; b++)
                    dpx |= (uint32_t) *r128_surf_at(&sd, a + (uint32_t) b)
                        << (b * 8);
                if (!rage128_2d_ccmp_px(&cc, 0, dpx))
                    continue;
            }
            for (int b = 0; b < bpp; b++) {
                uint8_t *dp = r128_surf_at(&sd, a + (uint32_t) b);
                uint8_t  pb = patpx ? patpx[b] : ((pat >> ((b & 3) * 8)) & 0xff);

                rage128_2d_store(dp, rage128_rop3(rop, pb, 0, *dp),
                                 (uint8_t) (wmask >> ((b & 3) * 8)));
            }
        }
        rage128_2d_mark_dirty(dev, &sd, addr, (uint32_t) w * bpp);
    }
    r128_surf_commit(dev, &sd);
    rage128_2d_vq_end(dev);
}

/* Run collector for queued line fills: plotted pixels next to each other
   on one row with the same color merge into one 1-row 32 bpp fill
   (route 5). An op may have at most 256 runs; past that it is refused
   as too fragmented. */
struct r128_line_q {
    uint32_t color;
    int      y, x0, x1;
    int      active, nruns;
};

static int
rage128_2d_lineq_flush(rage128_t *dev, struct r128_line_q *q)
{
    uint32_t addr, len;

    if (!q->active)
        return 1;
    q->active = 0;
    if (++q->nruns > 256) {
        rage128_gpu_2d_qskip(dev, 6, 8);
        return 0;
    }
    addr = dev->dst_offset
        + ((uint32_t) q->y * dev->dst_pitch + (uint32_t) q->x0) * 4u;
    len = (uint32_t) (q->x1 - q->x0 + 1) * 4u;
    if (!rage128_gpu_2d_fill(dev, addr, len, len, 1, q->color, 4, 5))
        return 0;
    rage128_2d_queue_mark(dev, addr, len, len, 1);
    return 1;
}

static int
rage128_2d_lineq_px(rage128_t *dev, struct r128_line_q *q, int x, int y,
                    uint32_t color)
{
    if (q->active && y == q->y && color == q->color
        && (x == q->x1 + 1 || x == q->x0 - 1)) {
        if (x > q->x1)
            q->x1 = x;
        if (x < q->x0)
            q->x0 = x;
        return 1;
    }
    if (!rage128_2d_lineq_flush(dev, q))
        return 0;
    q->active = 1;
    q->y      = y;
    q->x0 = q->x1 = x;
    q->color      = color;
    return 1;
}

/* Read-modify-write line queue: collect plotted pixels into 1-row runs
   by class (0 = foreground, 1 = background), and queue them only after
   every run has been checked. A store that depends on the destination
   cannot be repeated by a CPU re-walk, so a partial queue is not
   allowed: all or nothing. */
struct r128_lrq {
    struct {
        uint32_t x, y, px;
        uint8_t  cls;
    } run[256];
    int     n, active;
    int     y, x0, x1;
    uint8_t cls;
};

static int
rage128_2d_lrq_flush(struct r128_lrq *q)
{
    if (!q->active)
        return 1;
    q->active = 0;
    if (q->n >= 256)
        return 0; /* over the run cap: refused as too fragmented */
    q->run[q->n].x   = (uint32_t) q->x0;
    q->run[q->n].y   = (uint32_t) q->y;
    q->run[q->n].px  = (uint32_t) (q->x1 - q->x0 + 1);
    q->run[q->n].cls = q->cls;
    q->n++;
    return 1;
}

static int
rage128_2d_lrq_px(struct r128_lrq *q, int x, int y, uint8_t cls)
{
    if (q->active && y == q->y && cls == q->cls
        && (x == q->x1 + 1 || x == q->x0 - 1)) {
        if (x > q->x1)
            q->x1 = x;
        if (x < q->x0)
            q->x0 = x;
        return 1;
    }
    if (!rage128_2d_lrq_flush(q))
        return 0;
    q->active = 1;
    q->y      = y;
    q->x0 = q->x1 = x;
    q->cls        = cls;
    return 1;
}

/* Check every collected run, then stage the A/B row of each run and
   queue the read-modify-write dispatches. Once the checks pass nothing
   can fail, so the op is queued whole or not at all. 0 = nothing
   queued. */
static int
rage128_2d_lrq_queue(rage128_t *dev, struct r128_lrq *q, uint8_t rop,
                     int bpp)
{
    uint8_t  ab[2][2][4]; /* [cls][a/b][lane] */
    uint32_t total = 0, soff, off = 0;
    uint8_t *st;

    if (!q->n)
        return 1;
    for (int cls = 0; cls < 2; cls++)
        for (int b = 0; b < bpp; b++)
            rage128_2d_rmw_ab(rop,
                              (uint8_t) ((cls ? dev->dp_brush_bkgd_clr
                                              : dev->dp_brush_frgd_clr)
                                         >> ((b & 3) * 8)),
                              0,
                              (uint8_t) (dev->dp_write_mask >> ((b & 3) * 8)),
                              &ab[cls][0][b & 3], &ab[cls][1][b & 3]);
    for (int i = 0; i < q->n; i++) {
        uint32_t addr = dev->dst_offset
            + (q->run[i].y * rage128_2d_dst_stride(dev, bpp) + q->run[i].x * (uint32_t) bpp);
        uint32_t len = q->run[i].px * (uint32_t) bpp;

        if (r128_card_is_agp(addr)
            || (((uint64_t) addr + len + 3u) & ~3ull)
                > (uint64_t) dev->vram_size) {
            rage128_gpu_2d_qskip(dev, 8, 32);
            return 0;
        }
        total += len;
    }
    st = (uint8_t *) rage128_gpu_2d_stage(dev, total * 2u, &soff);
    if (!st) {
        rage128_gpu_2d_qskip(dev, 8, 64);
        return 0;
    }
    for (int i = 0; i < q->n; i++) {
        uint32_t addr = dev->dst_offset
            + (q->run[i].y * rage128_2d_dst_stride(dev, bpp) + q->run[i].x * (uint32_t) bpp);
        uint32_t len = q->run[i].px * (uint32_t) bpp;
        int      cls = q->run[i].cls;

        for (uint32_t j = 0; j < len; j++) {
            st[off + j]         = ab[cls][0][j % (uint32_t) bpp];
            st[total + off + j] = ab[cls][1][j % (uint32_t) bpp];
        }
        if (!rage128_gpu_2d_rmw(dev, addr, len, len, 1, soff + off,
                                soff + total + off, 0)) {
            rage128_gpu_2d_qskip(dev, 8, 32);
            return 0; /* unreachable after the prechecks */
        }
        rage128_2d_queue_mark(dev, addr, len, len, 1);
        off += len;
    }
    rage128_gpu_2d_qdone(dev, 8);
    return 1;
}

/* Queue a line segment: the same Bresenham walk as rage128_2d_line, but
   each plotted pixel joins a merged 1-row fill whose dword is the brush
   color put through the ROP (ROP that does not use the destination,
   write mask fully open, 32 bpp; the caller checked). Advances *phase
   exactly as the walk would; the caller puts it back before any
   re-walk. 0 = not queued (a partial queue is harmless: the re-walk
   stores the same bytes). */
static int
rage128_2d_queue_line(rage128_t *dev, int x0, int y0, int x1, int y1,
                      uint32_t pat32, int pat_en, int pat_la, int *phase,
                      uint8_t rop)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    int                aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    uint32_t           v_set  = rage128_rop3(rop, dev->dp_brush_frgd_clr, 0, 0);
    uint32_t           v_clr  = rage128_rop3(rop, dev->dp_brush_bkgd_clr, 0, 0);
    int                cx0, cy0, cx1, cy1;
    int                dx       = x1 > x0 ? x1 - x0 : x0 - x1;
    int                dy       = y1 > y0 ? y1 - y0 : y0 - y1;
    int                sx       = x0 < x1 ? 1 : -1;
    int                sy       = y0 < y1 ? 1 : -1;
    int                err      = dx - dy;
    int                last_pel = (dev->dp_cntl & RAGE128_DP_CNTL_DST_LAST_PEL) != 0;
    struct r128_line_q q        = { 0 };

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    for (;;) {
        int set    = 1;
        int at_end = (x0 == x1 && y0 == y1);

        if (at_end && !last_pel)
            break; /* DST_LAST_PEL 0: last pixel not drawn */
        if (pat_en) {
            set = rage128_2d_pat32_bit(dev, pat32, *phase);
            (*phase)++;
        }
        if ((set || !pat_la)
            && x0 >= cx0 && x0 <= cx1 && y0 >= cy0 && y0 <= cy1
            && (!aux_on || rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x0, y0))) {
            if (!rage128_2d_lineq_px(dev, &q, x0, y0, set ? v_set : v_clr))
                return 0;
        }
        if (at_end)
            break;
        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
    return rage128_2d_lineq_flush(dev, &q);
}

/* Queue a Bresenham line started by register writes: the same plot pass
   as rage128_2d_bres_line, on a copy of the walk state made by the
   caller. Same rules as rage128_2d_queue_line (the pattern position
   advances in *phase; the caller puts it back before any re-walk). */
static int
rage128_2d_queue_bres(rage128_t *dev, int x, int y, int n, int32_t inc,
                      int32_t dec, int32_t e, int ymajor, int xstep,
                      int ystep, int zero_pos, uint32_t pat32, int pat_en,
                      int pat_la, int *phase, uint8_t rop)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    int                aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    uint32_t           v_set  = rage128_rop3(rop, dev->dp_brush_frgd_clr, 0, 0);
    uint32_t           v_clr  = rage128_rop3(rop, dev->dp_brush_bkgd_clr, 0, 0);
    int                cx0, cy0, cx1, cy1;
    struct r128_line_q q = { 0 };

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    for (int i = 0; i < n; i++) {
        int set = 1;

        if (pat_en) {
            set = rage128_2d_pat32_bit(dev, pat32, *phase);
            (*phase)++;
        }
        if ((set || !pat_la)
            && x >= cx0 && x <= cx1 && y >= cy0 && y <= cy1
            && (!aux_on || rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x, y))) {
            if (!rage128_2d_lineq_px(dev, &q, x, y, set ? v_set : v_clr))
                return 0;
        }
        if (zero_pos ? e >= 0 : e > 0) {
            if (ymajor)
                x += xstep;
            else
                y += ystep;
            e += dec;
        }
        e += inc;
        if (ymajor)
            y += ystep;
        else
            x += xstep;
    }
    return rage128_2d_lineq_flush(dev, &q);
}

/* Read-modify-write form of rage128_2d_queue_line: same walk,
   foreground and background runs, any ROP3 and write mask. Same rules
   as rage128_2d_queue_line (the pattern position advances; the caller
   puts it back before a re-walk). */
static int
rage128_2d_queue_line_rmw(rage128_t *dev, int x0, int y0, int x1, int y1,
                          uint32_t pat32, int pat_en, int pat_la,
                          int *phase, uint8_t rop, int bpp)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    int             aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    int             cx0, cy0, cx1, cy1;
    int             dx       = x1 > x0 ? x1 - x0 : x0 - x1;
    int             dy       = y1 > y0 ? y1 - y0 : y0 - y1;
    int             sx       = x0 < x1 ? 1 : -1;
    int             sy       = y0 < y1 ? 1 : -1;
    int             err      = dx - dy;
    int             last_pel = (dev->dp_cntl & RAGE128_DP_CNTL_DST_LAST_PEL) != 0;
    struct r128_lrq q        = { 0 };

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    for (;;) {
        int set    = 1;
        int at_end = (x0 == x1 && y0 == y1);

        if (at_end && !last_pel)
            break; /* DST_LAST_PEL 0: last pixel not drawn */
        if (pat_en) {
            set = rage128_2d_pat32_bit(dev, pat32, *phase);
            (*phase)++;
        }
        if ((set || !pat_la)
            && x0 >= cx0 && x0 <= cx1 && y0 >= cy0 && y0 <= cy1
            && (!aux_on || rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x0, y0))) {
            if (!rage128_2d_lrq_px(&q, x0, y0, set ? 0 : 1)) {
                rage128_gpu_2d_qskip(dev, 8, 8);
                return 0;
            }
        }
        if (at_end)
            break;
        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
    if (!rage128_2d_lrq_flush(&q)) {
        rage128_gpu_2d_qskip(dev, 8, 8);
        return 0;
    }
    return rage128_2d_lrq_queue(dev, &q, rop, bpp);
}

/* Read-modify-write form of rage128_2d_queue_bres (foreground and
   background runs, same pattern position rules). */
static int
rage128_2d_queue_bres_rmw(rage128_t *dev, int x, int y, int n, int32_t inc,
                          int32_t dec, int32_t e, int ymajor, int xstep,
                          int ystep, int zero_pos, uint32_t pat32, int pat_en,
                          int pat_la, int *phase, uint8_t rop, int bpp)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    int             aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    int             cx0, cy0, cx1, cy1;
    struct r128_lrq q = { 0 };

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    for (int i = 0; i < n; i++) {
        int set = 1;

        if (pat_en) {
            set = rage128_2d_pat32_bit(dev, pat32, *phase);
            (*phase)++;
        }
        if ((set || !pat_la)
            && x >= cx0 && x <= cx1 && y >= cy0 && y <= cy1
            && (!aux_on || rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x, y))) {
            if (!rage128_2d_lrq_px(&q, x, y, set ? 0 : 1)) {
                rage128_gpu_2d_qskip(dev, 8, 8);
                return 0;
            }
        }
        if (zero_pos ? e >= 0 : e > 0) {
            if (ymajor)
                x += xstep;
            else
                y += ystep;
            e += dec;
        }
        e += inc;
        if (ymajor)
            y += ystep;
        else
            x += xstep;
    }
    if (!rage128_2d_lrq_flush(&q)) {
        rage128_gpu_2d_qskip(dev, 8, 8);
        return 0;
    }
    return rage128_2d_lrq_queue(dev, &q, rop, bpp);
}

/* Line segment from two endpoints (Bresenham), with the brush as the
   ROP3 pattern operand, clipped by the scissor. The last pixel follows
   DP_CNTL.DST_LAST_PEL (RRG: DP_CNTL, p. 3-165 / PDF 183). At its
   default of 0 the last pixel is not drawn, so the joint between two
   polyline segments is drawn once (which matters under XOR), and the
   endpoint Windows GDI treats as excluded is never drawn. At 1 the last
   pixel is drawn and uses a pattern bit like any other pixel.
   Brush types 6 and 7 are a 32x1 line pattern: each bit, MSB first,
   picks foreground or background (6) or foreground or leave alone (7).
   The pattern position advances per pixel and carries on into the next
   segment (POLY_LINE "implies BRUSH tiling", RRG: DP_CNTL, p. 3-166 /
   PDF 184). pat_en 0 = solid foreground. */
static void
rage128_2d_line(rage128_t *dev, int x0, int y0, int x1, int y1,
                uint32_t pat32, int pat_en, int pat_la, int *phase)
{
    int              bpp    = rage128_2d_bpp(rage128_dp_dst_dt(dev));
    uint8_t          rop    = rage128_dp_rop3(dev);
    uint32_t         wmask  = dev->dp_write_mask;
    int              aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    struct r128_ccmp cc;
    int              cca = rage128_2d_ccmp_setup(dev, 0, bpp, bpp, &cc);
    r128_surf_t      sd;
    int              cx0, cy0, cx1, cy1;
    int              dx  = x1 > x0 ? x1 - x0 : x0 - x1;
    int              dy  = y1 > y0 ? y1 - y0 : y0 - y1;
    int              sx  = x0 < x1 ? 1 : -1;
    int              sy  = y0 < y1 ? 1 : -1;
    int              err = dx - dy;
    int              bx1, by0, by1;

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    /* The window is the segment's bounding box cut by the scissor. If
       they do not overlap, the empty window is mapped and the walk still
       runs, so the pattern position advances over a segment that is
       clipped away completely. */
    by0 = (y0 < y1 ? y0 : y1);
    if (by0 < cy0)
        by0 = cy0;
    by1 = (y0 > y1 ? y0 : y1);
    if (by1 > cy1)
        by1 = cy1;
    bx1 = (x0 > x1 ? x0 : x1);
    if (bx1 > cx1)
        bx1 = cx1;

    /* GPU queue path. A ROP that does not use the destination, with the
       mask fully open, at 32 bpp, is queued as merged 1-row fills.
       Anything else (another ROP3, a partial mask, another depth) is
       queued as foreground and background runs through the
       read-modify-write dispatch. The queue walk advances the pattern
       position, so it is put back before any CPU re-walk. */
    if (!cca && dev->gpu && !dev->synctel) {
        int dind = rage128_2d_rop_dindep(rop) && wmask == 0xffffffffu
            && bpp == 4;
        int ph  = pat_en ? *phase : 0;
        int bx0 = (x0 < x1 ? x0 : x1);
        int vq, ok;

        if (bx0 < cx0)
            bx0 = cx0;
        vq = (by1 >= by0 && bx1 >= bx0)
            ? rage128_2d_vq_begin(dev, dind ? 6 : 8,
                                  dev->dst_offset
                                      + ((uint32_t) by0 * rage128_2d_dst_stride(dev, bpp)
                                         + (uint32_t) bx0 * (uint32_t) bpp),
                                  rage128_2d_dst_stride(dev, bpp),
                                  (uint32_t) (bx1 - bx0 + 1) * (uint32_t) bpp,
                                  (uint32_t) (by1 - by0 + 1))
            : 0;
        ok = dind
            ? rage128_2d_queue_line(dev, x0, y0, x1, y1, pat32, pat_en,
                                    pat_la, phase, rop)
            : rage128_2d_queue_line_rmw(dev, x0, y0, x1, y1, pat32,
                                        pat_en, pat_la, phase, rop, bpp);
        if (ok) {
            if (dind)
                rage128_gpu_2d_qdone(dev, 6);
            if (!vq)
                return;
            rage128_2d_vq_after(dev, 1);
        } else if (vq)
            rage128_2d_vq_after(dev, 0);
        if (pat_en)
            *phase = ph; /* the re-walk advances it again */
    }

    if (!rage128_2d_map_span(dev, &sd, &dev->s2d_dst, dev->dst_offset,
                             dev->dst_pitch, bpp,
                             (x0 < x1 ? x0 : x1) < cx0 ? cx0 : (x0 < x1 ? x0 : x1),
                             by0, by1, bx1)) {
        rage128_2d_vq_abort(dev);
        return;
    }

    for (;;) {
        int set    = 1;
        int at_end = (x0 == x1 && y0 == y1);

        if (at_end && !(dev->dp_cntl & RAGE128_DP_CNTL_DST_LAST_PEL))
            break; /* DST_LAST_PEL 0: last pixel not drawn */
        if (pat_en) {
            set = rage128_2d_pat32_bit(dev, pat32, *phase);
            (*phase)++;
        }
        if ((set || !pat_la)
            && x0 >= cx0 && x0 <= cx1 && y0 >= cy0 && y0 <= cy1
            && (!aux_on || rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x0, y0))) {
            uint32_t a   = dev->dst_offset + ((uint32_t) y0 * rage128_2d_dst_stride(dev, bpp) + x0 * bpp);
            uint32_t pat = set ? dev->dp_brush_frgd_clr : dev->dp_brush_bkgd_clr;
            int      cok = 1;

            if (cca) {
                uint32_t dpx = 0;

                for (int b = 0; b < bpp; b++)
                    dpx |= (uint32_t) *r128_surf_at(&sd, a + (uint32_t) b)
                        << (b * 8);
                cok = rage128_2d_ccmp_px(&cc, 0, dpx);
            }
            for (int b = 0; cok && b < bpp; b++) {
                uint8_t *dp = r128_surf_at(&sd, a + (uint32_t) b);
                uint8_t  pb = (pat >> ((b & 3) * 8)) & 0xff;

                rage128_2d_store(dp, rage128_rop3(rop, pb, 0, *dp),
                                 (uint8_t) (wmask >> ((b & 3) * 8)));
            }
            rage128_2d_mark_dirty(dev, &sd, a, bpp);
        }
        if (at_end)
            break;
        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
    r128_surf_commit(dev, &sd);
    rage128_2d_vq_end(dev);
}

/* Bresenham line started by register writes, as the X server's 2D
   driver draws lines (xf86-video-r128 r128_accel.c, XAA version,
   R128SubsequentSolidBresenhamLine and
   R128SubsequentDashedBresenhamLine). It walks the latched setup
   (DST_BRES_ERR, DST_BRES_INC, DST_BRES_DEC, DST_BRES_LNTH; RRG:
   DST_BRES_ERR, pp. 3-142-3-143 / PDF 160-161): the major axis steps
   every pixel; when the error term is positive (or zero, see the
   BRES_SIGN note below) the minor axis steps too and DST_BRES_DEC is
   added; DST_BRES_INC is added every pixel. The
   brush goes through the ROP3, scissor and write mask as in
   rage128_2d_line: solid foreground, or for brush types 6 and 7 the
   32x1 line pattern in BRUSH_DATA0 (foreground and background, or
   foreground and leave alone), starting at the position in BRUSH_Y_X
   [20:16]. Under POLY_LINE the line leaves that position advanced;
   otherwise it is left alone (the X driver's dashed line writes it
   before every segment). */
/* DP_CNTL_XDIR_YDIR_YMAJOR holds the DP_CNTL [2:0] direction bits at
   other positions: DST_X_DIR [31], DST_Y_DIR [15], DST_Y_MAJOR [2]
   (RRG: DP_CNTL_XDIR_YDIR_YMAJOR, p. 3-170 / PDF 188; RRG: DP_CNTL,
   p. 3-165 / PDF 183). */
static uint32_t
rage128_2d_line_dir(const rage128_t *dev)
{
    return ((dev->dp_cntl & RAGE128_DP_CNTL_DST_X_DIR) ? RAGE128_DP_LINE_X_DIR : 0)
        | ((dev->dp_cntl & RAGE128_DP_CNTL_DST_Y_DIR) ? RAGE128_DP_LINE_Y_DIR : 0)
        | ((dev->dp_cntl & RAGE128_DP_CNTL_DST_Y_MAJOR) ? RAGE128_DP_LINE_Y_MAJOR : 0);
}

/* End of a line: with POLY_LINE on, BRUSH_Y_X [20:16] "is reloaded from
   the current Brush pointer at the end of the line"; with it off it
   keeps its start value (SDK: Table F-1, p. F-16 / PDF 306). The walk's
   pattern position is stored whether the segment was walked, queued or
   clipped away completely: a clipped segment still uses up its pattern
   bits, so the next dash starts where the driver expects. */
static void
rage128_2d_line_phase_done(rage128_t *dev, int pat_en, int phase)
{
    if (pat_en && (dev->dp_cntl & RAGE128_DP_CNTL_POLY_LINE))
        dev->brush_yx = (dev->brush_yx & ~0x001f0000u)
            | (((uint32_t) phase & 0x1fu) << 16);
}

static void
rage128_2d_bres_line(rage128_t *dev, uint32_t dir)
{
    int      bpp    = rage128_2d_bpp(rage128_dp_dst_dt(dev));
    uint8_t  rop    = rage128_dp_rop3(dev);
    uint32_t wmask  = dev->dp_write_mask;
    int      aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    int      bt     = rage128_dp_brush_dt(dev);
    int      pat_en = (bt == 6 || bt == 7);
    int      pat_la = (bt == 7);
    uint32_t pat32  = dev->brush_data[0];
    int      phase  = (int) ((dev->brush_yx >> 16) & 0x1f);
    int      ymajor = (dir & RAGE128_DP_LINE_Y_MAJOR) != 0;
    int      xstep  = (dir & RAGE128_DP_LINE_X_DIR) ? 1 : -1;
    int      ystep  = (dir & RAGE128_DP_LINE_Y_DIR) ? 1 : -1;
    /* BRES_SIGN (RRG: DP_CNTL, p. 3-166 / PDF 184) lists two cases in
       which a zero error term counts as positive: an X-major line with
       DST_Y_DIR 0 and a Y-major line with DST_X_DIR 0. The code applies
       that rule from the direction bits; in every other case a zero
       error term does not step the minor axis. This makes a line and
       its reverse plot the same pixels, so closed outlines meet. */
    int zero_pos = (!ymajor && ystep < 0) || (ymajor && xstep < 0);
    int n        = (int) (dev->bres_lnth & 0x3fff);
    /* The error, increment and decrement fields are [19:0] (RRG:
       DST_BRES_ERR, pp. 3-142-3-143 / PDF 160-161); << 12 then >> 12
       keeps the field and sign-extends it. */
    int32_t          inc = (int32_t) (dev->bres_inc << 12) >> 12;
    int32_t          dec = (int32_t) (dev->bres_dec << 12) >> 12;
    int32_t          e   = (int32_t) (dev->bres_err << 12) >> 12;
    int              x   = dev->gui_dst_x;
    int              y   = dev->gui_dst_y;
    struct r128_ccmp cc;
    int              cca = rage128_2d_ccmp_setup(dev, 0, bpp, bpp, &cc);
    r128_surf_t      sd;
    int              cx0, cy0, cx1, cy1;
    int              xa, ya, xb, yb;

    if (n <= 0)
        return;
    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);

    /* Pass 1: the bounding box of the walk, so a destination that may
       live in AGP memory is mapped once for the whole line. */
    {
        int     px = x, py = y;
        int32_t pe = e;

        xa = xb = px;
        ya = yb = py;
        for (int i = 0; i < n; i++) {
            if (px < xa)
                xa = px;
            if (px > xb)
                xb = px;
            if (py < ya)
                ya = py;
            if (py > yb)
                yb = py;
            if (zero_pos ? pe >= 0 : pe > 0) {
                if (ymajor)
                    px += xstep;
                else
                    py += ystep;
                pe += dec;
            }
            pe += inc;
            if (ymajor)
                py += ystep;
            else
                px += xstep;
        }
    }
    if (ya < cy0)
        ya = cy0;
    if (yb > cy1)
        yb = cy1;
    if (xb > cx1)
        xb = cx1;
    if (ya > yb) { /* entirely outside the scissor in Y */
        rage128_2d_line_phase_done(dev, pat_en, phase + n);
        return;
    }

    /* GPU queue path: a ROP that does not use the destination, mask
       fully open, 32 bpp, gives merged 1-row fills; anything else gives
       foreground and background runs through the read-modify-write
       dispatch. The walk state is passed by value, so if the queue
       refuses, the CPU plot pass below runs from the same state, and a
       refused read-modify-write op has recorded nothing. */
    if (!cca && dev->gpu && !dev->synctel) {
        int dind = rage128_2d_rop_dindep(rop) && wmask == 0xffffffffu
            && bpp == 4;
        int bx0 = xa;
        int ph  = phase;
        int vq, ok;

        /* The self-check window starts at the signed left edge, as for
           the two-point line. With a negative scissor the walk plots at
           x < 0; a window cut at column 0 would leave those bytes out of
           the restore before the CPU re-walk (a read-modify-write ROP
           would be applied twice) and out of the compare. */
        if (bx0 < cx0)
            bx0 = cx0;
        vq = (xb >= bx0)
            ? rage128_2d_vq_begin(dev, dind ? 6 : 8,
                                  dev->dst_offset
                                      + ((uint32_t) ya * rage128_2d_dst_stride(dev, bpp)
                                         + (uint32_t) bx0 * (uint32_t) bpp),
                                  rage128_2d_dst_stride(dev, bpp),
                                  (uint32_t) (xb - bx0 + 1) * (uint32_t) bpp,
                                  (uint32_t) (yb - ya + 1))
            : 0;
        ok = dind
            ? rage128_2d_queue_bres(dev, x, y, n, inc, dec, e, ymajor,
                                    xstep, ystep, zero_pos, pat32, pat_en,
                                    pat_la, &phase, rop)
            : rage128_2d_queue_bres_rmw(dev, x, y, n, inc, dec, e, ymajor,
                                        xstep, ystep, zero_pos, pat32,
                                        pat_en, pat_la, &phase, rop, bpp);
        if (ok) {
            if (dind)
                rage128_gpu_2d_qdone(dev, 6);
            if (!vq) {
                rage128_2d_line_phase_done(dev, pat_en, phase);
                return;
            }
            rage128_2d_vq_after(dev, 1);
        } else if (vq)
            rage128_2d_vq_after(dev, 0);
        phase = ph; /* the re-walk advances it again */
    }

    if (!rage128_2d_map_span(dev, &sd, &dev->s2d_dst, dev->dst_offset,
                             dev->dst_pitch, bpp, xa < cx0 ? cx0 : xa, ya, yb, xb)) {
        rage128_2d_vq_abort(dev);
        rage128_2d_line_phase_done(dev, pat_en, phase + n);
        return;
    }

    /* Pass 2: plot DST_BRES_LNTH pixels. */
    for (int i = 0; i < n; i++) {
        int set = 1;

        if (pat_en) {
            set = rage128_2d_pat32_bit(dev, pat32, phase);
            phase++;
        }
        if ((set || !pat_la)
            && x >= cx0 && x <= cx1 && y >= cy0 && y <= cy1
            && (!aux_on || rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x, y))) {
            uint32_t a   = dev->dst_offset + ((uint32_t) y * rage128_2d_dst_stride(dev, bpp) + x * bpp);
            uint32_t pat = set ? dev->dp_brush_frgd_clr : dev->dp_brush_bkgd_clr;
            int      cok = 1;

            if (cca) {
                uint32_t dpx = 0;

                for (int b = 0; b < bpp; b++)
                    dpx |= (uint32_t) *r128_surf_at(&sd, a + (uint32_t) b)
                        << (b * 8);
                cok = rage128_2d_ccmp_px(&cc, 0, dpx);
            }
            for (int b = 0; cok && b < bpp; b++) {
                uint8_t *dp = r128_surf_at(&sd, a + (uint32_t) b);
                uint8_t  pb = (pat >> ((b & 3) * 8)) & 0xff;

                rage128_2d_store(dp, rage128_rop3(rop, pb, 0, *dp),
                                 (uint8_t) (wmask >> ((b & 3) * 8)));
            }
            rage128_2d_mark_dirty(dev, &sd, a, bpp);
        }
        if (zero_pos ? e >= 0 : e > 0) {
            if (ymajor)
                x += xstep;
            else
                y += ystep;
            e += dec;
        }
        e += inc;
        if (ymajor)
            y += ystep;
        else
            x += xstep;
    }
    r128_surf_commit(dev, &sd);
    rage128_2d_vq_end(dev);
    rage128_2d_line_phase_done(dev, pat_en, phase);
}

/* Queue a screen-to-screen blit under any ROP3 and write mask as one
   read-modify-write dispatch. The source rect is copied at queue time.
   The CPU walk picks top-down or bottom-up order so that every source
   row is read before the op writes over it, so the copy matches the
   walk even when the rects overlap, and on both paths the destination
   is each pixel's value before the op. A solid brush, a pattern brush
   and the write mask apply by byte position within the pixel, exactly
   as the walk stores them; a clear bit of a leave-alone mono pattern
   becomes the A/B pair that keeps the destination. The caller has
   checked the aux test and non-negative coordinates and has clipped
   the rect. 0 = not queued, nothing recorded. */
static int
rage128_2d_queue_blit_rmw(rage128_t *dev, int sx, int sy, int dx, int dy,
                          int w, int h, uint8_t rop, int bpp,
                          const uint8_t *pat8x8, const uint8_t *col8x8,
                          int mono_la)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    if (pat8x8 && rage128_2d_brush_32x32(dev))
        return 0;
    uint32_t dpb   = rage128_2d_dst_stride(dev, bpp);
    uint32_t daddr = dev->dst_offset
        + ((uint32_t) dy * rage128_2d_dst_stride(dev, bpp) + (uint32_t) dx * (uint32_t) bpp);
    uint32_t    len  = (uint32_t) w * (uint32_t) bpp;
    uint32_t    rows = (uint32_t) h;
    uint32_t    soff;
    uint8_t    *st;
    r128_surf_t ssrc;

    int aux = rage128_2d_aux_seg(dev, dx, dy, dx + w - 1, dy + h - 1);

    st = (uint8_t *) rage128_gpu_2d_stage(dev, rows * len * 2u, &soff);
    if (!st) {
        rage128_gpu_2d_qskip(dev, 8, 64);
        return 0;
    }
    if (!rage128_2d_map_span(dev, &ssrc, &dev->s2d_src, dev->src_offset,
                             dev->src_pitch, bpp, 0, sy < 0 ? 0 : sy,
                             sy + h - 1, sx + w - 1)) {
        rage128_gpu_2d_qskip(dev, 8, 32);
        return 0;
    }
    for (uint32_t r = 0; r < rows; r++) {
        uint32_t saddr = dev->src_offset
            + ((uint32_t) (sy + (int) r) * rage128_2d_src_stride(dev, bpp)
               + (uint32_t) sx * (uint32_t) bpp);

        for (uint32_t rb = 0; rb < len; rb++) {
            int     px = (int) (rb / (uint32_t) bpp);
            int     pb = (int) (rb % (uint32_t) bpp);
            uint8_t pv;

            if (aux && !rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, dx + px, dy + (int) r)) {
                st[r * len + rb]              = 0xff;
                st[rows * len + r * len + rb] = 0;
                continue;
            }
            if (pat8x8) {
                int pset = rage128_2d_pat8_bit(dev,
                                               pat8x8[rage128_2d_pat_row(dev, dy + (int) r)],
                                               dx + px);

                if (!pset && mono_la) {
                    st[r * len + rb]              = 0xff;
                    st[rows * len + r * len + rb] = 0;
                    continue;
                }
                pv = (uint8_t) ((pset ? dev->dp_brush_frgd_clr
                                      : dev->dp_brush_bkgd_clr)
                                >> ((pb & 3) * 8));
            } else if (col8x8)
                pv = col8x8[((rage128_2d_pat_row(dev, dy + (int) r) * 8
                              + rage128_2d_pat_col(dev, dx + px))
                             * bpp)
                            + pb];
            else
                pv = (uint8_t) (dev->dp_brush_frgd_clr >> ((pb & 3) * 8));
            rage128_2d_rmw_ab(rop, pv,
                              *r128_surf_at(&ssrc, saddr + rb),
                              (uint8_t) (dev->dp_write_mask >> ((pb & 3) * 8)),
                              &st[r * len + rb], &st[rows * len + r * len + rb]);
        }
    }
    r128_surf_release(&ssrc);
    if (!rage128_gpu_2d_rmw(dev, daddr, dpb, len, rows, soff,
                            soff + rows * len, 0xffffffffu)) {
        rage128_gpu_2d_qskip(dev, 8, 32);
        return 0;
    }
    rage128_2d_queue_mark(dev, daddr, dpb, len, rows);
    rage128_gpu_2d_qdone(dev, 8);
    if (aux)
        rage128_gpu_2d_qaux(dev, 3);
    return 1;
}

/* Screen-to-screen blit through the ROP3, with the source read from
   card memory. Each source row is copied to a buffer before it is
   written, and rows run bottom-up when the destination is below the
   source, so overlapping copies come out right. A ROP that uses the
   pattern takes it from the brush: pat8x8 or col8x8 gives the pattern
   value per destination pixel, lined up with the screen as in the
   paint executors (both NULL = solid foreground). */
static void
rage128_2d_blit_rect_pat(rage128_t *dev, int sx, int sy, int dx, int dy,
                         int w, int h, const uint8_t *pat8x8,
                         const uint8_t *col8x8, int mono_la)
{
    int              bpp    = rage128_2d_bpp(rage128_dp_dst_dt(dev));
    uint8_t          rop    = rage128_dp_rop3(dev);
    uint32_t         pat    = dev->dp_brush_frgd_clr;
    uint32_t         wmask  = dev->dp_write_mask;
    int              aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    struct r128_ccmp cc;
    int              cca;
    /* Large enough for the widest row: DST_WIDTH is [13:0] (RRG:
       DST_WIDTH, p. 3-139 / PDF 157), so 16383 pixels at 4 bytes. The
       GPU queue paths get the full width, so a smaller buffer would cut
       only this walk short. */
    uint8_t     rowbuf[16384 * 4];
    r128_surf_t sdst, ssrc;
    int         cx0, cy0, cx1, cy1;

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    if (dx < cx0) {
        w -= cx0 - dx;
        sx += cx0 - dx;
        dx = cx0;
    }
    if (dy < cy0) {
        h -= cy0 - dy;
        sy += cy0 - dy;
        dy = cy0;
    }
    if (dx + w - 1 > cx1)
        w = cx1 - dx + 1;
    if (dy + h - 1 > cy1)
        h = cy1 - dy + 1;
    if (w <= 0 || h <= 0)
        return;
    cca = rage128_2d_ccmp_setup(dev, 1, bpp, bpp, &cc);

    /* GPU queue path. A SRCCOPY with the write mask fully open and no
       per-pixel clipping is a plain byte copy of the rect, so it is
       queued (overlapping rects go through the staging ring) rather
       than waiting on the GPU barriers at both surface maps below.
       With synctel on, the CPU walk is kept so the access record stays
       complete. Tiled surfaces are refused here, as the queue_* helpers
       refuse them internally: this block calls the backend blit
       directly with linear row math, so the test has to be made
       here. */
    if (dev->gpu && !dev->synctel && !rage128_2d_tiled_any(dev)) {
        int why = 0;
        int aux = rage128_2d_aux_seg(dev, dx, dy, dx + w - 1, dy + h - 1);

        if (sx < 0 || sy < 0 || dx < 0 || dy < 0)
            why |= 32; /* bad coordinates wrap in the CPU walk's window */
        if (cca)
            why |= 16; /* per-pixel color compare: CPU walk only */
        if (!why && !aux && rop == 0xccu && wmask == 0xffffffffu
            && !(pat8x8 && mono_la)) {
            uint32_t dpb   = rage128_2d_dst_stride(dev, bpp);
            uint32_t spb   = rage128_2d_src_stride(dev, bpp);
            uint32_t daddr = dev->dst_offset
                + ((uint32_t) dy * rage128_2d_dst_stride(dev, bpp) + (uint32_t) dx * (uint32_t) bpp);
            uint32_t saddr = dev->src_offset
                + ((uint32_t) sy * rage128_2d_src_stride(dev, bpp) + (uint32_t) sx * (uint32_t) bpp);

            if (rage128_gpu_2d_blit(dev, daddr, dpb, saddr, spb,
                                    (uint32_t) w * (uint32_t) bpp,
                                    (uint32_t) h)) {
                rage128_2d_queue_mark(dev, daddr, dpb,
                                      (uint32_t) w * (uint32_t) bpp,
                                      (uint32_t) h);
                return;
            }
        } else if (!why) {
            /* ROP that uses the destination, partial mask or per-pixel
               aux test: one read-modify-write dispatch (pixels the aux
               test rejects keep the destination) */
            int vq = rage128_2d_vq_begin(dev, 8,
                                         dev->dst_offset
                                             + ((uint32_t) dy * rage128_2d_dst_stride(dev, bpp) + (uint32_t) dx * (uint32_t) bpp),
                                         rage128_2d_dst_stride(dev, bpp),
                                         (uint32_t) w * (uint32_t) bpp, (uint32_t) h);

            if (rage128_2d_queue_blit_rmw(dev, sx, sy, dx, dy, w, h, rop,
                                          bpp, pat8x8, col8x8, mono_la)) {
                if (!vq)
                    return;
                rage128_2d_vq_after(dev, 1);
            } else if (vq)
                rage128_2d_vq_after(dev, 0);
        } else
            rage128_gpu_2d_qskip(dev, 3, why);
    }

    if ((size_t) (uint32_t) w * (uint32_t) bpp > sizeof(rowbuf))
        w = sizeof(rowbuf) / bpp;
    if (!rage128_2d_map_span(dev, &sdst, &dev->s2d_dst, dev->dst_offset,
                             dev->dst_pitch, bpp, dx, dy, dy + h - 1, dx + w - 1)) {
        rage128_2d_vq_abort(dev);
        return;
    }
    /* The source rect is not scissored. Negative coordinates (from a bad
       packet) wrap inside the mapped window, so only the requested
       extent is clamped. */
    if (!rage128_2d_map_span(dev, &ssrc, &dev->s2d_src, dev->src_offset,
                             dev->src_pitch, bpp, 0, sy < 0 ? 0 : sy,
                             sy + h - 1 < 0 ? 0 : sy + h - 1,
                             sx + w - 1 < 0 ? 0 : sx + w - 1)) {
        r128_surf_release(&sdst); /* nothing written yet */
        rage128_2d_vq_abort(dev);
        return;
    }

    int top_down = !(dy > sy);

    /* Fast path: a SRCCOPY (ROP 0xCC, result = source) with aux off and
       the write mask fully open copies the buffered source row straight
       to the destination. That stores the same bytes as the general
       walk (rop3(0xCC, pb, sb, d) = sb, mask byte 0xff), and the row
       buffer still keeps overlapping copies right. */
    int fast = !aux_on && wmask == 0xffffffffu && rop == 0xccu && !cca
        && !(pat8x8 && mono_la);

    for (int i = 0; i < h; i++) {
        int      row   = top_down ? i : (h - 1 - i);
        uint32_t saddr = dev->src_offset + ((uint32_t) (sy + row) * rage128_2d_src_stride(dev, bpp) + sx * bpp);
        uint32_t daddr = dev->dst_offset + ((uint32_t) (dy + row) * rage128_2d_dst_stride(dev, bpp) + dx * bpp);

        for (int b = 0; b < w * bpp; b++)
            rowbuf[b] = *r128_surf_at(&ssrc, saddr + (uint32_t) b);
        if (fast) {
            uint8_t *run = r128_surf_run(&sdst, daddr, (uint32_t) w * bpp);

            if (run) {
                memcpy(run, rowbuf, (size_t) w * bpp);
                rage128_2d_mark_dirty(dev, &sdst, daddr, (uint32_t) w * bpp);
                continue;
            }
        }
        for (int col = 0; col < w; col++) {
            uint32_t       pcol  = 0;
            const uint8_t *patpx = NULL;

            if (aux_on && !rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, dx + col, dy + row))
                continue;
            if (pat8x8) {
                int pset = rage128_2d_mono_bit(dev, pat8x8, dx + col, dy + row);

                if (!pset && mono_la)
                    continue; /* brush type 1: leave alone */
                pcol = pset ? dev->dp_brush_frgd_clr : dev->dp_brush_bkgd_clr;
            } else if (col8x8)
                patpx = &col8x8[(rage128_2d_pat_row(dev, dy + row) * 8
                                 + rage128_2d_pat_col(dev, dx + col))
                                * bpp];
            if (cca) {
                uint32_t rb0 = (uint32_t) col * bpp;
                uint32_t spx = 0, dpx = 0;
                int      cr;

                for (int b = 0; b < bpp; b++) {
                    spx |= (uint32_t) rowbuf[rb0 + (uint32_t) b] << (b * 8);
                    dpx |= (uint32_t) *r128_surf_at(&sdst,
                                                    daddr + rb0 + (uint32_t) b)
                        << (b * 8);
                }
                cr = rage128_2d_ccmp_px(&cc, spx, dpx);
                if (!cr)
                    continue;
                if (cr == 2) {
                    for (int b = 0; b < bpp; b++)
                        rage128_2d_store(r128_surf_at(&sdst,
                                                      daddr + rb0 + (uint32_t) b),
                                         (uint8_t) ((spx ^ cc.flip) >> (b * 8)),
                                         (uint8_t) (wmask >> ((b & 3) * 8)));
                    continue;
                }
            }
            for (int b = 0; b < bpp; b++) {
                /* Solid and pattern brushes both apply by byte position
                   within the pixel, as in the paint executors and
                   rage128_2d_blit_rect_key. The write mask is per pixel
                   (byte b & 3). */
                uint32_t rb = (uint32_t) col * bpp + (uint32_t) b;
                uint8_t *dp = r128_surf_at(&sdst, daddr + rb);
                uint8_t  pb = patpx ? patpx[b]
                     : pat8x8       ? (uint8_t) ((pcol >> ((b & 3) * 8)) & 0xff)
                                    : (uint8_t) ((pat >> ((b & 3) * 8)) & 0xff);

                rage128_2d_store(dp, rage128_rop3(rop, pb, rowbuf[rb], *dp),
                                 (uint8_t) (wmask >> ((b & 3) * 8)));
            }
        }
        rage128_2d_mark_dirty(dev, &sdst, daddr, (uint32_t) w * bpp);
    }
    r128_surf_release(&ssrc);
    r128_surf_commit(dev, &sdst);
    rage128_2d_vq_end(dev);
}

static void
rage128_2d_blit_rect(rage128_t *dev, int sx, int sy, int dx, int dy, int w, int h)
{
    rage128_2d_blit_rect_pat(dev, sx, sy, dx, dy, w, h, NULL, NULL, 0);
}

/* Transparent (color-keyed) screen-to-screen blit, packet 0x9c
   (R128_CCE_PACKET3_CNTL_TRANS_BITBLT in xf86-video-r128).
   Same datapath as rage128_2d_blit_rect, plus a per-pixel key test
   from the color compare values the packet carries: a source pixel
   that equals the key (under the mask) is skipped (key_eq_skip = 1, as
   CLR_CMP_FN_SRC 5, draw when not equal) or is the only kind drawn
   (key_eq_skip = 0, as CLR_CMP_FN_SRC 4) (RRG: CLR_CMP_CNTL,
   pp. 3-178-3-179 / PDF 196-197). There is no row buffer for
   overlapping rects: in captured Windows 98 traffic (Age of Empires II)
   the packet copies an on-screen area to a separate offscreen buffer.
   The packet is built by the Windows 98 Direct3D/DirectDraw driver
   with a fixed 12-dword payload (RE: ati3draa.dll @b00ba268). */
/* The store decision for one keyed-blit pixel: it passes the aux
   scissor and the key test on the raw source pixel. Same test as the
   CPU walk. */
static int
rage128_2d_key_pass(rage128_t *dev, const r128_surf_t *ssrc, uint32_t sa,
                    uint32_t key, uint32_t kmask, int key_eq_skip, int bpp,
                    int aux_on, int x, int y)
{
    uint32_t s = 0;

    if (aux_on && !rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x, y))
        return 0;
    for (int b = 0; b < bpp; b++)
        s |= (uint32_t) *r128_surf_at((r128_surf_t *) ssrc,
                                      sa + (uint32_t) b)
            << (b * 8);
    return ((s & kmask) == key) != key_eq_skip;
}

/* Queue a color-keyed blit. With a ROP that does not use the
   destination, every stored byte depends only on the brush and the
   source. So the source is read here (its GPU barrier is paid at the
   span map), the runs that pass the key test go through the ROP into
   the staging ring, and one copy per run is queued. The destination is
   never mapped, so it needs no barrier; the queue keeps the order. Any
   depth works, since the copies are byte-wise. The keyed CPU walk has
   no row buffer (later rows read earlier stores), so overlapping
   source and destination bytes are left to the walk, and so are
   negative coordinates, whose wrap inside the window the linear queue
   addresses cannot express. 0 = not queued; a partial queue is
   harmless (the re-walk stores the same bytes). */
static int
rage128_2d_queue_blit_key(rage128_t *dev, int sx, int sy, int dx, int dy,
                          int w, int h, uint32_t key, uint32_t kmask,
                          int key_eq_skip, int bpp, uint8_t rop)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    uint32_t pat    = dev->dp_brush_frgd_clr;
    int      aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    uint32_t dpb    = rage128_2d_dst_stride(dev, bpp);
    uint32_t spb    = rage128_2d_src_stride(dev, bpp);
    uint32_t rowlen = (uint32_t) w * (uint32_t) bpp;
    uint32_t daddr0 = dev->dst_offset
        + ((uint32_t) dy * rage128_2d_dst_stride(dev, bpp) + (uint32_t) dx * (uint32_t) bpp);
    uint32_t saddr0 = dev->src_offset
        + ((uint32_t) sy * rage128_2d_src_stride(dev, bpp) + (uint32_t) sx * (uint32_t) bpp);
    uint64_t    dend = (uint64_t) daddr0 + (uint64_t) (h - 1) * dpb + rowlen;
    uint64_t    send = (uint64_t) saddr0 + (uint64_t) (h - 1) * spb + rowlen;
    r128_surf_t ssrc;
    int         vq, nruns = 0;

    if (sx < 0 || sy < 0 || dx < 0 || dy < 0 || r128_card_is_agp(daddr0)
        || dend > (uint64_t) dev->vram_size || (h > 1 && dpb < rowlen)
        || ((uint64_t) daddr0 < send && dend > (uint64_t) saddr0)) {
        rage128_gpu_2d_qskip(dev, 4, 32);
        return 0;
    }
    if (!rage128_2d_map_span(dev, &ssrc, &dev->s2d_src, dev->src_offset,
                             dev->src_pitch, bpp, 0, sy < 0 ? 0 : sy,
                             sy + h - 1, sx + w - 1)) {
        rage128_gpu_2d_qskip(dev, 4, 32);
        return 0;
    }
    vq = rage128_2d_vq_begin(dev, 4, daddr0, dpb, rowlen, (uint32_t) h);

    for (int row = 0; row < h; row++) {
        uint32_t saddr = dev->src_offset
            + ((uint32_t) (sy + row) * rage128_2d_src_stride(dev, bpp) + sx * (uint32_t) bpp);
        uint32_t daddr = daddr0 + (uint32_t) row * dpb;
        int      col   = 0;

#define R128_KEYP(c) rage128_2d_key_pass(dev, &ssrc,                                          \
                                         saddr + (uint32_t) (c) * (uint32_t) bpp, key, kmask, \
                                         key_eq_skip, bpp, aux_on, dx + (c), dy + row)

        while (col < w) {
            int s0;

            while (col < w && !R128_KEYP(col))
                col++;
            s0 = col;
            while (col < w && R128_KEYP(col))
                col++;
            if (col > s0) {
                uint32_t rl = (uint32_t) (col - s0) * (uint32_t) bpp;
                uint32_t so;
                uint8_t *st;

                if (++nruns > 256) {
                    rage128_gpu_2d_qskip(dev, 4, 8);
                    goto refuse;
                }
                st = (uint8_t *) rage128_gpu_2d_stage(dev, rl, &so);
                if (!st) {
                    rage128_gpu_2d_qskip(dev, 4, 64);
                    goto refuse;
                }
                for (int c = s0; c < col; c++) {
                    uint32_t sa = saddr + (uint32_t) c * (uint32_t) bpp;

                    for (int b = 0; b < bpp; b++) {
                        uint8_t sb = *r128_surf_at(&ssrc, sa + (uint32_t) b);
                        uint8_t pb = (pat >> ((b & 3) * 8)) & 0xff;

                        *st++ = (uint8_t) rage128_rop3(rop, pb, sb, 0);
                    }
                }
                if (!rage128_gpu_2d_copy(dev, daddr + (uint32_t) s0 * bpp,
                                         rl, rl, 1, so, rl, 3))
                    goto refuse;
                rage128_2d_queue_mark(dev, daddr + (uint32_t) s0 * bpp,
                                      rl, rl, 1);
            }
        }
#undef R128_KEYP
    }
    r128_surf_release(&ssrc);
    rage128_gpu_2d_qdone(dev, 4);
    if (vq) {
        rage128_2d_vq_after(dev, 1);
        return 0; /* self-check: the CPU walk runs, the end compares */
    }
    return 1;

refuse:
    r128_surf_release(&ssrc);
    if (vq)
        rage128_2d_vq_after(dev, 0);
    return 0;
}

static void
rage128_2d_blit_rect_key(rage128_t *dev, int sx, int sy, int dx, int dy, int w, int h,
                         uint32_t key, uint32_t kmask, int key_eq_skip)
{
    int         bpp    = rage128_2d_bpp(rage128_dp_dst_dt(dev));
    uint8_t     rop    = rage128_dp_rop3(dev);
    uint32_t    pat    = dev->dp_brush_frgd_clr;
    uint32_t    wmask  = dev->dp_write_mask;
    int         aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    uint32_t    pmask  = (bpp >= 4) ? 0xffffffffu : ((1u << (bpp * 8)) - 1);
    r128_surf_t sdst, ssrc;
    int         cx0, cy0, cx1, cy1;

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    if (dx < cx0) {
        w -= cx0 - dx;
        sx += cx0 - dx;
        dx = cx0;
    }
    if (dy < cy0) {
        h -= cy0 - dy;
        sy += cy0 - dy;
        dy = cy0;
    }
    if (dx + w - 1 > cx1)
        w = cx1 - dx + 1;
    if (dy + h - 1 > cy1)
        h = cy1 - dy + 1;
    if (w <= 0 || h <= 0)
        return;
    kmask &= pmask;
    key &= kmask;

    /* GPU queue path: with a ROP that does not use the destination and
       the write mask fully open, every stored byte depends only on the
       brush and the source, so it is worked out now and queued as copies
       per run (rage128_2d_queue_blit_key). With synctel on, the CPU walk
       is kept so the access record stays complete. */
    if (dev->gpu && !dev->synctel) {
        int why = 0;

        if (!rage128_2d_rop_dindep(rop))
            why |= 1;
        if (wmask != 0xffffffffu)
            why |= 2;
        if (!why) {
            if (rage128_2d_queue_blit_key(dev, sx, sy, dx, dy, w, h, key,
                                          kmask, key_eq_skip, bpp, rop))
                return;
        } else
            rage128_gpu_2d_qskip(dev, 4, why);
    }

    if (!rage128_2d_map_span(dev, &sdst, &dev->s2d_dst, dev->dst_offset,
                             dev->dst_pitch, bpp, dx, dy, dy + h - 1, dx + w - 1)) {
        rage128_2d_vq_abort(dev);
        return;
    }
    if (!rage128_2d_map_span(dev, &ssrc, &dev->s2d_src, dev->src_offset,
                             dev->src_pitch, bpp, 0, sy < 0 ? 0 : sy,
                             sy + h - 1 < 0 ? 0 : sy + h - 1,
                             sx + w - 1 < 0 ? 0 : sx + w - 1)) {
        r128_surf_release(&sdst); /* nothing written yet */
        rage128_2d_vq_abort(dev);
        return;
    }

    for (int row = 0; row < h; row++) {
        uint32_t saddr = dev->src_offset + ((uint32_t) (sy + row) * rage128_2d_src_stride(dev, bpp) + sx * bpp);
        uint32_t daddr = dev->dst_offset + ((uint32_t) (dy + row) * rage128_2d_dst_stride(dev, bpp) + dx * bpp);

        for (int col = 0; col < w; col++) {
            uint32_t sa = saddr + (uint32_t) col * bpp;
            uint32_t da = daddr + (uint32_t) col * bpp;
            uint32_t s  = 0;

            if (aux_on && !rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, dx + col, dy + row))
                continue;
            for (int b = 0; b < bpp; b++)
                s |= (uint32_t) *r128_surf_at(&ssrc, sa + (uint32_t) b) << (b * 8);
            if (((s & kmask) == key) == key_eq_skip)
                continue; /* keyed out: transparent pixel */
            for (int b = 0; b < bpp; b++) {
                uint8_t *dp = r128_surf_at(&sdst, da + (uint32_t) b);
                uint8_t  pb = (pat >> ((b & 3) * 8)) & 0xff;
                uint8_t  sb = (s >> (b * 8)) & 0xff;

                rage128_2d_store(dp, rage128_rop3(rop, pb, sb, *dp),
                                 (uint8_t) (wmask >> ((b & 3) * 8)));
            }
        }
        rage128_2d_mark_dirty(dev, &sdst, daddr, (uint32_t) w * bpp);
    }
    r128_surf_release(&ssrc);
    r128_surf_commit(dev, &sdst);
    rage128_2d_vq_end(dev);
}

/* aRGB4444 (datatype 15, "intermediate format only. Not understood by
   the Display Controller", RRG: DP_DATATYPE, p. 3-168 / PDF 186) to and
   from ARGB8888: each nibble is repeated on the way out and truncated on
   the way back, so a round trip in the same format is exact. */
static uint32_t
rage128_2d_argb4444_expand(uint32_t v)
{
    uint32_t a = (v >> 12) & 0xf, r = (v >> 8) & 0xf, g = (v >> 4) & 0xf, b = v & 0xf;

    return (a * 0x11u << 24) | (r * 0x11u << 16) | (g * 0x11u << 8) | (b * 0x11u);
}

static uint32_t
rage128_2d_argb4444_pack(uint32_t argb)
{
    return ((argb >> 16) & 0xf000) | ((argb >> 12) & 0x0f00)
        | ((argb >> 8) & 0x00f0) | ((argb >> 4) & 0x000f);
}

/* Scaler texel fetch: expand one source pixel of datatype dt to ARGB8888
   by repeating each channel's top bits into its low bits. RGB 565 and
   24 bpp RGB have no alpha, so alpha is 0xff; aRGB 1555 expands bit 15.
   An 8 bpp value v becomes a = r = g = b = v, so packing it back to
   8 bpp gives v again. */
static uint32_t
rage128_2d_texel_argb(const r128_surf_t *s, uint32_t addr, int dt)
{
    uint32_t v, r, g, b;

    switch (dt) {
        case 15: /* aRGB 4444 */
            v = *r128_surf_at((r128_surf_t *) s, addr)
                | ((uint32_t) *r128_surf_at((r128_surf_t *) s, addr + 1) << 8);
            return rage128_2d_argb4444_expand(v);
        case 3: /* aRGB 1555 */
            v = *r128_surf_at((r128_surf_t *) s, addr)
                | ((uint32_t) *r128_surf_at((r128_surf_t *) s, addr + 1) << 8);
            r = (v >> 10) & 0x1f;
            g = (v >> 5) & 0x1f;
            b = v & 0x1f;
            return ((v & 0x8000) ? 0xff000000u : 0)
                | (((r << 3) | (r >> 2)) << 16)
                | (((g << 3) | (g >> 2)) << 8)
                | ((b << 3) | (b >> 2));
        case 4: /* RGB 565 */
            v = *r128_surf_at((r128_surf_t *) s, addr)
                | ((uint32_t) *r128_surf_at((r128_surf_t *) s, addr + 1) << 8);
            r = (v >> 11) & 0x1f;
            g = (v >> 5) & 0x3f;
            b = v & 0x1f;
            return 0xff000000u
                | (((r << 3) | (r >> 2)) << 16)
                | (((g << 2) | (g >> 4)) << 8)
                | ((b << 3) | (b >> 2));
        case 5: /* 24 bpp RGB */
            return 0xff000000u
                | ((uint32_t) *r128_surf_at((r128_surf_t *) s, addr + 2) << 16)
                | ((uint32_t) *r128_surf_at((r128_surf_t *) s, addr + 1) << 8)
                | *r128_surf_at((r128_surf_t *) s, addr);
        case 6:  /* aRGB 8888 */
        case 14: /* aYUV 8888, raw same-format resample only */
            return *r128_surf_at((r128_surf_t *) s, addr)
                | ((uint32_t) *r128_surf_at((r128_surf_t *) s, addr + 1) << 8)
                | ((uint32_t) *r128_surf_at((r128_surf_t *) s, addr + 2) << 16)
                | ((uint32_t) *r128_surf_at((r128_surf_t *) s, addr + 3) << 24);
        case 11: /* YUV422 raw 16-bit unit (convert path bypasses here) */
        case 12:
            return *r128_surf_at((r128_surf_t *) s, addr)
                | ((uint32_t) *r128_surf_at((r128_surf_t *) s, addr + 1) << 8);
        default: /* 8 bpp raw */
            v = *r128_surf_at((r128_surf_t *) s, addr);
            return (v << 24) | (v << 16) | (v << 8) | v;
    }
}

/* Truncating pack of ARGB8888 into GMC datatype dt (lossless round-trip
   with the replicated expand above, so same-format scaling is bit-exact). */
static uint32_t
rage128_2d_argb_pack(uint32_t argb, int dt)
{
    switch (dt) {
        case 15:
            return rage128_2d_argb4444_pack(argb);
        case 3:
            return ((argb >> 16) & 0x8000)
                | ((argb >> 9) & 0x7c00)
                | ((argb >> 6) & 0x03e0)
                | ((argb >> 3) & 0x001f);
        case 4:
            return ((argb >> 8) & 0xf800)
                | ((argb >> 5) & 0x07e0)
                | ((argb >> 3) & 0x001f);
        case 5:
            return argb & 0x00ffffff;
        case 6:
        case 14: /* raw same-format resample, value already the unit */
            return argb;
        case 11: /* YUV422 raw unit passthrough */
        case 12:
            return argb & 0xffff;
        default: /* 8 bpp raw */
            return argb & 0xff;
    }
}

/* Expand a raw pixel value already packed in GMC datatype dt to ARGB8888
   (same channel-replication rules as rage128_2d_texel_argb). */
static uint32_t
rage128_2d_val_argb(uint32_t v, int dt)
{
    uint32_t r, g, b;

    switch (dt) {
        case 15:
            return rage128_2d_argb4444_expand(v);
        case 3:
            r = (v >> 10) & 0x1f;
            g = (v >> 5) & 0x1f;
            b = v & 0x1f;
            return ((v & 0x8000) ? 0xff000000u : 0)
                | (((r << 3) | (r >> 2)) << 16)
                | (((g << 3) | (g >> 2)) << 8)
                | ((b << 3) | (b >> 2));
        case 4:
            r = (v >> 11) & 0x1f;
            g = (v >> 5) & 0x3f;
            b = v & 0x1f;
            return 0xff000000u
                | (((r << 3) | (r >> 2)) << 16)
                | (((g << 2) | (g >> 4)) << 8)
                | ((b << 3) | (b >> 2));
        case 5:
            return 0xff000000u | (v & 0x00ffffff);
        case 6:
            return v;
        default: /* 8 bpp raw */
            v &= 0xff;
            return (v << 24) | (v << 16) | (v << 8) | v;
    }
}

/* A packed YUV 4:2:2 texel (datatype 11 or 12) through the YUV to RGB
   converter, ARGB8888 out. The chroma comes from the aligned pair of
   pixels that share it (this assumes the source lines are a whole
   number of qwords, as SECONDARY_SCALE_PITCH requires; RRG:
   SECONDARY_SCALE_PITCH, p. 3-160 / PDF 178). The bytes of a pair are
   Y0 U Y1 V for datatype 11 (VYUY) and U Y0 V Y1 for datatype 12 (YVYU)
   (Rage Pro guide, sec. 8.5, the packed pixel table). The equations are
   the CCIR-601 ones of the Rage Pro guide, sec. 8.4.5, scaled by 128.
   DP_CONVERSION_TEMP picks the red equation: 0 = red at 6500 K, green
   and blue at 9300 K; 1 = all at 9300 K (RRG: DP_DATATYPE, p. 3-170 /
   PDF 188). The result is clamped to 0..255. The rounding (here
   rounding down) is not documented. */
static uint32_t
rage128_2d_yuv_texel_argb(const rage128_t *dev, const r128_surf_t *s,
                          uint32_t addr, int sdt)
{
    uint32_t pa  = addr & ~3u;
    int      odd = (int) (addr >> 1) & 1;
    uint8_t  p0  = *r128_surf_at((r128_surf_t *) s, pa);
    uint8_t  p1  = *r128_surf_at((r128_surf_t *) s, pa + 1);
    uint8_t  p2  = *r128_surf_at((r128_surf_t *) s, pa + 2);
    uint8_t  p3  = *r128_surf_at((r128_surf_t *) s, pa + 3);
    int      y, u, v, r, g, b;

    if (sdt == 11) {
        y = odd ? p2 : p0;
        u = p1;
        v = p3;
    } else {
        y = odd ? p3 : p1;
        u = p0;
        v = p2;
    }
    if (dev->dp_datatype & 0x80000000u)
        r = (144 * y + 200 * v - 27904) >> 7;
    else
        r = (144 * y + 400 * v - 53504) >> 7;
    g = (144 * y - 104 * v - 50 * u + 17408) >> 7;
    b = (144 * y + 256 * u - 35072) >> 7;
    if (r < 0)
        r = 0;
    else if (r > 255)
        r = 255;
    if (g < 0)
        g = 0;
    else if (g > 255)
        g = 255;
    if (b < 0)
        b = 0;
    else if (b > 255)
        b = 255;
    return 0xff000000u | ((uint32_t) r << 16) | ((uint32_t) g << 8)
        | (uint32_t) b;
}

/* One stretch-source texel. An 8 bpp palette-index source going to a
   wider destination is looked up in the scaler palette. The
   LOAD_PALETTE packet loads its entries with blue in [7:0], green in
   [15:8] and red in [23:16] (SDK: Table F-39, p. F-46 / PDF 336); they
   are read here as 24 bpp RGB, alpha ignored, and the pack converts
   them to the destination format. A packed YUV 4:2:2 source going to an
   RGB destination goes through the YUV to RGB converter. Everything
   else converts directly (a YUV source going to the same YUV format
   stays a raw unit, through the 11, 12 and 14 cases of the texel and
   pack helpers). */
static uint32_t
rage128_2d_stretch_texel(const rage128_t *dev, const r128_surf_t *s,
                         uint32_t addr, int sdt, int ddt, int usepal)
{
    if (usepal) {
        uint32_t idx = *r128_surf_at((r128_surf_t *) s, addr);

        return rage128_2d_val_argb(dev->scl_palette[idx], 5);
    }
    if ((sdt == 11 || sdt == 12) && sdt != ddt)
        return rage128_2d_yuv_texel_argb(dev, s, addr, sdt);
    return rage128_2d_texel_argb(s, addr, sdt);
}

/* Per-channel lerp of two ARGB8888 values, weight w in [0,256]. */
static uint32_t
rage128_2d_lerp_argb(uint32_t c0, uint32_t c1, uint32_t w)
{
    uint32_t iw = 256 - w;
    uint32_t rb = (((c0 & 0x00ff00ffu) * iw + (c1 & 0x00ff00ffu) * w) >> 8) & 0x00ff00ffu;
    uint32_t ag = ((((c0 >> 8) & 0x00ff00ffu) * iw + ((c1 >> 8) & 0x00ff00ffu) * w)) & 0xff00ff00u;

    return rb | ag;
}

/* Work out one destination pixel of the stretch walk (the row values
   are computed by the caller): key test on the raw texel, texel fetch,
   optional bilinear blend, pack to the destination format. Returns 1 =
   store *out, 0 = keyed out. The CPU walk and the queue path both use
   it, so the two cannot differ. xph is the 16.16 fraction the
   accumulator starts at. */
static int
rage128_2d_stretch_resolve(rage128_t *dev, const r128_surf_t *ssrc,
                           int sbpp, int sdt, int ddt, int usepal,
                           int blend, int sx0, uint32_t xph, uint32_t xinc,
                           int xskip, int col, uint32_t saddr, uint32_t sbddr,
                           uint32_t wy, int sxlim, uint32_t key,
                           uint32_t kmask, int key_skip, uint32_t *out)
{
    uint32_t xacc = xph + (uint32_t) (col + xskip) * xinc;
    int      sx   = sx0 + (int) (xacc >> 16);
    uint32_t a    = saddr + (uint32_t) sx * (uint32_t) sbpp;
    uint32_t c;

    if (key_skip >= 0) {
        uint32_t raw = 0;

        for (int b = 0; b < sbpp; b++)
            raw |= (uint32_t) *r128_surf_at((r128_surf_t *) ssrc,
                                            a + (uint32_t) b)
                << (b * 8);
        if (((raw & kmask) == key) == key_skip)
            return 0; /* keyed out: transparent texel */
    }
    c = rage128_2d_stretch_texel(dev, ssrc, a, sdt, ddt, usepal);
    if (blend) {
        int      sxb = (sx < sxlim) ? sx + 1 : sx;
        uint32_t wx  = (xacc & 0xffff) >> 8;
        uint32_t xb  = (uint32_t) sxb * (uint32_t) sbpp;
        uint32_t top = rage128_2d_lerp_argb(c,
                                            rage128_2d_stretch_texel(dev, ssrc, saddr + xb, sdt, ddt, usepal), wx);
        uint32_t bot = rage128_2d_lerp_argb(
            rage128_2d_stretch_texel(dev, ssrc, sbddr + (uint32_t) sx * (uint32_t) sbpp, sdt, ddt, usepal),
            rage128_2d_stretch_texel(dev, ssrc, sbddr + xb, sdt, ddt, usepal), wx);

        c = rage128_2d_lerp_argb(top, bot, wy);
    }
    *out = rage128_2d_argb_pack(c, ddt);
    return 1;
}

/* Queue a stretch blit. With a ROP that does not use the destination,
   every stored byte depends only on the brush and the texel, so the
   walk runs here against the mapped source (only its GPU barrier is
   paid), the finished runs go through the ROP into the staging ring,
   and one copy per run is queued. The destination is never mapped, so
   it needs no barrier; the queue keeps the order. It uses
   rage128_2d_stretch_resolve, as the CPU walk does. Overlapping source
   and destination bytes and negative source origins are left to the
   walk (the walk reads the source as it goes, and the wrap of a
   negative origin cannot be expressed in linear queue addresses).
   0 = not queued; a partial queue is harmless (the re-walk stores the
   same bytes). */
static int
rage128_2d_queue_stretch(rage128_t *dev, uint32_t soff, uint32_t spitch_px,
                         int sx0, int sy0, uint32_t xph, uint32_t yph,
                         uint32_t xinc, uint32_t yinc,
                         int dx, int dy, int w, int h, int sdt, int blend,
                         uint32_t key, uint32_t kmask, int key_skip,
                         int xskip, int yskip, int ddt, int bpp, int sbpp,
                         int usepal, uint8_t rop, int sxlim, int sylim,
                         int symax, int sxmax)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    uint32_t pat    = dev->dp_brush_frgd_clr;
    int      aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    uint32_t dpb    = rage128_2d_dst_stride(dev, bpp);
    uint32_t rowlen = (uint32_t) w * (uint32_t) bpp;
    uint32_t daddr0 = dev->dst_offset
        + ((uint32_t) dy * rage128_2d_dst_stride(dev, bpp) + (uint32_t) dx * (uint32_t) bpp);
    uint64_t dend = (uint64_t) daddr0 + (uint64_t) (h - 1) * dpb + rowlen;
    int      ymax = (blend && symax < sylim) ? symax + 1 : symax;
    /* the YUV to RGB path reads the chroma pair up to its odd pixel */
    int xmax = ((blend && sxmax < sxlim) ? sxmax + 1 : sxmax)
        | (((sdt == 11 || sdt == 12) && sdt != ddt) ? 1 : 0);
    uint64_t shi = (uint64_t) soff
        + (uint64_t) ymax * rage128_2d_span_stride(dev, &dev->s2d_src, soff, spitch_px, sbpp)
        + ((uint64_t) xmax + 1u) * (uint64_t) sbpp;
    r128_surf_t ssrc;
    uint8_t    *rb;
    int         vq, nruns = 0;

    if (sx0 < 0 || sy0 < 0 || r128_card_is_agp(daddr0)
        || dend > (uint64_t) dev->vram_size || (h > 1 && dpb < rowlen)
        || ((uint64_t) daddr0 < shi && dend > (uint64_t) soff)) {
        rage128_gpu_2d_qskip(dev, 5, 32);
        return 0;
    }
    if (!rage128_2d_map_span(dev, &ssrc, &dev->s2d_src, soff, spitch_px,
                             sbpp, 0, sy0 < 0 ? 0 : sy0, ymax, xmax)) {
        rage128_gpu_2d_qskip(dev, 5, 32);
        return 0;
    }
    rb = (uint8_t *) malloc(rowlen);
    if (!rb) {
        r128_surf_release(&ssrc);
        rage128_gpu_2d_qskip(dev, 5, 32);
        return 0;
    }
    vq = rage128_2d_vq_begin(dev, 5, daddr0, dpb, rowlen, (uint32_t) h);

    /* The source stride, worked out once for the whole walk: the same
       one the window above was mapped with. */
    uint32_t spb = rage128_2d_span_stride(dev, &dev->s2d_src, soff,
                                          spitch_px, sbpp);

    for (int row = 0; row < h; row++) {
        uint32_t yacc  = yph + (uint32_t) (row + yskip) * yinc;
        int      sy    = sy0 + (int) (yacc >> 16);
        int      syb   = (sy < sylim) ? sy + 1 : sy;
        uint32_t wy    = (yacc & 0xffff) >> 8;
        uint32_t saddr = soff + (uint32_t) sy * spb;
        uint32_t sbddr = soff + (uint32_t) syb * spb;
        uint32_t daddr = daddr0 + (uint32_t) row * dpb;
        uint32_t rlen  = 0;
        int      rs    = -1;

        for (int col = 0; col < w; col++) {
            uint32_t spx;
            int      store = (!aux_on
                         || rage128_aux_sc_pass_2d(dev->aux_sc_cntl,
                                                        dev->aux_sc_rect,
                                                        dx + col, dy + row))
                && rage128_2d_stretch_resolve(dev, &ssrc, sbpp, sdt, ddt,
                                              usepal, blend, sx0, xph, xinc,
                                              xskip, col, saddr, sbddr,
                                              wy, sxlim, key, kmask,
                                              key_skip, &spx);

            if (store) {
                if (rs < 0)
                    rs = col;
                for (int b = 0; b < bpp; b++) {
                    uint8_t pb = (pat >> ((b & 3) * 8)) & 0xff;

                    rb[rlen++] = (uint8_t) rage128_rop3(rop, pb,
                                                        (spx >> (b * 8)) & 0xff, 0);
                }
            }
            if (rs >= 0 && (!store || col == w - 1)) {
                uint32_t so;
                uint8_t *st;

                if (++nruns > 256) {
                    rage128_gpu_2d_qskip(dev, 5, 8);
                    goto refuse;
                }
                st = (uint8_t *) rage128_gpu_2d_stage(dev, rlen, &so);
                if (!st) {
                    rage128_gpu_2d_qskip(dev, 5, 64);
                    goto refuse;
                }
                memcpy(st, rb, rlen);
                if (!rage128_gpu_2d_copy(dev, daddr + (uint32_t) rs * bpp,
                                         rlen, rlen, 1, so, rlen, 4))
                    goto refuse;
                rage128_2d_queue_mark(dev, daddr + (uint32_t) rs * bpp,
                                      rlen, rlen, 1);
                rs   = -1;
                rlen = 0;
            }
        }
    }
    free(rb);
    r128_surf_release(&ssrc);
    rage128_gpu_2d_qdone(dev, 5);
    if (vq) {
        rage128_2d_vq_after(dev, 1);
        return 0; /* self-check: the CPU walk runs, the end compares */
    }
    return 1;

refuse:
    free(rb);
    r128_surf_release(&ssrc);
    if (vq)
        rage128_2d_vq_after(dev, 0);
    return 0;
}

/* Stretch blit (packet 0x96, R128_CCE_PACKET3_CNTL_SCALING). The walk
   runs over the destination and steps the source by 16.16 increments
   from (sx0, sy0), starting at the 16.16 fractions (xph, yph).
   SCALE_PIX_REP picks pixel replication or a bilinear blend (RRG:
   MISC_3D_STATE_CNTL_REG, p. 3-256 / PDF 274); the blend weights are
   the fractions of the same walk, and the neighbor texels are clamped
   to the SCALE_SCR_HEIGHT_WIDTH extent. How the chip places and rounds
   the blend is not documented. The source datatype is converted to the
   destination format; the ROP3, scissor and destination follow the
   latched state.
   The color compare state adds a key test (packet 0x97,
   R128_CCE_PACKET3_CNTL_TRANS_SCALING): the raw source texel under the
   mask is compared to the key, and the nearest texel decides even when
   blending. Captured Windows 98 traffic sends 0x97 only with
   replication, so blend plus key has not been observed. key_skip 1 = skip
   when equal (CLR_CMP_FN_SRC 5, draw when not equal), 0 = skip when not
   equal (CLR_CMP_FN_SRC 4), -1 = no key. */
static void
rage128_2d_stretch_rect(rage128_t *dev, uint32_t soff, uint32_t spitch_px,
                        int sx0, int sy0, uint32_t xph, uint32_t yph,
                        uint32_t xinc, uint32_t yinc,
                        int dx, int dy, int w, int h, int sdt, int blend)
{
    int         ddt    = rage128_dp_dst_dt(dev);
    int         bpp    = rage128_2d_bpp(ddt);
    int         sbpp   = rage128_2d_bpp(sdt);
    uint8_t     rop    = rage128_dp_rop3(dev);
    uint32_t    pat    = dev->dp_brush_frgd_clr;
    uint32_t    wmask  = dev->dp_write_mask;
    int         aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    r128_surf_t sdst, ssrc;
    int         cx0, cy0, cx1, cy1;
    int         xskip = 0;
    int         yskip = 0;
    int         symax, sxmax, sxlim, sylim;
    int         usepal = (sdt == 2 && bpp != 1);

    /* The scaler keys on the raw source texel, before conversion. In
       captured Windows 98 traffic (Age of Empires II, packet 0x97) an
       8 bpp palette index is keyed against an 8 bpp key while being
       converted to a wider format. A source-only function 4 or 5 uses
       the key path shared by the resolve and the queue; any other active
       compare state takes the general per-pixel test in the CPU walk
       below. */
    struct r128_ccmp cc;
    int              cca  = rage128_2d_ccmp_setup(dev, 1, sbpp, bpp, &cc);
    int              keyq = cca && cc.src_on && (cc.fn_src == 4 || cc.fn_src == 5)
        && (!cc.dst_on || cc.fn_dst == 0);
    int      cgen     = cca && !keyq;
    int      key_skip = keyq ? (cc.fn_src == 5) : -1;
    uint32_t key      = keyq ? cc.key_src : 0;
    uint32_t kmask    = keyq ? cc.smask : 0;

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    if (dx < cx0) {
        xskip = cx0 - dx;
        w -= xskip;
        dx = cx0;
    }
    if (dy < cy0) {
        yskip = cy0 - dy;
        h -= yskip;
        dy = cy0;
    }
    if (dx + w - 1 > cx1)
        w = cx1 - dx + 1;
    if (dy + h - 1 > cy1)
        h = cy1 - dy + 1;
    if (w <= 0 || h <= 0)
        return;
    /* The rightmost and bottom source texels the 16.16 walk can reach
       (the increments are never negative, so the maxima are at the last
       column and row). A blend reads one texel further, limited by the
       source extent in SCALE_SCR_HEIGHT_WIDTH. */
    symax = sy0 + (int) ((yph + (uint32_t) (h - 1 + yskip) * yinc) >> 16);
    sxmax = sx0 + (int) ((xph + (uint32_t) (w - 1 + xskip) * xinc) >> 16);
    sxlim = (dev->scale_scr_height_width & 0xffff)
        ? (int) (dev->scale_scr_height_width & 0xffff) - 1
        : sxmax;
    sylim = (dev->scale_scr_height_width >> 16)
        ? (int) (dev->scale_scr_height_width >> 16) - 1
        : symax;

    /* GPU queue path: with a ROP that does not use the destination and
       the write mask fully open, every stored byte depends only on the
       texel, so it is worked out here and queued as copies per run
       (rage128_2d_queue_stretch). With synctel on, the CPU walk is kept
       so the access record stays complete. */
    if (dev->gpu && !dev->synctel) {
        int why = 0;

        if (!rage128_2d_rop_dindep(rop))
            why |= 1;
        if (wmask != 0xffffffffu)
            why |= 2;
        if (cgen)
            why |= 16; /* compare state the resolve key cannot express */
        if (!why) {
            if (rage128_2d_queue_stretch(dev, soff, spitch_px, sx0, sy0,
                                         xph, yph, xinc, yinc, dx, dy, w, h, sdt,
                                         blend, key, kmask, key_skip,
                                         xskip, yskip, ddt, bpp, sbpp,
                                         usepal, rop, sxlim, sylim,
                                         symax, sxmax))
                return;
        } else
            rage128_gpu_2d_qskip(dev, 5, why);
    }

    if (!rage128_2d_map_span(dev, &sdst, &dev->s2d_dst, dev->dst_offset,
                             dev->dst_pitch, bpp, dx, dy, dy + h - 1, dx + w - 1)) {
        rage128_2d_vq_abort(dev);
        return;
    }
    {
        /* The YUV to RGB path reads the chroma from the aligned pixel
           pair, so the rightmost mapped pixel is rounded up to the odd
           pixel of its pair. */
        int sxmap = (blend && sxmax < sxlim) ? sxmax + 1
                                             : (sxmax < 0 ? 0 : sxmax);

        if ((sdt == 11 || sdt == 12) && sdt != ddt)
            sxmap |= 1;
        if (!rage128_2d_map_span(dev, &ssrc, &dev->s2d_src, soff, spitch_px,
                                 sbpp, 0, sy0 < 0 ? 0 : sy0,
                                 (blend && symax < sylim) ? symax + 1
                                                          : (symax < 0 ? 0 : symax),
                                 sxmap)) {
            r128_surf_release(&sdst); /* nothing written yet */
            rage128_2d_vq_abort(dev);
            return;
        }
    }

    /* The source stride, worked out once for the whole walk: the same
       one the window above was mapped with. */
    uint32_t spb = rage128_2d_span_stride(dev, &dev->s2d_src, soff,
                                          spitch_px, sbpp);

    for (int row = 0; row < h; row++) {
        uint32_t yacc  = yph + (uint32_t) (row + yskip) * yinc;
        int      sy    = sy0 + (int) (yacc >> 16);
        int      syb   = (sy < sylim) ? sy + 1 : sy;
        uint32_t wy    = (yacc & 0xffff) >> 8;
        uint32_t saddr = soff + (uint32_t) sy * spb;
        uint32_t sbddr = soff + (uint32_t) syb * spb;
        uint32_t daddr = dev->dst_offset + ((uint32_t) (dy + row) * rage128_2d_dst_stride(dev, bpp) + (uint32_t) dx * (uint32_t) bpp);

        for (int col = 0; col < w; col++) {
            uint32_t da = daddr + (uint32_t) col * (uint32_t) bpp;
            uint32_t spx;

            if (aux_on && !rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, dx + col, dy + row))
                continue;
            if (cgen) {
                /* the same nearest texel the resolve key path tests */
                uint32_t xacc = xph + (uint32_t) (col + xskip) * xinc;
                int      rsx  = sx0 + (int) (xacc >> 16);
                uint32_t ra   = saddr + (uint32_t) rsx * (uint32_t) sbpp;
                uint32_t raw = 0, dpx = 0;
                int      cr;

                for (int b = 0; b < sbpp; b++)
                    raw |= (uint32_t) *r128_surf_at(&ssrc, ra + (uint32_t) b)
                        << (b * 8);
                for (int b = 0; b < bpp; b++)
                    dpx |= (uint32_t) *r128_surf_at(&sdst, da + (uint32_t) b)
                        << (b * 8);
                cr = rage128_2d_ccmp_px(&cc, raw, dpx);
                if (!cr)
                    continue;
                if (cr == 2) {
                    for (int b = 0; b < bpp; b++)
                        rage128_2d_store(r128_surf_at(&sdst, da + (uint32_t) b),
                                         (uint8_t) ((raw ^ cc.flip) >> (b * 8)),
                                         (uint8_t) (wmask >> ((b & 3) * 8)));
                    continue;
                }
            }
            if (!rage128_2d_stretch_resolve(dev, &ssrc, sbpp, sdt, ddt,
                                            usepal, blend, sx0, xph, xinc,
                                            xskip, col, saddr, sbddr, wy,
                                            sxlim, key, kmask, key_skip, &spx))
                continue; /* keyed out: transparent pixel */
            for (int b = 0; b < bpp; b++) {
                uint8_t *dp = r128_surf_at(&sdst, da + (uint32_t) b);
                uint8_t  s  = (uint8_t) (spx >> (b * 8));
                uint8_t  pb = (pat >> ((b & 3) * 8)) & 0xff;

                rage128_2d_store(dp, rage128_rop3(rop, pb, s, *dp),
                                 (uint8_t) (wmask >> ((b & 3) * 8)));
            }
        }
        rage128_2d_mark_dirty(dev, &sdst, daddr, (uint32_t) w * bpp);
    }
    r128_surf_release(&ssrc);
    r128_surf_commit(dev, &sdst);
    rage128_2d_vq_end(dev);
}

/* Decode and run the 11-dword scaler data block of packets 0x96 and
   0x97 (SDK: Table F-17, pp. F-27-F-28 / PDF 317-318). b[0] is
   MISC_3D_STATE, b[1] and b[2] TEX_CNTL and TEX_COMB_CTL, b[3] the
   source datatype, b[4] the source byte offset, b[5] the source pitch:
   SCALE_PITCH in [8:0], units of 8 pixels, with [16:9] reserved and
   SCALE_PITCH_ADJ in [31:30], 1 = x2 and 2 = x4 (SDK: Table F-21,
   pp. F-32-F-33 / PDF 322-323).
   b[6] is the source start, y in [29:16] and x in [13:0], read in half
   pixels. The SDK calls this slot reserved and says to write 0, and the
   Windows 98 display driver does. The Windows 2000 display driver
   writes x << 1 | y << 17 (RE: ati2dvaa.dll @1096b), and the Windows 98
   Direct3D driver's antialiasing resize writes 0x10001, half a pixel in
   each direction (RE: ati3draa.dll @b00d28c0); half-pixel units are the
   reading that fits both. The fraction bit starts the 16.16 walk at
   0x8000.
   b[7] and b[8] are the X and Y increments, 4.12 in [19:4] with [3:0]
   and [31:20] reserved (SDK Table F-17; RRG: SECONDARY_SCALE_X_INC,
   p. 3-160 / PDF 178, for the same layout). They are masked here, the
   one place every walk path reads them.
   b[9] is the destination position as SCALE_DST_X_Y lays it out, x in
   [29:16] and y in [13:0]; both Windows 98 drivers build it as
   left << 16 | top (RE: ati3draa.dll @b00bb3e0, from the DirectDraw
   blit's destination rect; RE: ati2draa.drv @9017, a file offset, whose
   bottom-to-top arm offsets the low word by the height). b[10] is
   (height << 16) | width.
   Returns 0 when the source datatype cannot be paired with the
   destination format (the caller then reports the packet as
   unhandled). */
static int
rage128_2d_scale_block(rage128_t *dev, const uint32_t *b)
{
    uint32_t depth  = b[3];
    uint32_t spitch = (b[5] & 0x1ff) * 8;

    if ((b[5] >> 30) == 1)
        spitch *= 2;
    else if ((b[5] >> 30) == 2)
        spitch *= 4;
    /* RGB sources convert. An 8 bpp palette-index source (2) is copied
       raw to an 8 bpp destination, or looked up in the scaler palette
       (loaded by the LOAD_PALETTE packet, 0x2c) for a wider one. The
       other 8 bpp source types need an 8 bpp destination. A YUV source
       into the same YUV format is resampled as raw units; packed YUV
       4:2:2 into an RGB destination goes through the YUV to RGB
       converter. An RGB source into a YUV destination is left unhandled:
       the guides do not give the RGB to YUV equations, and in captured
       driver traffic only a packed YUV 4:2:2 source is ever scaled into
       another format. Mixed YUV formats and 16-bit palette sources are
       unhandled too. */
    switch (rage128_dp_dst_dt(dev)) {
        case 11:
        case 12:
        case 14:
            if (depth != rage128_dp_dst_dt(dev))
                return 0;
            break;
        default:
            break;
    }
    switch (depth) {
        case 2:
            break;
        case 7:
        case 8:
        case 9:
            if (rage128_2d_bpp(rage128_dp_dst_dt(dev)) != 1)
                return 0;
            break;
        case 3:
        case 4:
        case 5:
        case 6:
        case 15:
            if (rage128_2d_bpp(rage128_dp_dst_dt(dev)) == 1)
                return 0;
            break;
        case 11:
        case 12:
            /* The YUV to RGB path reads the chroma from aligned pixel
               pairs; a source base off that alignment would take the
               chroma from the wrong pixels, so it is refused (the
               guide requires source lines of whole qwords, RRG:
               SECONDARY_SCALE_PITCH, p. 3-160 / PDF 178). */
            if (b[4] & 3)
                return 0;
            if (depth != rage128_dp_dst_dt(dev)
                && rage128_2d_bpp(rage128_dp_dst_dt(dev)) == 1)
                return 0;
            break;
        case 14:
            if (depth != rage128_dp_dst_dt(dev))
                return 0;
            break;
        default:
            return 0;
    }
    rage128_2d_stretch_rect(dev, b[4], spitch,
                            (int) ((b[6] & 0x3fff) >> 1),
                            (int) (((b[6] >> 16) & 0x3fff) >> 1),
                            (b[6] & 1u) << 15,
                            ((b[6] >> 16) & 1u) << 15,
                            b[7] & 0x000ffff0u, b[8] & 0x000ffff0u,
                            rage128_sx14(b[9] >> 16), /* SCALE_DST_X_Y: X high */
                            rage128_sx14(b[9]),
                            (int) (b[10] & 0x3fff),
                            (int) ((b[10] >> 16) & 0x3fff),
                            (int) depth,
                            !((b[0] >> 10) & 1) /* SCALE_PIX_REP, bit 10 */);
    return 1;
}

/* Mono expansion: set bits paint the source foreground color; clear
   bits paint the source background or are left alone, by
   GMC_SRC_DATATYPE (0 = foreground and background, 1 = foreground and
   leave alone; RRG: DP_GUI_MASTER_CNTL, p. 3-174 / PDF 192). bitpitch
   is in bits (glyphs are bit-packed, host bitmaps byte-aligned). The
   bit order within each byte follows GMC_BYTE_PIX_ORDER, bit 14 (0 =
   MSB to LSB, 1 = LSB to MSB, same page). In captured Windows traffic
   the bit is always 0; the X driver sets it. The pattern operand comes
   from the brush: solid foreground, or a screen-aligned 8x8 mono or
   color pattern (pat8x8 and patcol as in rage128_2d_paint_rect; both
   NULL = solid). pat_la: brush type 1, a clear pattern cell leaves the
   destination alone. */
static void
rage128_2d_mono_rect(rage128_t *dev, int x, int y, int w, int h,
                     const uint8_t *bits, uint32_t bitpitch,
                     const uint8_t *pat8x8, const uint8_t *patcol, int pat_la)
{
    int              bpp       = rage128_2d_bpp(rage128_dp_dst_dt(dev));
    uint8_t          rop       = rage128_dp_rop3(dev);
    int              use_bkgd  = (rage128_dp_src_dt(dev) == 0);
    int              lsb_first = rage128_dp_lsb_first(dev);
    uint32_t         wmask     = dev->dp_write_mask;
    int              aux_on    = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    struct r128_ccmp cc;
    int              cca = rage128_2d_ccmp_setup(dev, 1, bpp, bpp, &cc);
    r128_surf_t      sd;
    int              cx0, cy0, cx1, cy1;

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);

    /* GPU queue path. Stores whose values depend only on the operands
       (solid or 8x8 pattern brush, write mask fully open, ROP that does
       not use the destination, any depth) are queued directly; any
       other state goes through the read-modify-write plane. This covers
       the small-text, host-data and host-row expansions, which would
       otherwise wait on the GPU barrier at the surface map below. With
       synctel on, the CPU walk is kept so the access record stays
       complete. */
    if (!cca && dev->gpu && !dev->synctel) {
        /* a leave-alone pattern keeps the destination cell by cell,
           which only the read-modify-write plane can express */
        int dind = rage128_2d_rop_dindep(rop)
            && wmask == 0xffffffffu && !(pat8x8 && pat_la);
        int pat = (pat8x8 || patcol) ? 1 : 0;
        int rx0 = x < cx0 ? cx0 : x;
        int ry0 = y < cy0 ? cy0 : y;
        int rx1 = x + w - 1 > cx1 ? cx1 : x + w - 1;
        int ry1 = y + h - 1 > cy1 ? cy1 : y + h - 1;
        int vq  = (rx0 <= rx1 && ry0 <= ry1)
             ? rage128_2d_vq_begin(dev, dind ? (pat ? 9 : 0) : 8,
                                   dev->dst_offset
                                       + ((uint32_t) ry0 * rage128_2d_dst_stride(dev, bpp)
                                         + (uint32_t) rx0 * (uint32_t) bpp),
                                   rage128_2d_dst_stride(dev, bpp),
                                   (uint32_t) (rx1 - rx0 + 1) * (uint32_t) bpp,
                                   (uint32_t) (ry1 - ry0 + 1))
             : 0;
        int ok  = dind
             ? rage128_2d_queue_mono(dev, x, y, w, h, bits, bitpitch,
                                     use_bkgd, lsb_first, rop, bpp,
                                     pat8x8, patcol)
             : rage128_2d_queue_mono_rmw(dev, x, y, w, h, bits,
                                         bitpitch, use_bkgd, lsb_first,
                                         rop, bpp, pat8x8, patcol, pat_la);

        if (ok) {
            if (pat && dind)
                rage128_gpu_2d_qdone(dev, 9);
            if (bpp != 4)
                rage128_gpu_2d_nb(dev, 0);
            if (!vq)
                return;
            rage128_2d_vq_after(dev, 1);
        } else if (vq)
            rage128_2d_vq_after(dev, 0);
    }

    /* The window covers the part inside the scissor (rows and columns
       outside it are skipped per pixel below; if nothing is inside, the
       empty window is mapped). */
    if (!rage128_2d_map_span(dev, &sd, &dev->s2d_dst, dev->dst_offset,
                             dev->dst_pitch, bpp,
                             x < cx0 ? cx0 : x,
                             y < cy0 ? cy0 : y,
                             y + h - 1 > cy1 ? cy1 : y + h - 1,
                             x + w - 1 > cx1 ? cx1 : x + w - 1)) {
        rage128_2d_vq_abort(dev);
        return;
    }

    for (int row = 0; row < h; row++) {
        if (y + row < cy0 || y + row > cy1)
            continue;
        uint32_t addr = dev->dst_offset + ((uint32_t) (y + row) * rage128_2d_dst_stride(dev, bpp) + x * bpp);

        for (int col = 0; col < w; col++) {
            uint32_t bit = (uint32_t) row * bitpitch + col;
            int      set;

            if (x + col < cx0 || x + col > cx1)
                continue;
            if (aux_on && !rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x + col, y + row))
                continue;
            if (lsb_first)
                set = (bits[bit >> 3] >> (bit & 7)) & 1;
            else
                set = (bits[bit >> 3] >> (7 - (bit & 7))) & 1;
            if (!set && !use_bkgd)
                continue;
            uint32_t       fg    = set ? dev->dp_src_frgd_clr : dev->dp_src_bkgd_clr;
            uint32_t       a     = addr + (uint32_t) col * bpp;
            uint32_t       pat   = dev->dp_brush_frgd_clr;
            const uint8_t *patpx = NULL;

            if (pat8x8) {
                int pset = rage128_2d_mono_bit(dev, pat8x8, x + col, y + row);

                if (!pset && pat_la)
                    continue;
                pat = pset ? dev->dp_brush_frgd_clr : dev->dp_brush_bkgd_clr;
            } else if (patcol)
                patpx = &patcol[(rage128_2d_pat_row(dev, y + row) * 8
                                 + rage128_2d_pat_col(dev, x + col))
                                * bpp];
            if (cca) {
                uint32_t dpx = 0;
                int      cr;

                for (int b = 0; b < bpp; b++)
                    dpx |= (uint32_t) *r128_surf_at(&sd, a + (uint32_t) b)
                        << (b * 8);
                /* the compared source color is the expanded mono color */
                cr = rage128_2d_ccmp_px(&cc, fg, dpx);
                if (!cr)
                    continue;
                if (cr == 2) {
                    for (int b = 0; b < bpp; b++)
                        rage128_2d_store(r128_surf_at(&sd, a + (uint32_t) b),
                                         (uint8_t) ((fg ^ cc.flip) >> (b * 8)),
                                         (uint8_t) (wmask >> ((b & 3) * 8)));
                    continue;
                }
            }
            for (int b = 0; b < bpp; b++) {
                uint8_t *dp = r128_surf_at(&sd, a + (uint32_t) b);
                uint8_t  sb = (fg >> ((b & 3) * 8)) & 0xff;
                uint8_t  pb = patpx ? patpx[b] : ((pat >> ((b & 3) * 8)) & 0xff);

                /* The expanded mono is the source operand and the brush
                   the pattern operand. They must stay separate for
                   ROP 0xB8 (S ? D : P), which in captured traffic draws
                   transparent stamps through packet 0x94 with GMC
                   0x74b802d8. */
                rage128_2d_store(dp, rage128_rop3(rop, pb, sb, *dp),
                                 (uint8_t) (wmask >> ((b & 3) * 8)));
            }
        }
        rage128_2d_mark_dirty(dev, &sd, addr, (uint32_t) w * bpp);
    }
    r128_surf_commit(dev, &sd);
    rage128_2d_vq_end(dev);
}

/* Color host data: the source pixels come inline in the packet, in the
   destination pixel format, as rows of exactly w * bpp bytes back to
   back, with dword padding only at the end of the data (the layout the
   Windows 98 display driver builds; RE: ati2draa.drv @5805, a file
   offset). They are combined through the ROP3, with the brush as the
   pattern operand: solid foreground, or a screen-aligned 8x8 mono or
   color pattern (pat8x8 and patcol as in rage128_2d_mono_rect); pat_la
   = brush type 1, a clear cell leaves the destination alone.
   The payload length is a second clip. In captured Windows 98 Direct3D
   traffic the uploads of the smallest mipmap levels give the 8-pixel
   tile in the size fields but carry only the level's real pixels (8
   pixels wide, 1 row, one data dword, scissor fully open), so pixels
   whose source bytes lie past avail are not written. */
static void
rage128_2d_host_color_rect(rage128_t *dev, int x, int y, int w, int h,
                           const uint8_t *px, uint32_t avail,
                           const uint8_t *pat8x8, const uint8_t *patcol,
                           int pat_la)
{
    int              bpp    = rage128_2d_bpp(rage128_dp_dst_dt(dev));
    uint8_t          rop    = rage128_dp_rop3(dev);
    uint32_t         wmask  = dev->dp_write_mask;
    int              aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    uint32_t         stride = (uint32_t) w * (uint32_t) bpp;
    struct r128_ccmp cc;
    int              cca = rage128_2d_ccmp_setup(dev, 1, bpp, bpp, &cc);
    r128_surf_t      sd;
    int              cx0, cy0, cx1, cy1;

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);

    /* GPU queue path: payload pixels put through a ROP that does not
       use the destination do not depend on it, so they are staged and
       copied at the destination depth instead of waiting on the GPU
       barrier at the surface map. */
    if (!cca && dev->gpu && !dev->synctel) {
        /* a pattern brush changes the pattern value per pixel, which
           only the read-modify-write plane can express */
        int dind = rage128_2d_rop_dindep(rop) && wmask == 0xffffffffu
            && !(pat8x8 || patcol);
        int rx0 = x < cx0 ? cx0 : x;
        int ry0 = y < cy0 ? cy0 : y;
        int rx1 = x + w - 1 > cx1 ? cx1 : x + w - 1;
        int ry1 = y + h - 1 > cy1 ? cy1 : y + h - 1;
        int vq  = (rx0 <= rx1 && ry0 <= ry1)
             ? rage128_2d_vq_begin(dev, dind ? 1 : 8,
                                   dev->dst_offset
                                       + ((uint32_t) ry0 * rage128_2d_dst_stride(dev, bpp)
                                         + (uint32_t) rx0 * (uint32_t) bpp),
                                   rage128_2d_dst_stride(dev, bpp),
                                   (uint32_t) (rx1 - rx0 + 1) * (uint32_t) bpp,
                                   (uint32_t) (ry1 - ry0 + 1))
             : 0;
        int ok  = dind
             ? rage128_2d_queue_hostc(dev, x, y, w, h, px, avail, rop, bpp)
             : rage128_2d_queue_hostc_rmw(dev, x, y, w, h, px, avail, rop,
                                          bpp, pat8x8, patcol, pat_la);

        if (ok) {
            if (bpp != 4)
                rage128_gpu_2d_nb(dev, 1);
            if (!vq)
                return;
            rage128_2d_vq_after(dev, 1);
        } else if (vq)
            rage128_2d_vq_after(dev, 0);
    }

    if (!rage128_2d_map_span(dev, &sd, &dev->s2d_dst, dev->dst_offset,
                             dev->dst_pitch, bpp,
                             x < cx0 ? cx0 : x,
                             y < cy0 ? cy0 : y,
                             y + h - 1 > cy1 ? cy1 : y + h - 1,
                             x + w - 1 > cx1 ? cx1 : x + w - 1)) {
        rage128_2d_vq_abort(dev);
        return;
    }

    for (int row = 0; row < h; row++) {
        if (y + row < cy0 || y + row > cy1)
            continue;
        uint32_t       addr = dev->dst_offset + ((uint32_t) (y + row) * rage128_2d_dst_stride(dev, bpp) + x * bpp);
        const uint8_t *srow = px + (uint32_t) row * stride;

        for (int col = 0; col < w; col++) {
            if (x + col < cx0 || x + col > cx1)
                continue;
            if ((uint32_t) row * stride + ((uint32_t) col + 1) * bpp > avail)
                goto done; /* payload exhausted: source rows are back-to-back */
            if (aux_on && !rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x + col, y + row))
                continue;
            uint32_t       a     = addr + (uint32_t) col * bpp;
            uint32_t       pat   = dev->dp_brush_frgd_clr;
            const uint8_t *patpx = NULL;

            if (pat8x8) {
                int pset = rage128_2d_mono_bit(dev, pat8x8, x + col, y + row);

                if (!pset && pat_la)
                    continue;
                pat = pset ? dev->dp_brush_frgd_clr : dev->dp_brush_bkgd_clr;
            } else if (patcol)
                patpx = &patcol[(rage128_2d_pat_row(dev, y + row) * 8
                                 + rage128_2d_pat_col(dev, x + col))
                                * bpp];
            if (cca) {
                uint32_t spx = 0, dpx = 0;
                int      cr;

                for (int b = 0; b < bpp; b++) {
                    spx |= (uint32_t) srow[(uint32_t) col * bpp + b] << (b * 8);
                    dpx |= (uint32_t) *r128_surf_at(&sd, a + (uint32_t) b)
                        << (b * 8);
                }
                cr = rage128_2d_ccmp_px(&cc, spx, dpx);
                if (!cr)
                    continue;
                if (cr == 2) {
                    for (int b = 0; b < bpp; b++)
                        rage128_2d_store(r128_surf_at(&sd, a + (uint32_t) b),
                                         (uint8_t) ((spx ^ cc.flip) >> (b * 8)),
                                         (uint8_t) (wmask >> ((b & 3) * 8)));
                    continue;
                }
            }
            for (int b = 0; b < bpp; b++) {
                uint8_t *dp = r128_surf_at(&sd, a + (uint32_t) b);
                uint8_t  s  = srow[(uint32_t) col * bpp + b];
                uint8_t  pb = patpx ? patpx[b] : ((pat >> ((b & 3) * 8)) & 0xff);

                rage128_2d_store(dp, rage128_rop3(rop, pb, s, *dp),
                                 (uint8_t) (wmask >> ((b & 3) * 8)));
            }
        }
        rage128_2d_mark_dirty(dev, &sd, addr, (uint32_t) w * bpp);
    }
done:
    r128_surf_commit(dev, &sd);
    rage128_2d_vq_end(dev);
}

/* Span fill for packet 0x98 (R128_CCE_PACKET3_CNTL_POLYSCANLINES): rows
   of the solid or pattern brush through the ROP3. Each entry is a span
   count, (h << 16) | y, then (x_right << 16) | x_left per span (SDK:
   Table F-28, p. F-39 / PDF 329), with x_right excluded. The SDK does
   not say whether the end is included; the exclusive end is taken from
   captured Windows traffic: a window's 3D frame drawn as nested 1-pixel
   spans (top edge 230..617 in one color, right edge column 617 in
   another, inner area 231..616) fits only if x_right is excluded. */
static void
rage128_2d_span_rect(rage128_t *dev, int y, int h, int xl, int xr,
                     const uint8_t *mono8x8, const uint8_t *col8x8, int mono_la)
{
    if (xr <= xl || h <= 0)
        return;
    rage128_2d_paint_rect(dev, xl, y, xr - xl, h, mono8x8, col8x8, mono_la);
}

/* Parse the brush data that follows the setup dwords. The dword count
   and order per brush type are those of the SDK (SDK: Table F-2,
   pp. F-16-F-17 / PDF 306-307); the Windows 98 display driver uses
   types 0, 1, 6, 7, 10, 13 and 15 (RE: ati2draa.drv @6b95, a file
   offset):
       15 -> nothing      13 -> foreground
     0, 1 -> [background] foreground, two 8x8 mono pattern dwords
     2, 3 -> [background] foreground, one 8x1 mono row
     4, 5 -> [background] foreground, one 1x8 mono column
     6, 7 -> [background] foreground, one 32x1 line pattern dword
     8, 9 -> [background] foreground, 32 rows of the 32x32 mono pattern
       10 -> 16 * bytes per pixel dwords: 8x8 color pattern in the
             destination format (64 dwords at 24 bpp)
   11, 12 -> 2 * bytes per pixel dwords: 8x1 or 1x8 color pattern
   The background color is sent only for the even type; the odd type
   leaves clear cells alone. Returns 0 when the packet is too short and
   for the reserved type 14. */
static int
rage128_2d_brush_block(rage128_t *dev, uint32_t g, const uint32_t *pl,
                       uint32_t count, uint32_t *pp,
                       const uint8_t **mono, const uint8_t **col,
                       int *mono_la, uint32_t *linepat, int *line_en)
{
    uint32_t p  = *pp;
    uint32_t bt = RAGE128_GMC_BRUSH_TYPE(g);

    *mono    = NULL;
    *col     = NULL;
    *mono_la = 0;
    *linepat = 0;
    *line_en = 0;
    switch (bt) {
        case 15:
            break;
        case 13:
            if (p >= count)
                return 0;
            dev->dp_brush_frgd_clr = pl[p++];
            break;
        case 0:
            if (p + 3 >= count)
                return 0;
            dev->dp_brush_bkgd_clr = pl[p++];
            dev->dp_brush_frgd_clr = pl[p++];
            /* Copy the pattern to BRUSH_DATA0 and BRUSH_DATA1 so a later
               paint from the latched state (packets 0x11 and 0x1d, or a
               register trigger) uses the same brush as this packet. */
            dev->brush_data[0] = pl[p];
            dev->brush_data[1] = pl[p + 1];
            *mono              = (const uint8_t *) &pl[p];
            p += 2;
            break;
        case 1:
            if (p + 2 >= count)
                return 0;
            dev->dp_brush_frgd_clr = pl[p++];
            dev->brush_data[0]     = pl[p];
            dev->brush_data[1]     = pl[p + 1];
            *mono                  = (const uint8_t *) &pl[p];
            *mono_la               = 1;
            p += 2;
            break;
        case 10:
            {
                uint32_t bpp = (uint32_t) rage128_2d_bpp(RAGE128_GMC_DST_DATATYPE(g));
                uint32_t nb  = (bpp == 3) ? 64u : (64u * bpp + 3) / 4;

                if (p + nb > count)
                    return 0;
                *col = (const uint8_t *) &pl[p];
                if (bpp == 3) {
                    /* "a 24-BPP pixel is still represented by 4 bytes"
                       (SDK: Table F-2, p. F-17 / PDF 307); the pixel is the
                       low 3 bytes of its dword, blue in [7:0] as in the
                       color fields of Table F-4. */
                    for (uint32_t i = 0; i < 64 * 3; i++)
                        dev->brush_tile[i] = (uint8_t) (pl[p + i / 3] >> ((i % 3) * 8));
                    *col = dev->brush_tile;
                }
                p += nb;
                break;
            }
        case 6:
            if (p + 2 >= count)
                return 0;
            dev->dp_brush_bkgd_clr = pl[p++];
            dev->dp_brush_frgd_clr = pl[p++];
            *linepat               = pl[p++];
            *line_en               = 1;
            break;
        case 7:
            if (p + 1 >= count)
                return 0;
            dev->dp_brush_frgd_clr = pl[p++];
            *linepat               = pl[p++];
            *line_en               = 1;
            *mono_la               = 1;
            break;
        case 2:
        case 3:
        case 4:
        case 5:
            {
                /* 8 pixels in the low bits of the one pattern dword ("the
                   pixels take the lower bits", SDK: Table F-4, p. F-18 /
                   PDF 308), expanded to the 8x8 tile they repeat as. The
                   1x8 column runs down rows 0-7 in the bit order a row byte
                   uses. */
                uint32_t m;

                if (p + ((bt & 1) ? 1u : 2u) >= count)
                    return 0;
                if (!(bt & 1))
                    dev->dp_brush_bkgd_clr = pl[p++];
                dev->dp_brush_frgd_clr = pl[p++];
                m                      = pl[p++] & 0xffu;
                for (int r = 0; r < 8; r++) {
                    int sh = rage128_dp_lsb_first(dev) ? r : 7 - r;

                    dev->brush_tile[r] = (bt <= 3) ? (uint8_t) m
                                                   : (uint8_t) (((m >> sh) & 1u) ? 0xffu : 0x00u);
                }
                *mono    = dev->brush_tile;
                *mono_la = (int) (bt & 1);
                break;
            }
        case 8:
        case 9:
            if (p + ((bt & 1) ? 32u : 33u) >= count)
                return 0;
            if (!(bt & 1))
                dev->dp_brush_bkgd_clr = pl[p++];
            dev->dp_brush_frgd_clr = pl[p++];
            *mono                  = (const uint8_t *) &pl[p];
            *mono_la               = (int) (bt & 1);
            p += 32;
            break;
        case 11:
        case 12:
            {
                /* 8 destination-format pixels (2 * N dwords, N bytes per
                   pixel from SDK: Table F-3, pp. F-17-F-18 / PDF 307-308),
                   expanded to the 8x8 color tile they repeat as. */
                uint32_t       bpp = (uint32_t) rage128_2d_bpp(RAGE128_GMC_DST_DATATYPE(g));
                const uint8_t *src = (const uint8_t *) &pl[p];

                if (p + 2u * bpp > count)
                    return 0;
                for (uint32_t r = 0; r < 8; r++)
                    for (uint32_t c = 0; c < 8; c++)
                        memcpy(&dev->brush_tile[(r * 8 + c) * bpp],
                               &src[(bt == 11 ? c : r) * bpp], bpp);
                *col = dev->brush_tile;
                p += 2u * bpp;
                break;
            }
        default:
            return 0;
    }
    /* GMC_LD_BRUSH_Y_X (bit 31): a BRUSH_Y_X dword follows the brush data,
       before the geometry, and the register is loaded from it (RRG:
       DP_GUI_MASTER_CNTL, p. 3-175 / PDF 193; SDK: Table F-1, p. F-16 /
       PDF 306). Without the bit the latched value stays. */
    if (g & RAGE128_GMC_LD_BRUSH_Y_X) {
        if (p >= count)
            return 0;
        dev->brush_yx = pl[p++];
    }
    *pp = p;
    return 1;
}

/* --- CCE type-3 packet parsers, one per opcode family ---
   What follows the leading GMC dword depends on that dword. A setup
   dword is present only when its GMC control bit says the packet
   supplies that state: bit 0 the source pitch and offset, bit 1 the
   destination pitch and offset, bit 2 one source scissor bottom-right
   dword, bit 3 the destination scissor pair (SDK: Table 4-16 and Table
   F-1, pp. F-12-F-16 / PDF 302-306). Then comes the brush data for the
   GMC brush type, then the geometry. Coordinates are x [13:0] and
   y [29:16], signed 14 bits, as in DST_Y_X and SC_TOP_LEFT (RRG:
   DST_Y_X, p. 3-139 / PDF 157; RRG: SC_TOP_LEFT, p. 3-162 / PDF 180).
   The layouts also match the packets the Windows 98 display driver
   builds.
   A parser that can meet a malformed payload returns 1 = consumed, 0 =
   malformed (the dispatcher then logs one UNHANDLED line);
   pkt3_paint_nc has no such case and returns nothing. g = pl[0], the
   GMC dword, where the packet has one. */

/* 0x91 PAINT / 0x9a PAINT_MULTI / 0x95 POLYLINE / 0x98 SPANLIST */
static int
pkt3_paint(rage128_t *dev, uint32_t op, uint32_t g, const uint32_t *pl, uint32_t count)
{
    uint32_t       p   = 1;
    const uint8_t *pat = NULL;
    const uint8_t *col = NULL;
    int            mono_la, line_en;
    uint32_t       linepat;

    rage128_2d_apply_gmc(dev, g);
    if (g & RAGE128_GMC_DST_PITCH_OFFSET_LEAVE) {
        if (p >= count)
            return 0;
        rage128_2d_set_pitch_offset(dev, 1, pl[p++]);
    }
    if (g & RAGE128_GMC_DST_CLIP_LEAVE) {
        if (p + 1 >= count)
            return 0;
        dev->sc_top_left     = pl[p++];
        dev->sc_bottom_right = pl[p++];
    }
    if (!rage128_2d_brush_block(dev, g, pl, count, &p,
                                &pat, &col, &mono_la, &linepat, &line_en))
        return 0;
    if (op == RAGE128_PM4_OP_POLYLINE) {
        /* Connected points, y << 16 | x (x in [13:0]). The Windows
           98 display driver copies them straight from GDI, one dword
           per point, with no closing point and no endpoint
           adjustment (RE: ati2draa.drv @82c6, a file offset). */
        int phase = (int) ((dev->brush_yx >> 16) & 0x1f);

        for (; p + 1 < count; p++)
            rage128_2d_line(dev,
                            rage128_sx14(pl[p]),
                            rage128_sx14(pl[p] >> 16),
                            rage128_sx14(pl[p + 1]),
                            rage128_sx14(pl[p + 1] >> 16),
                            linepat, line_en, mono_la, &phase);
        rage128_2d_line_phase_done(dev, line_en, phase);
        return 1;
    }
    if (op == RAGE128_PM4_OP_SPANLIST) {
        /* An entry count, then per entry: a span count,
           (h << 16) | y, and (x_right << 16) | x_left per span
           (SDK: Tables F-27 and F-28, p. F-39 / PDF 329), x_right
           excluded. The Windows 98 display driver sends
           either one entry of height 1 with up to wCount - 1 spans
           (its style 4) or several entries with h = y1 - y0 (its
           style 5); a span count of 0 is allowed (RE: ati2draa.drv
           @bb8d and @bea6, file offsets). */
        uint32_t nent;

        if (p >= count)
            return 1; /* a setup-only 0x98 (the Windows 2000 driver's
                         DrvFillPath sends one): it latches the state for
                         the 0x1d packets that follow; nothing to draw,
                         and not an UNHANDLED */
        nent = pl[p++];
        for (uint32_t e = 0; e < nent && p + 1 < count; e++) {
            uint32_t nspan = pl[p++];
            int      yy    = rage128_sx14(pl[p]);
            int      hh    = (pl[p] >> 16) & 0x3fff;

            p++;
            for (uint32_t s = 0; s < nspan && p < count; s++, p++)
                rage128_2d_span_rect(dev, yy, hh,
                                     rage128_sx14(pl[p]),
                                     rage128_sx14(pl[p] >> 16),
                                     pat, col, mono_la);
        }
        return 1;
    }
    for (; p + 1 < count; p += 2) {
        int x, y, w, h;

        if (op == RAGE128_PM4_OP_PAINT) {
            /* Corner pair: top-left and bottom-right, each
               y << 16 | x (x in [13:0]) (SDK: Table F-7, p. F-20 /
               PDF 310). The SDK does not say whether the bottom-right
               corner is included; it is excluded here. In captured
               traffic both ATI 3D drivers (Direct3D and
               OpenGL) clear a W x H surface with bottom-right (W, H)
               and place the next allocation right after it; reading
               the corner as inclusive paints row H over that
               allocation, which showed as stripes across Quake 3
               textures. The scissor stays inclusive (the display
               driver's builders subtract 1 only from it). */
            x = rage128_sx14(pl[p]);
            y = rage128_sx14(pl[p] >> 16);
            w = rage128_sx14(pl[p + 1]) - x;
            h = rage128_sx14(pl[p + 1] >> 16) - y;
        } else {
            /* Position x << 16 | y and size w << 16 | h (SDK: Table
               F-32, pp. F-41-F-42 / PDF 331-332), exact sizes: in
               captured traffic all 368 such rects fit the scissor
               sent with them. */
            x = rage128_sx14(pl[p] >> 16);
            y = rage128_sx14(pl[p]);
            w = (pl[p + 1] >> 16) & 0x3fff;
            h = pl[p + 1] & 0x3fff;
        }
        rage128_2d_paint_rect(dev, x, y, w, h, pat, col, mono_la);
    }
    return 1;
}

/* Packet 0x11 (RAGE128_PM4_OP_PAINT_NC; R128_CCE_PACKET3_PAINT in
   xf86-video-r128): a paint with the latched GUI state. There
   is no GMC dword, so rage128_2d_apply_gmc is not called (its default
   reloads would move the destination and the scissor). The payload is
   only top-left and bottom-right pairs, bottom-right excluded, whatever
   bits 1 and 3 of the latched GMC say: in captured Windows 98 traffic
   the display driver sends 0x11 with 2 dwords while those bits are set,
   so no setup dwords are read here. There is no brush data either: the
   brush is the latched one, and DP_BRUSH_DATATYPE 0 and 1 take the 8x8
   mono pattern from BRUSH_DATA0 and BRUSH_DATA1, as the paint started by
   register writes does. */
static void
pkt3_paint_nc(rage128_t *dev, const uint32_t *pl, uint32_t count)
{
    uint32_t bt       = (uint32_t) rage128_dp_brush_dt(dev);
    int      pat_mono = (bt == 0 || bt == 1);

    for (uint32_t pp = 0; pp + 1 < count; pp += 2) {
        int x = rage128_sx14(pl[pp]);
        int y = rage128_sx14(pl[pp] >> 16);
        int w = rage128_sx14(pl[pp + 1]) - x; /* corner excluded, as 0x91 */
        int h = rage128_sx14(pl[pp + 1] >> 16) - y;

        rage128_2d_paint_rect(dev, x, y, w, h,
                              pat_mono ? (const uint8_t *) dev->brush_data
                                       : NULL,
                              NULL, bt == 1);
    }
}

/* Packet 0x1b (RAGE128_PM4_OP_BITBLT_NC; R128_CCE_PACKET3_BITBLT_MULTI
   in xf86-video-r128): a blit with no DP_GUI_MASTER_CNTL
   dword and no setup dwords, using the GUI state latched now (the
   driver writes DP_GUI_MASTER_CNTL, SRC_PITCH_OFFSET and
   DST_PITCH_OFFSET with type-0 packets first). The payload is only
   triples in the 0x92 / 0x28 coordinate layout. The Windows 2000 OpenGL
   driver presents every frame this way (RE: atioglaa.dll @69005e1c and
   @69063a44, header 0xc0021b00); if the packet is dropped, the driver's
   swap chain stalls and the game stops. */
static int
pkt3_bitblt_nc(rage128_t *dev, const uint32_t *pl, uint32_t count)
{
    uint32_t q = 0;

    if (count % 3)
        return 0;
    for (; q + 2 < count; q += 3) {
        int sx = rage128_sx14(pl[q] >> 16);
        int sy = rage128_sx14(pl[q]);
        int dx = rage128_sx14(pl[q + 1] >> 16);
        int dy = rage128_sx14(pl[q + 1]);
        int w  = (pl[q + 2] >> 16) & 0x3fff;
        int h  = pl[q + 2] & 0x3fff;

        rage128_2d_blit_rect(dev, sx, sy, dx, dy, w, h);
    }
    return 1;
}

/* Packets 0x28 (RAGE128_PM4_OP_BLIT_MULTI), 0x92
   (RAGE128_PM4_OP_CNTL_BITBLT) and 0x9b (RAGE128_PM4_OP_BITBLT_MULTI):
   the setup dwords the GMC asks for, then (src x_y, dst x_y,
   width_height) triples. */
static int
pkt3_blit_multi(rage128_t *dev, uint32_t g, const uint32_t *pl, uint32_t count)
{
    uint32_t       p   = 1;
    const uint8_t *pat = NULL;
    const uint8_t *col = NULL;
    int            mono_la, line_en;
    uint32_t       linepat;

    rage128_2d_apply_gmc(dev, g);
    if (g & RAGE128_GMC_SRC_PITCH_OFFSET_LEAVE) {
        if (p >= count)
            return 0;
        rage128_2d_set_pitch_offset(dev, 0, pl[p++]);
    }
    if (g & RAGE128_GMC_DST_PITCH_OFFSET_LEAVE) {
        if (p >= count)
            return 0;
        rage128_2d_set_pitch_offset(dev, 1, pl[p++]);
    }
    if (g & RAGE128_GMC_SRC_CLIP_LEAVE) {
        /* One dword: the source scissor bottom-right. The source
           scissor has no top-left: its default load sets only
           SRC_SC_RIGHT and SRC_SC_BOTTOM (RRG: DP_GUI_MASTER_CNTL,
           p. 3-172 / PDF 190), and the packet carries one dword for
           it (SDK: Table F-1, p. F-15 / PDF 305).
           The Windows 98 display driver always sends the source
           origin plus the size, without subtracting 1 (RE:
           ati2draa.drv @4e70, a file offset), so it never clips
           inside the blit; it is only latched. */
        if (p >= count)
            return 0;
        dev->src_sc_right  = pl[p] & 0x3fff;
        dev->src_sc_bottom = (pl[p] >> 16) & 0x3fff;
        p++;
    }
    if (g & RAGE128_GMC_DST_CLIP_LEAVE) {
        /* Destination scissor pair. Unlike its 0x94, 0x95 and 0x98
           builders, the Windows 98 display driver's blit builder
           does not subtract 0x10001 (RE: ati2draa.drv @4e8b, a file
           offset): the bottom-right it sends is the destination plus
           the size, one past the rect. Read as inclusive, that is one
           pixel of slack, never a wrong clip. */
        if (p + 1 >= count)
            return 0;
        dev->sc_top_left     = pl[p++];
        dev->sc_bottom_right = pl[p++];
    }
    /* ROPs that use both pattern and source (0xB8 and 0xE2, for
       example) carry brush data before the triples (RE:
       ati2draa.drv @4fe2, a file offset); the parsed mono or color
       pattern is the blit's pattern operand per destination pixel. */
    if (!rage128_2d_brush_block(dev, g, pl, count, &p,
                                &pat, &col, &mono_la, &linepat, &line_en))
        return 0;
    (void) linepat;
    (void) line_en;
    if ((count - p) % 3)
        return 0;
    /* (src x_y, dst x_y, width_height) triples, x and width in [29:16]
       (SDK: Table F-34, p. F-42 / PDF 332). */
    for (; p + 2 < count; p += 3) {
        int sx = rage128_sx14(pl[p] >> 16);
        int sy = rage128_sx14(pl[p]);
        int dx = rage128_sx14(pl[p + 1] >> 16);
        int dy = rage128_sx14(pl[p + 1]);
        int w  = (pl[p + 2] >> 16) & 0x3fff;
        int h  = pl[p + 2] & 0x3fff;

        rage128_2d_blit_rect_pat(dev, sx, sy, dx, dy, w, h,
                                 pat, col, mono_la);
    }
    return 1;
}

/* Packet 0x9c (R128_CCE_PACKET3_CNTL_TRANS_BITBLT in xf86-video-r128):
   a transparent, color-keyed blit. The payload is the GMC, the setup
   dwords its bits ask for, the brush block for its brush type, then
   six dwords: CLR_CMP_CNTL, the source reference color, the
   destination reference color, the source and destination positions
   and the size (SDK: Table 4-16, Table F-1, pp. F-12-F-16 / PDF
   302-306; SDK: Table F-36, p. F-43 / PDF 333). The setup and brush
   parse is the one every other 2D packet uses, so the data block
   starts where the GMC puts it and a length alone never changes which
   slot is which. The forms the Windows drivers send all meet that
   grammar: the Windows 98 DirectDraw/Direct3D driver sets all four
   setup bits with no brush, 12 dwords (RE: ati3draa.dll @b00ba268,
   header 0xc00b9c00; the slots are filled by @b00bb3e0); the Windows
   2000 display driver sets bits 0 and 1 for its DirectDraw source-keyed
   blit, 9 dwords (RE: ati2dvaa.dll @18824, header 0xc0089c00), and
   bits 0, 1 and 3 for DrvTransparentBlt, 11 dwords, one packet per
   clip rect with the scissor pair set to that rect, bottom-right
   inclusive (RE: ati2dvaa.dll @2a55c, header 0xc00a9c00).
   The SDK puts x in [15:0] of the two positions; both drivers put x in
   [29:16], as SRC_X_Y and DST_X_Y do, and the code follows them. The
   key test comes from the CLR_CMP_CNTL value in the payload
   (0x01000005 from the Windows 98 driver: source compare, function 5,
   draw when not equal), not from the GMC, whose GMC_CLR_CMP_CNTL_DIS
   clears the compare functions first; GMC_WR_MSK_DIS leaves CLR_CMP_MSK
   all ones. The Windows 2000 driver leaves leftover data or 0 in the
   destination reference slot, which is harmless while its destination
   compare function is 0. */
static int
pkt3_trans_bitblt(rage128_t *dev, uint32_t g, const uint32_t *pl, uint32_t count)
{
    uint32_t       p   = 1;
    const uint8_t *pat = NULL;
    const uint8_t *col = NULL;
    int            mono_la, line_en;
    uint32_t       linepat;
    int            sx, sy, dx, dy, w, h;

    rage128_2d_apply_gmc(dev, g);
    if (g & RAGE128_GMC_SRC_PITCH_OFFSET_LEAVE) {
        if (p >= count)
            return 0;
        rage128_2d_set_pitch_offset(dev, 0, pl[p++]);
    }
    if (g & RAGE128_GMC_DST_PITCH_OFFSET_LEAVE) {
        if (p >= count)
            return 0;
        rage128_2d_set_pitch_offset(dev, 1, pl[p++]);
    }
    if (g & RAGE128_GMC_SRC_CLIP_LEAVE) {
        /* One dword, the source scissor bottom-right, latched as in
           pkt3_bitblt. The Windows 98 driver fills it with the blit's
           own source rect. */
        if (p >= count)
            return 0;
        dev->src_sc_right  = pl[p] & 0x3fff;
        dev->src_sc_bottom = (pl[p] >> 16) & 0x3fff;
        p++;
    }
    if (g & RAGE128_GMC_DST_CLIP_LEAVE) {
        /* The destination scissor pair, latched so that the stretch
           packets, which carry the same slots, do not leave their rect
           as the scissor of the next blit. The Windows 98 driver fills
           it with the blit's own destination rect, bottom-right one
           past the rect; read as inclusive that is one pixel of slack,
           never a wrong clip. */
        if (p + 1 >= count)
            return 0;
        dev->sc_top_left     = pl[p++];
        dev->sc_bottom_right = pl[p++];
    }
    if (!rage128_2d_brush_block(dev, g, pl, count, &p,
                                &pat, &col, &mono_la, &linepat, &line_en))
        return 0;
    (void) linepat;
    (void) line_en;
    if (count != p + 6)
        return 0;
    dev->clr_cmp_cntl    = pl[p];
    dev->clr_cmp_clr_src = pl[p + 1]; /* the key */
    dev->clr_cmp_clr_dst = pl[p + 2];
    sx                   = rage128_sx14(pl[p + 3] >> 16);
    sy                   = rage128_sx14(pl[p + 3]);
    dx                   = rage128_sx14(pl[p + 4] >> 16);
    dy                   = rage128_sx14(pl[p + 4]);
    w                    = (pl[p + 5] >> 16) & 0x3fff;
    h                    = pl[p + 5] & 0x3fff;
    if (w <= 0 || h <= 0)
        return 1;
    /* A source-only compare with function 4 or 5 takes the keyed
       walk (its queue path and its rule of no row buffer for overlap
       were checked against captured traffic); any other latched
       compare state runs through the ordinary blit, whose output
       stage applies the same test, with the packet's brush as the
       pattern operand the way pkt3_bitblt runs it. */
    {
        int kb = rage128_2d_bpp(
            rage128_dp_dst_dt(dev));
        struct r128_ccmp cc;
        int              cca = rage128_2d_ccmp_setup(dev, 1, kb, kb, &cc);

        if (cca && cc.src_on && (cc.fn_src == 4 || cc.fn_src == 5)
            && (!cc.dst_on || cc.fn_dst == 0))
            rage128_2d_blit_rect_key(dev, sx, sy, dx, dy, w, h,
                                     cc.key_src, cc.smask,
                                     cc.fn_src == 5);
        else
            rage128_2d_blit_rect_pat(dev, sx, sy, dx, dy, w, h,
                                     pat, col, mono_la);
    }
    return 1;
}

/* Packet 0x93 (RAGE128_PM4_OP_SMALLTEXT): the foreground color, the
   base position y << 16 | x (y is the text baseline), then per glyph a
   geometry dword (dX [7:0], the step from the previous glyph; dY [15:8],
   the glyph top's distance above the baseline; W [23:16]; H [31:24],
   which the SDK prints as [31:25]) and ceil(W * H / 32) dwords of
   bit-packed rows, MSB first (SDK: Tables F-9 and F-10, pp. F-21-F-22 /
   PDF 311-312). The SDK does not say dX
   and dY are signed; they are read as signed bytes because in captured
   Windows 98 traffic 0xff means -1 (a kerning step, or ink below the
   baseline: an underscore is a 6x1 glyph with dY -1). Read unsigned,
   they put the pen or the glyph outside the scissor and the text
   disappears. */
static int
pkt3_smalltext(rage128_t *dev, uint32_t g, const uint32_t *pl, uint32_t count)
{
    uint32_t p = 1;
    int      x, y;

    rage128_2d_apply_gmc(dev, g);
    if (g & RAGE128_GMC_DST_PITCH_OFFSET_LEAVE) {
        if (p >= count)
            return 0;
        rage128_2d_set_pitch_offset(dev, 1, pl[p++]);
    }
    if (g & RAGE128_GMC_DST_CLIP_LEAVE) {
        if (p + 1 >= count)
            return 0;
        dev->sc_top_left     = pl[p++];
        dev->sc_bottom_right = pl[p++];
    }
    if (p + 1 >= count)
        return 0;
    dev->dp_src_frgd_clr = pl[p++]; /* glyph color: DP_SRC_FRGD_CLR */
    x                    = rage128_sx14(pl[p]);
    y                    = rage128_sx14(pl[p] >> 16);
    p++;
    while (p < count) {
        uint32_t geom   = pl[p++];
        int      adv    = (int8_t) (geom & 0xff);
        int      ascent = (int8_t) ((geom >> 8) & 0xff);
        int      w      = (geom >> 16) & 0xff;
        int      h      = (geom >> 24) & 0xff;
        uint32_t nd     = ((uint32_t) w * h + 31) / 32;

        x += adv;
        if (w == 0 || h == 0) /* space: pen advance, no bitmap */
            continue;
        if (count - p < nd) {
            break;
        }
        rage128_2d_mono_rect(dev, x, y - ascent, w, h,
                             (const uint8_t *) &pl[p], (uint32_t) w,
                             NULL, NULL, 0);
        p += nd;
    }
    return 1;
}

/* Packet 0x94 (R128_CCE_PACKET3_CNTL_HOSTDATA_BLT in xf86-video-r128):
   bitmaps sent inline, mono or color (SDK: Tables F-12 and
   F-13, pp. F-24-F-25 / PDF 314-315). */
static int
pkt3_hostdata_blt(rage128_t *dev, uint32_t g, const uint32_t *pl, uint32_t count)
{
    uint32_t       p = 1;
    int            x, y, w, h;
    uint32_t       nd;
    uint32_t       srcdt;
    const uint8_t *bpat = NULL;
    const uint8_t *bcol = NULL;
    int            bmono_la, bline_en;
    uint32_t       blinepat;

    rage128_2d_apply_gmc(dev, g);
    if (g & RAGE128_GMC_DST_PITCH_OFFSET_LEAVE) {
        if (p >= count)
            return 0;
        rage128_2d_set_pitch_offset(dev, 1, pl[p++]);
    }
    if (g & RAGE128_GMC_SRC_CLIP_LEAVE) {
        /* One dword, the source scissor bottom-right, the same slot
           the blit packets carry. The Windows 98 display driver's
           upload of a device-independent bitmap to 8 bpp (a
           palette-index image staged for the scaler) sends it; the
           stamp forms do not. If it were not read, the destination
           scissor pair and the color pair would shift by one dword
           and the size would read as 0x0. */
        if (p >= count)
            return 0;
        dev->src_sc_right  = pl[p] & 0x3fff;
        dev->src_sc_bottom = (pl[p] >> 16) & 0x3fff;
        p++;
    }
    if (g & RAGE128_GMC_DST_CLIP_LEAVE) {
        if (p + 1 >= count)
            return 0;
        dev->sc_top_left     = pl[p++];
        dev->sc_bottom_right = pl[p++];
    }
    srcdt = RAGE128_GMC_SRC_DATATYPE(g);
    if (RAGE128_GMC_SRC_SOURCE(g) == 3 && srcdt == 1) {
        /* The large-glyph text form (the Windows 98 display driver's
           ExtTextOut path for large fonts, RE: ati2draa.drv @7b2e, a
           file offset). The foreground color, then the background
           color slot (unused for leave-alone mono, and sent as 0),
           then per glyph: y << 16 | x, (h << 16) | w, the dword
           count, and that many dwords of rows packed one after
           another with no row padding (SDK: Tables F-12 and F-13,
           pp. F-24-F-25 / PDF 314-315). The test is srcdt == 1 rather
           than srcdt != 3: srcdt 0 is the BitBlt mono stamp, which
           carries its own layout and takes the path below. A 6-dword
           srcdt 1 setup packet from the Windows 2000 display driver,
           sent before its 0x19 rows, also lands here: the colors are
           latched and there are no glyph records. */
        if (p + 1 >= count)
            return 0;
        dev->dp_src_frgd_clr = pl[p++];
        p++; /* background color slot, unused here */
        while (p + 2 < count) {
            x  = rage128_sx14(pl[p]);
            y  = rage128_sx14(pl[p] >> 16);
            h  = (pl[p + 1] >> 16) & 0x3fff;
            w  = pl[p + 1] & 0x3fff;
            nd = pl[p + 2];
            p += 3;
            if (w <= 0 || h <= 0 || count - p < nd)
                return 0;
            rage128_2d_mono_rect(dev, x, y, w, h,
                                 (const uint8_t *) &pl[p], (uint32_t) w,
                                 NULL, NULL, 0);
            p += nd;
        }
        return 1;
    }
    /* BitBlt stamp forms. The brush data comes before the color
       pair and goes through the shared parser for every brush type.
       The common stamps carry type 13 (one solid pattern dword) or
       15 (none); in captured Windows 98 traffic at 16 bpp, patterned
       window fills arrive as type 10 stamps with ROP 0xE2 (GMC
       0x74e204aa). */
    if (!rage128_2d_brush_block(dev, g, pl, count, &p,
                                &bpat, &bcol, &bmono_la,
                                &blinepat, &bline_en))
        return 0;
    if (p + 4 >= count)
        return 0;
    /* Source color pair, foreground first (SDK: Table F-12, p. F-24 /
       PDF 314). The Windows 98 display driver stores DRAWMODE.bkColor
       then TextColor there (RE: ati2draa.drv @5988, a file offset):
       GDI expands mono 1 bits to bkColor, and the engine expands 1
       bits to DP_SRC_FRGD_CLR, so the first slot is the foreground. */
    dev->dp_src_frgd_clr = pl[p++];
    dev->dp_src_bkgd_clr = pl[p++];
    x                    = rage128_sx14(pl[p]);
    y                    = rage128_sx14(pl[p] >> 16);
    /* The size dword is height << 16 | width (SDK: Table F-13, p. F-24
       / PDF 314). The Windows 98 display driver builds it by rotating
       (w << 16) | h by 16 bits (RE: ati2draa.drv @59a1, a file
       offset), and the one non-square case in captured traffic
       confirms the order (GMC 0x742202fa, size 0x00140398, scissor
       (0,0)-(919,19), 575 dwords = 920 x 20 bits). */
    h  = (pl[p + 1] >> 16) & 0x3fff;
    w  = pl[p + 1] & 0x3fff;
    nd = pl[p + 2];
    p += 3;
    if (w <= 0 || h <= 0)
        return 0;
    if (srcdt == 3) {
        /* Color rows in the destination format. The data is sized
           by the geometry, not by the dword-count slot: the Windows
           display drivers and the Linux DRM write the count in
           dwords (linux r128 DRM r128_state.c r128_cce_dispatch_blit),
           the Windows 98 Direct3D driver writes it in pixels, and
           both are shipping drivers, so the count is not used as a
           length in either unit. Only pixels inside the
           scissor are read, so the bound is ((rows - 1) * w + px) *
           bpp over the clipped extent. */
        int sc_r   = rage128_sx14(dev->sc_bottom_right);
        int sc_b   = rage128_sx14(dev->sc_bottom_right >> 16);
        int last_x = x + w - 1;
        int last_y = y + h - 1;

        if (last_x > sc_r)
            last_x = sc_r;
        if (last_y > sc_b)
            last_y = sc_b;
        if (last_x < x || last_y < y)
            return 1; /* fully scissored out */
        /* A payload shorter than the clipped geometry is the Windows
           98 Direct3D driver's small-mipmap upload (the size gives
           the 8-pixel tile, the data only the level's real pixels);
           rage128_2d_host_color_rect limits the write to the dwords
           present. */
        rage128_2d_host_color_rect(dev, x, y, w, h,
                                   (const uint8_t *) &pl[p],
                                   (count - p) * 4, bpat, bcol, bmono_la);
    } else {
        /* Mono rows, each starting on a byte boundary. Every mono
           0x94 stamp from the Windows 98 display driver has srcdt 0
           (GMC_SRC_DATATYPE; value 2 is not defined, RRG:
           DP_GUI_MASTER_CNTL, p. 3-174 / PDF 192). Here the count
           slot is the number of packed dwords, as the 920 x 20 case
           above shows (575 dwords). */
        if (count - p < nd
            || (uint64_t) nd * 4 < (uint64_t) ((((uint32_t) w + 7) & ~7u) / 8) * (uint32_t) h)
            return 0;
        rage128_2d_mono_rect(dev, x, y, w, h, (const uint8_t *) &pl[p],
                             ((uint32_t) w + 7) & ~7u, bpat, bcol, bmono_la);
    }
    return 1;
}

/* Packet 0x96 (R128_CCE_PACKET3_CNTL_SCALING in xf86-video-r128): a
   stretch blit, sent by the drivers' StretchBlt paths.
   After the setup dwords the GMC asks for comes the fixed 11-dword
   scaler block that rage128_2d_scale_block decodes (SDK: Table F-17,
   pp. F-27-F-28 / PDF 317-318). As the Windows 98 display driver
   builds it (RE: ati2draa.drv @8f46, a file offset):
     +0 MISC_3D_STATE (0x100 blend, 0x500 replicate)
     +1 0x00800000, +2 0x02180840 (fixed scaler settings)
     +3 source datatype (4 = RGB 565, 6 = aRGB 8888)
     +4 source byte offset   +5 source pitch in units of 8 pixels
     +6 source start, half-pixel units (0 from this driver)
     +7 X increment          +8 Y increment
     +9 destination x_y (x high)   +10 (h << 16) | w
   The increments are (source size << 16) / destination size with the
   low 4 bits cleared; in captured traffic a 48x48 to 16x16 shrink
   carries 0x30000. */
static int
pkt3_scaling(rage128_t *dev, uint32_t g, const uint32_t *pl, uint32_t count)
{
    uint32_t p = 1;

    rage128_2d_apply_gmc(dev, g);
    if (g & RAGE128_GMC_SRC_PITCH_OFFSET_LEAVE) {
        if (p >= count)
            return 0;
        rage128_2d_set_pitch_offset(dev, 0, pl[p++]);
    }
    if (g & RAGE128_GMC_DST_PITCH_OFFSET_LEAVE) {
        if (p >= count)
            return 0;
        rage128_2d_set_pitch_offset(dev, 1, pl[p++]);
    }
    if (g & RAGE128_GMC_SRC_CLIP_LEAVE) {
        if (p >= count)
            return 0;
        p++; /* source scissor bottom-right, unused (one dword, in
                the same place as in the 0x28 blit) */
    }
    if (g & RAGE128_GMC_DST_CLIP_LEAVE) {
        if (p + 1 >= count)
            return 0;
        dev->sc_top_left     = pl[p++];
        dev->sc_bottom_right = pl[p++];
    }
    if (count - p < 11)
        return 0;
    if (!rage128_2d_scale_block(dev, &pl[p]))
        return 0;
    return 1;
}

/* Packet 0x97 (R128_CCE_PACKET3_CNTL_TRANS_SCALING in xf86-video-r128):
   a color-keyed stretch blit. The SDK layout is the GMC,
   its setup dwords, then CLR_CMP_CNTL, the source and destination
   reference colors and the 11-dword scaler block of 0x96 (SDK: Table
   F-24, pp. F-36-F-37 / PDF 326-327). The code handles only the 20-dword
   form, with all four setup bits set, that the Windows 98
   DirectDraw/Direct3D driver sends when a
   DirectDraw blit asks for a source color key and a stretch at once
   (RE: ati3draa.dll @b00ba7ea, header 0xc0139700); it is to 0x96 what
   0x9c is to 0x92. The slot map comes from the shared slot builder (RE:
   ati3draa.dll @b00bb3e0): p2 is the destination surface in every form,
   and p1, the source surface, is written only by the 0x92 and 0x9c
   paths. The stretch paths leave p1 holding an earlier blit's source,
   so nothing is taken from it:
     p0 GMC                p1  (stale SRC_PITCH_OFFSET)
     p2 DST_PITCH_OFFSET   p3  SRC_SC_BOTTOM_RIGHT
     p4 SC_TOP_LEFT (0)    p5  SC_BOTTOM_RIGHT (destination rect)
     p6 CLR_CMP_CNTL       p7  CLR_CMP_CLR_SRC (raw source texel)
     p8 CLR_CMP_CLR_DST; CLR_CMP_MSK comes from its register or
        GMC_WR_MSK_DIS, not from the packet
     p9..p19 the 0x96 scaler block
   p6 is 0x01000005 (CLR_CMP_SRC 1, CLR_CMP_FN_SRC 5, draw when not
   equal) in the form the driver sends without p8; the form with p8 ORs
   in 0x400 (CLR_CMP_FN_DST 4) and sets CLR_CMP_SRC 2. The driver always
   writes 0x500 (replicate) in the scaler block's MISC_3D_STATE. The
   executor uses the latched color compare state directly; the scaler
   reads its source from p13 and p14, never from SRC_PITCH_OFFSET. */
static int
pkt3_trans_scaling(rage128_t *dev, uint32_t g, const uint32_t *pl, uint32_t count)
{
    if (count < 20)
        return 0;
    rage128_2d_apply_gmc(dev, g);               /* p0 */
    rage128_2d_set_pitch_offset(dev, 1, pl[2]); /* p2 DST_PITCH_OFFSET */
    dev->sc_top_left     = pl[4];               /* p4 SC_TOP_LEFT */
    dev->sc_bottom_right = pl[5];               /* p5 SC_BOTTOM_RIGHT */
    dev->clr_cmp_cntl    = pl[6];
    dev->clr_cmp_clr_src = pl[7];
    dev->clr_cmp_clr_dst = pl[8];
    if (!rage128_2d_scale_block(dev, &pl[9]))
        return 0;
    return 1;
}

/* Packet 0x19 (RAGE128_PM4_OP_HOSTROW; NEXTCHAR in the SDK,
   R128_CCE_PACKET3_NEXT_CHAR in xf86-video-r128): one bitmap
   for the host-data packet before it (0x9a with DP_SRC_SOURCE 4, color,
   from the Windows 98 display driver; 0x94 with DP_SRC_SOURCE 3, mono or
   color, from the Windows 2000 one). The payload is y << 16 | x,
   (h << 16) | w, then the raw data dwords (SDK: Table F-30, p. F-40 /
   PDF 330). There is no GMC dword: the state latched by that earlier
   packet applies. Color rows (DP_SRC_DATATYPE 3) are pixels in the
   destination format. Mono rows (0 or 1) are bit-packed glyphs, w bits
   per row packed one after another with no row padding, MSB first in
   each byte, as captured Windows 2000 glyph traffic shows (GMC
   0x73cc12fa). */
static int
pkt3_hostrow(rage128_t *dev, const uint32_t *pl, uint32_t count)
{
    int      x, y, w, h;
    uint32_t nd, srcdt;

    if (count < 2)
        return 0;
    srcdt = rage128_dp_src_dt(dev);
    x     = rage128_sx14(pl[0]);
    y     = rage128_sx14(pl[0] >> 16);
    h     = (pl[1] >> 16) & 0x3fff;
    w     = pl[1] & 0x3fff;
    nd    = count - 2;
    if (w <= 0 || h <= 0)
        return 0;
    if (srcdt == 3) {
        if (nd * 4 < (uint32_t) w * (uint32_t) h
                * (uint32_t) rage128_2d_bpp(rage128_dp_dst_dt(dev)))
            return 0;
        rage128_2d_host_color_rect(dev, x, y, w, h,
                                   (const uint8_t *) &pl[2],
                                   (count - 2) * 4, NULL, NULL, 0);
    } else {
        /* mono glyph: w bits per row, rows packed back to back */
        if ((uint64_t) nd * 32 < (uint64_t) w * (uint64_t) h)
            return 0;
        rage128_2d_mono_rect(dev, x, y, w, h,
                             (const uint8_t *) &pl[2], (uint32_t) w,
                             NULL, NULL, 0);
    }
    return 1;
}

/* Packet 0x1d (R128_CCE_PACKET3_PLY_NEXTSCAN in xf86-video-r128):
   HEIGHT << 16 | TOP, then one END << 16 | START dash per
   remaining dword (SDK: Table F-38, p. F-45 / PDF 335), END excluded as
   for 0x98, drawn with the latched state; there is no GMC dword. The
   Windows 2000 display driver's DrvFillPath sends one dash of height 1
   (RE: ati2dvaa.dll @21af8, which ORs in 0x10000). Brush types 0 and 1
   expand BRUSH_DATA0 and BRUSH_DATA1, as the paint started by register
   writes does. */
static int
pkt3_ply_nextscan(rage128_t *dev, const uint32_t *pl, uint32_t count)
{
    uint32_t bt       = (uint32_t) rage128_dp_brush_dt(dev);
    int      pat_mono = (bt == 0 || bt == 1);
    int      y, h;

    if (count < 2)
        return 0;
    y = (int) (pl[0] & 0xffff);
    h = (int) (pl[0] >> 16);
    for (uint32_t i = 1; i < count; i++)
        rage128_2d_span_rect(dev, y, h, (int) (pl[i] & 0xffff),
                             (int) (pl[i] >> 16),
                             pat_mono ? (const uint8_t *) dev->brush_data
                                      : NULL,
                             NULL, bt == 1);
    return 1;
}

/* --- CCE type-3 packet dispatch --- */

void
rage128_2d_packet3(rage128_t *dev, uint32_t hdr, const uint32_t *pl, uint32_t count)
{
    uint32_t op = RAGE128_PM4_T3_OPCODE(hdr);
    uint32_t g;

    if (op == RAGE128_PM4_OP_NOP)
        return;
    if (count < 1)
        goto unhandled;
    g = pl[0];

    switch (op) {
        case RAGE128_PM4_OP_PAINT:
        case RAGE128_PM4_OP_PAINT_MULTI:
        case RAGE128_PM4_OP_POLYLINE:
        case RAGE128_PM4_OP_SPANLIST:
            if (!pkt3_paint(dev, op, g, pl, count))
                goto unhandled;
            return;

        case RAGE128_PM4_OP_PAINT_NC:
            pkt3_paint_nc(dev, pl, count);
            return;

        case RAGE128_PM4_OP_BITBLT_NC:
            if (!pkt3_bitblt_nc(dev, pl, count))
                goto unhandled;
            return;

        case RAGE128_PM4_OP_CNTL_BITBLT:
            /* The Windows 98 Direct3D driver's present blit, back
               buffer to front (in captured traffic: GMC 0x32cc34ff,
               one triple, a 640x480 SRCCOPY). Same layout as 0x28. The
               Windows 98 display driver itself does not send 0x92. */
            /* FALLTHROUGH */
        case RAGE128_PM4_OP_BITBLT_MULTI:
            /* 0x9b: the Linux DRM's SwapBuffers present blit, one
               (src x_y, dst x_y, width_height) triple per clip rect,
               back buffer to front (linux r128 DRM r128_state.c
               r128_cce_dispatch_swap). In captured Linux traffic it
               carries GMC 0x72cc36f3, source pitch-offset 0x14025940
               (back buffer at 0x4b2800) and destination 0x14000000
               (front buffer at 0), 300x300. Same layout as 0x28. */
            /* FALLTHROUGH */
        case RAGE128_PM4_OP_BLIT_MULTI:
            if (!pkt3_blit_multi(dev, g, pl, count))
                goto unhandled;
            return;

        case 0x9c: /* TRANS_BITBLT: transparent color-keyed blit */
            if (!pkt3_trans_bitblt(dev, g, pl, count))
                goto unhandled;
            return;

        case RAGE128_PM4_OP_SMALLTEXT:
            if (!pkt3_smalltext(dev, g, pl, count))
                goto unhandled;
            return;

        case 0x94: /* HOSTDATA_BLT: inline bitmap stamp (mono or color) */
            if (!pkt3_hostdata_blt(dev, g, pl, count))
                goto unhandled;
            return;

        case 0x2d: /* PURGE, "Purge the pixel cache"; here before a 0x96 */
        case 0x26: /* not in the SDK; before a 0x96, payload 0x11111111 */
            /* Neither changes state this model keeps. The Windows 98
               display driver sends both only as the fixed 6-dword
               opening before every 0x96 (RE: ati2draa.drv @8ebd, a file
               offset); 0x2d is PURGE in the SDK packet list (SDK: Table
               4-13, pp. F-10-F-11 / PDF 300-301). */
            return;

        case 0x96: /* SCALE: stretch blit (the drivers' StretchBlt paths) */
            if (!pkt3_scaling(dev, g, pl, count))
                goto unhandled;
            return;

        case 0x97: /* TRANS_SCALE: color-keyed stretch blit */
            if (!pkt3_trans_scaling(dev, g, pl, count))
                goto unhandled;
            return;

        case RAGE128_PM4_OP_HOSTROW: /* 0x19: one host-data bitmap */
            if (!pkt3_hostrow(dev, pl, count))
                goto unhandled;
            return;

        case 0x1d: /* PLY_NEXTSCAN: one scanline's dashes, latched state */
            if (!pkt3_ply_nextscan(dev, pl, count))
                goto unhandled;
            return;

        case 0x1e: /* SET_SCISSORS: sets the clip rectangle only */
            /* Two dwords: p0 to SC_TOP_LEFT, p1 to SC_BOTTOM_RIGHT, x in
               [13:0] and y in [29:16] (SDK: Table F-40, p. F-47 / PDF
               337; R128_CCE_PACKET3_SET_SCISSORS in
               xf86-video-r128), both inclusive, the same
               decode as the GMC bit 3 scissor pair; the mapping matches
               captured Windows 2000 traffic. There is no GMC dword and
               no other state changes. */
            if (count < 2)
                goto unhandled;
            dev->sc_top_left     = pl[0];
            dev->sc_bottom_right = pl[1];
            return;

        default:
            break;
    }

unhandled:
    pclog("[r128 UNHANDLED] op=%02x count=%u g=%08x\n",
          op, count, (count >= 1) ? pl[0] : 0);
    /* Unknown or malformed packet: logged and skipped. */
    return;
}

/* --- Direct register file interface (MMIO / IOR / MM_INDEX) --- */

int
rage128_2d_reg_read(rage128_t *dev, uint32_t off, uint32_t *val)
{
    /* BRUSH_DATA0 to BRUSH_DATA31 (0x1480-0x14fc) hold the 32x32 mono
       pattern; the first two also serve as the 8x8 mono pattern. The
       DRM loads a polygon stipple as one type-0 burst of all 32 (linux
       r128 DRM r128_state.c r128_cce_dispatch_stipple). */
    if (off >= RAGE128_BRUSH_DATA0 && off < RAGE128_BRUSH_DATA0 + 32 * 4) {
        *val = dev->brush_data[(off - RAGE128_BRUSH_DATA0) >> 2];
        return 1;
    }
    /* clang-format off */
    switch (off) {
        case RAGE128_DP_GUI_MASTER_CNTL: *val = dev->dp_gui_master_cntl; return 1;
        case RAGE128_DP_BRUSH_BKGD_CLR:  *val = dev->dp_brush_bkgd_clr; return 1;
        case RAGE128_DP_BRUSH_FRGD_CLR:  *val = dev->dp_brush_frgd_clr; return 1;
        case RAGE128_BRUSH_Y_X:          *val = dev->brush_yx;          return 1;
        case RAGE128_DP_SRC_FRGD_CLR:    *val = dev->dp_src_frgd_clr; return 1;
        case RAGE128_DP_SRC_BKGD_CLR:    *val = dev->dp_src_bkgd_clr; return 1;
        case RAGE128_CLR_CMP_CLR_SRC:    *val = dev->clr_cmp_clr_src; return 1;
        case RAGE128_CLR_CMP_CLR_DST:    *val = dev->clr_cmp_clr_dst; return 1;
        case RAGE128_CLR_CMP_CNTL:       *val = dev->clr_cmp_cntl; return 1;
        case RAGE128_CLR_CMP_MASK:       *val = dev->clr_cmp_mask; return 1;
        case RAGE128_AUX_SC_CNTL:        *val = dev->aux_sc_cntl; return 1;
        case RAGE128_AUX1_SC_LEFT ... RAGE128_AUX3_SC_BOTTOM: {
            uint32_t idx = (off - RAGE128_AUX1_SC_LEFT) >> 2;

            *val = dev->aux_sc_rect[idx >> 2][idx & 3];
            return 1;
        }
        case RAGE128_DP_CNTL:            *val = dev->dp_cntl; return 1;
        case RAGE128_DP_CNTL_XDIR_YDIR_YMAJOR: *val = rage128_2d_line_dir(dev); return 1;
        case RAGE128_DP_DATATYPE:        *val = dev->dp_datatype; return 1;
        case RAGE128_DP_MIX:             *val = dev->dp_mix; return 1;
        case RAGE128_DP_WRITE_MASK:      *val = dev->dp_write_mask; return 1;
        case RAGE128_DEFAULT_OFFSET:     *val = dev->default_offset; return 1;
        case RAGE128_DEFAULT_PITCH:      *val = dev->default_pitch; return 1;
        case RAGE128_DEFAULT_SC_BOTTOM_RIGHT: *val = dev->default_sc_bottom_right; return 1;
        case RAGE128_SC_TOP_LEFT:        *val = dev->sc_top_left; return 1;
        case RAGE128_SC_BOTTOM_RIGHT:    *val = dev->sc_bottom_right; return 1;
        /* The single-field registers read the same latches the packed
           forms use, each with its own field mask (RRG: DST_OFFSET,
           pp. 3-137-3-142 / PDF 155-160; RRG: SRC_OFFSET,
           pp. 3-146-3-148 / PDF 164-166; RRG: SC_LEFT, pp. 3-155-3-156
           / PDF 173-174). */
        case RAGE128_DST_OFFSET:  *val = dev->dst_offset & 0x03fffff0u; return 1;
        case RAGE128_SRC_OFFSET:  *val = dev->src_offset & 0x03fffff0u; return 1;
        case RAGE128_DST_PITCH:   *val = dev->dst_pitch_reg; return 1;
        case RAGE128_SRC_PITCH:   *val = dev->src_pitch_reg; return 1;
        case RAGE128_DST_WIDTH:   *val = dev->gui_dst_w; return 1;
        case RAGE128_DST_HEIGHT:  *val = dev->gui_dst_h; return 1;
        case RAGE128_DST_X:       *val = (uint32_t) dev->gui_dst_x & 0x3fff; return 1;
        case RAGE128_DST_Y:       *val = (uint32_t) dev->gui_dst_y & 0x3fff; return 1;
        case RAGE128_SRC_X:       *val = (uint32_t) dev->gui_src_x & 0x3fff; return 1;
        case RAGE128_SRC_Y:       *val = (uint32_t) dev->gui_src_y & 0x3fff; return 1;
        case RAGE128_SC_LEFT:     *val = dev->sc_top_left & 0x3fff; return 1;
        case RAGE128_SC_TOP:      *val = (dev->sc_top_left >> 16) & 0x3fff; return 1;
        case RAGE128_SC_RIGHT:    *val = dev->sc_bottom_right & 0x3fff; return 1;
        case RAGE128_SC_BOTTOM:   *val = (dev->sc_bottom_right >> 16) & 0x3fff; return 1;
        case RAGE128_SRC_SC_RIGHT:  *val = dev->src_sc_right; return 1;
        case RAGE128_SRC_SC_BOTTOM: *val = dev->src_sc_bottom; return 1;
        case RAGE128_PC_GUI_CTLSTAT:
            /* Pixel cache status: PC_BUSY [31] and PC_BUSY_GUI [29] are
               set while the CCE thread has fetched or running work, and
               clear otherwise (RRG: PC_GUI_CTLSTAT, p. 3-253 / PDF 271).
               The guide gives these bits no description; treating
               PC_BUSY as an OR of the busy bits follows GUI_ACTIVE, "'OR'
               of the above bits" (RRG: GUI_STAT, p. 3-244 / PDF 262). */
            *val = rage128_pm4_active(dev) ? 0xa0000000u : 0;
            return 1;
        case RAGE128_SCALE_SCR_HEIGHT_WIDTH:
            *val = dev->scale_scr_height_width;
            return 1;
        /* Readback of the gradient registers at 0x1a40-0x1a60 (not
           named in the guides; see grad_start in vid_ati_rage128.h).
           The Windows 2000 display driver's gradient fill reads them
           back and writes the same values again for each clip rect
           (RE: ati2dvaa.dll @00022184), so a read of 0 here would turn
           every clip rect after the first gray. */
        case 0x1a40: *val = (uint32_t) dev->grad_slope_x[0]; return 1;
        case 0x1a44: *val = (uint32_t) dev->grad_slope_y[0]; return 1;
        case 0x1a48: *val = dev->grad_start[0];              return 1;
        case 0x1a4c: *val = (uint32_t) dev->grad_slope_x[1]; return 1;
        case 0x1a50: *val = (uint32_t) dev->grad_slope_y[1]; return 1;
        case 0x1a54: *val = dev->grad_start[1];              return 1;
        case 0x1a58: *val = (uint32_t) dev->grad_slope_x[2]; return 1;
        case 0x1a5c: *val = (uint32_t) dev->grad_slope_y[2]; return 1;
        case 0x1a60: *val = dev->grad_start[2];              return 1;
        default:
            break;
    }
    /* clang-format on */
    if (off >= RAGE128_GUI_SCRATCH_REG0 && off < RAGE128_GUI_SCRATCH_REG0 + 6 * 4) {
        *val = dev->gui_scratch[(off - RAGE128_GUI_SCRATCH_REG0) >> 2];
        return 1;
    }
    return 0;
}

/* GUI engine ops started by register writes, as the X server's 2D
   driver uses them. The driver writes the setup (GMC, brush, colors,
   SRC_PITCH_OFFSET and DST_PITCH_OFFSET, scissor, SRC_Y_X, DST_Y_X) and
   then a size register, which starts the op (xf86-video-r128 r128_exa.c
   R128Solid and R128Copy; r128_accel.c, XAA version,
   R128SubsequentSolidFillRect and R128SubsequentScreenToScreenCopy).
   DP_SRC_SOURCE picks the op: 2 is a screen-to-screen copy, 3 or 4 a
   host-data op (set up here, its data then streamed through HOST_DATA0
   to HOST_DATA_LAST), anything else a brush fill. The packet path's
   executors do the drawing. */

/* Solid fill of a destination rect with a given color, clipped to the
   scissor. It is the fallback of the scaler-pipe fill (GMC_3D_FCN_EN
   set). There the source pixels come from the 3D/scaler pipe, not from
   memory, so DP_SRC_SOURCE is ignored (RRG: DP_MIX, p. 3-171 / PDF 189).
   The ROP3 of these ops is 0xcc (copy source), so the result is the
   pipe color, written directly; going through rage128_2d_paint_rect
   would apply 0xcc to its fixed source of 0 and paint black. */
static void
rage128_2d_fill_solid(rage128_t *dev, int x, int y, int w, int h, uint32_t color)
{
    int              bpp    = rage128_2d_bpp(rage128_dp_dst_dt(dev));
    uint32_t         wmask  = dev->dp_write_mask;
    int              aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    struct r128_ccmp cc;
    int              cca;
    r128_surf_t      sd;
    int              cx0, cy0, cx1, cy1;

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    if (x < cx0) {
        w -= cx0 - x;
        x = cx0;
    }
    if (y < cy0) {
        h -= cy0 - y;
        y = cy0;
    }
    if (x + w - 1 > cx1)
        w = cx1 - x + 1;
    if (y + h - 1 > cy1)
        h = cy1 - y + 1;
    if (w <= 0 || h <= 0)
        return;
    cca = rage128_2d_ccmp_setup(dev, 0, bpp, bpp, &cc);
    if (!rage128_2d_map_span(dev, &sd, &dev->s2d_dst, dev->dst_offset,
                             dev->dst_pitch, bpp, x, y, y + h - 1, x + w - 1))
        return;

    for (int row = 0; row < h; row++) {
        uint32_t addr = dev->dst_offset + ((uint32_t) (y + row) * rage128_2d_dst_stride(dev, bpp) + x * bpp);

        for (int col = 0; col < w; col++) {
            uint32_t a = addr + (uint32_t) col * bpp;

            if (aux_on && !rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x + col, y + row))
                continue;
            if (cca) {
                uint32_t dpx = 0;

                for (int b = 0; b < bpp; b++)
                    dpx |= (uint32_t) *r128_surf_at(&sd, a + (uint32_t) b)
                        << (b * 8);
                if (!rage128_2d_ccmp_px(&cc, 0, dpx))
                    continue;
            }
            for (int b = 0; b < bpp; b++)
                rage128_2d_store(r128_surf_at(&sd, a + (uint32_t) b),
                                 (uint8_t) (color >> ((b & 3) * 8)),
                                 (uint8_t) (wmask >> ((b & 3) * 8)));
        }
        rage128_2d_mark_dirty(dev, &sd, addr, (uint32_t) w * bpp);
    }
    r128_surf_commit(dev, &sd);
}

/* One gradient pixel: start + slope_x * kx + slope_y * ky per channel
   (8.16 fixed point), clamped, packed to the destination format. The CPU
   walk and the queue path both use it, so the two cannot differ. */
static uint32_t
rage128_2d_grad_px(rage128_t *dev, int kx, int ky)
{
    int ch[3];

    for (int c = 0; c < 3; c++) {
        int64_t acc = (int64_t) dev->grad_start[c]
            + (int64_t) dev->grad_slope_x[c] * kx
            + (int64_t) dev->grad_slope_y[c] * ky;
        int v8 = (int) (acc >> 16);

        ch[c] = v8 < 0 ? 0 : (v8 > 255 ? 255 : v8);
    }
    return rage128_2d_pack_rgb(dev, ch[0], ch[1], ch[2]);
}

/* Queue a gradient fill. The stored bytes depend only on the latched
   gradient values (no source and no destination read: the pipe color is
   written directly), so the rect is worked out into the staging ring
   and queued as copies; the destination is never mapped. With aux off
   the whole rect is one staged copy (so a tall gradient needs no run
   cap); with aux on it is queued as runs per row. 0 = not queued (the
   re-walk stores the same bytes). */
static int
rage128_2d_queue_grad(rage128_t *dev, int x, int y, int w, int h,
                      int kx0, int ky0, int bpp)
{
    if (rage128_2d_tiled_any(dev))
        return 0; /* tiled surfaces take the CPU walk */
    int      aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    uint32_t dpb    = rage128_2d_dst_stride(dev, bpp);
    uint32_t rowlen = (uint32_t) w * (uint32_t) bpp;
    uint32_t daddr0 = dev->dst_offset
        + ((uint32_t) y * rage128_2d_dst_stride(dev, bpp) + (uint32_t) x * (uint32_t) bpp);
    uint64_t dend = (uint64_t) daddr0 + (uint64_t) (h - 1) * dpb + rowlen;
    int      vq, nruns = 0;

    if (x < 0 || y < 0 || r128_card_is_agp(daddr0)
        || dend > (uint64_t) dev->vram_size || (h > 1 && dpb < rowlen)) {
        rage128_gpu_2d_qskip(dev, 7, 32);
        return 0;
    }
    vq = rage128_2d_vq_begin(dev, 7, daddr0, dpb, rowlen, (uint32_t) h);

    if (!aux_on) {
        uint32_t soff;
        uint8_t *st = (uint8_t *) rage128_gpu_2d_stage(dev,
                                                       (uint32_t) h * rowlen, &soff);

        if (!st) {
            rage128_gpu_2d_qskip(dev, 7, 64);
            goto refuse;
        }
        for (int row = 0; row < h; row++)
            for (int col = 0; col < w; col++) {
                uint32_t pix = rage128_2d_grad_px(dev, kx0 + col, ky0 + row);

                for (int b = 0; b < bpp; b++)
                    *st++ = (uint8_t) (pix >> ((b & 3) * 8));
            }
        if (!rage128_gpu_2d_copy(dev, daddr0, dpb, rowlen, (uint32_t) h,
                                 soff, rowlen, 6))
            goto refuse;
        rage128_2d_queue_mark(dev, daddr0, dpb, rowlen, (uint32_t) h);
    } else {
        uint8_t *rb = (uint8_t *) malloc(rowlen);

        if (!rb) {
            rage128_gpu_2d_qskip(dev, 7, 32);
            goto refuse;
        }
        for (int row = 0; row < h; row++) {
            uint32_t daddr = daddr0 + (uint32_t) row * dpb;
            uint32_t rlen  = 0;
            int      rs    = -1;

            for (int col = 0; col < w; col++) {
                int store = rage128_aux_sc_pass_2d(dev->aux_sc_cntl,
                                                   dev->aux_sc_rect,
                                                   x + col, y + row);

                if (store) {
                    uint32_t pix = rage128_2d_grad_px(dev, kx0 + col,
                                                      ky0 + row);

                    if (rs < 0)
                        rs = col;
                    for (int b = 0; b < bpp; b++)
                        rb[rlen++] = (uint8_t) (pix >> ((b & 3) * 8));
                }
                if (rs >= 0 && (!store || col == w - 1)) {
                    uint32_t so;
                    uint8_t *st;

                    if (++nruns > 256) {
                        rage128_gpu_2d_qskip(dev, 7, 8);
                        free(rb);
                        goto refuse;
                    }
                    st = (uint8_t *) rage128_gpu_2d_stage(dev, rlen, &so);
                    if (!st) {
                        rage128_gpu_2d_qskip(dev, 7, 64);
                        free(rb);
                        goto refuse;
                    }
                    memcpy(st, rb, rlen);
                    if (!rage128_gpu_2d_copy(dev,
                                             daddr + (uint32_t) rs * bpp,
                                             rlen, rlen, 1, so, rlen, 6)) {
                        free(rb);
                        goto refuse;
                    }
                    rage128_2d_queue_mark(dev, daddr + (uint32_t) rs * bpp,
                                          rlen, rlen, 1);
                    rs   = -1;
                    rlen = 0;
                }
            }
        }
        free(rb);
    }
    rage128_gpu_2d_qdone(dev, 7);
    if (vq) {
        rage128_2d_vq_after(dev, 1);
        return 0; /* self-check: the CPU walk runs, the end compares */
    }
    return 1;

refuse:
    if (vq)
        rage128_2d_vq_after(dev, 0);
    return 0;
}

/* Gradient fill of a destination rect, as the Windows 2000 display
   driver's DrvGradientFill draws it through the scaler pipe. The color
   per pixel is start + slope_x * kx + slope_y * ky (8.16 fixed point),
   and ROP3 0xcc copies it to the destination. The driver sends the
   gradient values again for each clip rect, so the gradient starts at
   the rect's own top-left before clipping, and k is measured from
   there even when the scissor cuts the rect. In captured traffic,
   horizontal title bars set only the x slopes (y slopes 0) and vertical
   fills set the y slopes. */
static void
rage128_2d_fill_gradient(rage128_t *dev, int x, int y, int w, int h)
{
    int              bpp    = rage128_2d_bpp(rage128_dp_dst_dt(dev));
    uint32_t         wmask  = dev->dp_write_mask;
    int              aux_on = (dev->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    struct r128_ccmp cc;
    int              cca = rage128_2d_ccmp_setup(dev, 1, bpp, bpp, &cc);
    r128_surf_t      sd;
    int              cx0, cy0, cx1, cy1;
    int              kx0 = 0, ky0 = 0;

    rage128_2d_clip(dev, &cx0, &cy0, &cx1, &cy1);
    if (x < cx0) {
        kx0 = cx0 - x;
        w -= cx0 - x;
        x = cx0;
    }
    if (y < cy0) {
        ky0 = cy0 - y;
        h -= cy0 - y;
        y = cy0;
    }
    if (x + w - 1 > cx1)
        w = cx1 - x + 1;
    if (y + h - 1 > cy1)
        h = cy1 - y + 1;
    if (w <= 0 || h <= 0)
        return;

    /* GPU queue path: with the write mask fully open every stored byte
       depends only on the latched gradient values, so the rect is worked
       out into the staging ring and queued as copies
       (rage128_2d_queue_grad). With synctel on, the CPU walk is kept so
       the access record stays complete. */
    if (dev->gpu && !dev->synctel) {
        if (wmask == 0xffffffffu && !cca) {
            if (rage128_2d_queue_grad(dev, x, y, w, h, kx0, ky0, bpp))
                return;
        } else
            rage128_gpu_2d_qskip(dev, 7, cca ? 16 : 2);
    }

    if (!rage128_2d_map_span(dev, &sd, &dev->s2d_dst, dev->dst_offset,
                             dev->dst_pitch, bpp, x, y, y + h - 1, x + w - 1)) {
        rage128_2d_vq_abort(dev);
        return;
    }

    for (int row = 0; row < h; row++) {
        uint32_t addr = dev->dst_offset + ((uint32_t) (y + row) * rage128_2d_dst_stride(dev, bpp) + x * bpp);

        for (int col = 0; col < w; col++) {
            uint32_t a = addr + (uint32_t) col * bpp;
            uint32_t pix;

            if (aux_on && !rage128_aux_sc_pass_2d(dev->aux_sc_cntl, dev->aux_sc_rect, x + col, y + row))
                continue;
            pix = rage128_2d_grad_px(dev, kx0 + col, ky0 + row);
            if (cca) {
                uint32_t dpx = 0;
                int      cr;

                for (int b = 0; b < bpp; b++)
                    dpx |= (uint32_t) *r128_surf_at(&sd, a + (uint32_t) b)
                        << (b * 8);
                /* scaler-pipe fill: the compared source is the pipe color */
                cr = rage128_2d_ccmp_px(&cc, pix, dpx);
                if (!cr)
                    continue;
                if (cr == 2)
                    pix ^= cc.flip;
            }
            for (int b = 0; b < bpp; b++)
                rage128_2d_store(r128_surf_at(&sd, a + (uint32_t) b),
                                 (uint8_t) (pix >> ((b & 3) * 8)),
                                 (uint8_t) (wmask >> ((b & 3) * 8)));
        }
        rage128_2d_mark_dirty(dev, &sd, addr, (uint32_t) w * bpp);
    }
    r128_surf_commit(dev, &sd);
    rage128_2d_vq_end(dev);
}

/* Paint the host data streamed for an op started by register writes.
   The op latched the destination rect and the source datatype;
   hostdata_buf holds the dwords the driver wrote to HOST_DATA0 through
   HOST_DATA7 and HOST_DATA_LAST (RRG: HOST_DATA0, pp. 3-151-3-153 / PDF
   169-171). Each scanline is padded to a dword, as the X driver's
   color expansion sends it (xf86-video-r128 r128_accel.c, XAA version,
   R128SubsequentScanlineCPUToScreenColorExpandFill: (w + 31) >> 5
   dwords per scanline); the 0x19 packet path packs mono rows back to
   back instead. The mono and color executors do the drawing. */
static void
rage128_2d_hostdata_paint(rage128_t *dev)
{
    int      w    = dev->hostdata_w;
    int      h    = dev->hostdata_h;
    uint32_t have = dev->hostdata_ndw; /* dwords streamed */

    if (w <= 0 || h <= 0 || have == 0)
        return;

    if (dev->hostdata_srcdt == 3) {
        /* Color pixels in the destination format, rows back to back with
           a dword pad at the end (the X driver streams one scanline per
           op on this path). */
        rage128_2d_host_color_rect(dev, dev->hostdata_x, dev->hostdata_y, w, h,
                                   (const uint8_t *) dev->hostdata_buf, have * 4, NULL, NULL, 0);
    } else {
        /* Mono: w bits per scanline, each scanline padded to a dword, so
           bitpitch is the padded width in bits. If fewer dwords arrived
           than the whole op needs, only the complete rows are painted. */
        uint32_t dpw      = ((uint32_t) w + 31) >> 5; /* dwords per scanline */
        uint32_t bitpitch = dpw * 32;

        if (have < dpw * (uint32_t) h)
            h = (int) (have / dpw);
        if (h > 0)
            rage128_2d_mono_rect(dev, dev->hostdata_x, dev->hostdata_y, w, h,
                                 (const uint8_t *) dev->hostdata_buf, bitpitch,
                                 NULL, NULL, 0);
    }
}

/* What a finished draw leaves in the destination position. DST_X_TILE
   and DST_Y_TILE enable "rectangular tiling" in x and y (RRG: DP_CNTL,
   p. 3-165 / PDF 183); the Rage 128 guide says no more. The Rage Pro
   guide gives the rule: after a blit, with the tile bit set the position
   moves by the width (or height), forward or back by the direction bit;
   with it clear the position goes back to its value before the draw
   (Rage Pro guide, sec. 6.2.4). Leaving the latch alone already does
   the second case. "Blit" there means the rectangle draws as opposed to
   lines. The move applies to the latched coordinate, offset included: a
   reverse walk starts at the far edge and steps back from there. No
   captured traffic sets either bit, so two readings here are this
   code's own: that a fill with no source also moves, and that the move
   adds to the y step DST_WIDTH_X_INCY makes (the Rage Pro has no such
   register, so no guide covers the pair). The result wraps to the
   signed 14-bit field, "range -8192 to 8191" (RRG: DST_X, p. 3-138 /
   PDF 156), so the next draw starts from what the register holds, not
   from an int that ran past it. */
static void
rage128_2d_dst_traj_advance(rage128_t *dev, int w, int h)
{
    if (dev->dp_cntl & RAGE128_DP_CNTL_DST_X_TILE)
        dev->gui_dst_x = rage128_sx14((uint32_t) (dev->gui_dst_x
                                                  + ((dev->dp_cntl & RAGE128_DP_CNTL_DST_X_DIR) ? w : -w)));
    if (dev->dp_cntl & RAGE128_DP_CNTL_DST_Y_TILE)
        dev->gui_dst_y = rage128_sx14((uint32_t) (dev->gui_dst_y
                                                  + ((dev->dp_cntl & RAGE128_DP_CNTL_DST_Y_DIR) ? h : -h)));
}

/* One dword written to HOST_DATA0-7 or HOST_DATA_LAST. It is added to
   the waiting op's buffer; the HOST_DATA_LAST write ends the data, so
   the op is painted and closed. Writes with no op waiting are
   ignored. */
static void
rage128_2d_hostdata_word(rage128_t *dev, uint32_t off, uint32_t val)
{
    if (!dev->hostdata_active)
        return;
    if (dev->hostdata_ndw < (uint32_t) (sizeof(dev->hostdata_buf) / sizeof(dev->hostdata_buf[0])))
        dev->hostdata_buf[dev->hostdata_ndw++] = val;
    if (off == RAGE128_HOST_DATA_LAST) {
        rage128_2d_hostdata_paint(dev);
        dev->hostdata_ndw    = 0;
        dev->hostdata_active = 0;
        rage128_2d_dst_traj_advance(dev, dev->hostdata_w, dev->hostdata_h);
        atomic_store(&dev->gui_idle_event, 1);
    }
}

/* 1 = the draw finished here, so its destination-position update
   (rage128_2d_dst_traj_advance) is due now; 0 = nothing was drawn, or a
   host-data op was only set up and finishes on its HOST_DATA_LAST
   write. */
static int
rage128_2d_gui_op_run(rage128_t *dev, int w, int h)
{
    uint32_t gmc = dev->dp_gui_master_cntl;
    int      src = rage128_dp_src_source(dev);

    if (w <= 0 || h <= 0)
        return 0;

    /* Scaler-pipe fill (the Windows 2000 display driver's DrvGradientFill,
       used for window captions). With GMC_3D_FCN_EN (bit 27) set, the
       source is the 3D/scaler pipe, not memory, so DP_SRC_SOURCE (2 here)
       is ignored: "during 3D/Scalar Operations ... data is always loaded
       from the 3D/Scalar pipeline" (RRG: DP_MIX, p. 3-171 / PDF 189).
       The op is a fill of the latched destination rect, never a
       screen-to-screen copy; treated as a copy it would paint the screen
       contents at (0,0) over the caption. */
    if (gmc & RAGE128_GMC_3D_FCN_EN) {
        /* The pipe color comes from the gradient registers at
           0x1a40-0x1a60, not the brush. In captured traffic the driver
           writes them all to zero after each gradient op, and a later
           caption fill can run on those zeros; an all-zero gradient
           would paint black, so it counts as unset and the brush
           foreground is used. */
        uint32_t gnz = dev->grad_start[0] | dev->grad_start[1] | dev->grad_start[2]
            | (uint32_t) dev->grad_slope_x[0] | (uint32_t) dev->grad_slope_x[1]
            | (uint32_t) dev->grad_slope_x[2] | (uint32_t) dev->grad_slope_y[0]
            | (uint32_t) dev->grad_slope_y[1] | (uint32_t) dev->grad_slope_y[2];

        if (dev->grad_valid && gnz)
            rage128_2d_fill_gradient(dev, dev->gui_dst_x, dev->gui_dst_y, w, h);
        else
            rage128_2d_fill_solid(dev, dev->gui_dst_x, dev->gui_dst_y, w, h,
                                  dev->dp_brush_frgd_clr);
        return 1;
    }

    if (src == 2) {
        /* The driver sets the blit direction in DP_CNTL (DST_X_DIR 1 =
           left to right, DST_Y_DIR 1 = top to bottom; RRG: DP_CNTL,
           p. 3-165 / PDF 183). For a reverse walk it moves the start
           coordinates to the far edge of the rect, adding w - 1 to the
           source and destination x when going right to left and h - 1 to
           y when going bottom to top, so an overlapping copy comes out
           right (xf86-video-r128 r128_exa.c R128Copy; r128_accel.c, XAA
           version, R128SubsequentScreenToScreenCopy). The top-left
           corner is worked back out here; rage128_2d_blit_rect_pat then
           picks its own safe row order for the rect. */
        int sx = dev->gui_src_x, sy = dev->gui_src_y;
        int dx = dev->gui_dst_x, dy = dev->gui_dst_y;

        if (!(dev->dp_cntl & RAGE128_DP_CNTL_DST_X_DIR)) {
            sx -= w - 1;
            dx -= w - 1;
        }
        if (!(dev->dp_cntl & RAGE128_DP_CNTL_DST_Y_DIR)) {
            sy -= h - 1;
            dy -= h - 1;
        }
        /* brush types 0 and 1: the 8x8 mono pattern in BRUSH_DATA0 and
           BRUSH_DATA1, as for the register-started paint below */
        {
            uint32_t bt       = (uint32_t) rage128_dp_brush_dt(dev);
            int      pat_mono = (bt == 0 || bt == 1);

            rage128_2d_blit_rect_pat(dev, sx, sy, dx, dy, w, h,
                                     pat_mono ? (const uint8_t *) dev->brush_data
                                              : NULL,
                                     NULL, bt == 1);
        }
    } else if (src >= 3) {
        /* Set up a host-data op (the X driver's color expansion). The
           destination rect and the source datatype are latched; the data
           dwords then arrive through HOST_DATA0-7 and HOST_DATA_LAST
           (rage128_2d_reg_write) and are painted on the HOST_DATA_LAST
           write. */
        dev->hostdata_active = 1;
        dev->hostdata_x      = dev->gui_dst_x;
        dev->hostdata_y      = dev->gui_dst_y;
        dev->hostdata_w      = w;
        dev->hostdata_h      = h;
        dev->hostdata_srcdt  = (uint32_t) rage128_dp_src_dt(dev);
        dev->hostdata_ndw    = 0;
        return 0;
    } else {
        /* Brush types 0 and 1: the 8x8 mono pattern in BRUSH_DATA0 and
           BRUSH_DATA1 (a packet carries its pattern inline; a
           register-started op uses what the registers hold). Other
           pattern types paint the solid foreground: no driver traffic
           seen uses them on this path. */
        uint32_t bt       = (uint32_t) rage128_dp_brush_dt(dev);
        int      pat_mono = (bt == 0 || bt == 1);

        rage128_2d_paint_rect(dev, dev->gui_dst_x, dev->gui_dst_y, w, h,
                              pat_mono ? (const uint8_t *) dev->brush_data : NULL,
                              NULL, bt == 1);
    }
    return 1;
}

/* Entry point of an op started by a register write. The op runs to the
   end on the thread that wrote the register. A CPU write to the engine
   registers first waits for the CCE executor thread to go idle and then
   applies the value (rage128_reg_write in vid_ati_rage128.c;
   rage128_pm4_drain_wait in vid_ati_rage128_pm4.c), so the executor
   never sees this op, and the busy-to-idle event behind GUI_IDLE_INT is
   raised here, as the executor raises it when it runs out of work. */
static void
rage128_2d_gui_op(rage128_t *dev, int w, int h)
{
    /* Started from the ring (a type-0 register write run by the CCE
       thread): the op uses the same surfaces as any 3D batch still
       waiting to be rendered, so that batch is rendered first. On the
       CPU thread the CCE is already idle (see above), so pending GPU work
       can be flushed directly from here. */
    if (rage128_on_cce_thread)
        rage128_raster_flush(dev);
    if (dev->gpu)
        rage128_gpu_flush(dev, R128_GPU_2D_REG_GUI);
    if (rage128_2d_gui_op_run(dev, w, h))
        rage128_2d_dst_traj_advance(dev, w, h);
    atomic_store(&dev->gui_idle_event, 1);
}

int
rage128_2d_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask)
{
#define MERGE(field) ((field) = ((field) & ~mask) | (val & mask))
    /* BRUSH_DATA0 to BRUSH_DATA31: see rage128_2d_reg_read. */
    if (off >= RAGE128_BRUSH_DATA0 && off < RAGE128_BRUSH_DATA0 + 32 * 4) {
        MERGE(dev->brush_data[(off - RAGE128_BRUSH_DATA0) >> 2]);
        return 1;
    }
    switch (off) {
        case RAGE128_DP_GUI_MASTER_CNTL:
            rage128_2d_apply_gmc(dev, (dev->dp_gui_master_cntl & ~mask) | (val & mask));
            return 1;
        case RAGE128_SRC_PITCH_OFFSET:
            rage128_2d_set_pitch_offset(dev, 0, val & mask);
            return 1;
        case RAGE128_DST_PITCH_OFFSET:
            rage128_2d_set_pitch_offset(dev, 1, val & mask);
            return 1;
            /* clang-format off */
        /* The gradient registers at 0x1a40-0x1a60, not named in the
           guides. The Windows 2000 display driver's DrvGradientFill
           writes them as one 9-dword type-0 burst (header 0x80690): per
           channel a slope along x, a slope along y and a start value, all
           8.16 fixed point (RE: ati2dvaa.dll @00021f28). The scaler-pipe
           fill (GMC_3D_FCN_EN set) in rage128_2d_gui_op_run uses them;
           without them the caption gradients would fall back to the gray
           brush color. */
        case 0x1a40: MERGE(dev->grad_slope_x[0]); dev->grad_valid = 1; return 1;
        case 0x1a44: MERGE(dev->grad_slope_y[0]); return 1;
        case 0x1a48: MERGE(dev->grad_start[0]); return 1;
        case 0x1a4c: MERGE(dev->grad_slope_x[1]); dev->grad_valid = 1; return 1;
        case 0x1a50: MERGE(dev->grad_slope_y[1]); return 1;
        case 0x1a54: MERGE(dev->grad_start[1]); return 1;
        case 0x1a58: MERGE(dev->grad_slope_x[2]); dev->grad_valid = 1; return 1;
        case 0x1a5c: MERGE(dev->grad_slope_y[2]); return 1;
        case 0x1a60: MERGE(dev->grad_start[2]); return 1;
        /* clang-format on */
        /* Ops started by register writes (the X server's 2D path): the
           position registers only latch; a write to a size register
           starts the op. SRC_Y_X and DST_Y_X hold x in [13:0] and y in
           [29:16] (RRG: DST_Y_X, p. 3-139 / PDF 157). */
        case RAGE128_SRC_Y_X:
            dev->gui_src_x = rage128_sx14(val & mask);
            dev->gui_src_y = rage128_sx14((val & mask) >> 16);
            return 1;
        case RAGE128_DST_Y_X:
            dev->gui_dst_x = rage128_sx14(val & mask);
            dev->gui_dst_y = rage128_sx14((val & mask) >> 16);
            return 1;
        /* SRC_X_Y and DST_X_Y hold the same two coordinates the other way
           round: x in [29:16] and y in [13:0] (RRG: DST_X_Y,
           pp. 3-138-3-139 / PDF 156-157). The Windows 2000 display
           driver's caption gradient fill sends its destination position
           here; if the write were dropped, the position would stay at
           (0,0) and the caption would not be redrawn. */
        case RAGE128_SRC_X_Y:
            dev->gui_src_x = rage128_sx14((val & mask) >> 16);
            dev->gui_src_y = rage128_sx14(val & mask);
            return 1;
        case RAGE128_DST_X_Y:
            dev->gui_dst_x = rage128_sx14((val & mask) >> 16);
            dev->gui_dst_y = rage128_sx14(val & mask);
            return 1;
        /* HOST_DATA0-7 and HOST_DATA_LAST: the data of a host-data op
           set up by rage128_2d_gui_op_run arrives here; the HOST_DATA_LAST
           write ends it and paints it. */
        case RAGE128_HOST_DATA0 ... RAGE128_HOST_DATA_LAST:
            rage128_2d_hostdata_word(dev, off, val & mask);
            return 1;
        case RAGE128_DST_WIDTH_HEIGHT: /* w << 16 | h, starts the op (the X driver's fills) */
            dev->gui_dst_w = ((val & mask) >> 16) & 0x3fff;
            dev->gui_dst_h = (val & mask) & 0x3fff;
            rage128_2d_gui_op(dev, (int) dev->gui_dst_w, (int) dev->gui_dst_h);
            return 1;
        case RAGE128_DST_HEIGHT_WIDTH: /* h << 16 | w, starts the op (the X driver's copies) */
            dev->gui_dst_w = (val & mask) & 0x3fff;
            dev->gui_dst_h = ((val & mask) >> 16) & 0x3fff;
            rage128_2d_gui_op(dev, (int) dev->gui_dst_w, (int) dev->gui_dst_h);
            return 1;
        /* Single-field registers: the same state the packed forms set,
           each with its own field mask. Setup registers only latch; the
           write-only registers below that carry a size start the op. */
        case RAGE128_DST_OFFSET:
            dev->dst_offset = (val & mask) & 0x03fffff0u;
            return 1;
        case RAGE128_SRC_OFFSET:
            dev->src_offset = (val & mask) & 0x03fffff0u;
            return 1;
        case RAGE128_DST_PITCH:
            {
                /* pitch / 8 in [9:0], DST_TILE [16], DST_PITCH_ADJ [18:17]
                   (see rage128_2d_dst_pitch_apply) */
                dev->dst_pitch_reg = (val & mask) & 0x000703ffu;
                rage128_2d_dst_pitch_apply(dev);
                return 1;
            }
        case RAGE128_SRC_PITCH:
            {
                uint32_t v = (val & mask) & 0x000103ffu;

                dev->src_pitch_reg = v;
                dev->src_pitch     = (v & 0x3ff) * 8u;
                return 1;
            }
            /* clang-format off */
        case RAGE128_DST_WIDTH:  dev->gui_dst_w = (val & mask) & 0x3fff; return 1;
        case RAGE128_DST_HEIGHT: dev->gui_dst_h = (val & mask) & 0x3fff; return 1;
        case RAGE128_DST_X: dev->gui_dst_x = rage128_sx14(val & mask); return 1;
        case RAGE128_DST_Y: dev->gui_dst_y = rage128_sx14(val & mask); return 1;
        case RAGE128_SRC_X: dev->gui_src_x = rage128_sx14(val & mask); return 1;
        case RAGE128_SRC_Y: dev->gui_src_y = rage128_sx14(val & mask); return 1;
        /* clang-format on */
        case RAGE128_SC_LEFT:
            dev->sc_top_left = (dev->sc_top_left & ~0x3fffu)
                | ((val & mask) & 0x3fff);
            return 1;
        case RAGE128_SC_TOP:
            dev->sc_top_left = (dev->sc_top_left & ~(0x3fffu << 16))
                | (((val & mask) & 0x3fff) << 16);
            return 1;
        case RAGE128_SC_RIGHT:
            dev->sc_bottom_right = (dev->sc_bottom_right & ~0x3fffu)
                | ((val & mask) & 0x3fff);
            return 1;
        case RAGE128_SC_BOTTOM:
            dev->sc_bottom_right = (dev->sc_bottom_right & ~(0x3fffu << 16))
                | (((val & mask) & 0x3fff) << 16);
            return 1;
            /* clang-format off */
        case RAGE128_SRC_SC_RIGHT:  dev->src_sc_right  = (val & mask) & 0x3fff; return 1;
        case RAGE128_SRC_SC_BOTTOM: dev->src_sc_bottom = (val & mask) & 0x3fff; return 1;
        /* clang-format on */
        case RAGE128_SRC_SC_BOTTOM_RIGHT:
            dev->src_sc_right  = (val & mask) & 0x3fff;
            dev->src_sc_bottom = ((val & mask) >> 16) & 0x3fff;
            return 1;
        /* Other registers that carry a size and start the op (RRG:
           DST_HEIGHT_Y, pp. 3-141-3-142 / PDF 159-160). Each latches its
           fields, then starts the op with the other size as latched.
           After DST_WIDTH_X_INCY the code also moves y down by the
           height, for drivers that draw scanline by scanline; the guide
           gives the register no description, so that step is modeled
           from its name. */
        case RAGE128_DST_WIDTH_X:
            dev->gui_dst_x = rage128_sx14(val & mask);
            dev->gui_dst_w = ((val & mask) >> 16) & 0x3fff;
            rage128_2d_gui_op(dev, (int) dev->gui_dst_w, (int) dev->gui_dst_h);
            return 1;
        case RAGE128_DST_WIDTH_X_INCY:
            dev->gui_dst_x = rage128_sx14(val & mask);
            dev->gui_dst_w = ((val & mask) >> 16) & 0x3fff;
            rage128_2d_gui_op(dev, (int) dev->gui_dst_w, (int) dev->gui_dst_h);
            dev->gui_dst_y = rage128_sx14((uint32_t) (dev->gui_dst_y
                                                      + (int32_t) dev->gui_dst_h));
            return 1;
        case RAGE128_DST_HEIGHT_Y:
            dev->gui_dst_y = rage128_sx14(val & mask);
            dev->gui_dst_h = ((val & mask) >> 16) & 0x3fff;
            rage128_2d_gui_op(dev, (int) dev->gui_dst_w, (int) dev->gui_dst_h);
            return 1;
        case RAGE128_DST_WIDTH_BW:
            /* Block-write fill, "valid for all memory types" (RRG:
               DST_WIDTH_BW, p. 3-236 / PDF 254). The emulated memory is
               byte-maskable, so it is an ordinary fill. */
            dev->gui_dst_w = (val & mask) & 0x3fff;
            rage128_2d_gui_op(dev, (int) dev->gui_dst_w, (int) dev->gui_dst_h);
            return 1;
            /* clang-format off */
        /* Bresenham lines. The driver writes the direction, DST_Y_X,
           DST_BRES_ERR, DST_BRES_INC and DST_BRES_DEC, then DST_BRES_LNTH
           last, and that write starts the line (xf86-video-r128
           r128_accel.c, XAA version, R128SubsequentSolidBresenhamLine). */
        case RAGE128_DST_BRES_ERR:  MERGE(dev->bres_err);  return 1;
        case RAGE128_DST_BRES_INC:  MERGE(dev->bres_inc);  return 1;
        case RAGE128_DST_BRES_DEC:  MERGE(dev->bres_dec);  return 1;
        /* clang-format on */
        case RAGE128_DP_CNTL_XDIR_YDIR_YMAJOR:
            {
                /* another view of DP_CNTL [2:0], not a register of its own:
                   the line setup writes the three direction bits here in one
                   write, and a GMC or DP_CNTL write changes the same bits */
                uint32_t v = (rage128_2d_line_dir(dev) & ~mask) | (val & mask);

                dev->dp_cntl = (dev->dp_cntl & ~7u)
                    | ((v & RAGE128_DP_LINE_X_DIR) ? RAGE128_DP_CNTL_DST_X_DIR : 0)
                    | ((v & RAGE128_DP_LINE_Y_DIR) ? RAGE128_DP_CNTL_DST_Y_DIR : 0)
                    | ((v & RAGE128_DP_LINE_Y_MAJOR) ? RAGE128_DP_CNTL_DST_Y_MAJOR : 0);
                return 1;
            }
        case RAGE128_DST_BRES_LNTH:
            MERGE(dev->bres_lnth);
            if (rage128_on_cce_thread)
                rage128_raster_flush(dev); /* after any waiting 3D batch */
            if (dev->gpu)
                rage128_gpu_flush(dev, R128_GPU_2D_REG_BRES);
            rage128_2d_bres_line(dev, rage128_2d_line_dir(dev));
            atomic_store(&dev->gui_idle_event, 1);
            return 1;
            /* clang-format off */
        case RAGE128_DP_BRUSH_BKGD_CLR: MERGE(dev->dp_brush_bkgd_clr); return 1;
        case RAGE128_DP_BRUSH_FRGD_CLR: MERGE(dev->dp_brush_frgd_clr); return 1;
        case RAGE128_BRUSH_Y_X:         MERGE(dev->brush_yx);          return 1;
        case RAGE128_DP_SRC_FRGD_CLR:   MERGE(dev->dp_src_frgd_clr); return 1;
        case RAGE128_DP_SRC_BKGD_CLR:   MERGE(dev->dp_src_bkgd_clr); return 1;
        case RAGE128_CLR_CMP_CLR_SRC:   MERGE(dev->clr_cmp_clr_src); return 1;
        case RAGE128_CLR_CMP_CLR_DST:   MERGE(dev->clr_cmp_clr_dst); return 1;
        case RAGE128_CLR_CMP_CNTL:      MERGE(dev->clr_cmp_cntl); return 1;
        case RAGE128_CLR_CMP_MASK:      MERGE(dev->clr_cmp_mask); return 1;
        case RAGE128_AUX_SC_CNTL:       MERGE(dev->aux_sc_cntl); return 1;
        /* clang-format on */
        /* The three aux scissor rects: 12 consecutive registers, left,
           right, top and bottom for each, every bound signed in [13:0],
           "range -8192 to 8191" (RRG: AUX1_SC_LEFT, pp. 3-157-3-158 / PDF
           175-176); the bounds are treated as inclusive. */
        case RAGE128_AUX1_SC_LEFT ... RAGE128_AUX3_SC_BOTTOM:
            {
                uint32_t  idx = (off - RAGE128_AUX1_SC_LEFT) >> 2;
                uint32_t *r   = &dev->aux_sc_rect[idx >> 2][idx & 3];

                *r = ((*r & ~mask) | (val & mask)) & 0x3fff;
                return 1;
            }
            /* clang-format off */
        case RAGE128_DP_CNTL:           MERGE(dev->dp_cntl); return 1;
        case RAGE128_DP_DATATYPE:       MERGE(dev->dp_datatype); return 1;
        case RAGE128_DP_MIX:            MERGE(dev->dp_mix); return 1;
        case RAGE128_DP_WRITE_MASK:     MERGE(dev->dp_write_mask); return 1;
        case RAGE128_DEFAULT_OFFSET:    MERGE(dev->default_offset); return 1;
        case RAGE128_DEFAULT_PITCH:     MERGE(dev->default_pitch); return 1;
        case RAGE128_DEFAULT_SC_BOTTOM_RIGHT: MERGE(dev->default_sc_bottom_right); return 1;
        case RAGE128_SC_TOP_LEFT:       MERGE(dev->sc_top_left); return 1;
        case RAGE128_SC_BOTTOM_RIGHT:   MERGE(dev->sc_bottom_right); return 1;
        /* clang-format on */
        case RAGE128_PC_GUI_CTLSTAT:
            return 1; /* flush request; each op is drawn at once, so
                         nothing is waiting to be flushed */
        case RAGE128_SCALE_SCR_HEIGHT_WIDTH:
            MERGE(dev->scale_scr_height_width);
            return 1;
        default:
            break;
    }
#undef MERGE
    if (off >= RAGE128_GUI_SCRATCH_REG0 && off < RAGE128_GUI_SCRATCH_REG0 + 6 * 4) {
        uint32_t *r = &dev->gui_scratch[(off - RAGE128_GUI_SCRATCH_REG0) >> 2];

        *r = (*r & ~mask) | (val & mask);
        return 1;
    }
    return 0;
}
