/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- display block: the PLL register file and
 *          clocks, the extended CRTC, the DAC and palette, the hardware
 *          cursor, page flips, scanout through the svga core, and the
 *          realtime pacer's control loop.
 *
 *          In VGA mode (CRTC_GEN_CNTL.CRTC_EXT_DISP_EN clear) the svga
 *          core's own VGA timings drive the display. In extended mode
 *          this file derives the core's timings, pixel format and scan
 *          address from the chip's CRTC registers and wraps the core's
 *          scan address and line renderer for flips, tiling and the
 *          optional GPU backend.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 Software Development Guide",
 *              SDK-G04000 Rev 0.01, August 1999 (the hardware cursor
 *              image format). Cited as "SDK: ...".
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include "cpu.h"
#include <86box/vid_ati_rage128.h>
#include <86box/vid_ati_rage128_regs.h>

/* Guest CPU name and speed for the telemetry header. It lives here
   because the segment macros in cpu.h clash with the GPU file, which
   therefore cannot include it. Returns 0 when there is no guest CPU, as
   in the test harnesses. */
int
rage128_guest_cpu(const char **family, const char **model, unsigned *mhz)
{
    if (!cpu_f || !cpu_s)
        return 0;
    *family = cpu_f->name;
    *model  = cpu_s->name;
    *mhz    = cpu_s->rspeed / 1000000u;
    return 1;
}

/* PPLL post-divider codes (PPLL_POST0_DIV in RRG: PPLL_DIV_0, p. 3-90 /
   PDF 108, and the same field in PPLL_DIV_1 to PPLL_DIV_3): 0 = /1,
   1 = /2, 2 = /4, 3 = /8, 4 = /3, 5 reserved, 6 = /6, 7 = /12. The
   reserved code is modeled as /1. */
static const int rage128_ppll_post_div[8] = { 1, 2, 4, 8, 3, 1, 6, 12 };

/* ------------------------------------------------------------------ */
/* Clock computation.                                                  */
/* ------------------------------------------------------------------ */

/* Copy the programmed PPLL dividers into the working set the pixel clock
   runs from. pll_regs holds what software wrote and reads back;
   ppll_work holds what the PLL uses. With atomic update enabled in
   PPLL_CNTL, a divider write changes only pll_regs, and the working set
   reloads when software writes PPLL_ATOMIC_UPDATE_W: at once, or at the
   next VSYNC when PPLL_ATOMIC_UPDATE_SYNC is set (RRG: PPLL_REF_DIV,
   p. 3-89 / PDF 107; RRG: PPLL_CNTL, p. 3-88 / PDF 106). */
static void
rage128_ppll_commit(rage128_t *dev)
{
    for (int i = 0; i < 5; i++)
        dev->ppll_work[i] = dev->pll_regs[RAGE128_PLL_PPLL_REF_DIV + i];
}

static double rage128_xpll_hz(const rage128_t *dev);
static double rage128_mpll_hz(const rage128_t *dev);

/* PPLL reference input, chosen by PPLL_REF_DIV_SRC: 0 = XTALIN,
   1 = MPllClk/2, 2 = XPllClk/2 (RRG: PPLL_REF_DIV, p. 3-89 / PDF 107).
   The guide does not list code 3; it is modeled as XTALIN. This is the
   input to the reference divider, so both the VCO and the test-mux tap
   on the divider output read it. */
static double
rage128_ppll_ref_hz(const rage128_t *dev)
{
    switch ((dev->ppll_work[0] >> RAGE128_PPLL_REF_DIV_SRC_SHIFT) & RAGE128_PPLL_REF_DIV_SRC_MASK) {
        case RAGE128_PPLL_REF_SRC_MPLL:
            return rage128_mpll_hz(dev) / 2.0;
        case RAGE128_PPLL_REF_SRC_XPLL:
            return rage128_xpll_hz(dev) / 2.0;
        default:
            return dev->ref_freq_hz;
    }
}

/* PPLL VCO: PPllClk = N * PPLL_REF / M, with M = PPLL_REF_DIV [9:0] and
   N = PPLL_FBx_DIV [10:0] of the selected divider set. The guide says a
   divider stops below M = 2 or N = 4 (RRG: PPLL_REF_DIV, p. 3-89 /
   PDF 107; RRG: PPLL_DIV_0, p. 3-90 / PDF 108); the code clamps to those
   minimums instead. The caller picks the set: CLOCK_CNTL_INDEX
   PPLL_DIV_SEL in extended modes, GENMO_WT VGA_CKSEL in VGA modes
   (RRG: PPLL_DIV_0, p. 3-91 / PDF 109). */
static double
rage128_ppll_vco_hz(const rage128_t *dev, int sel)
{
    uint32_t div = dev->ppll_work[1 + (sel & 3)];
    uint32_t m   = dev->ppll_work[0] & RAGE128_PPLL_REF_DIV_MASK;
    uint32_t n   = div & RAGE128_PPLL_FB_DIV_MASK;

    if (m < 2)
        m = 2;
    if (n < 4)
        n = 4;
    return rage128_ppll_ref_hz(dev) * (double) n / (double) m;
}

static double
rage128_dot_clock_hz(const rage128_t *dev)
{
    int      sel;
    uint32_t div;

    /* VCLK_SRC_SEL picks the pixel clock source: 0 = PCICLK (its reset
       value), 1 = the PCLK pin, 2 = BYTE_CLK, 3 = PPllClk (RRG:
       VCLK_ECP_CNTL, p. 3-95 / PDF 113). Only the PPLL is modeled; for
       the other sources the reference oscillator rate is returned so the
       timings stay usable until a mode set selects the PPLL. */
    if ((dev->pll_regs[RAGE128_PLL_VCLK_ECP_CNTL] & RAGE128_VCLK_SRC_SEL_MASK) != RAGE128_VCLK_SRC_PPLL)
        return dev->ref_freq_hz;

    if (dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN)
        sel = (dev->clock_cntl_index >> RAGE128_PPLL_DIV_SEL_SHIFT) & 3;
    else
        sel = (dev->svga.miscout >> 2) & 3; /* GENMO_WT VGA_CKSEL [3:2] */

    div = dev->ppll_work[1 + sel];
    return rage128_ppll_vco_hz(dev, sel) / rage128_ppll_post_div[(div >> RAGE128_PPLL_POST_DIV_SHIFT) & 7];
}

/* DAC_MASK acts only in VGA modes and has no effect in VESA or extended
   display modes (RRG: DAC_CNTL, p. 3-125 / PDF 143). The svga core's
   8 bpp renderer, which extended mode also uses, applies svga->dac_mask
   to every palette index, so the mask it sees is forced to 0xff while
   CRTC_EXT_DISP_EN is set. The value software wrote stays in
   dac_mask_prog and reads back through port 3C6 and DAC_CNTL. */
void
rage128_dac_mask_apply(rage128_t *dev)
{
    dev->svga.dac_mask = (dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN)
        ? 0xff
        : dev->dac_mask_prog;
}

/* XPLL and MPLL: PllClk = 2 * N * Xtalin / M. The two share M,
   X_MPLL_REF_DIV [7:0]; N is XPLL_FB_DIV [15:8] or MPLL_FB_DIV [23:16].
   The guide gives a minimum of 2 for each divider (RRG:
   X_MPLL_REF_FB_DIV, p. 3-98 / PDF 116), and the code clamps to it. */
static double
rage128_xpll_hz(const rage128_t *dev)
{
    uint32_t r = dev->pll_regs[RAGE128_PLL_X_MPLL_REF_FB_DIV];
    uint32_t m = r & 0xff;
    uint32_t n = (r >> 8) & 0xff;

    if (m < 2)
        m = 2;
    if (n < 2)
        n = 2;
    return 2.0 * dev->ref_freq_hz * (double) n / (double) m;
}

static double
rage128_mpll_hz(const rage128_t *dev)
{
    uint32_t r = dev->pll_regs[RAGE128_PLL_X_MPLL_REF_FB_DIV];
    uint32_t m = r & 0xff;
    uint32_t n = (r >> 16) & 0xff;

    if (m < 2)
        m = 2;
    if (n < 2)
        n = 2;
    return 2.0 * dev->ref_freq_hz * (double) n / (double) m;
}

/* Nominal PCI bus clock; the XCLK/MCLK muxes can select it. */
#define RAGE128_PCICLK_HZ 33333333.0

/* XCLK, the memory controller clock, chosen by XCLK_CNTL.XCLK_SRC_SEL
   [3:0]: 0 = inverted PCICLK (the same rate), 1 to 4 = XPllClk divided
   by 1, 2, 4 or 8 (RRG: XCLK_CNTL, p. 3-101 / PDF 119). The other codes
   select the HCLK0 or HCLK1 pins, the XDLL0 clock, or a stopped clock;
   none of those is generated here, so all of them read as stopped. */
static double
rage128_xclk_hz(const rage128_t *dev)
{
    switch (dev->pll_regs[RAGE128_PLL_XCLK_CNTL] & 0xf) {
        case 0:
            return RAGE128_PCICLK_HZ;
        case 1:
            return rage128_xpll_hz(dev);
        case 2:
            return rage128_xpll_hz(dev) / 2.0;
        case 3:
            return rage128_xpll_hz(dev) / 4.0;
        case 4:
            return rage128_xpll_hz(dev) / 8.0;
        default:
            return 0.0;
    }
}

/* MCLK, the main engine clock, chosen by MCLK_CNTL.MCLK_SRC_SEL [2:0]:
   0 = PCICLK, 1 to 4 = MPllClk divided by 1, 2, 4 or 8, 5 = XCLK,
   6 reserved, 7 = XTALIN (RRG: MCLK_CNTL, p. 3-103 / PDF 121). The
   reserved code reads as stopped. */
static double
rage128_mclk_hz(const rage128_t *dev)
{
    switch (dev->pll_regs[RAGE128_PLL_MCLK_CNTL] & 0x7) {
        case 0:
            return RAGE128_PCICLK_HZ;
        case 1:
            return rage128_mpll_hz(dev);
        case 2:
            return rage128_mpll_hz(dev) / 2.0;
        case 3:
            return rage128_mpll_hz(dev) / 4.0;
        case 4:
            return rage128_mpll_hz(dev) / 8.0;
        case 5:
            return rage128_xclk_hz(dev);
        case 7:
            return dev->ref_freq_hz;
        default:
            return 0.0;
    }
}

/* HTOTAL_CNTL lengthens each display line by less than a character
   (RRG: HTOTAL_CNTL, p. 3-97 / PDF 115). HTOT_PIX_SLIP (valid 0 to 7)
   adds that many pixels to each line, two per step in a VGA mode with
   SEQ_PCLKBY2 set. HTOT_PPLL_SLIP [18:16] adds that many VCO phase
   slips at every HSYNC, each 0.2 of a PLLVCLK period (modeled as 0.2 of
   a pixel). In VGA modes the PLL slips apply only when HTOT_CNTL_VGA_EN
   [28] is set. The guide does not give HTOT_VCLK_SLIP a usable
   encoding, so it is stored and ignored. The result is the svga core's
   htotal multiplier, a ratio of line lengths, which stays exact however
   the core counts characters. */
static double
rage128_htotal_multiplier(const rage128_t *dev, int line_dots, int vga_mode, int pclkby2)
{
    uint32_t htc  = dev->pll_regs[RAGE128_PLL_HTOTAL_CNTL];
    double   xtra = (double) ((htc & 0x7) * ((vga_mode && pclkby2) ? 2 : 1));

    if (!vga_mode || (htc & (1u << 28)))
        xtra += 0.2 * (double) ((htc >> 16) & 7);
    if (line_dots <= 0 || xtra <= 0.0)
        return 1.0;
    return 1.0 + xtra / (double) line_dots;
}

/* Rate of the clock TEST_DEBUG_MUX.TEST_DEBUG_CLK [12:8] puts on the
   test mux (RRG: TEST_DEBUG_MUX, p. 3-133 / PDF 151). The modeled codes
   are 0x01 Xtalin, 0x02 PPllClk/2, 0x03 the PPLL reference divider
   output, 0x05 and 0x06 PPllClk, 0x0b XPllClk, 0x0c XPllClk/2, 0x0f
   XCLK, 0x13 MPllClk, 0x14 MPllClk/2 and 0x16 MCLK. Any other code
   returns 0, the same as the guide's code 0, "no clock, output 0". */
static double
rage128_test_clock_hz(const rage128_t *dev)
{
    uint32_t m;
    int      sel      = (dev->test_debug_mux >> RAGE128_TEST_DEBUG_CLK_SHIFT) & 0x1f;
    int      ppll_sel = (dev->clock_cntl_index >> RAGE128_PPLL_DIV_SEL_SHIFT) & 3;

    switch (sel) {
        case 0x01: /* Xtalin, the board's reference oscillator, whose
                      rate comes from the PLL block of the board's BIOS
                      image. The video BIOS delay routine selects this
                      code and counts TEST_COUNT ticks (RE:
                      Rage128progl_zerostate.VBI @c000:6155). */
            return dev->ref_freq_hz;
        case 0x02:
            return rage128_ppll_vco_hz(dev, ppll_sel) / 2.0;
        case 0x03:
            m = dev->ppll_work[0] & RAGE128_PPLL_REF_DIV_MASK;
            return rage128_ppll_ref_hz(dev) / (double) ((m < 2) ? 2 : m);
        case 0x05:
        case 0x06:
            return rage128_ppll_vco_hz(dev, ppll_sel);
        case 0x0b:
            return rage128_xpll_hz(dev);
        case 0x0c:
            return rage128_xpll_hz(dev) / 2.0;
        case 0x0f: /* XCLK: source/divider selected by XCLK_CNTL. */
            return rage128_xclk_hz(dev);
        case 0x13:
            return rage128_mpll_hz(dev);
        case 0x14:
            return rage128_mpll_hz(dev) / 2.0;
        case 0x16: /* MCLK: source/divider selected by MCLK_CNTL. */
            return rage128_mclk_hz(dev);
        default:
            return 0.0;
    }
}

/* PLL_TEST_CNTL.TEST_COUNT [31:24]. The guide names the field and gives
   no description (RRG: PLL_TEST_CNTL, p. 3-105 / PDF 123). The video
   BIOS delay routine uses it as a free-running 8-bit count of the test
   mux clock: it selects Xtalin on the mux, clears PLL_MASK_READ_B [9],
   writes 0 to the count byte and polls the byte until it reaches a
   tick threshold (RE: Rage128progl_zerostate.VBI @c000:6155). The model
   follows that use: writing the count byte restarts the count, the
   count advances at the selected clock's rate, and the live value reads
   back only while PLL_MASK_READ_B is 0. */
/* Count time credited for each CLOCK_CNTL_DATA access, standing for the
   bus time of one register transaction (a modeled value). It is kept
   in seconds so the credit follows the selected test clock: 16 ticks at
   the AGP board's 27 MHz reference, about 17.5 at the PCI board's
   29.5 MHz. See rage128_pll_test_count. */
#define RAGE128_PLL_TEST_ACCESS_S (16.0 / 27000000.0)

static uint8_t
rage128_pll_test_count(const rage128_t *dev)
{
    double   hz;
    double   elapsed_us;
    uint64_t ticks;

    if (dev->pll_regs[RAGE128_PLL_TEST_CNTL] & (1 << 9)) /* PLL_MASK_READ_B */
        return dev->pll_test_count_base;

    hz = rage128_test_clock_hz(dev);
    if (hz <= 0.0)
        return dev->pll_test_count_base;

    /* The count is a time term, tsc elapsed since the restart converted
       at the clock's rate, plus the per-access credit in pll_test_acc.
       The credit is needed because the Windows 98 driver's PLL
       calibration writes the restart and reads the count back inside
       one block of recompiled code, and the emulated tsc does not
       advance within a block. With the time term alone the count would
       never move there and the calibration loop would never end.
       TIMER_USEC is tsc ticks per microsecond in 32.32 fixed point, so
       the delta is shifted left 32 bits before the divide, the same
       scaling as rage128_pace_update; the shift is done in 128 bits so
       a large delta cannot overflow, and the divide stays in floating
       point so fractions of a microsecond still count at 27 MHz. */
    elapsed_us = (double) ((unsigned __int128) (tsc - dev->pll_test_zero_tsc) << 32) / (double) TIMER_USEC;
    ticks      = (uint64_t) (elapsed_us * hz / 1000000.0 + dev->pll_test_acc);
    return (uint8_t) (dev->pll_test_count_base + ticks);
}

/* ------------------------------------------------------------------ */
/* PLL register file (via CLOCK_CNTL_INDEX/DATA).                      */
/* ------------------------------------------------------------------ */
static uint32_t
rage128_pll_read(rage128_t *dev)
{
    int      idx = dev->clock_cntl_index & RAGE128_PLL_ADDR_MASK;
    uint32_t v   = dev->pll_regs[idx];

    switch (idx) {
        case RAGE128_PLL_PPLL_REF_DIV:
        case RAGE128_PLL_PPLL_DIV_0:
        case RAGE128_PLL_PPLL_DIV_1:
        case RAGE128_PLL_PPLL_DIV_2:
        case RAGE128_PLL_PPLL_DIV_3:
            /* Bit 15 reads as PPLL_ATOMIC_UPDATE_R: 1 = divider update
               still pending, 0 = done (RRG: PPLL_REF_DIV, p. 3-89 /
               PDF 107). */
            v = (v & ~RAGE128_PPLL_ATOMIC_UPDATE) | (dev->ppll_update_pending ? RAGE128_PPLL_ATOMIC_UPDATE : 0);
            break;
        case RAGE128_PLL_TEST_CNTL:
            /* Each readback is one bus access, which earns the count its
               per-access credit (see RAGE128_PLL_TEST_ACCESS_S). */
            if (!(dev->pll_regs[RAGE128_PLL_TEST_CNTL] & (1 << 9))
                && rage128_test_clock_hz(dev) > 0.0)
                dev->pll_test_acc += RAGE128_PLL_TEST_ACCESS_S * rage128_test_clock_hz(dev);
            v = (v & 0x00ffffff) | ((uint32_t) rage128_pll_test_count(dev) << 24);
            break;
        default:
            break;
    }
    return v;
}

static void rage128_recalctimings_apply(rage128_t *dev);

static void
rage128_pll_write(rage128_t *dev, uint32_t val, uint32_t mask)
{
    int      idx = dev->clock_cntl_index & RAGE128_PLL_ADDR_MASK;
    uint32_t merged;

    /* Writes through CLOCK_CNTL_DATA reach the PLL registers only while
       CLOCK_CNTL_INDEX.PLL_WR_EN is set (RRG: CLOCK_CNTL_INDEX, p. 3-86 /
       PDF 104). */
    if (!(dev->clock_cntl_index & RAGE128_PLL_WR_EN))
        return;

    merged = (dev->pll_regs[idx] & ~mask) | (val & mask);

    switch (idx) {
        case RAGE128_PLL_PPLL_REF_DIV:
        case RAGE128_PLL_PPLL_DIV_0:
        case RAGE128_PLL_PPLL_DIV_1:
        case RAGE128_PLL_PPLL_DIV_2:
        case RAGE128_PLL_PPLL_DIV_3:
            /* Bit 15 written as 1 is PPLL_ATOMIC_UPDATE_W: load the new
               settings into the working dividers, at the next VSYNC when
               PPLL_ATOMIC_UPDATE_SYNC is set and at once otherwise. The
               guide says the request is "not required" when neither
               PPLL_ATOMIC_UPDATE_EN nor PPLL_VGA_ATOMIC_UPDATE_EN is set,
               so then every write goes straight to the working set (RRG:
               PPLL_CNTL, p. 3-88 / PDF 106; RRG: PPLL_REF_DIV, p. 3-89 /
               PDF 107). */
            dev->pll_regs[idx] = merged & ~RAGE128_PPLL_ATOMIC_UPDATE;
            {
                uint32_t cntl      = dev->pll_regs[RAGE128_PLL_PPLL_CNTL];
                int      atomic    = !!(cntl & (RAGE128_PPLL_ATOMIC_UPDATE_EN | RAGE128_PPLL_VGA_ATOMIC_UPDATE_EN));
                int      requested = (mask & RAGE128_PPLL_ATOMIC_UPDATE) && (merged & RAGE128_PPLL_ATOMIC_UPDATE);

                if (atomic && requested && (cntl & RAGE128_PPLL_ATOMIC_UPDATE_SYNC)) {
                    dev->ppll_update_pending = 1; /* commit at VSYNC */
                    return;
                }
                if (atomic && !requested)
                    return; /* new settings only; working dividers hold */
                rage128_ppll_commit(dev);
                dev->ppll_update_pending = 0;
            }
            rage128_recalctimings_apply(dev);
            return;
        case RAGE128_PLL_TEST_CNTL:
            if (mask & 0xff000000) {
                /* Writing the count byte restarts the test counter. */
                dev->pll_test_count_base = merged >> 24;
                dev->pll_test_zero_tsc   = tsc;
                dev->pll_test_acc        = 0;
            }
            dev->pll_regs[idx] = merged & 0x00ffffff;
            return;
        case RAGE128_PLL_VCLK_ECP_CNTL:
            dev->pll_regs[idx] = merged;
            rage128_recalctimings_apply(dev); /* pixel clock source change */
            return;
        case RAGE128_PLL_HTOTAL_CNTL:
            dev->pll_regs[idx] = merged;
            rage128_recalctimings_apply(dev); /* line-length slip change */
            return;
        case RAGE128_PLL_X_MPLL_REF_FB_DIV:
            dev->pll_regs[idx] = merged;
            /* The pixel clock follows when the PPLL reference is taken
               from MPllClk or XPllClk. */
            if ((dev->ppll_work[0] >> RAGE128_PPLL_REF_DIV_SRC_SHIFT) & RAGE128_PPLL_REF_DIV_SRC_MASK)
                rage128_recalctimings_apply(dev);
            return;
        default:
            dev->pll_regs[idx] = merged;
            return;
    }
}

/* ------------------------------------------------------------------ */
/* Palette through PALETTE_INDEX and PALETTE_DATA, one 24-bit entry per
   dword (RRG: PALETTE_INDEX, p. 3-199 / PDF 217; RRG: PALETTE_DATA,
   p. 3-200 / PDF 218). The legacy byte path at ports 3C6-3C9 stays in
   the svga core. Both reach the same palette RAM, the core's vgapal
   and pallook tables.                                                  */
/* ------------------------------------------------------------------ */
static void
rage128_palette_data_write(rage128_t *dev, uint32_t val, uint32_t mask)
{
    svga_t  *svga = &dev->svga;
    int      idx  = dev->palette_index & 0xff;
    uint8_t  r, g, b;
    uint32_t cur, merged;

    /* Build the current entry as the register would read it, so a write
       that covers only some byte lanes keeps the others. Layout: [7:0]
       blue, [15:8] green, [23:16] red (RRG: PALETTE_DATA, p. 3-200 /
       PDF 218). */
    if (dev->dac_cntl & RAGE128_DAC_8BIT_EN)
        cur = svga->vgapal[idx].b | ((uint32_t) svga->vgapal[idx].g << 8) | ((uint32_t) svga->vgapal[idx].r << 16);
    else
        cur = (svga->vgapal[idx].b >> 2) | ((uint32_t) (svga->vgapal[idx].g >> 2) << 8)
            | ((uint32_t) (svga->vgapal[idx].r >> 2) << 16);
    merged = (cur & ~mask) | (val & mask);

    b = merged & 0xff;
    g = (merged >> 8) & 0xff;
    r = (merged >> 16) & 0xff;
    if (!(dev->dac_cntl & RAGE128_DAC_8BIT_EN)) {
        /* With DAC_8BIT_EN clear, a write shifts each 6-bit value left by
           2 into the palette RAM (RRG: DAC_CNTL, p. 3-124 / PDF 142). */
        b = (b & 0x3f) << 2;
        g = (g & 0x3f) << 2;
        r = (r & 0x3f) << 2;
    }
    svga->vgapal[idx].r = r;
    svga->vgapal[idx].g = g;
    svga->vgapal[idx].b = b;
    svga->pallook[idx]  = makecol32(r, g, b);
    svga->fullchange    = svga->monitor->mon_changeframecount;
    dev->lut_dirty      = 1;
    atomic_store(&dev->conv_stale, 1);

    /* PALETTE_W_INDEX advances by one on each write (RRG: PALETTE_INDEX,
       p. 3-199 / PDF 217). */
    dev->palette_index = (dev->palette_index & ~0xffu) | ((idx + 1) & 0xff);
}

static uint32_t
rage128_palette_data_read(rage128_t *dev)
{
    svga_t  *svga = &dev->svga;
    int      idx  = (dev->palette_index >> 16) & 0xff;
    uint32_t v;

    if (dev->dac_cntl & RAGE128_DAC_8BIT_EN)
        v = svga->vgapal[idx].b | ((uint32_t) svga->vgapal[idx].g << 8) | ((uint32_t) svga->vgapal[idx].r << 16);
    else
        /* With DAC_8BIT_EN clear, a read shifts each 8-bit value right
           by 2 (RRG: DAC_CNTL, p. 3-124 / PDF 142). */
        v = (svga->vgapal[idx].b >> 2) | ((uint32_t) (svga->vgapal[idx].g >> 2) << 8)
            | ((uint32_t) (svga->vgapal[idx].r >> 2) << 16);

    /* PALETTE_R_INDEX advances by one on each read (RRG: PALETTE_INDEX,
       p. 3-199 / PDF 217). */
    dev->palette_index = (dev->palette_index & ~0xff0000u) | (((idx + 1) & 0xff) << 16);
    return v;
}

/* Palette lookup for 15 and 16 bpp scanout, which is how gamma ramps
   reach direct color. A 5-bit component indexes the palette at its
   value times 8, the 6-bit green of 16 bpp at its value times 4. That
   is the spacing xf86-video-r128 r128_driver.c R128LoadPalette uses to
   load the ramp at depths 15 and 16. */
uint32_t
rage128_conv_16to32(svga_t *svga, uint16_t color, uint8_t bpp)
{
    uint8_t r, g, b;

    if (!svga->lut_map)
        return (bpp == 15) ? video_15to32[color] : video_16to32[color];

    if (bpp == 15) {
        r = getcolr(svga->pallook[((color >> 10) & 0x1f) << 3]);
        g = getcolg(svga->pallook[((color >> 5) & 0x1f) << 3]);
        b = getcolb(svga->pallook[(color & 0x1f) << 3]);
        return (video_15to32[color] & 0xff000000) | makecol(r, g, b);
    }
    r = getcolr(svga->pallook[((color >> 11) & 0x1f) << 3]);
    g = getcolg(svga->pallook[((color >> 5) & 0x3f) << 2]);
    b = getcolb(svga->pallook[(color & 0x1f) << 3]);
    return (video_16to32[color] & 0xff000000) | makecol(r, g, b);
}

/* ------------------------------------------------------------------ */
/* VGA aperture banking. MEM_VGA_WP_SEL and MEM_VGA_RP_SEL page the
   A0000 window. The guide enables them with CRTC_EXT_CNTL.VGA_MEM_PS_EN
   (RRG: CRTC_EXT_CNTL, p. 3-65 / PDF 83). The model also applies them
   whenever CRTC_EXT_DISP_EN is set, because the video BIOS's VBE
   set-window handler programs them with VGA_MEM_PS_EN clear (RE:
   Rage128progl_zerostate.VBI @c000:45a6). A later text mode set does
   not reset them (observed in captured traffic), so in VGA mode with
   VGA_MEM_PS_EN clear the stale selects are ignored.                   */
/* ------------------------------------------------------------------ */
void
rage128_update_banking(rage128_t *dev)
{
    svga_t *svga  = &dev->svga;
    int     paged = (dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN)
        || (dev->crtc_ext_cntl & RAGE128_VGA_MEM_PS_EN);
    /* Page unit: 8 KB, or 32 KB with CRTC_EXT_CNTL.VGA_ATI_LINEAR [3].
       The guide gives no unit. The video BIOS VBE set-window handler
       tests that bit and writes the pair {8N, 8N+4} or {2N, 2N+1} for
       64 KB window N (RE: Rage128progl_zerostate.VBI @c000:45a6). */
    uint32_t unit = (dev->crtc_ext_cntl & RAGE128_CRTC_VGA_ATI_LINEAR)
        ? (RAGE128_VGA_PAGE_SIZE * 4)
        : RAGE128_VGA_PAGE_SIZE;

    /* In extended mode the CRTC fetches linearly from CRTC_OFFSET and
       CRTC_PITCH, so the VGA byte, word and doubleword addressing set
       by VGA CRTC registers 14h and 17h does not apply (modeled; the
       guide defines CRTC_EXT_DISP_EN only as the VGA or extended
       select, RRG: CRTC_GEN_CNTL, p. 3-62 / PDF 80). fb_only is the
       svga core flag for that, the same one vid_ati_mach64.c uses.
       Without it, a return from a DOS session leaves the text mode's
       word-mode bits in the VGA CRTC and the extended picture comes out
       scrambled although VRAM holds the right bytes (observed in the
       emulator). */
    svga->fb_only = !!(dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN);

    if (paged) {
        dev->bank_w[0] = (dev->mem_vga_wp_sel & 0x3ff) * unit;
        dev->bank_w[1] = ((dev->mem_vga_wp_sel >> 16) & 0x3ff) * unit;
        dev->bank_r[0] = (dev->mem_vga_rp_sel & 0x3ff) * unit;
        dev->bank_r[1] = ((dev->mem_vga_rp_sel >> 16) & 0x3ff) * unit;
    } else {
        dev->bank_w[0] = dev->bank_r[0] = 0x0000;
        dev->bank_w[1] = dev->bank_r[1] = 0x8000;
    }

    /* The A0000 window goes through the split-bank handlers
       (rage128_updatemapping); the legacy B-windows stay unbanked. */
    svga->write_bank = svga->read_bank = 0;
}

/* ------------------------------------------------------------------ */
/* Frame events: scanout geometry, flips, the vblank and vsync hooks.  */
/* ------------------------------------------------------------------ */

/* Bytes the CRTC fetches for one full frame at the current timings: the
   pitch (rowoffset counts 8-byte units) times the displayed lines, both
   fields when interlaced. */
static uint32_t
rage128_scanout_bytes(const svga_t *svga)
{
    if (svga->dispend <= 0)
        return 0;
    return (uint32_t) svga->rowoffset * 8u
        * ((uint32_t) svga->dispend << (svga->interlace ? 1 : 0));
}

/* Bytes per display line as the svga core's scan walks them: rowoffset
   counts 8-byte units unless the core is in its FLAG_NO_SHIFT3
   addressing mode, where it counts bytes. */
static uint32_t
rage128_scanout_pitch_bytes(const svga_t *svga)
{
    return (svga->adv_flags & FLAG_NO_SHIFT3)
        ? (uint32_t) svga->rowoffset
        : ((uint32_t) svga->rowoffset << 3);
}

/* Whether scanout is tiled right now. The guide requires a tiled
   display pitch to be a multiple of 64 bytes, the tile width (RRG:
   CRTC_PITCH, p. 3-74 / PDF 92), so the r128_tiled_ok check the drawing
   engines use applies to CRTC_OFFSET_CNTL.CRTC_TILE_EN too. */
static int
rage128_scanout_tiled_now(const rage128_t *dev)
{
    return r128_tiled_ok(dev->crtc_offset_cntl & RAGE128_CRTC_TILE_EN,
                         rage128_scanout_pitch_bytes(&dev->svga));
}

/* Tiled scanout anchor. In tiled mode CRTC_OFFSET holds the tiled byte
   address of the first displayed pixel, and CRTC_TILE_LINE holds that
   pixel's line so the address generator knows where inside a tile it
   starts. The guide's example: with the surface at 0 and the display
   starting on line 3, CRTC_OFFSET = 0xC0, 64 bytes per tile line times
   3 (RRG: CRTC_OFFSET, p. 3-70 / PDF 88; RRG: CRTC_OFFSET_CNTL,
   p. 3-71 / PDF 89). The guide's general formula writes that last term
   as 64 * (start line DIV 16); its own example needs MOD 16, which is
   what the code uses. The surface is at least 64-byte aligned (the
   smallest CRTC_TILE_ALIGN), so base & 63 is the byte within the tile
   width, and taking off both in-tile terms gives the top-left byte of
   the start pixel's tile. Every later fetch is this anchor plus the
   tile transform of (lines walked, bytes walked). */
static void
rage128_scanout_tile_anchor(rage128_t *dev, uint32_t base)
{
    dev->scanout_tile_xin = base & 63u;
    dev->scanout_tile_c0  = base - (dev->scanout_tile_line << 6)
        - dev->scanout_tile_xin;
}

/* Working start line for the tiled walk. CRTC_OFFSET_LOCK holds both
   CRTC_OFFSET and CRTC_TILE_LINE until it is cleared, so the two change
   together (RRG: CRTC_OFFSET_CNTL, p. 3-71 / PDF 89). A written
   CRTC_OFFSET is pending until the flip point takes it, and the flip
   latch captures the tile line with it. While the lock is held or a
   flip is pending the scan keeps the pair it shows, so a new line never
   reaches the screen ahead of its offset, or with the old one. With no
   flip pending the programmed line is taken at this refresh, which runs
   at vsync, after a timing recalculation, and right after a flip is
   taken at a horizontal blank. CRTC_TILE_LINE holds the
   start line mod 32 for the memory checkerboarding, which is not
   modeled; the tile transform needs only the line within the 16-line
   tile, mod 16. CPU thread only, like the rest of the scan state. */
static void
rage128_scanout_tile_line_refresh(rage128_t *dev)
{
    if (!dev->crtc_offset_lock && !dev->crtc_offset_pending)
        dev->crtc_tile_line_latched = dev->crtc_offset_cntl & 31u;
    dev->scanout_tile_line = dev->crtc_tile_line_latched & 15u;
}

static void rage128_latch_crtc_offset(rage128_t *dev);

/* With CRTC_OFFSET_CNTL.CRTC_OFFSET_FLIP_CNTL set, a new CRTC_OFFSET is
   taken at the next horizontal blank instead of at vertical blank
   (RRG: CRTC_OFFSET_CNTL, p. 3-72 / PDF 90). Each svga poll tick is a
   boundary between line phases, which stands in for the horizontal
   blank, so an armed flip is taken here and never in the middle of a
   line render. The latch only publishes crtc_offset_latched and
   crtc_tile_line_latched; rage128_scan_remap moves the remaining lines
   to the new base. The tiled walk's start line is taken from the
   latched pair here as well, because the lines still to come are
   fetched through the tile transform from the new offset and must use
   the CRTC_TILE_LINE that was published with it, not the one the frame
   started with; the anchor follows so that a flip which moves only the
   line, not the offset, also lands. This tick runs on the CPU thread,
   the owner of the scan state. */
void
rage128_crtc_offset_hblank_tick(rage128_t *dev)
{
    if (dev->crtc_offset_hblank && !dev->crtc_offset_lock) {
        dev->crtc_offset_hblank = 0;
        rage128_latch_crtc_offset(dev);
        rage128_scanout_tile_line_refresh(dev);
        if (dev->scanout_tiled)
            rage128_scanout_tile_anchor(dev, dev->crtc_offset_latched & 0x01fffff8);
        rage128_pm4_flip_notify(dev);
    }
}

/* CRTC_GUI_TRIG_VLINE [31]: the raster line is between
   CRTC_GUI_TRIG_VLINE_START and CRTC_GUI_TRIG_VLINE_END, both inclusive
   (RRG: CRTC_GUI_TRIG_VLINE, p. 3-69 / PDF 87). The register readback
   and the per-line tick share this test. */
static int
rage128_gui_trig_vline_in(rage128_t *dev)
{
    uint32_t strt = dev->crtc_gui_trig_vline & 0x7ff;
    uint32_t end  = (dev->crtc_gui_trig_vline >> 16) & 0x7ff;
    uint32_t line = (uint32_t) dev->svga.displine;

    return line >= strt && line <= end;
}

/* Per scanline: publish whether the raster is inside the window and
   count its entry and exit edges for the WAIT_UNTIL raster-line events,
   waking a command executor that is stalled on one. */
void
rage128_gui_trig_vline_tick(rage128_t *dev)
{
    int in = rage128_gui_trig_vline_in(dev);

    if (in == atomic_load(&dev->vline_in_window))
        return;
    atomic_store(&dev->vline_in_window, in);
    if (in)
        atomic_fetch_add(&dev->vline_rise_seq, 1);
    else
        atomic_fetch_add(&dev->vline_fall_seq, 1);
    rage128_pm4_flip_notify(dev);
}

/* Take the programmed CRTC_OFFSET as the scanout base, with the
   CRTC_TILE_LINE that goes with it (CRTC_OFFSET_LOCK holds the two
   fields together: RRG: CRTC_OFFSET_CNTL, p. 3-71 / PDF 89), and clear
   the flip-pending status, CRTC_GUI_TRIG_OFFSET, which the guide says
   goes low when display starts at the new address (RRG: CRTC_OFFSET,
   p. 3-70 / PDF 88). This can run on the CCE thread. It only publishes
   crtc_offset_latched and crtc_tile_line_latched; the scan picks the
   base up at the start of its next line in rage128_scan_remap, at worst
   one line late, and the tile line at its next scan-state refresh. */
static void
rage128_latch_crtc_offset(rage128_t *dev)
{
    svga_t *svga = &dev->svga;

    dev->crtc_offset_latched    = dev->crtc_offset;
    dev->crtc_tile_line_latched = dev->crtc_offset_cntl & 31u;
    dev->crtc_offset_pending    = 0;
    svga->memaddr_latch         = (dev->crtc_offset_latched & 0x01fffff8) >> 2;
    if (dev->ftl_en && !atomic_load(&dev->ftl_latch_ns))
        atomic_store(&dev->ftl_latch_ns, rage128_now_ns());
    if (dev->synctel)
        rage128_synctel_present(dev);
    if (dev->gpu)
        rage128_gpu_present(dev);
    /* Force full redraws for the new buffer, which has no change marks
       of its own. The vblank latch runs inside the core's vblank tick,
       which decrements fullchange right after this returns, so a value
       of 1 would reach the next frame as 0 and the new buffer would be
       drawn only where change marks happened to be set. The core's
       mon_changeframecount leaves at least one forced frame after the
       decrement; a latch in mid-frame gets the rest of this frame and
       the next. */
    svga->fullchange = svga->monitor->mon_changeframecount;
}

/* Wait out GPU work still in flight over the displayed buffer, on the
   CCE thread rather than the emulation thread. A flip does this when
   CRTC_OFFSET is written, but a client that draws into the buffer it is
   already displaying never writes CRTC_OFFSET, and without this call
   the first scanned line that overlaps the work would wait for a whole
   GPU frame inside the per-line barrier. Pixels are not affected: the
   same fences are waited on, from another thread. Tiled scanout is
   skipped, as in the flip path, because it reads through the tile
   transform and has no linear range. The timings are read from another
   thread here, as the flip path also does; a torn read only gives a
   wrong range, which can make this wait on the wrong slots but never
   lets a later reader skip a live fence. CCE thread only. */
void
rage128_scanout_fence_idle(rage128_t *dev)
{
    if (!dev->gpu || rage128_scanout_tiled_now(dev))
        return;
    rage128_gpu_present_fence(dev, dev->crtc_offset_latched & 0x01fffff8,
                              rage128_scanout_bytes(&dev->svga));
}

/* The svga core's scan address hook (remap_func), wrapped. It does two
   jobs.

   Flips: a CRTC_OFFSET flip taken in mid-frame (CRTC_OFFSET_FLIP_CNTL
   set) only publishes crtc_offset_latched. The first fetch after that
   adds the difference between the latched base and the base the scan
   was seeded from to memaddr and memaddr_backup and records the new
   base, so this line and the ones after it read the new buffer through
   the renderer's fast path; remap_required stays clear for a linear
   frame. This is safe because every renderer calls remap_func at the
   start of a line on the CPU thread, the thread that also updates
   memaddr each line. The flip store from the CCE thread is an aligned
   32-bit value, read here at worst one line late.

   Tiling: for a tiled frame remap_required is set, every fetch comes
   through here, and the linear address the core walks is mapped
   through the tile transform.

   When a GPU present snapshot is on display (scanout_snap_cur, chosen
   once per frame at vsync) the scan stays on it for the whole frame:
   the guest buffer may already hold the next frame's drawing, and
   switching in mid-frame would leave a seam. */
static uint32_t
rage128_scan_remap(svga_t *svga, uint32_t in_addr)
{
    rage128_t     *dev  = (rage128_t *) svga->priv;
    const uint32_t base = dev->scanout_snap_cur
        ? dev->scanout_snap_cur
        : (dev->crtc_offset_latched & 0x01fffff8);
    const uint32_t bias = base - dev->scanout_seed_base;

    if (bias) {
        svga->memaddr += bias;
        svga->memaddr_backup += bias;
        dev->scanout_seed_base = base;
        if (dev->scanout_tiled)
            rage128_scanout_tile_anchor(dev, base);
    }
    {
        uint32_t out        = dev->scan_remap_orig(svga, in_addr) + bias;
        int      line_start = 1;
        uint32_t rng_base   = out;
        uint32_t rng_len    = (uint32_t) svga->hdisp
            * (uint32_t) ((svga->bpp + 7) >> 3);

        if (dev->scanout_tiled) {
            /* Recover (line, byte in line) from the core's linear
               walk and map both through the tile transform from the
               frame's tile anchor. The per-line work below runs only
               on a line's first byte, and a tiled line's bytes are
               spread over its 16-line tile row, so the range checked
               for GPU hazards and recorded by synctel is that tile
               row, not one linear line. */
            uint32_t delta = out - base;
            uint32_t n     = delta / dev->scanout_tile_pitch;
            uint32_t dx    = delta - n * dev->scanout_tile_pitch;

            out = dev->scanout_tile_c0
                + r128_tile_off(dev->scanout_tile_xin + dx,
                                dev->scanout_tile_line + n,
                                dev->scanout_tile_pitch);
            line_start = (dx == 0);
            rng_base   = dev->scanout_tile_c0
                + r128_tile_row_start(dev->scanout_tile_line + n,
                                      dev->scanout_tile_pitch);
            rng_len = 16u * dev->scanout_tile_pitch;
        }

        if (line_start && dev->synctel && svga->hdisp > 0)
            rage128_synctel_scanline(dev, rng_base & dev->vram_mask, rng_len);
        /* A snapshot line reads bytes in the hidden upper half of VRAM
           that do not change, so it has no hazard, and masking its
           address into guest range would invent one. A guest-buffer
           line waits on GPU work only while the once-per-frame hazard
           check at vsync found GPU writes in the scanned range. Every
           change of that state forces a full redraw, so a write that
           lands after an all-clear shows one frame late. That is
           treated as the ordinary race between scanout and drawing into
           the displayed buffer, not as an error. */
        if (line_start && dev->gpu && svga->hdisp > 0 && dev->scanout_hazard
            && !rage128_gpu_scan_barrier(dev, rng_base & dev->vram_mask,
                                         rng_len)) {
            /* The line was drawn before a GPU write into the displayed
               buffer completed, so it may show a tear. The change mark
               the 2D operation set can expire before a slow GPU slot
               finishes, so mark the line's pages again for the next
               frame. */
            uint32_t lo = rng_base & dev->vram_mask;
            uint32_t hi = (rng_base + rng_len - 1u) & dev->vram_mask;

            svga->changedvram[lo >> 12] = svga->monitor->mon_changeframecount;
            svga->changedvram[hi >> 12] = svga->monitor->mon_changeframecount;
        }
        return out;
    }
}

/* Rebuild the palette cache after a write set lut_dirty. An identity
   ramp, the usual desktop case, sets lut_identity so the line
   converters can use a plain mask. Any other ramp, such as the one
   Quake III's r_gamma setting loads, gets one table per channel with
   each entry already shifted into its byte position, giving the same
   result as svga_lookup_lut_ram: red out = getcolr(pallook[red in]),
   and so on, with the alpha byte of makecol32. The scan runs only after
   a change; software loads gamma ramps at a mode set or program start,
   not every frame. */
static void
rage128_lut_update(rage128_t *dev)
{
    const svga_t *svga = &dev->svga;
    int           id   = 1;

    if (!dev->lut_dirty)
        return;
    for (int i = 0; i < 256; i++) {
        uint32_t e = svga->pallook[i];

        dev->lut_b[i] = getcolb(e);
        dev->lut_g[i] = (uint32_t) getcolg(e) << 8;
        dev->lut_r[i] = (uint32_t) getcolr(e) << 16;
        if ((e & 0xffffff)
            != ((uint32_t) i | ((uint32_t) i << 8) | ((uint32_t) i << 16)))
            id = 0;
    }
    dev->lut_identity = id;
    dev->lut_dirty    = 0;
}

/* Convert one line of 32 bpp pixels. The emulation-thread renderer and
   the conversion worker both call this, so they produce the same
   bytes: a plain mask for an identity ramp, the per-channel tables
   otherwise. */
static void
rage128_conv_line(const rage128_t *dev, const uint8_t *src, uint32_t *dst,
                  uint32_t count)
{
    if (dev->lut_identity) {
        for (uint32_t x = 0; x < count; x++) {
            uint32_t dat;

            memcpy(&dat, src + x * 4, 4);
            dst[x] = (dat & 0xffffff) | 0xff000000u;
        }
    } else {
        for (uint32_t x = 0; x < count; x++) {
            uint32_t dat;

            memcpy(&dat, src + x * 4, 4);
            dst[x] = dev->lut_b[dat & 0xff] | dev->lut_g[(dat >> 8) & 0xff]
                | dev->lut_r[(dat >> 16) & 0xff] | 0xff000000u;
        }
    }
}

/* Conversion worker thread. Each kick converts one whole snapshot frame
   into conv_buf, top to bottom, publishing its progress one row at a
   time in conv_done. A palette write sets conv_stale and the worker
   stops. Rows it finished before the write carry the old ramp, which is
   what the svga core's renderer would also have put on lines it had
   already drawn. The renderer stops using finished rows as soon as
   conv_stale is set and converts the remaining rows itself with the
   new ramp. */
static void
rage128_conv_thread(void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    while (1) {
        thread_wait_event(dev->conv_wake, -1);
        thread_reset_event(dev->conv_wake);
        if (!dev->conv_run)
            break;
        if (!atomic_load(&dev->conv_busy))
            continue;
        for (uint32_t r = 0; r < dev->conv_lines; r++) {
            if (atomic_load(&dev->conv_stale) || !dev->conv_run)
                break;
            rage128_conv_line(dev,
                              &dev->svga.vram[(dev->conv_base + r * dev->conv_stride)
                                              & dev->svga.vram_display_mask],
                              (uint32_t *) &dev->conv_buf[(size_t) r * dev->conv_count * 4],
                              dev->conv_count);
            atomic_store(&dev->conv_done, (int) r + 1);
        }
        atomic_store(&dev->conv_busy, 0);
    }
}

void
rage128_conv_close(rage128_t *dev)
{
    if (dev->conv_thread) {
        dev->conv_run = 0;
        thread_set_event(dev->conv_wake);
        thread_wait(dev->conv_thread);
        dev->conv_thread = NULL;
    }
    free(dev->conv_buf);
    dev->conv_buf = NULL;
}

/* Fast 32 bpp line renderer. The svga core's svga_render_32bpp_highres
   calls svga_lookup_lut_ram for every pixel. This version converts
   through the cached tables of rage128_lut_update, and for an identity
   ramp the conversion is (dat & 0xffffff) | 0xff000000, a loop the
   compiler can vectorize. It keeps the core renderer's structure (one
   remap_func call at line start, the same changedvram test, the same
   memaddr advance) so the output is the same. A line the linear loop
   cannot handle, with remap_required set or a VRAM wrap inside it, goes
   to the core renderer. */
static void
rage128_render_32bpp_fast(svga_t *svga)
{
    int       y = svga->displine + svga->y_add;
    uint32_t  changed_addr;
    uint32_t  count;
    uint32_t  base;
    uint32_t *p;

    if ((y < 0) || (svga->monitor->target_buffer == NULL)
        || (svga->monitor->target_buffer->line[y] == NULL))
        return;

    changed_addr = svga->remap_func(svga, svga->memaddr);
    if (!(svga->changedvram[changed_addr >> 12]
          || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange))
        return;

    count = (uint32_t) (svga->hdisp + svga->scrollcache) + 1;
    base  = svga->memaddr & svga->vram_display_mask;
    if (svga->remap_required
        || (uint64_t) base + count * 4 > (uint64_t) svga->vram_display_mask + 1) {
        svga_render_32bpp_highres(svga);
        return;
    }

    p = &svga->monitor->target_buffer->line[y][svga->x_add];
    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    {
        rage128_t     *dev    = (rage128_t *) svga->priv;
        const uint8_t *src    = &svga->vram[base];
        uint32_t       row    = (uint32_t) svga->displine;
        int            served = 0;

        /* Copy from the worker's frame only when this line reads exactly
           the source address the worker used for the same row. Any
           other scan state (interlace, a split screen, a flip bias)
           fails that check and the line is converted here instead. After
           a palette write (conv_stale) no row is taken from the worker.
           R128_CONV_VERIFY (conv_verify) converts the line here as well
           and logs any difference. */
        if (dev->conv_active && !atomic_load(&dev->conv_stale)
            && row < dev->conv_lines && count == dev->conv_count
            && base == ((dev->conv_base + row * dev->conv_stride) & svga->vram_display_mask)
            && (int) row < atomic_load(&dev->conv_done)) {
            const uint8_t *crow = &dev->conv_buf[(size_t) row * count * 4];

            if (dev->conv_verify) {
                rage128_conv_line(dev, src, p, count);
                if (memcmp(p, crow, (size_t) count * 4)) {
                    if (dev->conv_mismatch++ < 8)
                        pclog("RAGE128 conv: MISMATCH row=%u base=%08x\n",
                              row, base);
                } else
                    dev->conv_rows_win++;
            } else {
                memcpy(p, crow, (size_t) count * 4);
                dev->conv_rows_win++;
            }
            served = 1;
        }
        if (!served)
            rage128_conv_line(dev, src, p, count);
    }
    svga->memaddr = (svga->memaddr + count * 4) & svga->vram_display_mask;
}

/* Convert one line of 16 bpp (565) pixels through the same three
   channel tables the 32 bpp line uses, indexed the way
   rage128_conv_16to32 indexes the palette (5-bit fields times 8, green
   times 4), so the output matches the core renderer for identity and
   gamma ramps alike. */
static void
rage128_conv_line16(const rage128_t *dev, const uint8_t *src, uint32_t *dst,
                    uint32_t count)
{
    for (uint32_t x = 0; x < count; x++) {
        uint16_t c;

        memcpy(&c, src + x * 2, 2);
        dst[x] = dev->lut_b[(c & 0x1f) << 3] | dev->lut_g[((c >> 5) & 0x3f) << 2]
            | dev->lut_r[((c >> 11) & 0x1f) << 3] | 0xff000000u;
    }
}

/* Fast 16 bpp line renderer. The core's svga_render_16bpp_highres makes
   an indirect conv_16to32 call for every pixel, three palette reads
   each when lut_map is set. Same structure and fallbacks as the 32 bpp
   renderer above. */
static void
rage128_render_16bpp_fast(svga_t *svga)
{
    int       y = svga->displine + svga->y_add;
    uint32_t  changed_addr;
    uint32_t  count;
    uint32_t  base;
    uint32_t *p;

    if ((y < 0) || (svga->monitor->target_buffer == NULL)
        || (svga->monitor->target_buffer->line[y] == NULL))
        return;

    changed_addr = svga->remap_func(svga, svga->memaddr);
    if (!(svga->changedvram[changed_addr >> 12]
          || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange))
        return;

    /* The core's loop works in groups of 8 pixels and advances memaddr
       by the rounded-up width; do the same so the output and the address
       stepping match. */
    count = ((uint32_t) (svga->hdisp + svga->scrollcache) + 8) & ~7u;
    base  = svga->memaddr & svga->vram_display_mask;
    if (svga->remap_required
        || (uint64_t) base + count * 2 > (uint64_t) svga->vram_display_mask + 1) {
        svga_render_16bpp_highres(svga);
        return;
    }

    p = &svga->monitor->target_buffer->line[y][svga->x_add];
    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    {
        rage128_t *dev = (rage128_t *) svga->priv;

        /* R128_CONV_VERIFY: draw the line both ways and compare, taking
           the core renderer's output and memaddr stepping as correct. */
        if (dev->conv_verify && count <= 2048) {
            uint32_t tmp[2048];
            uint32_t ma = svga->memaddr;

            rage128_conv_line16(dev, &svga->vram[base], tmp, count);
            svga_render_16bpp_highres(svga);
            if (memcmp(p, tmp, (size_t) count * 4)
                || svga->memaddr != ((ma + count * 2) & svga->vram_display_mask)) {
                /* A VRAM or palette write from the CCE thread between
                   the two renders is not a conversion error. Convert
                   again with fresh tables; a set lut_dirty, which was
                   clear at line start, shows a palette write came in
                   during the line. Such cases count as transient. */
                int pw = dev->lut_dirty;

                dev->lut_dirty = 1;
                rage128_lut_update(dev);
                rage128_conv_line16(dev, &svga->vram[base], tmp, count);
                if (svga->memaddr == ((ma + count * 2) & svga->vram_display_mask)
                    && memcmp(p, tmp, (size_t) count * 4) == 0)
                    dev->conv_transient++;
                else if (pw)
                    dev->conv_transient++;
                else if (dev->conv_mismatch++ < 8) {
                    uint32_t nd = 0, x0 = count, x1 = 0;

                    for (uint32_t x = 0; x < count; x++)
                        if (p[x] != tmp[x]) {
                            nd++;
                            if (x < x0)
                                x0 = x;
                            x1 = x;
                        }
                    pclog("RAGE128 conv16: MISMATCH row=%d base=%08x diff=%u x=%u..%u\n",
                          svga->displine, base, nd, x0, x1);
                }
            } else
                dev->conv_rows_win++;
            return;
        }
        rage128_conv_line16(dev, &svga->vram[base], p, count);
    }
    svga->memaddr = (svga->memaddr + count * 2) & svga->vram_display_mask;
}

/* 320-pixel-wide extended modes. The CRTC scans 320 pixels at a low
   pixel clock and the monitor stretches them across the screen; this
   presents each pixel twice so the picture keeps its shape. It is done
   here because the svga core's own 320-to-640 handling switches to its
   *_lowres renderers, whose packed-linear path does not double: it
   reads 640 source pixels at a 320-pixel pitch and shows two copies
   side by side. hdisp is 640 for the blit; the wrapped renderer runs at
   320. */
static void
rage128_render_hdbl(svga_t *svga)
{
    rage128_t *dev = (rage128_t *) svga->priv;
    int        y   = svga->displine + svga->y_add;
    uint32_t   ca;
    int        drew;

    svga->hdisp = 320;
    ca          = svga->remap_func(svga, svga->memaddr);
    drew        = svga->changedvram[ca >> 12]
        || svga->changedvram[(ca >> 12) + 1] || svga->fullchange;
    dev->render_hdbl_base(svga);
    svga->hdisp = 640;

    if (!drew || (y < 0) || (svga->monitor->target_buffer == NULL)
        || (svga->monitor->target_buffer->line[y] == NULL))
        return;

    uint32_t *p = &svga->monitor->target_buffer->line[y][svga->x_add];
    for (int x = 319; x >= 0; x--)
        p[(x << 1) + 1] = p[x << 1] = p[x];
}

/* Overscan border. Any nonzero width enables it. Left and right widths
   count 8-pixel characters, top and bottom count lines, and OVR_CLR is a
   24-bit color that does not go through the palette (RRG: Overscan,
   p. 3-79 / PDF 97). The border lies outside the active display:
   CRTC_H_DISP and CRTC_V_DISP do not include it (RRG: CRTC_H_TOTAL_DISP,
   p. 3-67 / PDF 85; RRG: CRTC_V_TOTAL_DISP, p. 3-68 / PDF 86). The
   core's own border calculation stays off (hoverride), because the
   core is given sync start and width here and would take the back
   porch for a left border. The totals are clamped to the 2048-pixel
   render buffer. The core splits each total evenly between the two
   sides, so rage128_vsync_callback sets the left and top edges again. */
static void
rage128_overscan_apply(rage128_t *dev, svga_t *svga)
{
    uint32_t left   = ((dev->ovr_wid_left_right >> 16) & 0x3f) * 8;
    uint32_t right  = (dev->ovr_wid_left_right & 0x3f) * 8;
    uint32_t top    = (dev->ovr_wid_top_bottom >> 16) & 0x1ff;
    uint32_t bottom = dev->ovr_wid_top_bottom & 0x1ff;
    uint32_t budget;

    budget = (svga->hdisp < 2048) ? (2048u - (uint32_t) svga->hdisp) : 0;
    if (left > budget)
        left = budget;
    budget -= left;
    if (right > budget)
        right = budget;

    budget = (svga->dispend < 2048) ? (2048u - (uint32_t) svga->dispend) : 0;
    if (top > budget)
        top = budget;
    budget -= top;
    if (bottom > budget)
        bottom = budget;

    dev->ovr_left = left;
    dev->ovr_top  = top;

    if ((svga->monitor->mon_overscan_x != (int) (left + right))
        || (svga->monitor->mon_overscan_y != (int) (top + bottom)))
        video_force_resize_set_monitor(1, svga->monitor_index);

    svga->monitor->mon_overscan_x = (int) (left + right);
    svga->monitor->mon_overscan_y = (int) (top + bottom);
    /* The border is drawn to the DAC like everything else, so a powered-down
       or forced DAC replaces it too. */
    svga->overscan_color = dev->dac_const_on
        ? dev->dac_const_color
        : makecol32((dev->ovr_clr >> 16) & 0xff,
                    (dev->ovr_clr >> 8) & 0xff,
                    dev->ovr_clr & 0xff);
}

/* A write to OVR_CLR, OVR_WID_LEFT_RIGHT or OVR_WID_TOP_BOTTOM updates
   the border at once, without a mode set. In VGA mode the core's border
   from the attribute controller stays in use; extended overscan in VGA
   modes (CRTC_EXT_CNTL.CRTC_VGA_XOVERSCAN, RRG: CRTC_EXT_CNTL, p. 3-63 /
   PDF 81) is not modeled. */
static void
rage128_overscan_touch(rage128_t *dev, svga_t *svga)
{
    if (!(dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN))
        return;

    rage128_overscan_apply(dev, svga);
    svga->fullchange = svga->monitor->mon_changeframecount;
}

/* Forced DAC data: the 8-bit DAC_FORCE_DATA value driven onto the DAC
   inputs, with DAC_FORCE_DATA_SEL [7:6] choosing red, green, blue or all
   three. The guide's first table gives the unselected lines as 0 (RRG:
   DAC_EXT_CNTL, p. 3-217 / PDF 235). */
static uint32_t
rage128_dac_forced_color(const rage128_t *dev)
{
    uint8_t v = (uint8_t) (dev->dac_ext_cntl >> RAGE128_DAC_FORCE_DATA_SHIFT);

    switch ((dev->dac_ext_cntl >> RAGE128_DAC_FORCE_DATA_SEL_SHIFT) & 3) {
        case 0:
            return makecol32(v, 0, 0);
        case 1:
            return makecol32(0, v, 0);
        case 2:
            return makecol32(0, 0, v);
        default:
            return makecol32(v, v, v);
    }
}

/* Black past the active area. With no border programmed, nothing but
   blank follows the last active pixel of a line or the last active
   line, so the two buffer pixels past the right and bottom edges are
   cleared to black. The host scaler filters across the edge of the
   blitted area, and without this the leftover content of an earlier,
   larger mode shows as a one-pixel line along the last column and row. */
static void
rage128_render_blank_ring(svga_t *svga)
{
    bitmap_t *bm = svga->monitor->target_buffer;
    int       y  = svga->displine + svga->y_add;
    int       x  = svga->x_add + svga->hdisp;

    if ((bm == NULL) || (y < 0) || (y >= 2048) || (svga->hdisp <= 0) || (x < 0))
        return;
    if (bm->line[y] != NULL)
        for (int i = 0; i < 2 && (x + i) < bm->w; i++)
            bm->line[y][x + i] = 0;
    if (svga->displine == (svga->dispend - 1)) {
        int w = x + 2;

        if (w > bm->w)
            w = bm->w;
        for (int r = y + 1; r <= y + 2 && r < 2048; r++)
            if (bm->line[r] != NULL)
                memset(bm->line[r], 0, (size_t) w * sizeof(uint32_t));
    }
}

/* One color for the whole line: the output while the DAC is powered
   down (DAC_CNTL.DAC_PDWN) or its inputs are forced
   (DAC_EXT_CNTL.DAC_FORCE_DATA_EN). It replaces the pixel renderer. The
   cursor and the overlay are drawn after the renderer, so their own
   draw hooks check for this case and draw nothing. On the chip both are
   part of the data sent to the DAC (the DAC_CRC_EN description lists
   graphics, cursor, overlay and overscan; RRG: DAC_CNTL, p. 3-125 /
   PDF 143). */
static void
rage128_render_dac_const(svga_t *svga)
{
    const rage128_t *dev = (rage128_t *) svga->priv;
    int              y   = svga->displine + svga->y_add;
    uint32_t        *p;
    int              w;

    if ((y < 0) || (y >= 2048) || (svga->monitor->target_buffer == NULL)
        || (svga->monitor->target_buffer->line[y] == NULL) || (svga->hdisp <= 0))
        return;

    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    p = svga->monitor->target_buffer->line[y];
    w = svga->hdisp;
    if (svga->x_add > 0) {
        p += svga->x_add;
        if ((svga->x_add + w) > 2048)
            w = 2048 - svga->x_add;
    }
    for (int x = 0; x < w; x++)
        p[x] = dev->dac_const_color;
}

/* Packed 4 bpp extended scanout (CRTC_PIX_WIDTH = 1, RRG:
   CRTC_GEN_CNTL, p. 3-62 / PDF 80): two palette indices per byte, the
   left pixel in the high nibble, or in the low nibble with
   DAC_4BPP_PIX_ORDER set (RRG: DAC_CNTL, p. 3-124 / PDF 142). The core's
   4 bpp renderers implement VGA planar memory; this one reads packed
   linear memory like the other extended depths, and calls remap_func
   for each dword so a tiled frame scans correctly. DAC_MASK has no
   effect in extended modes, so no mask is applied.                     */
static void
rage128_render_4bpp_ext(svga_t *svga)
{
    const rage128_t *dev  = (const rage128_t *) svga->priv;
    const int        y    = svga->displine + svga->y_add;
    const int        lsbl = !!(dev->dac_cntl & RAGE128_DAC_4BPP_PIX_ORDER);
    uint32_t        *p;
    uint32_t         addr;

    if ((y < 0) || (svga->monitor->target_buffer == NULL)
        || (svga->monitor->target_buffer->line[y] == NULL))
        return;

    addr = svga->force_old_addr ? svga->memaddr
                                : svga->remap_func(svga, svga->memaddr);
    if (!svga->changedvram[addr >> 12] && !svga->changedvram[(addr >> 12) + 1]
        && !svga->fullchange)
        return;

    p = &svga->monitor->target_buffer->line[y][svga->x_add];

    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    for (int x = 0; x <= svga->hdisp; x += 8) {
        uint32_t dat;

        if (svga->remap_required && !svga->force_old_addr)
            addr = svga->remap_func(svga, svga->memaddr);
        else
            addr = svga->memaddr;
        dat = *(uint32_t *) (&svga->vram[addr & svga->vram_display_mask]);

        for (int i = 0; i < 4; i++) {
            const uint8_t b = (uint8_t) (dat >> (i * 8));

            p[i * 2]     = svga->map8[lsbl ? (b & 0x0f) : (b >> 4)];
            p[i * 2 + 1] = svga->map8[lsbl ? (b >> 4) : (b & 0x0f)];
        }

        svga->memaddr += 4;
        p += 8;
    }
    svga->memaddr &= svga->vram_display_mask;
}

/* Wrapper around the core's line renderer. It sends 32 bpp and 16 bpp
   lines to the fast renderers above, clears the edge past the active
   area, and, under R128_SLICE_PROF, adds the time spent to the svga=
   field of the pace log line. That time includes the scanout fence
   waits taken inside rage128_scan_remap, so it overlaps the rf= field.
   svga_recalctimings replaces svga->render on a mode change; the vsync
   callback installs the wrapper again, as it does the remap wrapper. */
static void
rage128_render_timed(svga_t *svga)
{
    rage128_t *dev = (rage128_t *) svga->priv;
    uint64_t   t0  = dev->slice_prof ? rage128_now_ns() : 0;

    if (dev->svga_render_orig == svga_render_32bpp_highres
        && !svga->force_old_addr && (svga->hdisp + svga->scrollcache) >= 0
        && svga->lut_map) {
        rage128_lut_update(dev);
        rage128_render_32bpp_fast(svga);
    } else if (dev->svga_render_orig == svga_render_16bpp_highres
               && !svga->force_old_addr && (svga->hdisp + svga->scrollcache) >= 0
               && svga->lut_map) {
        rage128_lut_update(dev);
        rage128_render_16bpp_fast(svga);
    } else
        dev->svga_render_orig(svga);
    rage128_render_blank_ring(svga);
    if (dev->slice_prof)
        dev->svga_win_ns += rage128_now_ns() - t0;
}

/* Wrapper around svga_poll, the core's line timer callback, which runs
   about twice per scan line. It first runs the per-tick checks for a
   CRTC_OFFSET flip waiting for horizontal blank and for the
   CRTC_GUI_TRIG_VLINE window, so it stays installed with profiling
   off. Under R128_SLICE_PROF it adds the whole poll time (timer
   dispatch, address stepping, change checks, rendering) to the svgap=
   field of the pace log line. It is installed by pointing the svga
   timer callback at it; svga_set_poll can point the callback back, so
   the vsync callback and rage128_recalctimings_apply check it again. */
static void
rage128_svga_poll_timed(void *priv)
{
    svga_t    *svga = (svga_t *) priv;
    rage128_t *dev  = (rage128_t *) svga->priv;
    uint64_t   t0   = dev->slice_prof ? rage128_now_ns() : 0;

    rage128_crtc_offset_hblank_tick(dev);
    rage128_gui_trig_vline_tick(dev);

    svga_poll(priv);
    if (dev->slice_prof)
        dev->svgap_win_ns += rage128_now_ns() - t0;
}

/* Realtime pacer control loop, run from the vsync callback on the CPU
   thread; the pacer as a whole is described at pace_enabled in
   vid_ati_rage128.h. The goal is 100% emulation speed, and the frame
   rate is whatever is left. Once at least 250 ms of wall time have
   passed, the loop compares the emulated time that elapsed with the
   wall time. The window is that long because plat_get_ticks counts
   whole milliseconds: at 250 ms the rounding stays under 1%, while a
   100 ms window makes the loop oscillate on the rounding noise
   (measured in the emulator).

   Slip is wall time minus emulated time, and the band is pace_band_x
   microseconds per wall millisecond. Slip past the band raises the
   per-flip hold by three times the excess. A window inside the band
   lowers it by three times the distance below the band edge (so a
   window with no slip lowers it by three times the band), or by
   hold / 32 + 200 us when that is larger. Either step is at most 25 ms
   a window. A window with no 3D rendering lowers the hold by 2 ms, so a
   hold left over from earlier 3D work does not slow the start of the
   next. The hold is always clamped to 0-100 ms. A hold at the maximum
   that still slips means the CPU side alone is too slow, and the slip
   left over is what the window reports. The cap also bounds the effect
   of a host stall (paging, a switch to another program), so the engine
   can never freeze.

   Slip from waiting on GPU work is not handled here: the depth limit in
   rage128_gpu_pace_window works from the fence wait time itself, and
   the two loops share no state. The window bookkeeping and the pace log
   line run with pace_enabled off too, so a run without the pacer still
   reports its emulation speed. */
static void
rage128_pace_update(rage128_t *dev)
{
    uint32_t now_ms;
    uint32_t wall_ms;
    uint64_t emu_us;
    int64_t  slip_us;
    uint64_t rf_us = 0;
    unsigned cap   = 0;

    now_ms = plat_get_ticks();
    if (!dev->pace_wall_ms_last) {
        dev->pace_wall_ms_last = now_ms ? now_ms : 1;
        dev->pace_tsc_last     = tsc;
        return;
    }
    wall_ms = now_ms - dev->pace_wall_ms_last;
    if (wall_ms < 250)
        return;
    /* Ticks to microseconds the way timer_get_remaining_us does it:
       TIMER_USEC is ticks per microsecond in 32.32 fixed point, so the
       tick count is shifted up 32 bits before the divide. */
    emu_us                 = (uint64_t) (((unsigned __int128) (tsc - dev->pace_tsc_last) << 32) / TIMER_USEC);
    slip_us                = (int64_t) wall_ms * 1000 - (int64_t) emu_us;
    dev->pace_wall_ms_last = now_ms ? now_ms : 1;
    dev->pace_tsc_last     = tsc;
    if (dev->gpu)
        rf_us = rage128_gpu_pace_window(dev, wall_ms, &cap);
    if (dev->pace_enabled && dev->cce_thread) {
        int64_t band_us = (int64_t) wall_ms * dev->pace_band_x;
        int64_t excess  = slip_us - band_us;
        int     hold    = atomic_load(&dev->pace_hold_us);

#ifdef REG_HARNESS
        /* A harness barrier places producer activity on either side of
           the sample without relying on host thread timing. */
        extern void (*harness_pace_sample_hook)(void *dev, int phase);
        if (harness_pace_sample_hook)
            harness_pace_sample_hook(dev, 0);
#endif
        if (!atomic_exchange(&dev->pace_3d_seen, 0))
            hold -= 2000;
        else if (excess > 0)
            hold += (excess > 8333) ? 25000 : (int) (3 * excess);
        else {
            /* Lower the hold with the same gain of 3 the raise uses,
               applied to the distance below the band edge: a window
               inside the band ran faster than the edge allows, which
               shows the hold is larger than needed. The hold / 32 +
               200 us floor matters only for windows close to the edge.
               A fixed-rate decay in its place measures worse. */
            int64_t give = -excess;
            int     dec  = (give > 8333) ? 25000 : (int) (3 * give);
            int     tr   = (hold >> 5) + 200;

            hold -= (dec > tr) ? dec : tr;
        }
#ifdef REG_HARNESS
        if (harness_pace_sample_hook)
            harness_pace_sample_hook(dev, 1);
#endif
        if (hold > 100000)
            hold = 100000;
        if (hold < 0)
            hold = 0;
        atomic_store(&dev->pace_hold_us, hold);
    }
    /* One log line per window, with the emulation speed in tenths of a
       percent so a run's average can be computed from the log. rf= and
       cap= are the GPU depth limit's input and output, logged next to
       the speed they are meant to protect. */
    dev->pace_emu_us_tot += emu_us;
    dev->pace_wall_ms_tot += wall_ms;
    pclog("RAGE128 pace: win=%ums emu=%llu.%llu%% rf=%lluus flip=%lluus "
          "pump=%lluus mmr=%lluus mmw=%lluus lfb=%lluus svga=%lluus "
          "svgap=%lluus cv=%llu cap=%u hold=%dus slept=%llu/%lluus "
          "cvt=%llu cvm=%llu\n",
          wall_ms,
          (unsigned long long) (emu_us / wall_ms / 10),
          (unsigned long long) (emu_us / wall_ms % 10),
          (unsigned long long) rf_us,
          (unsigned long long) (dev->flip_stall_ns / 1000),
          (unsigned long long) (dev->pump_win_ns / 1000),
          (unsigned long long) (dev->mmr_win_ns / 1000),
          (unsigned long long) (dev->mmw_win_ns / 1000),
          (unsigned long long) (dev->lfbw_win_ns / 1000),
          (unsigned long long) (dev->svga_win_ns / 1000),
          (unsigned long long) (dev->svgap_win_ns / 1000),
          (unsigned long long) dev->conv_rows_win, cap,
          atomic_load(&dev->pace_hold_us),
          (unsigned long long) (atomic_exchange(&dev->pace_sleep_ns, 0) / 1000),
          (unsigned long long) (atomic_exchange(&dev->pace_sleep_ask_ns, 0) / 1000),
          (unsigned long long) dev->conv_transient,
          (unsigned long long) dev->conv_mismatch);
    dev->flip_stall_ns = 0;
    dev->pump_win_ns   = 0;
    dev->mmr_win_ns    = 0;
    dev->mmw_win_ns    = 0;
    dev->lfbw_win_ns   = 0;
    dev->svga_win_ns   = 0;
    dev->svgap_win_ns  = 0;
    dev->conv_rows_win = 0;
}

/* One step of the DAC CRC for one non-blank pixel, in raster order,
   the guide's recurrence (RRG: DAC_CRC_SIG, p. 3-126 / PDF 144). Each
   channel has an 8-bit accumulator. Bits 7:1 become the pixel's bits
   7:1 XOR the old bits 6:0. Bit 0 becomes the pixel's bit 0 XOR the old
   bits 0 and 7, and for blue and green also XOR bit 7 of the next
   accumulator (green for blue, red for green). The signature is blue,
   green, red from bit 23 down. */
static inline void
rage128_dac_crc_step(uint8_t *cb, uint8_t *cg, uint8_t *cr, uint32_t px)
{
    const uint8_t r   = (uint8_t) (px >> 16);
    const uint8_t g   = (uint8_t) (px >> 8);
    const uint8_t b   = (uint8_t) px;
    const uint8_t ob  = *cb;
    const uint8_t og  = *cg;
    const uint8_t orr = *cr;

    *cb = (uint8_t) (((b ^ (ob << 1)) & 0xfe)
                     | ((b ^ ob ^ (ob >> 7) ^ (og >> 7)) & 1));
    *cg = (uint8_t) (((g ^ (og << 1)) & 0xfe)
                     | ((g ^ og ^ (og >> 7) ^ (orr >> 7)) & 1));
    *cr = (uint8_t) (((r ^ (orr << 1)) & 0xfe)
                     | ((r ^ orr ^ (orr >> 7)) & 1));
}

/* Compute the CRC of the field or frame that just ended, at the start
   of vertical blank. It walks the visible raster the DAC sent: border
   rows and edges in overscan_color, active pixels from the finished
   target buffer. Lines the renderer skipped because nothing changed
   still hold what is on screen, so they read correctly. Interlaced
   frames take every other row; oddeven has not toggled yet, so it still
   names the field just drawn. A frame blanked throughout has no
   non-blank pixels and gives a signature of 0. The guide runs the CRC
   over one field or frame and includes graphics, cursor, overlay and
   overscan (RRG: DAC_CNTL, p. 3-125 / PDF 143; RRG: DAC_CRC_SIG,
   p. 3-126 / PDF 144). */
static void
rage128_dac_crc_capture(rage128_t *dev)
{
    svga_t   *svga = &dev->svga;
    bitmap_t *bm   = svga->monitor->target_buffer;
    uint8_t   cb = 0, cg = 0, cr = 0;

    if (!dev->dac_blank_frame && (bm != NULL) && (svga->hdisp > 0)
        && (svga->dispend > 0)) {
        const int frame_h = svga->dispend << (svga->interlace ? 1 : 0);
        const int top     = svga->y_add;
        const int left    = svga->x_add;
        int       right   = svga->monitor->mon_overscan_x - left;
        int       bottom  = svga->monitor->mon_overscan_y - top;
        const int base    = (svga->interlace && svga->oddeven) ? 1 : 0;
        const int y0      = svga->interlace ? ((top + base) & 1) : 0;
        const int step    = svga->interlace ? 2 : 1;
        int       width;
        int       height;

        if (right < 0)
            right = 0;
        if (bottom < 0)
            bottom = 0;
        width  = left + svga->hdisp + right;
        height = top + frame_h + bottom;
        if (height > 2048)
            height = 2048;

        for (int y = y0; y < height; y += step) {
            const int active_row = (y >= top) && (y < (top + frame_h));

            for (int x = 0; x < width; x++) {
                uint32_t px;

                if (active_row && (x >= left) && (x < (left + svga->hdisp)))
                    px = ((bm->line[y] != NULL) && (x < bm->w))
                        ? bm->line[y][x]
                        : 0;
                else
                    px = svga->overscan_color;
                rage128_dac_crc_step(&cb, &cg, &cr, px);
            }
        }
    }

    dev->dac_crc_sig = ((uint32_t) cb << 16) | ((uint32_t) cg << 8) | cr;
}

/* CRTC_EXT_CNTL.VGA_BLINK_RATE [2:1]: frames per blink in VGA modes,
   0 = the default VGA rate of 16 frames, then 32, 48 and 64 (RRG:
   CRTC_EXT_CNTL, p. 3-63 / PDF 81). */
static const uint32_t rage128_vga_blink_frames[4] = { 16, 32, 48, 64 };

/* Start of vertical blank. Besides the blink rate this runs the DAC CRC
   window, the CRTC_STATUS and GEN_INT_STATUS vblank flags, and the
   vblank page flip.

   Blink: the core advances svga->blink by one each frame, and the text
   and cursor code read the blink phase from bit 4, which makes 16
   frames per blink. Here the counter is advanced instead at 16 steps
   per selected frame count, so code 0 gives the core's own rate and the
   others stretch it in proportion. This runs at the start of vertical
   blank, just before the point where the core would advance it. */
void
rage128_vblank_start(svga_t *svga)
{
    rage128_t *dev = (rage128_t *) svga->priv;
    uint32_t   n   = rage128_vga_blink_frames[(dev->crtc_ext_cntl
                                           >> RAGE128_VGA_BLINK_RATE_SHIFT)
                                          & 3];

    /* DAC CRC: setting DAC_CRC_EN starts the CRC at the next vertical
       blank and runs it for one field or frame (RRG: DAC_CNTL, p. 3-125 /
       PDF 143). An armed capture (state 1) starts here; a running one
       (state 2) ends here and latches its signature, which then holds
       until DAC_CRC_EN is cleared and set again (RRG: DAC_CRC_SIG,
       p. 3-126 / PDF 144). The register write arms only on that
       0-to-1 change. */
    if (dev->dac_crc_state == 2) {
        rage128_dac_crc_capture(dev);
        dev->dac_crc_state = 0;
    } else if (dev->dac_crc_state == 1)
        dev->dac_crc_state = 2;

    /* Overlay: the double-buffered OV0 fields take effect at vertical
       blank unless OV0_LOCK holds them (Multimedia supplement:
       OV0_REG_LOAD_CNTL). This comes after the CRC capture, because the
       frame that just ended was composed from the old working set, and
       before the interrupt, so a driver woken by it sees the new set in
       force. */
    rage128_ov0_vblank(dev);

    /* CRTC_STATUS.CRTC_VBLANK_SAVE: set at the start of each vertical
       blank, cleared by software (RRG: CRTC_STATUS, p. 3-66 / PDF 84). */
    dev->vblank_save = 1;

    /* Overlay witness for OV0_VBLANK_DURING_LOCK: the OV0_REG_LOAD_CNTL
       write path compares this count at the lock's falling edge with the
       value it took at the rising edge (Multimedia supplement:
       OV0_REG_LOAD_CNTL). Only the count moves here; the status itself
       is latched by the register write. */
    atomic_fetch_add_explicit(&dev->ov0.vblank_seq, 1, memory_order_relaxed);

    /* GEN_INT_STATUS.CRTC_VBLANK_INT, "Vertical blank started since last
       cleared": latch the event and raise the interrupt if it is enabled
       (RRG: GEN_INT_STATUS, p. 3-193 / PDF 211). */
    rage128_gen_int_vblank(dev);

    /* Page flip: with CRTC_OFFSET_FLIP_CNTL clear, a new CRTC_OFFSET is
       used from vertical blank (RRG: CRTC_OFFSET_CNTL, p. 3-72 / PDF 90),
       unless CRTC_OFFSET_LOCK holds it back (RRG: CRTC_OFFSET, p. 3-71 /
       PDF 89). A flip still waiting for a horizontal blank is taken here
       too. This comes after the CRC capture above, because the frame
       that just ended was scanned from the old base. */
    if (dev->crtc_offset_pending && !dev->crtc_offset_lock) {
        rage128_latch_crtc_offset(dev);
        dev->crtc_offset_hblank = 0;
        /* Release a WAIT_UNTIL EVENT_CRTC_OFFSET stall on the CCE
           thread; that event waits until the new CRTC_OFFSET is being
           displayed (RRG: WAIT_UNTIL, p. 3-239 / PDF 257). */
        rage128_pm4_flip_notify(dev);
    }

    dev->vga_blink_acc += 16;
    while (dev->vga_blink_acc >= n) {
        dev->vga_blink_acc -= n;
        dev->vga_blink_step++;
    }
    svga->blink = dev->vga_blink_step & 0x7f;
}

/* Install this file's wrappers on the core: the scan address wrapper
   (flips and the tile transform), the line renderer wrapper, and
   remap_required for a tiled frame. A tiled line is not linear in VRAM,
   so every fetch has to go through the wrapper; a linear frame takes
   flips at line granularity and keeps the fast path. A tiled frame is
   always redrawn in full, because the change test looks only at the
   page where a line's first byte lands and cannot see the rest of the
   line's tile row. This runs at vsync and after every
   svga_recalctimings, because the core rebuilds remap_func and clears
   remap_required after its recalctimings hook returns. */
static void
rage128_scan_install(rage128_t *dev, int tiled, int wrap_render)
{
    svga_t *svga = &dev->svga;

    if (svga->remap_func != rage128_scan_remap) {
        dev->scan_remap_orig = svga->remap_func;
        svga->remap_func     = rage128_scan_remap;
    }
    if (wrap_render && svga->render && svga->render != rage128_render_timed) {
        dev->svga_render_orig = svga->render;
        svga->render          = rage128_render_timed;
    }
    svga->remap_required = tiled;
    if (tiled)
        svga->fullchange = svga->monitor->mon_changeframecount;
}

/* Recalculate timings after a register write in mid-frame.
   svga_recalctimings removes the wrappers this file installs (poll
   callback, scan address, renderer, remap_required), and the vsync
   callback would put them back only at the next frame; until then a
   tiled surface would scan as linear. This sets up the tiled walk from
   the current registers and installs the wrappers again at once. */
static void
rage128_recalctimings_apply(rage128_t *dev)
{
    svga_t *svga = &dev->svga;

    svga_recalctimings(&dev->svga);
    if (svga->timer.callback != rage128_svga_poll_timed)
        timer_set_callback(&svga->timer, rage128_svga_poll_timed);
    if (!(dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN))
        return;
    dev->scanout_tiled      = rage128_scanout_tiled_now(dev);
    dev->scanout_tile_pitch = rage128_scanout_pitch_bytes(svga);
    rage128_scanout_tile_line_refresh(dev);
    if (dev->scanout_tiled)
        rage128_scanout_tile_anchor(dev, dev->scanout_seed_base);
    /* The renderer wrapper waits for the next vsync, so code that checks
       svga->render right after a register write sees the renderer the
       core just chose. */
    rage128_scan_install(dev, dev->scanout_tiled, 0);
}

void
rage128_vsync_callback(svga_t *svga)
{
    rage128_t *dev = (rage128_t *) svga->priv;

    if (dev->svga.timer.callback != rage128_svga_poll_timed)
        timer_set_callback(&dev->svga.timer, rage128_svga_poll_timed);

    rage128_pace_update(dev);

    if (dev->synctel)
        rage128_synctel_frame(dev);

    /* CRTC_CRNT_FRAME, the 21-bit display frame counter (RRG:
       CRTC_CRNT_FRAME, p. 3-214 / PDF 232). */
    dev->frame_count = (dev->frame_count + 1) & 0x1fffff;

    /* GEN_INT_STATUS.CRTC_VSYNC_INT, "Vertical sync started since last
       cleared" (RRG: GEN_INT_STATUS, p. 3-194 / PDF 212). */
    rage128_gen_int_vsync(dev);

    /* Deferred PPLL atomic update: with PPLL_ATOMIC_UPDATE_SYNC set the
       working dividers load the new settings at VSYNC (RRG: PPLL_CNTL,
       p. 3-88 / PDF 106). */
    if (dev->ppll_update_pending) {
        rage128_ppll_commit(dev);
        dev->ppll_update_pending = 0;
        rage128_recalctimings_apply(dev);
    }

    /* Seed this frame's scan from one read of the latched base. The core
       seeds memaddr from memaddr_latch just before this callback, and a
       flip from the CCE thread can land in between. Reading the base
       once and setting both memaddr and scanout_seed_base from it means
       the scan wrapper's bias covers exactly the flips that land after
       this point. The wrappers are installed again here, because
       svga_recalctimings rebuilds remap_func after its recalctimings
       hook returns. Only this CPU thread changes the svga scan state; a
       flip on the CCE thread only publishes crtc_offset_latched. */
    if (dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN) {
        uint32_t base  = dev->crtc_offset_latched & 0x01fffff8;
        int      tiled = rage128_scanout_tiled_now(dev);
        uint32_t snap  = 0;
        uint64_t sseq  = 0;
        uint32_t seed;

        /* Present snapshot: scan this frame from the newest finished GPU
           copy of the latched buffer when there is one, because the
           guest buffer itself may already hold the next frame's
           drawing. The choice does not wait. With no finished copy the
           guest buffer is scanned, with the per-line barrier. A tiled
           frame never uses a copy: the copy is a linear range, so a
           tiled frame reads the guest bytes through the tile transform
           (and the flip makes no copy for it). */
        if (dev->gpu && !tiled && rage128_gpu_snap_pick(dev, base, &snap, &sseq))
            base = snap;

        /* Fix this frame's tiled-walk state, so the scan wrapper sees
           one consistent anchor, pitch and start line for the whole
           frame. The start line comes from the latched tile line, which
           CRTC_OFFSET_LOCK and a pending flip hold with the offset. */
        dev->scanout_tiled      = tiled;
        dev->scanout_tile_pitch = rage128_scanout_pitch_bytes(svga);
        rage128_scanout_tile_line_refresh(dev);
        if (tiled)
            rage128_scanout_tile_anchor(dev, base);
        /* A new snapshot, or a switch between a snapshot and the guest
           buffer, needs full redraws, since the hidden pages carry no
           change marks. Showing the same copy again (same address, same
           sequence number) does not, because its bytes are unchanged and
           a full redraw every frame would only cost time. */
        if (snap != dev->scanout_snap_cur
            || (snap && sseq != dev->scanout_snap_seq))
            svga->fullchange = svga->monitor->mon_changeframecount;
        dev->scanout_snap_cur = snap;
        dev->scanout_snap_seq = sseq;
        /* Hazard state for this frame, guest buffer only: one range
           check here stands in for a read fence on every line. The
           check can race GPU work submitted during the frame, which
           may start writing the scanned buffer just after an
           all-clear; a whole frame scanned without fences then flashes
           the buffer's previous content (seen in the emulator). So the
           fences are dropped only after 3 clean checks in a row: a
           buffer just flipped to or still being drawn into keeps them,
           and only a range that has stayed idle goes without. Every
           change of state forces full redraws, because GPU writes set
           no change marks and a line drawn from the bytes before a
           write would otherwise never be redrawn. */
        {
            int haz = 1;

            uint32_t hbase  = base;
            uint32_t hbytes = rage128_scanout_bytes(svga);

            /* A tiled frame reads whole tile rows from the anchor, not a
               linear range from the start pixel. */
            if (tiled && hbytes) {
                hbase  = dev->scanout_tile_c0;
                hbytes = r128_tile_rows_bytes(
                    dev->scanout_tile_line - 1u
                        + ((uint32_t) svga->dispend
                           << (svga->interlace ? 1 : 0)),
                    dev->scanout_tile_pitch);
            }
            if (snap) {
                haz                      = 0;
                dev->scanout_idle_frames = 0;
            } else if (rage128_gpu_scan_hazard(dev, hbase, hbytes))
                dev->scanout_idle_frames = 0;
            else if (++dev->scanout_idle_frames >= 3)
                haz = 0;
            if (haz != dev->scanout_hazard)
                svga->fullchange = svga->monitor->mon_changeframecount;
            dev->scanout_hazard = haz;
        }
        seed = base >> 2;

        if (svga->interlace && svga->oddeven)
            seed += (uint32_t) (svga->rowoffset << 1);
        seed += svga->hblank_sub;
        if (!(svga->adv_flags & FLAG_NO_SHIFT3))
            seed <<= 2;
        svga->memaddr = svga->memaddr_backup = seed;
        dev->scanout_seed_base               = base;
        /* Overscan edges, set here for the same reason as the wrappers
           below: the core splits mon_overscan_x and mon_overscan_y
           evenly after the recalctimings hook, while the chip has
           separate left, right, top and bottom widths. */
        svga->left_overscan = svga->x_add = (int) dev->ovr_left;
        svga->y_add                       = (int) dev->ovr_top;

        rage128_scan_install(dev, tiled, 1);

        /* Start the conversion worker on this frame. Only snapshot
           frames qualify: their source does not change from here on, and
           the scan stays on the snapshot for the whole frame. It is
           skipped while the worker is still busy or when the frame
           cannot be described as evenly spaced linear rows. The line
           renderer then converts each line itself, so a skipped start
           costs speed, never correctness. */
        dev->conv_active = 0;
        if (dev->gpu && snap && svga->fullchange && !svga->interlace
            && svga->lut_map && !svga->force_old_addr
            && dev->svga_render_orig == svga_render_32bpp_highres
            && svga->hdisp > 0 && (svga->hdisp + svga->scrollcache) >= 0
            && svga->dispend > 0 && !atomic_load(&dev->conv_busy)) {
            uint32_t ccount = (uint32_t) (svga->hdisp + svga->scrollcache) + 1;
            uint32_t stride = (svga->adv_flags & FLAG_NO_SHIFT3)
                ? (uint32_t) svga->rowoffset
                : ((uint32_t) svga->rowoffset << 3);
            uint32_t lines  = (uint32_t) svga->dispend;
            uint32_t cbase  = svga->memaddr & svga->vram_display_mask;
            uint64_t span   = (uint64_t) cbase
                + (uint64_t) (lines - 1) * stride + (uint64_t) ccount * 4;
            size_t need = (size_t) lines * ccount * 4;

            if (stride && span <= (uint64_t) svga->vram_display_mask + 1) {
                if (need > dev->conv_buf_sz) {
                    free(dev->conv_buf);
                    dev->conv_buf    = malloc(need);
                    dev->conv_buf_sz = dev->conv_buf ? (uint32_t) need : 0;
                }
                if (dev->conv_buf) {
                    if (!dev->conv_thread) {
                        dev->conv_wake   = thread_create_event();
                        dev->conv_run    = 1;
                        dev->conv_thread = thread_create_named(rage128_conv_thread,
                                                               dev, "rage128_conv");
                    }
                    rage128_lut_update(dev);
                    dev->conv_base   = cbase;
                    dev->conv_stride = stride;
                    dev->conv_count  = ccount;
                    dev->conv_lines  = lines;
                    atomic_store(&dev->conv_done, 0);
                    atomic_store(&dev->conv_stale, 0);
                    atomic_store(&dev->conv_busy, 1);
                    dev->conv_active = 1;
                    thread_set_event(dev->conv_wake);
                }
            }
        }
    } else
        dev->scanout_tiled = 0; /* VGA scanout is linear by definition */

    /* Schedule the CRTC_VLINE event (RRG: CRTC_VLINE_CRNT_VLINE, p. 3-68 /
       PDF 86) from here, where the line count equals vsyncstart. One
       scan line is dispontime + dispofftime ticks. The status bit is
       set whether or not the interrupt is enabled, since the enables
       only decide which status bits raise an interrupt (RRG:
       GEN_INT_CNTL, p. 3-193 / PDF 211). A compare line at or past
       vtotal never fires. */
    {
        int cmp = dev->crtc_vline & 0x7ff;

        if (svga->vtotal > 1 && cmp < svga->vtotal) {
            int lines = cmp - svga->vsyncstart;
            if (lines <= 0)
                lines += svga->vtotal;
            timer_set_delay_u64(&dev->vline_timer,
                                (uint64_t) lines * (svga->dispontime + svga->dispofftime));
        }
    }
}

/* Fires once per frame when the raster reaches the CRTC_VLINE line:
   GEN_INT_STATUS.CRTC_VLINE_INT (RRG: GEN_INT_STATUS, p. 3-194 /
   PDF 212). */
void
rage128_vline_timer(void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    dev->gen_int_status |= RAGE128_GIS_VLINE;
    rage128_gen_int_update(dev);
}

/* Current horizontal count in characters, worked out from the line
   timer's phase: linepos 0 is the active part of the line (dispontime
   long, characters 0 to hdisp), linepos 1 the horizontal blank
   (dispofftime long, the remaining characters up to htotal). The scan
   model moves a line at a time, so this scales the time into the line
   and is not exact to the pixel. */
static uint32_t
rage128_snapshot_hcount(rage128_t *dev)
{
    svga_t  *svga = &dev->svga;
    uint32_t htot = (uint32_t) svga->htotal;
    uint32_t hact;
    uint64_t phase;
    uint64_t rem;

    if (htot < 2)
        return 0;
    if (dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN)
        hact = ((dev->crtc_h_total_disp >> 16) & 0xff) + 1;
    else
        hact = (uint32_t) svga->crtc[1] + 1;
    if (hact >= htot)
        hact = htot - 1;

    rem = (uint64_t) timer_get_remaining_u64(&svga->timer);
    if (!svga->linepos) {
        phase = svga->dispontime ? svga->dispontime : 1;
        if (rem > phase)
            rem = phase;
        return (uint32_t) (((phase - rem) * hact) / phase) & 0x1ff;
    }
    phase = svga->dispofftime ? svga->dispofftime : 1;
    if (rem > phase)
        rem = phase;
    hact += (uint32_t) (((phase - rem) * (htot - hact)) / phase);
    if (hact >= htot)
        hact = htot - 1; /* rem 0 is the next tick's start, where it wraps */
    return hact & 0x1ff;
}

/* Take a snapshot: capture the current horizontal and vertical counts
   and the frame count, set GEN_INT_STATUS.SNAPSHOT_INT, "Snapshot taken
   since last cleared", and raise the interrupt if it is enabled (RRG:
   GEN_INT_STATUS, p. 3-194 / PDF 212). The guide's SNAPSHOT_VH_COUNTS
   descriptions are the wrong way round: SNAPSHOT_HCOUNT [8:0] is
   described as the vertical count and SNAPSHOT_VCOUNT [26:16] as the
   horizontal one (RRG: SNAPSHOT_VH_COUNTS, p. 3-215 / PDF 233). The
   field widths match the names, 9 bits like CRTC_H_TOTAL and 11 bits
   like CRTC_V_TOTAL, so the names are followed. */
static void
rage128_snapshot_take(rage128_t *dev)
{
    dev->snapshot_vh_counts = rage128_snapshot_hcount(dev)
        | (((uint32_t) dev->svga.displine & 0x7ff) << 16);
    dev->snapshot_f_count = dev->frame_count & 0x1fffff;
    dev->gen_int_status |= RAGE128_GIS_SNAPSHOT;
    rage128_gen_int_update(dev);
}

/* ------------------------------------------------------------------ */
/* Scanout: derive the svga timings from the chip's CRTC and PLL when
   CRTC_EXT_DISP_EN is set. This is the core's recalctimings hook and
   runs after svga_recalctimings has worked out the VGA timings.        */
/* ------------------------------------------------------------------ */
static void rage128_update_hwcursor(rage128_t *dev);

void
rage128_recalctimings(svga_t *svga)
{
    rage128_t *dev = (rage128_t *) svga->priv;
    double     dot_hz;
    uint32_t   hblank_strt;

    rage128_update_hwcursor(dev);
    rage128_ov0_update(dev);     /* overlay is gated on CRTC_EXT_DISP_EN too */
    rage128_dac_mask_apply(dev); /* the mask is live in VGA modes only */

    /* CRTC_HSYNC_DIS and CRTC_VSYNC_DIS are the monitor power controls
       (RRG: CRTC, p. 3-61 / PDF 79; RRG: CRTC_EXT_CNTL, p. 3-64 /
       PDF 82). xf86-video-r128 r128_crtc.c r128_crtc_dpms drops HSYNC
       for standby, VSYNC for suspend and both for off. The core has one
       monitor power-saving state, so either dropped sync enters it.
       CRTC_DISPLAY_DIS is handled apart from these: it blanks the
       picture to black and does not change the monitor's power state. */
    svga->dpms = !!(dev->crtc_ext_cntl
                    & (RAGE128_CRTC_HSYNC_DIS | RAGE128_CRTC_VSYNC_DIS));

    if (!(dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN)) {
        /* VGA mode (CRTC_EXT_DISP_EN = 0, RRG: CRTC_GEN_CNTL, p. 3-62 /
           PDF 80): the timings svga_recalctimings already worked out
           stand. A standard video BIOS mode set clears CRTC_EXT_DISP_EN
           itself, in a hand-off routine (RE: Rage128progl_zerostate.VBI
           @c000:64d0). The BIOS calls it only when, among other checks,
           a VGA CRTC vertical-display-end write reads back through the
           extended CRTC_V_TOTAL_DISP (RE: Rage128progl_zerostate.VBI
           @c000:50cf); rage128_vga_out in vid_ati_rage128_vga.c models
           that readback. */
        svga->bpp           = 8;
        svga->packed_chain4 = 0;
        svga->hoverride     = 0;
        svga->lut_map       = 0;
        svga->adv_flags |= FLAG_PANNING_ATI; /* per-line VGA pixel panning */
        /* VGA-mode blanking is the core's own; a CRC capture here reads
           the finished buffer as it is. */
        dev->dac_blank_frame = 0;
        /* Once VCLK_SRC_SEL selects the PPLL, it clocks VGA scanout too,
           with GENMO_WT VGA_CKSEL picking the divider set (RRG:
           PPLL_DIV_0, p. 3-91 / PDF 109; RRG: VCLK_ECP_CNTL, p. 3-95 /
           PDF 113); rage128_dot_clock_hz decodes that in VGA mode. With
           any other source (PCICLK after reset) the core's standard VGA
           clocks stay. Only the pixel clock is replaced; the geometry
           stays the core's. */
        if ((dev->pll_regs[RAGE128_PLL_VCLK_ECP_CNTL] & RAGE128_VCLK_SRC_SEL_MASK)
            == RAGE128_VCLK_SRC_PPLL) {
            dot_hz = rage128_dot_clock_hz(dev);
            if (dot_hz >= 1000000.0)
                svga->clock = (cpuclock * (double) (1ULL << 32)) / dot_hz;
        }
        /* HTOTAL_CNTL line slip, VGA form: dots_per_clock is the core's
           character width, including the SEQ_PCLKBY2 doubling. */
        svga->multiplier = rage128_htotal_multiplier(dev,
                                                     svga->htotal * svga->dots_per_clock,
                                                     1, !!(svga->seqregs[1] & 8));
        /* The DAC follows the CRTC whichever mode it is in, so DAC_PDWN
           and forced data act on VGA scanout as on extended scanout. The
           guide limits DAC_MASK in the same register to VGA modes but
           gives DAC_PDWN no such limit (RRG: DAC_CNTL, p. 3-125 /
           PDF 143). */
        dev->dac_const_on = 0;
        if (dev->dac_cntl & RAGE128_DAC_PDWN) {
            dev->dac_const_on    = 1;
            dev->dac_const_color = makecol32(0, 0, 0);
        } else if ((dev->dac_ext_cntl & RAGE128_DAC_FORCE_DATA_EN)
                   && (svga->render != svga_render_blank)) {
            dev->dac_const_on    = 1;
            dev->dac_const_color = rage128_dac_forced_color(dev);
        }
        if (dev->dac_const_on)
            svga->render = rage128_render_dac_const;
        return;
    }

    /* Take over the border geometry. The CRTC has no blank start or end
       registers, so the core is given sync start and width, and its own
       border calculation would turn the back porch into a left border
       that is not there and overflow the 2048-pixel render buffer on
       modes wider than about 1696 pixels. A border exists only when the
       OVR_WID_LEFT_RIGHT and OVR_WID_TOP_BOTTOM registers set one
       (usually they are zero); rage128_overscan_apply works it out once
       hdisp and dispend are final. */
    svga->hoverride               = 1;
    svga->hblank_sub              = 0;
    svga->monitor->mon_overscan_x = 0;
    svga->monitor->mon_overscan_y = 0;
    /* The VGA attribute controller's pixel panning (ATTR13) is not in
       the extended-mode pixel path. The core's per-line ATI panning
       update reads the VGA registers even with hoverride set, and a VGA
       core left in its power-on text state, as on the Rage Fury MAXX's
       second chip, would shift every line after the first by one pixel. */
    svga->adv_flags &= ~FLAG_PANNING_ATI;

    /* Extended mode fetches linearly from CRTC_OFFSET and CRTC_PITCH,
       without the VGA byte, word, doubleword or chain-4 address
       rearranging (see rage128_update_banking). packed_chain4 is the svga
       core's packed-linear addressing mode, for the A0000 window and for
       the scan address alike. */
    svga->packed_chain4 = 1;

    /* Geometry. Horizontal fields count 8-pixel characters and vertical
       fields count lines (RRG: CRTC, p. 3-61 / PDF 79). Each holds the
       count minus one, as xf86-video-r128 r128_crtc.c
       R128InitCrtcRegisters programs them. */
    svga->char_width = 8;
    svga->htotal     = (dev->crtc_h_total_disp & 0x1ff) + 1;
    svga->hdisp_time = svga->hdisp = ((dev->crtc_h_total_disp >> 16) & 0xff) + 1;
    /* HTOTAL_CNTL line slip, extended form (both slips always live). */
    svga->multiplier = rage128_htotal_multiplier(dev, svga->htotal * 8, 0, 0);
    svga->vtotal     = (dev->crtc_v_total_disp & 0x7ff) + 1;
    /* The displayed height comes from crtc_v_disp_active, not from the
       CRTC_V_DISP value that reads back. A VGA CRTC write to registers 07h
       or 12h changes the readback, which the video BIOS hand-off checks,
       but must not change the height of a running extended display: the
       X server's restore writes the saved VGA console registers after
       the extended mode registers. */
    svga->dispend    = (dev->crtc_v_disp_active & 0x7ff) + 1;
    svga->vsyncstart = (dev->crtc_v_sync_strt_wid & 0x7ff) + 1;

    /* The horizontal blank is taken to be the sync pulse,
       CRTC_H_SYNC_STRT_CHAR [11:3] for CRTC_H_SYNC_WID [21:16]
       characters (RRG: CRTC_H_SYNC_STRT_WID, p. 3-67 / PDF 85), since
       the CRTC has no blank start or end registers. */
    hblank_strt          = (dev->crtc_h_sync_strt_wid >> 3) & 0x1ff;
    svga->hblankstart    = hblank_strt;
    svga->hblank_end_val = (hblank_strt + ((dev->crtc_h_sync_strt_wid >> 16) & 0x3f) - 1) & 0x3f;

    svga->rowcount  = (dev->crtc_gen_cntl & RAGE128_CRTC_DBL_SCAN_EN) ? 1 : 0;
    svga->interlace = !!(dev->crtc_gen_cntl & RAGE128_CRTC_INTERLACE_EN);
    svga->linedbl   = 0;
    svga->split     = 0xffffff;

    /* In interlaced modes CRTC_V_TOTAL_DISP and CRTC_V_SYNC_STRT_WID
       hold whole-frame line counts. The guide does not say so; it is
       what the Windows XP driver programs for an interlaced 800x600
       mode at 36 MHz (total 703, display 600, sync start 611). The svga
       core counts lines per field, so it gets half of each. */
    if (svga->interlace) {
        svga->vtotal     = (svga->vtotal + 1) >> 1;
        svga->dispend    = (svga->dispend + 1) >> 1;
        svga->vsyncstart = (svga->vsyncstart + 1) >> 1;
    }
    svga->vblankstart = svga->dispend;

    /* The 256-entry palette stays in the pixel path at every depth:
       for direct color it holds the gamma ramp, which xf86-video-r128
       r128_driver.c R128LoadPalette loads at depths 15, 16 and 24. */
    svga->lut_map  = 1;
    dev->lut_dirty = 1; /* rebuild at every mode set: BIOS or driver
                           palette loads can arrive by paths that do not
                           go through the two palette write handlers */
    atomic_store(&dev->conv_stale, 1);

    /* Display base: CRTC_OFFSET [24:0] in bytes, bits 2:0 always 0
       (RRG: CRTC_OFFSET, p. 3-70 / PDF 88); pitch: CRTC_PITCH [9:0] in
       units of 8 pixels (RRG: CRTC_PITCH, p. 3-74 / PDF 92). Scanout uses
       the latched copy of the base, which a new CRTC_OFFSET reaches only
       at the flip point (RRG: CRTC_OFFSET_CNTL, p. 3-72 / PDF 90).
       memaddr_latch counts dwords: at vsync svga_poll sets memaddr to
       memaddr_latch << 2. */
    svga->memaddr_latch = (dev->crtc_offset_latched & 0x01fffff8) >> 2;
    /* scanout_seed_base is left alone: it is the base the running
       memaddr walk started from, and only the vsync seeding changes the
       two together. Changing it here would lose the bias of a flip taken
       at a horizontal blank that the scan wrapper has not yet applied. */
    dev->scanout_snap_cur    = 0; /* stale snapshots do not survive a mode set */
    dev->scanout_hazard      = 1; /* conservative until the next vsync check */
    dev->scanout_idle_frames = 0;

    /* Pixel clock. */
    dot_hz = rage128_dot_clock_hz(dev);
    if (dot_hz < 1000000.0)
        dot_hz = dev->ref_freq_hz;
    svga->clock = (cpuclock * (double) (1ULL << 32)) / dot_hz;

    /* Pixel format: CRTC_GEN_CNTL.CRTC_PIX_WIDTH [10:8], 1 = 4 bpp,
       2 = 8, 3 = 15, 4 = 16, 5 = 24, 6 = 32 (RRG: CRTC_GEN_CNTL, p. 3-62 /
       PDF 80). svga->rowoffset counts 8-byte units and CRTC_PITCH counts
       8-pixel units, so rowoffset = pitch * bytes per pixel. The guide
       notes that at 24 bpp the display pitch is still in 8-pixel units
       (RRG: CRTC_PITCH, p. 3-74 / PDF 92). */
    switch ((dev->crtc_gen_cntl >> RAGE128_CRTC_PIX_WIDTH_SHIFT) & 7) {
        case 1: /* 4 bpp packed */
            /* Half a byte per pixel: a line is pitch * 4 bytes, and
               rowoffset counts 8-byte units, so an odd pitch would need
               a 4-byte step the core cannot express and is rounded down.
               The guide does not say whether an odd pitch is valid at
               4 bpp. */
            svga->render    = rage128_render_4bpp_ext;
            svga->bpp       = 8;
            svga->rowoffset = (dev->crtc_pitch & 0x3ff) >> 1;
            svga->map8      = svga->pallook;
            break;
        case 2: /* 8 bpp */
            svga->render    = svga_render_8bpp_clone_highres;
            svga->bpp       = 8;
            svga->rowoffset = (dev->crtc_pitch & 0x3ff);
            svga->map8      = svga->pallook;
            break;
        case 3: /* 15 bpp (555) */
            svga->render    = svga_render_15bpp_highres;
            svga->bpp       = 15;
            svga->rowoffset = (dev->crtc_pitch & 0x3ff) * 2;
            break;
        case 4: /* 16 bpp (565) */
            svga->render    = svga_render_16bpp_highres;
            svga->bpp       = 16;
            svga->rowoffset = (dev->crtc_pitch & 0x3ff) * 2;
            break;
        case 5: /* 24 bpp packed */
            svga->render    = svga_render_24bpp_highres;
            svga->bpp       = 24;
            svga->rowoffset = (dev->crtc_pitch & 0x3ff) * 3;
            break;
        case 6: /* 32 bpp */
            svga->render    = svga_render_32bpp_highres;
            svga->bpp       = 32;
            svga->rowoffset = (dev->crtc_pitch & 0x3ff) * 4;
            break;
        default: /* codes the guide does not define: blanked */
            svga->render    = svga_render_blank;
            svga->bpp       = 8;
            svga->rowoffset = (dev->crtc_pitch & 0x3ff);
            break;
    }
    svga->hdisp *= 8;

    /* Set up the tiled walk from the current registers, the same values
       the vsync callback fixes for each frame. Clearing it here instead
       would make the rest of the frame scan a tiled surface as linear
       after any CRTC register write in mid-frame, and such writes occur:
       the Rage Fury MAXX driver copies cursor state to both chips at
       any raster position (observed in captured traffic). */
    dev->scanout_tiled      = rage128_scanout_tiled_now(dev);
    dev->scanout_tile_pitch = rage128_scanout_pitch_bytes(svga);
    rage128_scanout_tile_line_refresh(dev);
    if (dev->scanout_tiled)
        rage128_scanout_tile_anchor(dev, dev->scanout_seed_base);

    /* Blank while CRTC_EN holds the CRTC in reset, while
       CRTC_DISP_REQ_EN_B turns off display requests to memory (RRG:
       CRTC_GEN_CNTL, pp. 3-62-3-63 / PDF 80-81), or while
       CRTC_DISPLAY_DIS is set (RRG: CRTC_EXT_CNTL, p. 3-64 / PDF 82).
       DAC_EXT_CNTL.DAC_FORCE_BLANK_OFF_EN overrides only the last case:
       the guide describes CRTC_DISPLAY_DIS as "forcing the blanking
       signal to be active", and with the DAC's blank held off (RRG:
       DAC_EXT_CNTL, p. 3-217 / PDF 235) the pixels still get through.
       The other two stop the pixel data itself, which no blank override
       can restore (modeled). */
    if (!(dev->crtc_gen_cntl & RAGE128_CRTC_EN)
        || (dev->crtc_gen_cntl & RAGE128_CRTC_DISP_REQ_EN_B)
        || ((dev->crtc_ext_cntl & RAGE128_CRTC_DISPLAY_DIS)
            && !(dev->dac_ext_cntl & RAGE128_DAC_FORCE_BLANK_OFF_EN)))
        svga->render = svga_render_blank;

    /* The CRC counts only pixels that are not blank. A frame blanked
       here (by either kind of blank, or an undefined pixel width) has
       none, even when DAC_PDWN later puts its own black in place. */
    dev->dac_blank_frame = (svga->render == svga_render_blank);

    /* 320 pixels wide with 400 or more lines (double scan): show each
       pixel twice to keep the picture's shape (rage128_render_hdbl
       above). Setting hdisp to 640 also keeps the svga core's own
       320-to-640 handling from switching to its lowres renderers. */
    dev->render_hdbl_base = NULL;
    if ((svga->hdisp == 320) && (svga->dispend >= 400)
        && (svga->render != svga_render_blank)) {
        dev->render_hdbl_base = svga->render;
        svga->render          = rage128_render_hdbl;
        svga->hdisp           = 640;
    }

    /* The DAC stage, after graphics, cursor, overlay and border. A
       powered-down DAC (DAC_PDWN, RRG: DAC_CNTL, p. 3-125 / PDF 143)
       puts out black; DAC_FORCE_DATA_EN replaces the RGB at the DAC
       inputs (RRG: DAC_EXT_CNTL, p. 3-217 / PDF 235). The guide gives
       blank its own override, DAC_FORCE_BLANK_OFF_EN, so forced data is
       modeled as leaving a blanked frame blank. */
    dev->dac_const_on = 0;
    if (dev->dac_cntl & RAGE128_DAC_PDWN) {
        dev->dac_const_on    = 1;
        dev->dac_const_color = makecol32(0, 0, 0);
    } else if ((dev->dac_ext_cntl & RAGE128_DAC_FORCE_DATA_EN)
               && (svga->render != svga_render_blank)) {
        dev->dac_const_on    = 1;
        dev->dac_const_color = rage128_dac_forced_color(dev);
    }
    if (dev->dac_const_on)
        svga->render = rage128_render_dac_const;

    rage128_overscan_apply(dev, svga);

#ifdef ENABLE_RAGE128_LOG
    /* Log one line for each new extended mode, so a report of wrong
       colors or geometry can be matched to what the guest programmed. */
    {
        static uint32_t last_gen = 0, last_pitch = 0, last_htd = 0, last_vtd = 0;
        if ((dev->crtc_gen_cntl != last_gen) || (dev->crtc_pitch != last_pitch)
            || (dev->crtc_h_total_disp != last_htd) || (dev->crtc_v_total_disp != last_vtd)) {
            last_gen   = dev->crtc_gen_cntl;
            last_pitch = dev->crtc_pitch;
            last_htd   = dev->crtc_h_total_disp;
            last_vtd   = dev->crtc_v_total_disp;
            rage128_log("RAGE128 modeset: %dx%d pixw=%d bpp=%d pitch=%upx offset=%08x gen=%08x dac=%08x dacext=%08x\n",
                        svga->hdisp, svga->dispend,
                        (dev->crtc_gen_cntl >> RAGE128_CRTC_PIX_WIDTH_SHIFT) & 7, svga->bpp,
                        (dev->crtc_pitch & 0x3ff) * 8, dev->crtc_offset_latched,
                        dev->crtc_gen_cntl, dev->dac_cntl, dev->dac_ext_cntl);
        }
    }
#endif

    /* With the GPU backend svga.vram is twice the guest's size, and
       present snapshots are scanned from the hidden upper half; a display
       mask of the guest size would wrap those reads into guest memory. */
    svga->vram_display_mask = dev->gpu ? (dev->vram_size * 2 - 1)
                                       : dev->vram_mask;
}

/* ------------------------------------------------------------------ */
/* Hardware cursor: 64x64 pixels at 2 bits each, giving cursor color 0,
   cursor color 1, transparent or inverted screen, enabled by
   CRTC_GEN_CNTL.CRTC_CUR_EN. The guide defines only CRTC_CUR_MODE 0,
   this format, and calls the other codes reserved; the values it then
   lists for the field describe display address load timing and seem to
   belong to another field (RRG: CRTC_GEN_CNTL, p. 3-62 / PDF 80). The
   model draws this format whatever CRTC_CUR_MODE holds. The cursor
   registers are in RRG: Hardware Cursor, pp. 3-81-3-83 / PDF 99-101.   */
/* ------------------------------------------------------------------ */
static void
rage128_update_hwcursor(rage128_t *dev)
{
    svga_t *svga = &dev->svga;

    /* The cursor belongs to the extended CRTC and is not shown in VGA
       mode (modeled). */
    svga->hwcursor.ena = !!(dev->crtc_gen_cntl & RAGE128_CRTC_CUR_EN)
        && (dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN);
    svga->hwcursor.addr      = dev->cur_offset_act & 0x01fffff0;
    svga->hwcursor.x         = (dev->cur_posn_act >> 16) & 0x7ff;
    svga->hwcursor.y         = dev->cur_posn_act & 0x7ff;
    svga->hwcursor.xoff      = (dev->cur_hvoff_act >> 16) & 0x3f;
    svga->hwcursor.yoff      = dev->cur_hvoff_act & 0x3f;
    svga->hwcursor.cur_xsize = 64;
    svga->hwcursor.cur_ysize = 64;
}

/* CUR_LOCK is modeled as one flag that appears as bit 31 of all three
   cursor registers: the guide describes the same lock in CUR_OFFSET,
   CUR_HORZ_VERT_POSN and CUR_HORZ_VERT_OFF, locking all three (RRG:
   CUR_OFFSET, p. 3-81 / PDF 99). xf86-video-r128 r128_cursor.c
   r128_crtc_set_cursor_position writes CUR_HORZ_VERT_OFF and
   CUR_HORZ_VERT_POSN with the lock set and then CUR_OFFSET without
   it. That sequence unlocks only if bit 31 of the latest write to any
   of the three sets the lock state.
   While locked, writes change only the programmed copies; the write
   that clears the lock makes all three take effect together. The svga
   core latches the cursor once per frame, so an unlock reaches the
   screen whole, never half old and half new. */
static void
rage128_cursor_publish(rage128_t *dev)
{
    if (dev->cur_lock)
        return;
    dev->cur_offset_act = dev->cur_offset;
    dev->cur_posn_act   = dev->cur_horz_vert_posn;
    dev->cur_hvoff_act  = dev->cur_horz_vert_off;
    rage128_update_hwcursor(dev);
}

void
rage128_hwcursor_draw(svga_t *svga, int displine)
{
    rage128_t *dev    = (rage128_t *) svga->priv;
    uint32_t   col0   = makecol32((dev->cur_clr0 >> 16) & 0xff,
                                  (dev->cur_clr0 >> 8) & 0xff,
                                  dev->cur_clr0 & 0xff);
    uint32_t   col1   = makecol32((dev->cur_clr1 >> 16) & 0xff,
                                  (dev->cur_clr1 >> 8) & 0xff,
                                  dev->cur_clr1 & 0xff);
    uint32_t  *line   = buffer32->line[displine] + svga->x_add;
    int        offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;
    int        sh     = rage128_hdbl_shift(dev);

    /* The cursor is mixed in before the DAC, so a powered-down or forced
       DAC shows none of it. A blanked frame shows nothing at all:
       CRTC_DISPLAY_DIS forces the blanking signal active (RRG:
       CRTC_EXT_CNTL, p. 3-64 / PDF 82), which is modeled as covering the
       cursor too, as the DAC CRC already treats such a frame as blank. */
    if (dev->dac_const_on || dev->dac_blank_frame)
        return;

    /* 16 bytes per 64-pixel row: 8 bytes of AND bits, then 8 bytes of
       XOR bits, pixel 0 in bit 7 of the first byte (SDK: Hardware
       Cursor, p. 4-20 / PDF 94). AND = 0 draws CUR_CLR0 or CUR_CLR1 as
       the XOR bit is 0 or 1; AND = 1 is transparent (XOR = 0) or the
       inverse of the screen pixel (XOR = 1) (SDK: Hardware Cursor,
       p. 4-21 / PDF 95). xf86-video-r128 r128_cursor.c R128CursorInit
       asks the X server's cursor code for the same layout: source and mask
       interleaved in 64-bit units, most significant bit first. The
       CUR_HORZ_OFF clip, the position and the right edge (nothing past
       the displayed width) stay in scanned pixels; only the store goes
       through the column mapping of the doubled 320-wide display. */
    for (int xx = 0; xx < 64; xx++) {
        if ((offset >= svga->hwcursor_latch.x) && (offset < (svga->hdisp >> sh))) {
            uint32_t base = svga->hwcursor_latch.addr;
            int      a    = (svga->vram[(base + (xx >> 3)) & svga->vram_mask]
                     >> (7 - (xx & 7)))
                & 1;
            int s = (svga->vram[(base + 8 + (xx >> 3)) & svga->vram_mask]
                     >> (7 - (xx & 7)))
                & 1;

            if (!a)
                rage128_hdbl_put(line, sh, offset, s ? col1 : col0);
            else if (s)
                rage128_hdbl_xor(line, sh, offset, 0xffffff);
        }
        offset++;
    }
    /* Double scan: the cursor pitch is added only at the end of odd scan
       lines, so each image row shows twice and the 64-line cursor shows
       32 rows (RRG: CRTC_GEN_CNTL, p. 3-61 / PDF 79). Under interlace the
       core already walks the image at half rate in each field, which
       gives the same picture, so the repeat applies to progressive
       double scan only. */
    if (!(dev->crtc_gen_cntl & RAGE128_CRTC_DBL_SCAN_EN) || svga->interlace
        || (svga->displine & 1))
        svga->hwcursor_latch.addr += 16;
}

/* ------------------------------------------------------------------ */
/* Register file dispatch (display-owned offsets).                     */
/* ------------------------------------------------------------------ */
int
rage128_display_reg_read(rage128_t *dev, uint32_t off, uint32_t *val)
{
    svga_t *svga = &dev->svga;

    switch (off) {
        case RAGE128_CLOCK_CNTL_INDEX:
            *val = dev->clock_cntl_index;
            return 1;
        case RAGE128_CLOCK_CNTL_DATA:
            *val = rage128_pll_read(dev);
            return 1;
        case RAGE128_CRTC_GEN_CNTL:
            *val = dev->crtc_gen_cntl;
            return 1;
        case RAGE128_CRTC_EXT_CNTL:
            *val = dev->crtc_ext_cntl;
            return 1;
        case RAGE128_DAC_CNTL:
            /* DAC_MASK [31:24] is "a mirror of the VGA DAC_MASK
               register" at port 3C6, so both read one stored value
               (RRG: DAC_CNTL, p. 3-125 / PDF 143; RRG: VGA DAC Registers,
               p. 3-19 / PDF 37). DAC_CMP_OUTPUT [7] is the read-only
               monitor-sense comparator; it reads 1, the value the guide
               gives for terminated lines, since the emulated monitor is
               always attached (RRG: DAC_CNTL, p. 3-123 / PDF 141). */
            *val = (dev->dac_cntl & 0x00ffff7f)
                | RAGE128_DAC_CMP_OUTPUT
                | ((uint32_t) dev->dac_mask_prog << 24);
            return 1;
        case RAGE128_CRTC_STATUS:
            /* [0] CRTC_VBLANK_CUR, in vertical blank now; [1]
               CRTC_VBLANK_SAVE, vertical blank since last cleared; [2]
               CRTC_VLINE_SYNC, scan line odd; [3] CRTC_FRAME, frame odd
               (RRG: CRTC_STATUS, p. 3-66 / PDF 84). The guide contradicts
               itself on bit 3, "even (1) or odd (0)" in the text and
               "0=Even frame, 1=Odd frame" in the value list; the value
               list is followed. Under interlace displine counts frame
               lines while dispend holds field lines, hence the shift. */
            *val = RAGE128_CRTC_STATUS_DEFAULT
                | ((svga->displine >= (svga->dispend << svga->interlace)) ? 0x1 : 0x0)
                | (dev->vblank_save ? 0x2 : 0x0)
                | (((uint32_t) svga->displine & 1u) << 2)
                | ((dev->frame_count & 1u) << 3);
            return 1;
        case RAGE128_MEM_VGA_WP_SEL:
            *val = dev->mem_vga_wp_sel;
            return 1;
        case RAGE128_MEM_VGA_RP_SEL:
            *val = dev->mem_vga_rp_sel;
            return 1;
        case RAGE128_PALETTE_INDEX:
            *val = dev->palette_index;
            return 1;
        case RAGE128_PALETTE_DATA:
            *val = rage128_palette_data_read(dev);
            return 1;
        case RAGE128_CRTC_H_TOTAL_DISP:
            *val = dev->crtc_h_total_disp;
            return 1;
        case RAGE128_CRTC_H_SYNC_STRT_WID:
            *val = dev->crtc_h_sync_strt_wid;
            return 1;
        case RAGE128_CRTC_V_TOTAL_DISP:
            *val = dev->crtc_v_total_disp;
            return 1;
        case RAGE128_CRTC_V_SYNC_STRT_WID:
            *val = dev->crtc_v_sync_strt_wid;
            return 1;
        case RAGE128_CRTC_VLINE_CRNT_VLINE:
            /* [26:16] CRTC_CRNT_VLINE is the read-only current raster
               line (RRG: CRTC_VLINE_CRNT_VLINE, p. 3-68 / PDF 86). */
            *val = (dev->crtc_vline & 0x7ff) | (((uint32_t) svga->displine & 0x7ff) << 16);
            return 1;
        case RAGE128_CRTC_CRNT_FRAME:
            *val = dev->frame_count;
            return 1;
        case RAGE128_SNAPSHOT_VH_COUNTS:
            *val = dev->snapshot_vh_counts;
            return 1;
        case RAGE128_SNAPSHOT_F_COUNT:
            *val = dev->snapshot_f_count;
            return 1;
        case RAGE128_N_VIF_COUNT:
            *val = dev->n_vif_count;
            return 1;
        case RAGE128_SNAPSHOT_VIF_COUNT:
            /* The video-in field counts [20:0] read 0: they count fields
               from the video input port, which is not emulated. */
            *val = dev->snapshot_vif_cntl;
            return 1;
        case RAGE128_CRTC_GUI_TRIG_VLINE:
            *val = (dev->crtc_gui_trig_vline & 0x07ff07ff)
                | (rage128_gui_trig_vline_in(dev) ? 0x80000000u : 0);
            return 1;
        case RAGE128_CRTC_DEBUG:
            *val = dev->crtc_debug;
            return 1;
        case RAGE128_CRTC_OFFSET:
            /* [30] CRTC_GUI_TRIG_OFFSET, set while a written base is not
               yet displayed, and [31] CRTC_OFFSET_LOCK; both bits also
               appear in CRTC_OFFSET_CNTL (RRG: CRTC_OFFSET, pp. 3-70-3-71
               / PDF 88-89). */
            *val = (dev->crtc_offset & 0x01ffffff)
                | (dev->crtc_offset_pending ? 0x40000000u : 0)
                | (dev->crtc_offset_lock ? 0x80000000u : 0);
            return 1;
        case RAGE128_CRTC_OFFSET_CNTL:
            *val = (dev->crtc_offset_cntl & 0x3fffffff)
                | (dev->crtc_offset_pending ? 0x40000000u : 0)
                | (dev->crtc_offset_lock ? 0x80000000u : 0);
            return 1;
        case RAGE128_CRTC_PITCH:
            *val = dev->crtc_pitch;
            return 1;
        case RAGE128_CUR_OFFSET:
            /* Bit 31 of each of the three cursor registers reads the
               shared CUR_LOCK. */
            *val = dev->cur_offset | (dev->cur_lock ? 0x80000000u : 0);
            return 1;
        case RAGE128_CUR_HORZ_VERT_POSN:
            *val = dev->cur_horz_vert_posn | (dev->cur_lock ? 0x80000000u : 0);
            return 1;
        case RAGE128_CUR_HORZ_VERT_OFF:
            *val = dev->cur_horz_vert_off | (dev->cur_lock ? 0x80000000u : 0);
            return 1;
        case RAGE128_CUR_CLR0:
            *val = dev->cur_clr0;
            return 1;
        case RAGE128_CUR_CLR1:
            *val = dev->cur_clr1;
            return 1;
        case RAGE128_OVR_CLR:
            *val = dev->ovr_clr;
            return 1;
        case RAGE128_OVR_WID_LEFT_RIGHT:
            *val = dev->ovr_wid_left_right;
            return 1;
        case RAGE128_OVR_WID_TOP_BOTTOM:
            *val = dev->ovr_wid_top_bottom;
            return 1;
        case RAGE128_DAC_EXT_CNTL:
            *val = dev->dac_ext_cntl;
            return 1;
        case RAGE128_DAC_CRC_SIG:
            /* Latched when a capture ends; it holds until DAC_CRC_EN is
               cleared and set again (RRG: DAC_CRC_SIG, p. 3-126 /
               PDF 144). */
            *val = dev->dac_crc_sig;
            return 1;
        case RAGE128_DDA_CONFIG:
            *val = dev->dda_config;
            return 1;
        case RAGE128_DDA_ON_OFF:
            *val = dev->dda_on_off;
            return 1;
        case RAGE128_VGA_DDA_CONFIG:
            *val = dev->vga_dda_config;
            return 1;
        case RAGE128_VGA_DDA_ON_OFF:
            *val = dev->vga_dda_on_off;
            return 1;
        default:
            return 0;
    }
}

int
rage128_display_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask)
{
    svga_t *svga = &dev->svga;

#define MERGE(field) ((field) = ((field) & ~mask) | (val & mask))

    switch (off) {
        case RAGE128_CLOCK_CNTL_INDEX:
            MERGE(dev->clock_cntl_index);
            dev->clock_cntl_index &= 0x3ff;   /* [31:10] reserved */
            rage128_recalctimings_apply(dev); /* PPLL_DIV_SEL selects pixel clock */
            return 1;
        case RAGE128_CLOCK_CNTL_DATA:
            rage128_pll_write(dev, val, mask);
            return 1;
        case RAGE128_CRTC_GEN_CNTL:
            {
                uint32_t was_ext = dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN;

                MERGE(dev->crtc_gen_cntl);
                /* When CRTC_EXT_DISP_EN goes from 0 to 1, the displayed height
                   is taken again from CRTC_V_TOTAL_DISP, so a height set
                   through the VGA CRTC readback path before the switch to
                   extended mode is used. */
                if (!was_ext && (dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN))
                    dev->crtc_v_disp_active = (dev->crtc_v_total_disp >> 16) & 0x7ff;
                rage128_update_banking(dev);
                rage128_update_hwcursor(dev);
                svga->fullchange = svga->monitor->mon_changeframecount;
                rage128_recalctimings_apply(dev);
                return 1;
            }
        case RAGE128_CRTC_EXT_CNTL:
            MERGE(dev->crtc_ext_cntl);
            rage128_update_banking(dev);
            rage128_update_hwcursor(dev);
            svga->fullchange = svga->monitor->mon_changeframecount;
            rage128_recalctimings_apply(dev);
            return 1;
        case RAGE128_DAC_CNTL:
            {
                const uint32_t dac_old = dev->dac_cntl;

                /* [7] DAC_CMP_OUTPUT is read-only; [31:24] DAC_MASK is kept in
                   dac_mask_prog, shared with port 3C6. */
                MERGE(dev->dac_cntl);
                dev->dac_cntl &= 0x00ffff7f;
                /* Changing DAC_4BPP_PIX_ORDER changes the picture without a
                   VRAM write, so force a redraw; the change marks would
                   otherwise keep the old frame. */
                if ((dac_old ^ dev->dac_cntl) & RAGE128_DAC_4BPP_PIX_ORDER)
                    svga->fullchange = svga->monitor->mon_changeframecount;
                /* DAC_PDWN decides which renderer is installed, so recalculate
                   the timings, and force a redraw since VRAM did not change. */
                if ((dac_old ^ dev->dac_cntl) & RAGE128_DAC_PDWN) {
                    svga->fullchange = svga->monitor->mon_changeframecount;
                    rage128_recalctimings_apply(dev);
                }
                /* DAC_CRC_EN acts only when it changes: setting it arms the
                   CRC for the next vertical blank, clearing it stops a
                   capture and leaves the last signature readable (RRG:
                   DAC_CNTL, p. 3-125 / PDF 143). Writing 1 over 1 does
                   nothing. */
                if ((dac_old ^ dev->dac_cntl) & RAGE128_DAC_CRC_EN)
                    dev->dac_crc_state = (dev->dac_cntl & RAGE128_DAC_CRC_EN) ? 1 : 0;
                if (mask & 0xff000000) {
                    uint8_t m = (uint8_t) (mask >> 24);

                    dev->dac_mask_prog = (uint8_t) ((dev->dac_mask_prog & ~m)
                                                    | ((val >> 24) & m));
                    rage128_dac_mask_apply(dev);
                }
                svga_set_ramdac_type(svga, (dev->dac_cntl & RAGE128_DAC_8BIT_EN) ? RAMDAC_8BIT : RAMDAC_6BIT);
                return 1;
            }
        case RAGE128_CRTC_STATUS:
            /* [1] CRTC_VBLANK_SAVE_CLEAR: writing 1 clears
               CRTC_VBLANK_SAVE (RRG: CRTC_STATUS, p. 3-66 / PDF 84). */
            if ((mask & 0x2) && (val & 0x2))
                dev->vblank_save = 0;
            return 1;
        case RAGE128_MEM_VGA_WP_SEL:
            /* Page selects for the two 32 KB halves of the A0000 window
               (the sources are cited at RAGE128_MEM_VGA_WP_SEL in
               vid_ati_rage128_regs.h). rage128_update_banking turns
               them into bank_w and bank_r, which the split-bank A0000
               handlers add for each half. */
            MERGE(dev->mem_vga_wp_sel);
            dev->mem_vga_wp_sel &= 0x03ff03ff;
            rage128_update_banking(dev);
            return 1;
        case RAGE128_MEM_VGA_RP_SEL:
            MERGE(dev->mem_vga_rp_sel);
            dev->mem_vga_rp_sel &= 0x03ff03ff;
            rage128_update_banking(dev);
            return 1;
        case RAGE128_PALETTE_INDEX:
            MERGE(dev->palette_index);
            dev->palette_index &= 0x00ff00ff;
            return 1;
        case RAGE128_PALETTE_DATA:
            rage128_palette_data_write(dev, val, mask);
            return 1;
        case RAGE128_CRTC_H_TOTAL_DISP:
            MERGE(dev->crtc_h_total_disp);
            rage128_recalctimings_apply(dev);
            return 1;
        case RAGE128_CRTC_H_SYNC_STRT_WID:
            MERGE(dev->crtc_h_sync_strt_wid);
            rage128_recalctimings_apply(dev);
            return 1;
        case RAGE128_CRTC_V_TOTAL_DISP:
            MERGE(dev->crtc_v_total_disp);
            if (mask & 0x07ff0000)
                dev->crtc_v_disp_active = (dev->crtc_v_total_disp >> 16) & 0x7ff;
            rage128_recalctimings_apply(dev);
            return 1;
        case RAGE128_CRTC_V_SYNC_STRT_WID:
            MERGE(dev->crtc_v_sync_strt_wid);
            rage128_recalctimings_apply(dev);
            return 1;
        case RAGE128_CRTC_VLINE_CRNT_VLINE:
            MERGE(dev->crtc_vline);
            dev->crtc_vline &= 0x7ff; /* [26:16] is read-only */
            return 1;
        case RAGE128_CRTC_CRNT_FRAME:
            return 1; /* read-only frame counter */
        case RAGE128_SNAPSHOT_VH_COUNTS:
        case RAGE128_SNAPSHOT_F_COUNT:
            return 1; /* read-only latched captures */
        case RAGE128_N_VIF_COUNT:
            MERGE(dev->n_vif_count);
            dev->n_vif_count &= 0x800003ffu;
            return 1;
        case RAGE128_SNAPSHOT_VIF_COUNT:
            /* [24] AUTO_SNAPSHOT_TAKEN: writing 0 enables automatic
               snapshots again, which never happen here because their
               video-in event source is not emulated. [25]
               MANUAL_SNAPSHOT_NOW: each write of 1 takes a snapshot at
               once and holds off automatic ones. The video-in field
               counts [20:0] are read-only (RRG: SNAPSHOT_VIF_COUNT,
               p. 3-216 / PDF 234). */
            MERGE(dev->snapshot_vif_cntl);
            dev->snapshot_vif_cntl &= 0x03000000u;
            if (val & mask & 0x02000000u)
                rage128_snapshot_take(dev);
            return 1;
        case RAGE128_CRTC_GUI_TRIG_VLINE:
            MERGE(dev->crtc_gui_trig_vline);
            dev->crtc_gui_trig_vline &= 0x07ff07ff; /* [31] is read-only */
            return 1;
        case RAGE128_CRTC_DEBUG:
            MERGE(dev->crtc_debug);
            return 1;
        case RAGE128_CRTC_OFFSET:
            {
                uint32_t merged = ((dev->crtc_offset & ~mask) | (val & mask));

                /* [24:0] base address with bits 2:0 always 0. A write of the
                   base sets the CRTC_GUI_TRIG_OFFSET flip-pending status, and
                   scanout takes the new base at vertical blank, or at the next
                   horizontal blank with CRTC_OFFSET_FLIP_CNTL set (below).
                   CRTC_OFFSET_LOCK holds the update back until it is cleared
                   (RRG: CRTC_OFFSET, pp. 3-70-3-71 / PDF 88-89; RRG:
                   CRTC_OFFSET_CNTL, p. 3-72 / PDF 90). */
                dev->crtc_offset = merged & 0x01fffff8;
                if (mask & 0x80000000u)
                    dev->crtc_offset_lock = !!(val & 0x80000000u);
                if (mask & 0x01fffff8) {
                    /* Page flip: the new buffer must hold the finished frame
                       before scanout takes it, so finish any deferred 3D
                       batch now. Some clients, such as the OpenGL driver
                       running Quake III, flip by writing CRTC_OFFSET without a
                       WAIT_UNTIL, which makes this their only present
                       barrier. On the CCE thread the frame's drawing is
                       already submitted and a flush is enough; on the CPU
                       thread the drain first waits for the executor to go
                       idle, then flushes. */
                    if (rage128_on_cce_thread) {
                        if (dev->gpu)
                            rage128_gpu_ftl_flip(dev);
                        rage128_raster_flush(dev);
                    } else {
                        /* Timed for the flip= field of the pace log line. */
                        struct timespec t0, t1;

                        clock_gettime(CLOCK_MONOTONIC, &t0);
                        rage128_pm4_drain_wait(dev);
                        clock_gettime(CLOCK_MONOTONIC, &t1);
                        dev->flip_stall_ns += (uint64_t) (t1.tv_sec - t0.tv_sec) * 1000000000ull
                            + (uint64_t) (t1.tv_nsec - t0.tv_nsec);
                    }
                    if (dev->gpu) {
                        /* The end of the frame is now submitted (the flush
                           above on the CCE thread, the drain on the CPU
                           thread), so queue the present-snapshot copy after
                           it. Nothing waits for it; scanout uses it once its
                           fence signals. The copy is a linear range, so a
                           tiled frame gets none (and the vsync choice never
                           takes one for it): tiled scanout reads the guest
                           bytes through the tile transform. */
                        rage128_gpu_flip_mark(dev);
                        rage128_gpu_present(dev);
                        if (!rage128_scanout_tiled_now(dev)) {
                            rage128_gpu_present_copy(dev, merged & 0x01fffff8,
                                                     rage128_scanout_bytes(svga));
                            /* Wait out the new frame's GPU work here, on the
                               CCE thread, so the first scanned lines do not. */
                            if (rage128_on_cce_thread)
                                rage128_gpu_present_fence(dev, merged & 0x01fffff8,
                                                          rage128_scanout_bytes(svga));
                        }
                    }
                    /* Realtime pacer hold, applied once per frame at the
                       flip, the point where a delay holds the driver back
                       instead of only adding latency (see
                       rage128_pace_consume). It comes after the snapshot
                       copy is submitted, so the copy's fence can complete
                       during the hold, and with any hold active the vsync
                       choice finds a finished copy. Only a flip written by
                       the CCE thread sleeps. */
                    if (rage128_on_cce_thread)
                        rage128_pace_consume(dev);
                    dev->crtc_offset_pending = 1;
                    /* With CRTC_OFFSET_FLIP_CNTL set the new base is taken at
                       the next horizontal blank, not at the write (RRG:
                       CRTC_OFFSET_CNTL, p. 3-72 / PDF 90). Arm it for the next
                       svga poll tick: every tick is a boundary between line
                       phases on the CPU thread and stands in for the
                       horizontal blank. This store may come from the CCE
                       thread; it is an aligned int, which the tick sees at
                       worst one tick late. */
                    if (dev->crtc_offset_cntl & RAGE128_CRTC_OFFSET_FLIP_CNTL)
                        dev->crtc_offset_hblank = 1;
                }
                return 1;
            }
        case RAGE128_CRTC_OFFSET_CNTL:
            /* The tile controls take effect at the next vsync or timing
               recalculation: CRTC_TILE_EN and the low 4 bits of
               CRTC_TILE_LINE. CRTC_OFFSET_LOCK holds CRTC_TILE_LINE back
               with CRTC_OFFSET, and a pending CRTC_OFFSET carries the
               line to the flip point with it, so the written line waits
               here until rage128_scanout_tile_line_refresh or the flip
               latch takes it. CRTC_TILE_ALIGN and CRTC_TILE_LINE bit 4
               serve the memory checkerboarding, which is not modeled, so
               they are stored and read back only (RRG: CRTC_OFFSET_CNTL,
               pp. 3-71-3-72 / PDF 89-90). */
            MERGE(dev->crtc_offset_cntl);
            dev->crtc_offset_cntl &= 0x3fffffff;
            if (mask & 0x80000000u)
                dev->crtc_offset_lock = !!(val & 0x80000000u);
            return 1;
        case RAGE128_CRTC_PITCH:
            MERGE(dev->crtc_pitch);
            dev->crtc_pitch &= 0x3ff;
            rage128_recalctimings_apply(dev);
            return 1;
        case RAGE128_CUR_OFFSET:
            /* [24:0] image address, bits 3:0 always 0; [31] the shared
               CUR_LOCK (RRG: CUR_OFFSET, p. 3-81 / PDF 99). */
            MERGE(dev->cur_offset);
            if (mask & 0x80000000u)
                dev->cur_lock = !!(dev->cur_offset & 0x80000000u);
            dev->cur_offset &= 0x01fffff0;
            rage128_cursor_publish(dev);
            return 1;
        case RAGE128_CUR_HORZ_VERT_POSN:
            /* CUR_VERT_POSN [10:0], CUR_HORZ_POSN [26:16], [31] the
               shared CUR_LOCK (RRG: CUR_HORZ_VERT_POSN, pp. 3-81-3-82 /
               PDF 99-100). */
            MERGE(dev->cur_horz_vert_posn);
            if (mask & 0x80000000u)
                dev->cur_lock = !!(dev->cur_horz_vert_posn & 0x80000000u);
            dev->cur_horz_vert_posn &= 0x07ff07ff;
            rage128_cursor_publish(dev);
            return 1;
        case RAGE128_CUR_HORZ_VERT_OFF:
            /* CUR_VERT_OFF [5:0], CUR_HORZ_OFF [21:16], [31] the shared
               CUR_LOCK (RRG: CUR_HORZ_VERT_OFF, pp. 3-82-3-83 /
               PDF 100-101). */
            MERGE(dev->cur_horz_vert_off);
            if (mask & 0x80000000u)
                dev->cur_lock = !!(dev->cur_horz_vert_off & 0x80000000u);
            dev->cur_horz_vert_off &= 0x003f003f;
            rage128_cursor_publish(dev);
            return 1;
        case RAGE128_CUR_CLR0:
            MERGE(dev->cur_clr0);
            dev->cur_clr0 &= 0x00ffffff;
            return 1;
        case RAGE128_CUR_CLR1:
            MERGE(dev->cur_clr1);
            dev->cur_clr1 &= 0x00ffffff;
            return 1;
        case RAGE128_OVR_CLR:
            MERGE(dev->ovr_clr);
            rage128_overscan_touch(dev, svga);
            return 1;
        case RAGE128_OVR_WID_LEFT_RIGHT:
            MERGE(dev->ovr_wid_left_right);
            rage128_overscan_touch(dev, svga);
            return 1;
        case RAGE128_OVR_WID_TOP_BOTTOM:
            MERGE(dev->ovr_wid_top_bottom);
            rage128_overscan_touch(dev, svga);
            return 1;
        case RAGE128_DAC_EXT_CNTL:
            {
                const uint32_t ext_old = dev->dac_ext_cntl;

                MERGE(dev->dac_ext_cntl);
                /* DAC_FORCE_BLANK_OFF_EN, DAC_FORCE_DATA_EN, DAC_FORCE_DATA_SEL
                   and DAC_FORCE_DATA all change the output without a VRAM
                   write, so recalculate and force a redraw. */
                if ((ext_old ^ dev->dac_ext_cntl) & 0x0000fff0) {
                    svga->fullchange = svga->monitor->mon_changeframecount;
                    rage128_recalctimings_apply(dev);
                }
                return 1;
            }
        case RAGE128_DAC_CRC_SIG:
            return 1; /* read-only */
        case RAGE128_DDA_CONFIG:
            MERGE(dev->dda_config);
            return 1;
        case RAGE128_DDA_ON_OFF:
            MERGE(dev->dda_on_off);
            return 1;
        case RAGE128_VGA_DDA_CONFIG:
            MERGE(dev->vga_dda_config);
            return 1;
        case RAGE128_VGA_DDA_ON_OFF:
            MERGE(dev->vga_dda_on_off);
            return 1;
        default:
            return 0;
    }
#undef MERGE
}

/* Power-on state. The register values are the guide's per-field reset
   values, given with the register defines in vid_ati_rage128_regs.h
   (CLK_PIN_CNTL composes to 0xf7 with reserved bit 3 clear: RRG:
   CLK_PIN_CNTL, p. 3-87 / PDF 105). */
void
rage128_display_reset(rage128_t *dev)
{
    memset(dev->pll_regs, 0, sizeof(dev->pll_regs));
    dev->pll_regs[RAGE128_PLL_CLK_PIN_CNTL] = RAGE128_PLL_CLK_PIN_CNTL_DEFAULT;
    dev->pll_regs[RAGE128_PLL_PPLL_CNTL]    = 0x0000cc03;
    dev->pll_regs[RAGE128_PLL_XPLL_CNTL]    = 0x0000cc03;
    dev->pll_regs[RAGE128_PLL_XDLL_CNTL]    = 0x000b000b;
    dev->pll_regs[RAGE128_PLL_MPLL_CNTL]    = 0x0000cc03;
    dev->pll_regs[RAGE128_PLL_AGP_PLL_CNTL] = 0x7a770000;
    dev->pll_regs[RAGE128_PLL_FCP_CNTL]     = 0x00000404;
    dev->pll_regs[RAGE128_PLL_TEST_CNTL]    = 0x00000200 & 0x00ffffff;
    dev->pll_test_count_base                = 0;
    dev->pll_test_zero_tsc                  = tsc;
    dev->pll_test_acc                       = 0;
    /* PLL_MASK_READ_B [9] resets to 1, which hides the live count (RRG:
       PLL_TEST_CNTL, p. 3-105 / PDF 123). */
    dev->pll_regs[RAGE128_PLL_TEST_CNTL] |= (1 << 9);

    dev->clock_cntl_index    = 0;
    dev->ppll_update_pending = 0;
    rage128_ppll_commit(dev);

    dev->crtc_gen_cntl        = RAGE128_CRTC_GEN_CNTL_DEFAULT;
    dev->crtc_ext_cntl        = RAGE128_CRTC_EXT_CNTL_DEFAULT;
    dev->dac_cntl             = RAGE128_DAC_CNTL_DEFAULT & 0x00ffff7f;
    dev->dac_mask_prog        = 0xff;
    dev->svga.dac_mask        = 0xff;
    dev->crtc_h_total_disp    = 0;
    dev->crtc_h_sync_strt_wid = 0;
    dev->crtc_v_total_disp    = 0;
    dev->crtc_v_disp_active   = 0;
    dev->crtc_v_sync_strt_wid = 0;
    dev->crtc_vline           = 0;
    dev->crtc_gui_trig_vline  = 0;
    atomic_store(&dev->vline_in_window, 0);
    dev->crtc_debug             = 0;
    dev->crtc_offset            = 0;
    dev->crtc_offset_latched    = 0;
    dev->scanout_seed_base      = 0;
    dev->scanout_snap_cur       = 0;
    dev->scanout_hazard         = 1;
    dev->scanout_idle_frames    = 0;
    dev->scanout_tiled          = 0;
    dev->scanout_tile_pitch     = 0;
    dev->scanout_tile_line      = 0;
    dev->scanout_tile_xin       = 0;
    dev->scanout_tile_c0        = 0;
    dev->crtc_offset_cntl       = 0;
    dev->crtc_tile_line_latched = 0;
    dev->crtc_pitch             = 0;
    dev->cur_offset             = 0;
    dev->cur_horz_vert_posn     = 0;
    dev->cur_horz_vert_off      = 0;
    dev->cur_offset_act         = 0;
    dev->cur_posn_act           = 0;
    dev->cur_hvoff_act          = 0;
    dev->cur_lock               = 0;
    dev->cur_clr0               = 0;
    dev->cur_clr1               = 0;
    rage128_update_hwcursor(dev);
    dev->ovr_clr             = 0;
    dev->ovr_wid_left_right  = 0;
    dev->ovr_wid_top_bottom  = 0;
    dev->dac_ext_cntl        = 0;
    dev->dac_const_on        = 0;
    dev->dac_const_color     = 0;
    dev->dac_blank_frame     = 0;
    dev->dac_crc_state       = 0;
    dev->dac_crc_sig         = 0;
    dev->dda_config          = 0;
    dev->dda_on_off          = 0;
    dev->vga_dda_config      = 0;
    dev->vga_dda_on_off      = 0;
    dev->palette_index       = 0;
    dev->frame_count         = 0;
    dev->snapshot_vh_counts  = 0;
    dev->snapshot_f_count    = 0;
    dev->n_vif_count         = 0;
    dev->snapshot_vif_cntl   = 0;
    dev->crtc_offset_pending = 0;
    dev->crtc_offset_hblank  = 0;
    dev->crtc_offset_lock    = 0;
    dev->vblank_save         = 0;

    dev->mem_vga_wp_sel = 0;
    dev->mem_vga_rp_sel = 0;
    rage128_update_banking(dev);
    rage128_updatemapping(dev);
}
