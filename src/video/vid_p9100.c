/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Weitek Power 9100 emulation (Diamond Viper SE PCI).
 *
 *          The SVGA unit is the W5286 core (the P9100 data book says its
 *          SVGA registers are identical to the W5286 ones), emulated
 *          together with the W5286 GHA blitter at graphics
 *          indexes 09-0F as the W5186/W5286 Windows drivers use it. The GHA
 *          addressing modes in the upper bits of graphics index 03, the
 *          BLKMOV instruction and the GHA interrupt are not emulated, nor
 *          are the odd character widths, bit order and CPU divide by 2 of
 *          control register 0.
 *
 *          The native side covers the configuration registers, RAMDAC
 *          access through the coprocessor address space, the native
 *          video timing generator and the parameter and drawing engines
 *          (quad, blit, pixel1, pixel8 and next_pixels), based on the
 *          FMT-3631/FMT-3632 emulation in Tsugaru by CaptainYS and the
 *          Power 9100 data book. Drawing completes immediately, so the
 *          engine never reports busy. Pick mode, buffer 1 and the video
 *          coprocessor are not emulated.
 *
 * Authors: Cacodemon345
 *          Soji Yamakawa (CaptainYS)
 *
 *          Copyright 2020 Soji Yamakawa.
 *          Copyright 2026 Cacodemon345.
 *
 *          The native parameter and drawing engine code is derived from
 *          Tsugaru (src/towns/crtc/fmt3631.cpp), which carries the
 *          following licence:
 *
 *          Copyright 2020 Soji Yamakawa (CaptainYS, http://www.ysflight.com)
 *
 *          Redistribution and use in source and binary forms, with or
 *          without modification, are permitted provided that the
 *          following conditions are met:
 *
 *          1. Redistributions of source code must retain the above
 *             copyright notice, this list of conditions and the following
 *             disclaimer.
 *
 *          2. Redistributions in binary form must reproduce the above
 *             copyright notice, this list of conditions and the following
 *             disclaimer in the documentation and/or other materials
 *             provided with the distribution.
 *
 *          3. Neither the name of the copyright holder nor the names of its
 *             contributors may be used to endorse or promote products
 *             derived from this software without specific prior written
 *             permission.
 *
 *          THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *          CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *          INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *          MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *          DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *          CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *          SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *          LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *          USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *          AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *          LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *          ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *          POSSIBILITY OF SUCH DAMAGE.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/plat_unused.h>
#include "cpu.h"

#define BIOS_VIPER_SE_PCI_PATH "roms/video/weitek/viper_pci_1.05.bin"
#define BIOS_PAPILIO_PCI_PATH  "roms/video/weitek/papilio_ultracad_pro_1.46.bin"

enum {
    P9100_VIPER_SE = 0,
    P9100_PAPILIO
};

/* CONFIG[65] */
#define CFG65_MODESELECT 0x02 /* 1 = VGA emulation mode, 0 = native mode */
#define CFG65_NA_ENABLE  0x04 /* Native mode: decode A0000-AFFFF */
#define CFG65_NA_SELECT  0x08 /* Native mode: A0000 window is the coprocessor (1) or the frame buffer (0) */

/* SVGA unit miscellaneous register (sequencer index 11) */
#define MISC_BSD 0x40 /* Bank switching disable */
#define MISC_CRL 0x20 /* Extended register lock */

/* pu_config straps: PCI bus, VGA mode at reset, VGA present, add-in board.
   Bits 15-12 are read by the BIOS to tell the RAMDAC fitted, 8 = IBM RGB525. */
#define PU_CONFIG_DEFAULT 0x44008001

/* Raster register */
#define RASTER_TRANSPARENT 0x20000 /* Clear 2-colour pattern bits leave the destination alone */
#define RASTER_OVERSIZED   0x10000 /* Quads also touch their Bresenham outline */
#define RASTER_PIX1_TRANSP 0x08000 /* Clear pixel1 bits leave the destination alone */
#define RASTER_PAT_4COLOR  0x04000
#define RASTER_PAT_ENABLE  0x02000 /* Solid colour disable */

/* Load coordinates vertex types */
#define PRIM_POINT 0
#define PRIM_LINE  1
#define PRIM_TRI   2
#define PRIM_QUAD  3
#define PRIM_RECT  4

/* Status register */
#define STATUS_BLIT_SW         0x00000020
#define STATUS_QUAD_SW         0x00000010
#define STATUS_QUAD_HIDDEN     0x00000004
#define STATUS_QUAD_VISIBLE    0x00000002
#define STATUS_QUAD_INTERSECTS 0x00000001

typedef struct p9100_t {
    svga_t svga;

    int type;

    rom_t         bios_rom;
    mem_mapping_t native_mapping;

    /* Papilio: ICS9161A on CKSEL[1:0], making the RGB525 reference clock. */
    void   *clock_chip;
    uint8_t cksel;

    uint8_t pci_regs[256];
    uint8_t pci_slot;
    uint8_t irq_state;
    uint8_t config65;
    uint8_t config66;

    uint8_t bank;
    uint8_t gha_exec;
    uint8_t unlock_writes;

    uint32_t vram_size;
    uint32_t vram_mask;
    uint32_t native_base;

    uint32_t sysconfig;
    uint32_t interrupt;
    uint32_t interrupt_en;
    uint32_t alt_read_bank;
    uint32_t alt_write_bank;
    uint32_t video_regs[0x20];
    uint32_t vram_regs[0x20];

    /* Parameter engine */
    int32_t  x[4];
    int32_t  y[4];
    uint8_t  cindex;
    uint8_t  prim;
    uint32_t w_off;
    uint32_t status;

    /* Drawing engine */
    uint32_t color[4];
    uint32_t pmask;
    uint32_t draw_mode;
    uint32_t pat_originx;
    uint32_t pat_originy;
    uint32_t raster;
    uint32_t pixel8_reg;
    uint32_t p_w_min;
    uint32_t p_w_max;
    uint32_t b_w_min;
    uint32_t b_w_max;
    uint32_t pattern[8];
} p9100_t;

static video_timings_t timing_p9100_pci = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 10, .read_w = 10, .read_l = 10 };

#ifdef ENABLE_P9100_LOG
int p9100_do_log = ENABLE_P9100_LOG;

static void
p9100_log(const char *fmt, ...)
{
    va_list ap;

    if (p9100_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define p9100_log(fmt, ...)
#endif

static void     p9100_recalcmapping(p9100_t *p9100);
static int      p9100_bytes_pp(const p9100_t *p9100);
static uint32_t p9100_pitch(const p9100_t *p9100);

/* W5286 graphics hardware accelerator (GHA).

   The GHA works on 32-bit longwords. A longword address L is the same
   frame buffer doubleword in every mode: in planar mode its four bytes are
   the four planes at plane offset L, in packed mode they are the four bytes
   at linear address L * 4. Each longword is seen as eight nibbles, nibble 0
   being the leftmost pixel (bit 7 of each plane byte in planar mode, the
   high nibble of the lowest byte in packed mode); bit 7 of the start, end
   and rotate masks stands for nibble 0. */
#define GHA_PLANAR   0
#define GHA_PACKED8  1
#define GHA_PACKED16 3

static int
p9100_gha_mode(p9100_t *p9100)
{
    return (p9100->svga.gdcreg[0x0c] >> 4) & 0x03;
}

static uint32_t
p9100_gha_read(p9100_t *p9100, uint32_t addr)
{
    svga_t *svga = &p9100->svga;

    return *(uint32_t *) &svga->vram[(addr << 2) & p9100->vram_mask];
}

static void
p9100_gha_write(p9100_t *p9100, uint32_t addr, uint32_t val, uint32_t mask)
{
    svga_t  *svga = &p9100->svga;
    uint32_t off  = (addr << 2) & p9100->vram_mask;
    uint32_t old  = *(uint32_t *) &svga->vram[off];

    *(uint32_t *) &svga->vram[off] = (old & ~mask) | (val & mask);
    svga->changedvram[off >> 12]   = svga->monitor->mon_changeframecount;
}

/* Nibble mask to bit mask. */
static uint32_t
p9100_gha_nmask(int mode, uint8_t m)
{
    uint32_t ret = 0;

    if (mode == GHA_PLANAR)
        return m * 0x01010101;

    for (int j = 0; j < 4; j++) {
        if (m & (0x80 >> (j << 1)))
            ret |= 0xf0 << (j << 3);
        if (m & (0x40 >> (j << 1)))
            ret |= 0x0f << (j << 3);
    }
    return ret;
}

/* Planar plane mask to bit mask. */
static uint32_t
p9100_gha_pmask(uint8_t planes)
{
    uint32_t ret = 0;

    for (int p = 0; p < 4; p++) {
        if (planes & (1 << p))
            ret |= 0xff << (p << 3);
    }
    return ret;
}

/* Cyclic right rotation by cnt nibbles. */
static uint32_t
p9100_gha_rotate(int mode, uint32_t val, int cnt)
{
    uint32_t ret = 0;

    if (!cnt)
        return val;

    if (mode == GHA_PLANAR) {
        for (int p = 0; p < 4; p++) {
            uint8_t b = val >> (p << 3);

            b = (b >> cnt) | (b << (8 - cnt));
            ret |= b << (p << 3);
        }
        return ret;
    }

    /* Bytes in display order, lowest address first. */
    val = ((val & 0xff) << 24) | ((val & 0xff00) << 8) | ((val >> 8) & 0xff00) | (val >> 24);
    val = (val >> (cnt << 2)) | (val << (32 - (cnt << 2)));
    return ((val & 0xff) << 24) | ((val & 0xff00) << 8) | ((val >> 8) & 0xff00) | (val >> 24);
}

/* Inversion and logical operation, per plane in planar mode (each field
   carries one bit per plane), the same for every bit in packed mode. */
static uint32_t
p9100_gha_rop(int mode, const uint8_t *ins, uint32_t src, uint32_t dst)
{
    uint32_t ret = 0;
    uint8_t  sinv;
    uint8_t  dinv;
    uint8_t  lop0;
    uint8_t  lop1;

    if (mode == GHA_PLANAR) {
        sinv = ins[4] & 0x0f;
        dinv = ins[4] >> 4;
        lop0 = ins[5] & 0x0f;
        lop1 = ins[5] >> 4;
    } else {
        sinv = (ins[0] & 0x04) ? 0x0f : 0x00;
        dinv = (ins[0] & 0x08) ? 0x0f : 0x00;
        lop0 = (ins[0] & 0x01) ? 0x0f : 0x00;
        lop1 = (ins[0] & 0x02) ? 0x0f : 0x00;
    }

    for (int p = 0; p < 4; p++) {
        uint8_t s = src >> (p << 3);
        uint8_t d = dst >> (p << 3);
        uint8_t r;

        if (sinv & (1 << p))
            s = ~s;
        if (dinv & (1 << p))
            d = ~d;

        switch ((((lop1 >> p) & 1) << 1) | ((lop0 >> p) & 1)) {
            default:
            case 0:
                r = s;
                break;
            case 1:
                r = s & d;
                break;
            case 2:
                r = s | d;
                break;
            case 3:
                r = s ^ d;
                break;
        }
        ret |= r << (p << 3);
    }

    return ret;
}

/* 9-bit pitch, made a 10-bit two's complement value by the DIR bit. */
static int32_t
p9100_gha_pitch(const uint8_t *ins, int low, int high)
{
    int32_t pitch = ins[low] | ((ins[high] & 0x01) << 8);

    if (ins[0x0f] & 0x80)
        pitch -= 0x200;
    return pitch;
}

/* Packed colour expansion source bits for one longword, most significant
   bit of the lowest byte first. */
static uint8_t
p9100_gha_expand_bits(p9100_t *p9100, uint32_t bitpos, int ppl)
{
    uint8_t ret = 0;

    for (int j = 0; j < ppl; j++) {
        uint32_t b    = bitpos + j;
        uint32_t lw   = p9100_gha_read(p9100, b >> 5);
        uint8_t  byte = lw >> (((b >> 3) & 3) << 3);

        if (byte & (0x80 >> (b & 7)))
            ret |= 1 << j;
    }
    return ret;
}

static uint32_t
p9100_gha_expand_color(int mode, uint8_t bits, uint16_t fg, uint16_t bg, uint8_t *pixmask)
{
    uint32_t ret = 0;

    *pixmask = 0;
    if (mode == GHA_PACKED16) {
        for (int j = 0; j < 2; j++) {
            ret |= ((bits & (1 << j)) ? fg : bg) << (j << 4);
            if (bits & (1 << j))
                *pixmask |= 0xf0 >> (j << 2);
        }
    } else {
        for (int j = 0; j < 4; j++) {
            ret |= (((bits & (1 << j)) ? fg : bg) & 0xff) << (j << 3);
            if (bits & (1 << j))
                *pixmask |= 0xc0 >> (j << 1);
        }
    }
    return ret;
}

static void
p9100_gha_blit(p9100_t *p9100, const uint8_t *ins)
{
    svga_t  *svga    = &p9100->svga;
    int      mode    = p9100_gha_mode(p9100);
    int      opcode  = ins[0] >> 5;
    int      is_long = opcode & 1;
    int      is_move = opcode & 2;
    int      ht      = ins[2];
    int      vt      = ins[7] | ((ins[0x0d] >> 6) << 8);
    uint32_t dst     = ins[8] | (ins[9] << 8) | (((ins[0x0d] >> 1) & 3) << 16);
    uint32_t src     = ins[0x0a] | (ins[0x0b] << 8) | (((ins[0x0f] >> 1) & 3) << 16);
    int32_t  dpitch  = p9100_gha_pitch(ins, 0x0c, 0x0d);
    int32_t  spitch  = p9100_gha_pitch(ins, 0x0e, 0x0f);
    int      step    = (ins[0x0f] & 0x80) ? -1 : 1;
    int      rotcnt  = (ins[0x0d] >> 3) & 7;
    uint32_t rmask   = p9100_gha_nmask(mode, ins[3]);
    /* The four low bits of the opcode byte drive the four memory plane
       strobes. In packed mode they also hold PAC_ROP, so they only act as
       byte lane enables for moves, which store the source unchanged; the
       256-colour driver relies on that to clip lines to the byte. */
    uint32_t pmask         = ((mode == GHA_PLANAR) || is_move) ? p9100_gha_pmask(ins[0] & 0x0f) : 0xffffffff;
    int      expand        = (mode == GHA_PLANAR) ? ((ins[0x0f] >> 5) & 1) : ((ins[0x0f] >> 4) & 3);
    int      ppl           = (mode == GHA_PACKED16) ? 2 : 4;
    uint16_t fg            = ins[4] | (ins[5] << 8);
    uint16_t bg            = svga->gdcreg[0x0e] | (svga->gdcreg[0x0f] << 8);
    uint32_t fgl           = (mode == GHA_PACKED16) ? (fg | (fg << 16)) : ((fg & 0xff) * 0x01010101);
    uint32_t bitmask       = 0xffffffff;
    int      packed_expand = (mode != GHA_PLANAR) && (expand >= 2);

    /* The GHA bit mask only protects bits in 8-bit blits, not in moves. */
    if ((mode == GHA_PACKED8) && !is_move)
        bitmask = svga->gdcreg[0x0d] * 0x01010101;

    for (int y = 0; y < vt; y++) {
        uint32_t s      = src;
        uint32_t d      = dst;
        uint32_t prev   = 0;
        uint32_t bitpos = (src << 5) + (((ins[0x0f] >> 3) & 1) << 4);

        if (is_long && !packed_expand) {
            prev = p9100_gha_read(p9100, s);
            s += step;
        }

        for (int x = 0; x <= ht; x++) {
            uint8_t  m   = 0xff;
            uint32_t old = p9100_gha_read(p9100, d);
            uint32_t data;

            if (x == 0)
                m &= ins[1];
            if ((x == ht) && ht)
                m &= ins[6];

            if (packed_expand) {
                uint8_t bits;
                uint8_t pixmask;

                if (expand == 3) /* Pattern fill, tiled from the top left of the screen. */
                    bits = p9100_gha_expand_bits(p9100, (s << 5) + ((d * ppl) & 31), ppl);
                else {
                    bits = p9100_gha_expand_bits(p9100, bitpos, ppl);
                    bitpos += ppl;
                }

                data = p9100_gha_expand_color(mode, bits, fg, bg, &pixmask);
                if (!is_move) {
                    /* One colour, clear source bits leave the destination alone. */
                    m &= pixmask;
                    data = p9100_gha_rop(mode, ins, fgl, old);
                }
            } else {
                uint32_t cur = p9100_gha_read(p9100, s);

                s += step;
                if (expand)
                    cur = ((cur >> (((ins[0x0f] >> 3) & 3) << 3)) & 0xff) * 0x01010101;
                data = (p9100_gha_rotate(mode, cur, rotcnt) & ~rmask) | (p9100_gha_rotate(mode, prev, rotcnt) & rmask);
                prev = cur;
                if (is_move) {
                    /* Moves never read the destination. In planar mode the
                       DEST INV bits then leave zeroes in place of the source
                       and SOURCE INV turns them into a fill colour, which is
                       how the W5186 drivers do solid fills. */
                    if (mode == GHA_PLANAR) {
                        data &= ~p9100_gha_pmask(ins[4] >> 4);
                        data = p9100_gha_rop(mode, ins, data, 0);
                    }
                } else
                    data = p9100_gha_rop(mode, ins, data, old);
            }

            p9100_gha_write(p9100, d, data, p9100_gha_nmask(mode, m) & pmask & bitmask);
            d += step;
        }

        dst = d + dpitch;
        if (packed_expand && (expand == 3))
            src = (src & ~0x1f) | ((src + step) & 0x1f);
        else if (packed_expand)
            src = ((bitpos + 31) >> 5) + spitch;
        else
            src = s + spitch;
    }
}

static void
p9100_gha_line(p9100_t *p9100, const uint8_t *ins)
{
    int      mode   = p9100_gha_mode(p9100);
    int      steep  = (ins[0] >> 5) & 1;
    int      count  = ins[7] | ((ins[0x0d] >> 6) << 8);
    uint32_t addr   = ins[8] | (ins[9] << 8) | (((ins[0x0d] >> 1) & 3) << 16);
    int32_t  pitch  = p9100_gha_pitch(ins, 0x0c, 0x0d);
    int      rotcnt = (ins[0x0d] >> 3) & 7;
    int      psz    = (mode == GHA_PLANAR) ? 1 : ((mode == GHA_PACKED16) ? 4 : 2);
    int      right  = (rotcnt == psz);
    int32_t  err    = (int16_t) (ins[0x0a] | (ins[0x0b] << 8));
    int32_t  inc    = ins[0x0e] | ((ins[0x0f] & 0x3f) << 8);
    int32_t  dec    = (int16_t) (ins[2] | (ins[3] << 8));
    uint8_t  m      = ins[1];
    uint16_t fg     = ins[4] | (ins[5] << 8);
    uint32_t src;
    uint32_t pmask = 0xffffffff;
    int      lw    = 0;

    /* Lines draw a source of all ones in planar mode, FCOLOR in packed mode. */
    if (mode == GHA_PLANAR) {
        src   = 0xffffffff;
        pmask = p9100_gha_pmask(ins[0] & 0x0f);
    } else if (mode == GHA_PACKED16)
        src = fg | (fg << 16);
    else
        src = (fg & 0xff) * 0x01010101;

    if (!m)
        return;

    /* Steep lines count scan lines. Flat lines count the longword
       boundaries they cross, then end inside the next longword at the
       last pixel the end mask takes in. */
    for (int n = 0; steep ? (n < count) : (lw <= count); n++) {
        uint32_t old = p9100_gha_read(p9100, addr);
        int      minor;

        if (!steep && (lw == count) && !(m & ins[6]))
            break;

        p9100_gha_write(p9100, addr, p9100_gha_rop(mode, ins, src, old), p9100_gha_nmask(mode, m) & pmask);

        /* Every step along the major axis adds the increment, a step along
           the minor axis as well adds the decrement, which the drivers
           program as a negative number (2 * minor and -2 * major, from an
           error term of -(major + 1)). */
        err += inc;
        minor = (err >= 0);
        if (minor)
            err += dec;

        if (steep || minor)
            addr += pitch;

        if (!steep || minor) {
            if (right) {
                m >>= psz;
                if (!m) {
                    m = (0xff << (8 - psz)) & 0xff;
                    addr++;
                    lw++;
                }
            } else {
                m = (m << psz) & 0xff;
                if (!m) {
                    m = 0xff >> (8 - psz);
                    addr--;
                    lw++;
                }
            }
        }
    }
}

static void
p9100_gha_run(p9100_t *p9100)
{
    svga_t *svga = &p9100->svga;

    if (!(svga->seqregs[0x06] & 0x40))
        return;

    while (p9100->gha_exec != svga->gdcreg[0x0a]) {
        uint32_t base = (svga->gdcreg[0x09] << 10) | (p9100->gha_exec << 2);
        uint8_t  ins[16];

        for (int w = 0; w < 4; w++) {
            uint32_t lw = p9100_gha_read(p9100, base + w);

            for (int b = 0; b < 4; b++)
                ins[(w << 2) | b] = lw >> (b << 3);
        }

        p9100_log("P9100 GHA %02X mode %i: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  p9100->gha_exec, p9100_gha_mode(p9100),
                  ins[0], ins[1], ins[2], ins[3], ins[4], ins[5], ins[6], ins[7],
                  ins[8], ins[9], ins[10], ins[11], ins[12], ins[13], ins[14], ins[15]);

        switch (ins[0] >> 5) {
            case 0: /* SBLT */
            case 1: /* LBLT */
            case 2: /* SMOV */
            case 3: /* LMOV */
                p9100_gha_blit(p9100, ins);
                break;
            case 4: /* FLINE */
            case 5: /* SLINE */
                p9100_gha_line(p9100, ins);
                break;
            default:
                /* BLKMOV moves through the VRAM serial port, not emulated. */
                break;
        }

        p9100->gha_exec++;
    }
}

static int
p9100_ext_locked(p9100_t *p9100)
{
    return !!(p9100->svga.seqregs[0x11] & MISC_CRL);
}

static void
p9100_recalcbanking(p9100_t *p9100)
{
    svga_t *svga = &p9100->svga;

    if (svga->seqregs[0x11] & MISC_BSD) {
        svga->read_bank  = 0;
        svga->write_bank = 0;
    } else {
        svga->read_bank  = (p9100->bank >> 4) << 16;
        svga->write_bank = (p9100->bank & 0x0f) << 16;
    }
}

/* The CKSEL[1:0] pins come from the miscellaneous output register in VGA
   mode and from CONFIG[66] in native mode. On the Papilio they are the serial
   clock (0) and data (1) of the ICS9161A, whose selected output is the RGB525
   reference clock, and they also drive the RGB525 FS[1:0] inputs. */
static void
p9100_cksel_update(p9100_t *p9100)
{
    svga_t *svga = &p9100->svga;
    uint8_t cksel;
    float   ref;

    if (p9100->config65 & CFG65_MODESELECT)
        cksel = (svga->miscout >> 2) & 0x03;
    else
        cksel = (p9100->config66 >> 2) & 0x03;

    if (!p9100->clock_chip || (cksel == p9100->cksel))
        return;

    p9100->cksel = cksel;
    ics9161_write(p9100->clock_chip, cksel);

    ref = ics9161_getclock(cksel, p9100->clock_chip);
    if (ref >= 1000000.0f)
        ibm_rgb528_ramdac_set_ref_clock(svga->ramdac, svga, ref);
}

static void
p9100_out(uint16_t addr, uint8_t val, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;
    svga_t  *svga  = &p9100->svga;
    uint8_t  old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c0:
        case 0x3c1:
            if (svga->attrff && (svga->attraddr == 0x15)) {
                svga->attrregs[0x15] = val;
                svga->attrff ^= 1;
                return;
            }
            break;

        case 0x3c4:
            svga->seqaddr = val & 0x17;
            return;
        case 0x3c5:
            if (svga->seqaddr != 0x11)
                p9100->unlock_writes = 0;
            switch (svga->seqaddr) {
                case 0x05:
                case 0x06:
                    if (p9100_ext_locked(p9100))
                        return;
                    old                          = svga->seqregs[svga->seqaddr];
                    svga->seqregs[svga->seqaddr] = val;
                    p9100_log("P9100 SEQ%02X = %02X\n", svga->seqaddr, val);
                    if (svga->seqaddr == 0x06) {
                        /* Clearing the GHA enable halts it and resets the execution pointer. */
                        if (!(val & 0x40))
                            p9100->gha_exec = 0;
                        else
                            p9100_gha_run(p9100);
                    }
                    if (old != val)
                        svga_recalctimings(svga);
                    return;
                case 0x07:
                    return;
                case 0x10:
                    /* W5286 I/O base on write, fixed at 3xx here. */
                    if (!p9100_ext_locked(p9100))
                        svga->seqregs[0x10] = val;
                    return;
                case 0x11:
                    /* Extended register unlock: two writes followed by the real one,
                       see section 12.1.1 of the Power 9100 data book. */
                    if (p9100_ext_locked(p9100) && (p9100->unlock_writes < 2)) {
                        p9100->unlock_writes++;
                        return;
                    }
                    p9100->unlock_writes = 0;
                    svga->seqregs[0x11]  = (val & 0xfc) | 0x01;
                    p9100_log("P9100 SEQ11 = %02X\n", svga->seqregs[0x11]);
                    p9100_recalcbanking(p9100);
                    p9100_recalcmapping(p9100);
                    return;
                case 0x12:
                case 0x13:
                    if (p9100_ext_locked(p9100))
                        return;
                    svga->seqregs[svga->seqaddr] = val;
                    p9100_log("P9100 SEQ%02X = %02X\n", svga->seqaddr, val);
                    return;
                default:
                    if (svga->seqaddr > 0x04)
                        return;
                    break;
            }
            break;

        case 0x3c2:
            svga_out(addr, val, svga);
            p9100_cksel_update(p9100);
            return;

        case 0x3c6:
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            ibm_rgb528_ramdac_out(addr, 0, val, svga->ramdac, svga);
            return;

        case 0x3cd:
            p9100->bank = val;
            p9100_recalcbanking(p9100);
            return;

        case 0x3cf:
            switch (svga->gdcaddr) {
                case 0x05:
                case 0x06:
                    svga_out(addr, val, svga);
                    p9100_recalcmapping(p9100);
                    return;
                case 0x0c:
                    if (p9100_ext_locked(p9100))
                        return;
                    old                = svga->gdcreg[0x0c];
                    svga->gdcreg[0x0c] = val;
                    p9100_log("P9100 GDC0C = %02X\n", val);
                    svga->packed_chain4 = !!(val & 0x04);
                    svga->fast          = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1]) && svga->chain4 && svga->packed_chain4;
                    if (old != val)
                        svga_recalctimings(svga);
                    return;
                default:
                    if (svga->gdcaddr > 0x0f)
                        return;
                    if (svga->gdcaddr > 0x08) {
                        svga->gdcreg[svga->gdcaddr] = val;
                        if (svga->gdcaddr == 0x0a)
                            p9100_gha_run(p9100);
                        else if (svga->gdcaddr == 0x0b)
                            p9100->gha_exec = val;
                        return;
                    }
                    break;
            }
            break;

        case 0x3d4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3d5:
            if (svga->crtcreg >= 0x19) {
                if ((svga->crtcreg <= 0x1d) && !p9100_ext_locked(p9100)) {
                    old                       = svga->crtc[svga->crtcreg];
                    svga->crtc[svga->crtcreg] = val;
                    p9100_log("P9100 CRTC%02X = %02X\n", svga->crtcreg, val);
                    if (old != val)
                        svga_recalctimings(svga);
                }
                return;
            }
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;
            if (old != val) {
                if ((svga->crtcreg < 0x0e) || (svga->crtcreg > 0x10)) {
                    if ((svga->crtcreg == 0x0c) || (svga->crtcreg == 0x0d)) {
                        svga->fullchange    = 3;
                        svga->memaddr_latch = ((svga->crtc[0x0c] << 8) | svga->crtc[0x0d]) + ((svga->crtc[8] & 0x60) >> 5);
                    } else {
                        svga->fullchange = svga->monitor->mon_changeframecount;
                        svga_recalctimings(svga);
                    }
                }
            }
            return;

        default:
            break;
    }

    svga_out(addr, val, svga);
}

static uint8_t
p9100_in(uint16_t addr, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;
    svga_t  *svga  = &p9100->svga;
    uint8_t  ret;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c1:
            if (svga->attraddr == 0x15)
                return svga->attrregs[0x15];
            break;

        case 0x3c4:
            return svga->seqaddr;
        case 0x3c5:
            switch (svga->seqaddr) {
                case 0x05:
                case 0x06:
                case 0x12:
                case 0x13:
                    if (p9100_ext_locked(p9100))
                        return 0xff;
                    return svga->seqregs[svga->seqaddr];
                case 0x07:
                    /* Device ID 010 = Power 9100, revision 0. */
                    return 0x40;
                case 0x10:
                    if (p9100_ext_locked(p9100))
                        return 0xff;
                    return 0x50;
                case 0x11:
                    return svga->seqregs[0x11];
                default:
                    if (svga->seqaddr > 0x04)
                        return 0xff;
                    break;
            }
            break;

        case 0x3c6:
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            return ibm_rgb528_ramdac_in(addr, 0, svga->ramdac, svga);

        case 0x3cd:
            return p9100->bank;

        case 0x3cf:
            if (svga->gdcaddr == 0x0c) {
                if (p9100_ext_locked(p9100))
                    return 0xff;
                return svga->gdcreg[0x0c];
            }
            if (svga->gdcaddr == 0x0b)
                return p9100->gha_exec;
            if ((svga->gdcaddr > 0x08) && (svga->gdcaddr <= 0x0f))
                return svga->gdcreg[svga->gdcaddr];
            if (svga->gdcaddr > 0x0f)
                return 0xff;
            break;

        case 0x3d4:
            return svga->crtcreg;
        case 0x3d5:
            switch (svga->crtcreg) {
                case 0x19 ... 0x1d:
                    return svga->crtc[svga->crtcreg];
                case 0x24:
                    return svga->attrff ? 0x80 : 0x00;
                default:
                    if (svga->crtcreg > 0x18)
                        return 0xff;
                    return svga->crtc[svga->crtcreg];
            }

        default:
            break;
    }

    ret = svga_in(addr, svga);
    return ret;
}

/* Native 8 bpp scanout. The native CRTC feeds frame buffer bytes straight to
   the RAMDAC, so unlike svga_render_8bpp_highres nothing from the VGA
   attribute controller (blink, colour plane enable) or sequencer applies. */
static void
p9100_render_8bpp(svga_t *svga)
{
    uint32_t *p;

    if (((svga->displine + svga->y_add) < 0) ||
        (svga->monitor->target_buffer == NULL) ||
        (svga->monitor->target_buffer->line[svga->displine + svga->y_add] == NULL))
        return;

    if (!svga->changedvram[svga->memaddr >> 12] && !svga->changedvram[(svga->memaddr >> 12) + 1] && !svga->fullchange)
        return;

    p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    for (int x = 0; x < svga->hdisp; x++) {
        p[x]          = svga->map8[svga->vram[svga->memaddr] & svga->dac_mask];
        svga->memaddr = (svga->memaddr + 1) & svga->vram_display_mask;
    }
}

/* Native video timing. The CRTC counts CRTC clocks, which the RAMDAC
   divides down from the pixel clock so that one CRTC clock carries one 64-bit
   frame buffer word; the display is blanked outside br to bf. */
static void
p9100_native_timings(p9100_t *p9100)
{
    svga_t         *svga   = &p9100->svga;
    const uint32_t *v      = p9100->video_regs;
    int             bpp    = p9100_bytes_pp(p9100);
    int             htotal = (v[0x02] & 0xfff) + 1;
    int             hdisp  = (v[0x05] & 0xfff) - (v[0x04] & 0xfff);
    int             vtotal = v[0x08] & 0xfff;
    int             vdisp  = (v[0x0b] & 0xfff) - (v[0x0a] & 0xfff);
    double          freq   = 0.0;

    if (svga->getclock)
        /* CKSEL picks the RAMDAC frequency register; selects 0 and 1 would
           be taken for the fixed VGA clocks, so keep them above 3. */
        freq = svga->getclock(((p9100->config66 >> 2) & 0x03) | 0x04, svga->clock_gen);
    if (freq < 1000000.0)
        freq = 25175000.0;
    if (p9100->type == P9100_PAPILIO) {
        /* The CRTC runs off the RGB525 SCLK, one clock per 64-bit word of
           the pixel port, whatever DDOT DIV the driver leaves set. */
        freq = ibm_rgb528_getclock_pll(((p9100->config66 >> 2) & 0x03) | 0x04, svga->clock_gen);
        if (freq < 1000000.0)
            freq = 25175000.0;
        freq = (freq * bpp) / 8.0;
    } else if (bpp == 3) {
        /* A 64-bit word holds 8/3 pixels at 24 bpp. The drivers set the RGB525
           B24P DDOT bit there, which puts SCLK (three clocks per eight pixels)
           on DDOTCLK; the RAMDAC code does not model that and returns the
           PLL output over DDOT DIV, which the drivers leave at /8. */
        freq *= 3.0;
    }

    svga->hoverride      = 1;
    svga->clock          = (cpuclock * (double) (1ULL << 32)) / freq;
    svga->char_width     = 1;
    svga->dots_per_clock = 1;
    svga->htotal         = htotal;
    svga->hdisp_time     = hdisp;
    svga->hdisp          = (hdisp * 8) / bpp;
    svga->vtotal         = vtotal;
    svga->dispend        = vdisp;
    svga->vblankstart    = vdisp;
    svga->vsyncstart     = vdisp;
    svga->split          = 0xffffff;

    svga->rowoffset         = p9100_pitch(p9100);
    svga->adv_flags        |= FLAG_NO_SHIFT3;
    /* The native CRTC scans the frame buffer linearly, whatever byte, word
       or doubleword addressing the VGA CRTC is left in. */
    svga->fb_only           = 1;
    svga->memaddr_latch     = 0;
    svga->rowcount          = 0;
    svga->linedbl           = 0;
    svga->lowres            = 0;
    svga->interlace         = 0;
    svga->vram_display_mask = p9100->vram_mask;

    svga->monitor->mon_overscan_x = 16;
    svga->monitor->mon_overscan_y = 16;

    p9100_log("P9100 native %ix%i, %i bytes per pixel, pitch %i, total %ix%i, %f MHz\n",
              svga->hdisp, vdisp, bpp, svga->rowoffset, htotal, vtotal, freq / 1000000.0);

    if (!(v[0x0e] & 0x20) || (hdisp <= 0) || (vdisp <= 0) || (vtotal <= vdisp)) {
        svga->render = svga_render_blank;
        if ((hdisp <= 0) || (vdisp <= 0) || (vtotal <= vdisp)) {
            /* Not programmed yet, keep the timer sane. */
            svga->hdisp      = 640;
            svga->hdisp_time = 80;
            svga->htotal     = 100;
            svga->dispend    = svga->vblankstart = svga->vsyncstart = 480;
            svga->vtotal     = 525;
        }
        return;
    }

    switch (bpp) {
        default:
            svga->bpp    = 8;
            svga->map8   = svga->pallook;
            svga->render = p9100_render_8bpp;
            break;
        case 2:
            /* 5-5-5 or 5-6-5 is up to the RAMDAC. */
            if (svga->bpp != 15)
                svga->bpp = 16;
            svga->render = (svga->bpp == 15) ? svga_render_15bpp_highres : svga_render_16bpp_highres;
            break;
        case 3:
            svga->bpp    = 24;
            svga->render = svga_render_24bpp_highres;
            break;
        case 4:
            svga->bpp    = 32;
            svga->render = svga_render_32bpp_highres;
            break;
    }

    /* The frame buffer feeds the RGB525 pixel port, so its own formats,
       palette and gamma apply whenever they match the native pixel size. */
    ibm_rgb528_ramdac_set_render(svga->ramdac, svga, bpp);
}

static void
p9100_recalctimings(svga_t *svga)
{
    p9100_t *p9100   = (p9100_t *) svga->priv;
    uint8_t  epfc    = svga->gdcreg[0x0c] & 0x03;
    int      clk_sel = (svga->miscout >> 2) & 0x03;

    if (!(p9100->config65 & CFG65_MODESELECT)) {
        p9100_native_timings(p9100);
        return;
    }

    svga->hoverride  = 0;
    svga->fb_only    = 0;
    svga->adv_flags &= ~FLAG_NO_SHIFT3;

    if (svga->getclock)
        svga->clock = (cpuclock * (double) (1ULL << 32)) / svga->getclock(clk_sel, svga->clock_gen);

    /* Serial video address wrap control: wrap at 128K instead of 64K of
       32-bit words, opening up the whole 512K for display. */
    if (svga->crtc[0x08] & 0x80)
        svga->vram_display_mask = p9100->vram_mask;
    else
        svga->vram_display_mask = 0x3ffff;

    svga->interlace = !!(svga->seqregs[0x05] & 0x08);

    if (svga->seqregs[0x05] & 0x80) {
        /* Dual start address, the graphics plane comes from the serial port. */
        svga->memaddr_latch = (svga->crtc[0x1b] | (svga->crtc[0x1a] << 8) | ((svga->seqregs[0x05] & 0x04) << 14));
        svga->rowoffset     = svga->crtc[0x1c];
    }

    if (!svga->scrblank && svga->attr_palette_enable && (svga->gdcreg[6] & 0x01)) {
        switch (epfc) {
            case 0x01: /* 8-bit packed, one pixel per dot clock whatever AR10 bit 6 says */
                svga->bpp    = 8;
                svga->lowres = 0;
                svga->render = svga_render_8bpp_highres;
                break;
            case 0x03: /* 16-bit packed, two bytes per pixel clock */
                svga->hdisp >>= 1;
                svga->dots_per_clock >>= 1;
                svga->bpp    = 16;
                svga->lowres = 0;
                svga->render = svga_render_16bpp_highres;
                break;
            default:
                break;
        }
    }

    if (svga->ramdac)
        ibm_rgb528_recalctimings(svga->ramdac, svga);
}

/* Native coprocessor register space. */
static uint32_t
p9100_swap(uint32_t val, int h, int b, int bit)
{
    if (h)
        val = (val >> 16) | (val << 16);
    if (b)
        val = ((val >> 8) & 0x00ff00ff) | ((val << 8) & 0xff00ff00);
    if (bit) {
        val = ((val >> 1) & 0x55555555) | ((val << 1) & 0xaaaaaaaa);
        val = ((val >> 2) & 0x33333333) | ((val << 2) & 0xcccccccc);
        val = ((val >> 4) & 0x0f0f0f0f) | ((val << 4) & 0xf0f0f0f0);
    }
    return val;
}

static void
p9100_update_irq(p9100_t *p9100)
{
    /* Master enable, then the vblank, pick and idle enables. */
    if ((p9100->interrupt_en & 0x40) && (p9100->interrupt & p9100->interrupt_en & 0x15))
        pci_set_irq(p9100->pci_slot, PCI_INTA, &p9100->irq_state);
    else
        pci_clear_irq(p9100->pci_slot, PCI_INTA, &p9100->irq_state);
}

/* Registers whose fields are preceded by a write control bit: a field only
   takes the new value when its control bit is set, and the control bits
   read back as ones. */
static uint32_t
p9100_field_write(uint32_t old, uint32_t val, int fields)
{
    for (int f = 0; f < fields; f++) {
        if (val & (2 << (f << 1)))
            old = (old & ~(1 << (f << 1))) | (val & (1 << (f << 1)));
    }
    return old;
}

/* Power 9100 drawing engine.

   The engine works on big-endian doublewords: byte k of a register value
   (k = 0 being bits 31-24) belongs to byte address 4n + k. The svga VRAM
   array holds the bytes in address order, which is the order the display
   scans them out in. Quad and pixel1 coordinates are in pixels, blit and
   pixel8 ones in bytes. */
typedef struct p9100_draw_t {
    uint32_t pitch;
    int      bpp;
    int      xmin;
    int      ymin;
    int      xmax;
    int      ymax;
} p9100_draw_t;

static int
p9100_bytes_pp(const p9100_t *p9100)
{
    switch ((p9100->sysconfig >> 26) & 7) {
        case 3:
            return 2;
        case 5:
            return 4;
        case 7:
            return 3;
        default:
            return 1;
    }
}

/* The shift control fields add up to the scan line length in bytes. */
static uint32_t
p9100_pitch(const p9100_t *p9100)
{
    uint32_t s     = p9100->sysconfig;
    uint32_t pitch = 0;

    for (int i = 0; i < 3; i++) {
        int shift = (s >> (20 - (i * 3))) & 7;

        if (shift)
            pitch += 1 << (shift + 4);
    }
    if ((s >> 29) & 3)
        pitch += 512 << ((s >> 29) & 3);

    return pitch;
}

static void
p9100_draw_setup(const p9100_t *p9100, p9100_draw_t *d, int bytes)
{
    uint32_t wmin = bytes ? p9100->b_w_min : p9100->p_w_min;
    uint32_t wmax = bytes ? p9100->b_w_max : p9100->p_w_max;

    d->pitch = p9100_pitch(p9100);
    d->bpp   = p9100_bytes_pp(p9100);
    /* Nothing is drawn in a negative quadrant. */
    d->xmin = (wmin >> 16) & 0x1fff;
    d->ymin = wmin & 0x1fff;
    d->xmax = (wmax >> 16) & 0x1fff;
    d->ymax = wmax & 0x1fff;
}

static int
p9100_clipped(const p9100_draw_t *d, int x, int y)
{
    return (x < d->xmin) || (x > d->xmax) || (y < d->ymin) || (y > d->ymax);
}

/* Byte k of the value, for a byte in a pixel or the plane mask. */
static uint8_t
p9100_reg_byte(const p9100_draw_t *d, uint32_t val, uint32_t addr, int i)
{
    int k = (d->bpp == 3) ? i : ((addr + i) & 3);

    return val >> (24 - (k << 3));
}

/* Minterm bit (pattern << 2 | source << 1 | destination) gives the result,
   the same encoding as the Windows ternary raster operations. */
static uint8_t
p9100_rop(uint8_t m, uint8_t p, uint8_t s, uint8_t d)
{
    uint8_t r = 0;

    if (m & 0x01)
        r |= ~p & ~s & ~d;
    if (m & 0x02)
        r |= ~p & ~s & d;
    if (m & 0x04)
        r |= ~p & s & ~d;
    if (m & 0x08)
        r |= ~p & s & d;
    if (m & 0x10)
        r |= p & ~s & ~d;
    if (m & 0x20)
        r |= p & ~s & d;
    if (m & 0x40)
        r |= p & s & ~d;
    if (m & 0x80)
        r |= p & s & d;
    return r;
}

static void
p9100_write_byte(p9100_t *p9100, uint32_t addr, uint8_t p, uint8_t s, uint8_t pm)
{
    svga_t *svga = &p9100->svga;
    uint8_t d;

    /* The address bits above the fitted memory are ignored, as for the
       blit source. */
    addr &= p9100->vram_mask;

    d                             = svga->vram[addr];
    svga->vram[addr]              = (p9100_rop(p9100->raster, p, s, d) & pm) | (d & ~pm);
    svga->changedvram[addr >> 12] = svga->monitor->mon_changeframecount;
}

/* Pattern colour at pixel (x, y), 0 if a transparent pattern leaves the
   pixel alone. Pattern register n holds row 2n in bits 23-16 and row
   2n + 1 in bits 7-0, the leftmost pixel in bit 7; plane 1 of a 4 colour
   pattern is in the byte above each. */
static int
p9100_pattern(const p9100_t *p9100, int x, int y, uint32_t *col)
{
    uint32_t reg;
    int      px;
    int      py;
    int      sh;
    int      idx;

    if (!(p9100->raster & RASTER_PAT_ENABLE)) {
        *col = p9100->color[0];
        return 1;
    }

    px  = (x - p9100->pat_originx) & 7;
    py  = (y - p9100->pat_originy) & 7;
    reg = p9100->pattern[py >> 1];
    sh  = (py & 1) ? 0 : 16;
    idx = !!((reg >> sh) & (0x80 >> px));

    if (p9100->raster & RASTER_PAT_4COLOR)
        idx |= ((reg >> (sh + 8)) & (0x80 >> px)) ? 2 : 0;
    else if (!idx && (p9100->raster & RASTER_TRANSPARENT))
        return 0;

    *col = p9100->color[idx];
    return 1;
}

static int
p9100_uses_pattern(const p9100_t *p9100)
{
    return ((p9100->raster >> 4) & 0x0f) != (p9100->raster & 0x0f);
}

/* One pixel of a quad or pixel1 operation, the source being a colour. */
static void
p9100_pixel(p9100_t *p9100, const p9100_draw_t *d, int x, int y, uint32_t src)
{
    uint32_t addr = (y * d->pitch) + (x * d->bpp);
    uint32_t pat;

    if (!p9100_pattern(p9100, x, y, &pat))
        return;

    for (int i = 0; i < d->bpp; i++)
        p9100_write_byte(p9100, addr + i, p9100_reg_byte(d, pat, addr, i),
                         p9100_reg_byte(d, src, addr, i), p9100_reg_byte(d, p9100->pmask, addr, i));
}

#define P9100_MAX_LINES 8192

/* Quads are filled scan line by scan line. In X11 mode a pixel is touched
   when its centre is inside, or on a left or top edge; oversized mode also
   touches the Bresenham outline, which is what makes points and lines
   visible. The spans are gathered first so that no pixel is drawn twice. */
static void
p9100_quad_outline(int *lo, int *hi, int ymin, int ymax, int x0, int y0, int x1, int y1)
{
    int dx  = abs(x1 - x0);
    int dy  = -abs(y1 - y0);
    int sx  = (x0 < x1) ? 1 : -1;
    int sy  = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        if ((y0 >= ymin) && (y0 <= ymax)) {
            if (x0 < lo[y0 - ymin])
                lo[y0 - ymin] = x0;
            if (x0 > hi[y0 - ymin])
                hi[y0 - ymin] = x0;
        }
        if ((x0 == x1) && (y0 == y1))
            break;
        if ((2 * err) >= dy) {
            err += dy;
            x0 += sx;
        }
        if ((2 * err) <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* The four vertices the quad command works on, from the vertex type of the
   last load coordinates. */
static void
p9100_quad_verts(const p9100_t *p9100, int *vx, int *vy)
{
    switch (p9100->prim) {
        case PRIM_POINT:
            for (int i = 0; i < 4; i++) {
                vx[i] = p9100->x[0];
                vy[i] = p9100->y[0];
            }
            break;
        case PRIM_LINE:
            vx[0] = vx[3] = p9100->x[0];
            vy[0] = vy[3] = p9100->y[0];
            vx[1] = vx[2] = p9100->x[1];
            vy[1] = vy[2] = p9100->y[1];
            break;
        case PRIM_TRI:
            for (int i = 0; i < 3; i++) {
                vx[i] = p9100->x[i];
                vy[i] = p9100->y[i];
            }
            vx[3] = p9100->x[2];
            vy[3] = p9100->y[2];
            break;
        case PRIM_RECT:
            /* Two opposite corners. */
            vx[0] = vx[3] = p9100->x[0];
            vy[0] = vy[1] = p9100->y[0];
            vx[1] = vx[2] = p9100->x[1];
            vy[2] = vy[3] = p9100->y[1];
            break;
        default:
            for (int i = 0; i < 4; i++) {
                vx[i] = p9100->x[i];
                vy[i] = p9100->y[i];
            }
            break;
    }
}

/* Clip status of the vertices against the pixel window: exactly one of
   intersects, visible and hidden is set. The driver waits for it. */
static uint32_t
p9100_clip_status(const p9100_t *p9100)
{
    p9100_draw_t d;
    int          vx[4];
    int          vy[4];
    int          inside = 0;
    int          out[4] = { 0, 0, 0, 0 };

    p9100_draw_setup(p9100, &d, 0);
    p9100_quad_verts(p9100, vx, vy);

    for (int i = 0; i < 4; i++) {
        out[0] += (vx[i] < d.xmin);
        out[1] += (vx[i] > d.xmax);
        out[2] += (vy[i] < d.ymin);
        out[3] += (vy[i] > d.ymax);
        inside += !p9100_clipped(&d, vx[i], vy[i]);
    }

    if ((out[0] == 4) || (out[1] == 4) || (out[2] == 4) || (out[3] == 4))
        return STATUS_QUAD_HIDDEN;
    if (inside == 4)
        return STATUS_QUAD_VISIBLE;
    return STATUS_QUAD_INTERSECTS;
}

static void
p9100_cmd_quad(p9100_t *p9100)
{
    static int   lo[P9100_MAX_LINES];
    static int   hi[P9100_MAX_LINES];
    p9100_draw_t d;
    int          vx[4];
    int          vy[4];
    int          ymin;
    int          ymax;

    p9100_quad_verts(p9100, vx, vy);

    p9100_log("P9100 quad (%i,%i) (%i,%i) (%i,%i) (%i,%i) raster %05X col %08X %08X\n",
              vx[0], vy[0], vx[1], vy[1], vx[2], vy[2], vx[3], vy[3],
              p9100->raster, p9100->color[0], p9100->color[1]);

    /* Out of range coordinates make the quad fail, to be done in software. */
    for (int i = 0; i < 4; i++) {
        if ((vx[i] < -0x8000) || (vx[i] > 0x7fff) || (vy[i] < -0x8000) || (vy[i] > 0x7fff)) {
            p9100->status |= STATUS_QUAD_SW;
            return;
        }
    }

    p9100_draw_setup(p9100, &d, 0);
    ymin = ymax = vy[0];
    for (int i = 1; i < 4; i++) {
        if (vy[i] < ymin)
            ymin = vy[i];
        if (vy[i] > ymax)
            ymax = vy[i];
    }
    if (ymin < d.ymin)
        ymin = d.ymin;
    if (ymax > d.ymax)
        ymax = d.ymax;
    if (ymax >= (ymin + P9100_MAX_LINES))
        ymax = ymin + P9100_MAX_LINES - 1;
    if (ymin > ymax)
        return;

    for (int y = ymin; y <= ymax; y++) {
        double yc = y + 0.5;
        double xl = 0.0;
        double xr = 0.0;
        int    n  = 0;

        lo[y - ymin] = INT32_MAX;
        hi[y - ymin] = INT32_MIN;

        for (int i = 0; i < 4; i++) {
            int    a = i;
            int    b = (i + 1) & 3;
            double xi;

            if ((yc >= vy[a]) == (yc >= vy[b]))
                continue;
            xi = vx[a] + ((yc - vy[a]) * (vx[b] - vx[a])) / (double) (vy[b] - vy[a]);
            if (!n || (xi < xl))
                xl = xi;
            if (!n || (xi > xr))
                xr = xi;
            n++;
        }

        if (n >= 2) {
            int xs = (int) ceil(xl - 0.5);
            int xe = (int) ceil(xr - 0.5) - 1;

            if (xs <= xe) {
                lo[y - ymin] = xs;
                hi[y - ymin] = xe;
            }
        }
        if (n > 2)
            p9100->status |= STATUS_QUAD_SW;
    }

    if (p9100->raster & RASTER_OVERSIZED) {
        for (int i = 0; i < 4; i++)
            p9100_quad_outline(lo, hi, ymin, ymax, vx[i], vy[i], vx[(i + 1) & 3], vy[(i + 1) & 3]);
    }

    for (int y = ymin; y <= ymax; y++) {
        int xs = lo[y - ymin];
        int xe = hi[y - ymin];

        if (xs < d.xmin)
            xs = d.xmin;
        if (xe > d.xmax)
            xe = d.xmax;
        for (int x = xs; x <= xe; x++)
            p9100_pixel(p9100, &d, x, y, p9100->color[1]);
    }
}

/* Copies (x[0],y[0])-(x[1],y[1]) to (x[2],y[2])-(x[3],y[3]), as though the
   source were moved off screen first. */
static void
p9100_cmd_blit(p9100_t *p9100)
{
    p9100_draw_t d;
    int          sx  = MIN(p9100->x[0], p9100->x[1]);
    int          sy  = MIN(p9100->y[0], p9100->y[1]);
    int          dx  = MIN(p9100->x[2], p9100->x[3]);
    int          dy  = MIN(p9100->y[2], p9100->y[3]);
    int          w   = abs(p9100->x[1] - p9100->x[0]) + 1;
    int          h   = abs(p9100->y[1] - p9100->y[0]) + 1;
    int          pat = p9100_uses_pattern(p9100);
    int          xdir;
    int          ydir;
    int          rmin;
    int          rmax;
    int          cmin;
    int          cmax;

    p9100_log("P9100 blit (%i,%i) %ix%i -> (%i,%i) raster %05X\n", sx, sy, w, h, dx, dy, p9100->raster);

    if ((w != (abs(p9100->x[3] - p9100->x[2]) + 1)) || (h != (abs(p9100->y[3] - p9100->y[2]) + 1)))
        p9100_log("P9100 blit: source and destination sizes differ\n");

    p9100_draw_setup(p9100, &d, 1);
    ydir = (dy > sy) ? -1 : 1;
    xdir = ((dy == sy) && (dx > sx)) ? -1 : 1;

    /* Only walk the part of the destination inside the window. */
    rmin = MAX(0, d.ymin - dy);
    rmax = MIN(h - 1, d.ymax - dy);
    cmin = MAX(0, d.xmin - dx);
    cmax = MIN(w - 1, d.xmax - dx);

    for (int r = rmin; r <= rmax; r++) {
        int row = (ydir > 0) ? r : (rmax + rmin - r);
        int ty  = dy + row;

        for (int c = cmin; c <= cmax; c++) {
            int      col  = (xdir > 0) ? c : (cmax + cmin - c);
            int      tx   = dx + col;
            uint32_t src  = (((sy + row) * d.pitch) + sx + col) & p9100->vram_mask;
            uint32_t addr = (ty * d.pitch) + tx;
            uint32_t pcol = p9100->color[0];
            int      i    = tx % d.bpp;

            if ((tx < d.xmin) || (tx > d.xmax))
                continue;
            if (pat && !p9100_pattern(p9100, tx / d.bpp, ty, &pcol))
                continue;

            p9100_write_byte(p9100, addr, p9100_reg_byte(&d, pcol, addr - i, i), p9100->svga.vram[src],
                             p9100_reg_byte(&d, p9100->pmask, addr - i, i));
        }
    }
}

/* Each pixel8 stores up to four bytes from x[1],y[1] on; reaching the
   right edge x[2] drops the rest of the word, which is scan line padding,
   and moves to x[0] on the line y[3] further on. */
static void
p9100_cmd_pixel8(p9100_t *p9100, uint32_t val)
{
    p9100_draw_t d;

    p9100_draw_setup(p9100, &d, 1);

    for (int b = 0; b < 4; b++) {
        int      x    = p9100->x[1];
        int      y    = p9100->y[1];
        uint32_t addr = (y * d.pitch) + x;
        uint32_t pcol = p9100->color[0];
        int      i    = (x >= 0) ? (x % d.bpp) : 0;

        if (!p9100_clipped(&d, x, y) && p9100_pattern(p9100, x / d.bpp, y, &pcol))
            p9100_write_byte(p9100, addr, p9100_reg_byte(&d, pcol, addr - i, i), val >> (24 - (b << 3)),
                             p9100_reg_byte(&d, p9100->pmask, addr - i, i));

        p9100->x[1]++;
        if (p9100->x[1] >= p9100->x[2]) {
            p9100->x[1] = p9100->x[0];
            p9100->y[1] += p9100->y[3];
            break;
        }
    }
}

/* Pixel1 expands up to 32 bits, most significant first, into color[0] and
   color[1]; unlike pixel8 it carries on to the next line with the rest.
   Pixels are clipped against the byte window as well: the DSystems driver
   clips text that way, starting the bitmap left of the clip edge. */
static void
p9100_cmd_pixel1(p9100_t *p9100, uint32_t val, int count)
{
    p9100_draw_t d;
    p9100_draw_t db;

    p9100_draw_setup(p9100, &d, 0);
    p9100_draw_setup(p9100, &db, 1);

    for (int b = 0; b < count; b++) {
        int bit = (val >> (31 - b)) & 1;
        int x   = p9100->x[1];
        int y   = p9100->y[1];

        if ((bit || !(p9100->raster & RASTER_PIX1_TRANSP)) && !p9100_clipped(&d, x, y) &&
            !p9100_clipped(&db, x * d.bpp, y) && !p9100_clipped(&db, (x * d.bpp) + d.bpp - 1, y))
            p9100_pixel(p9100, &d, x, y, p9100->color[bit]);

        p9100->x[1]++;
        if (p9100->x[1] >= p9100->x[2]) {
            p9100->x[1] = p9100->x[0];
            p9100->y[1] += p9100->y[3];
        }
    }
}

/* The next block starts at the right edge of the previous one. */
static void
p9100_cmd_next_pixels(p9100_t *p9100, uint32_t val)
{
    p9100->x[0] = p9100->x[2];
    p9100->x[1] = p9100->x[2];
    p9100->y[1] = p9100->y[2];
    p9100->x[2] += (int32_t) val;
}

static void
p9100_engine_done(p9100_t *p9100)
{
    p9100->interrupt |= 0x01;
    p9100_update_irq(p9100);
}

static void
p9100_coord_write(p9100_t *p9100, int idx, int yx, uint32_t val, int32_t ox, int32_t oy)
{
    switch (yx) {
        case 1:
            p9100->x[idx] = (int32_t) val + ox;
            break;
        case 2:
            p9100->y[idx] = (int32_t) val + oy;
            break;
        case 3:
            p9100->x[idx] = (int16_t) (val >> 16) + ox;
            p9100->y[idx] = (int16_t) (val & 0xffff) + oy;
            break;
        default:
            break;
    }
}

static void
p9100_load_coord(p9100_t *p9100, uint32_t addr, uint32_t val)
{
    static const int verts[8] = { 1, 2, 3, 4, 2, 4, 4, 4 };
    int              type     = (addr >> 6) & 7;
    int              n        = verts[type];
    int              idx      = p9100->cindex % n;
    int              yx       = (addr >> 3) & 3;
    int32_t          ox;
    int32_t          oy;

    if (addr & 0x20) {
        /* Relative to the previous vertex. */
        int prev = (idx + n - 1) % n;

        ox = p9100->x[prev];
        oy = p9100->y[prev];
    } else {
        ox = (int16_t) (p9100->w_off >> 16);
        oy = (int16_t) (p9100->w_off & 0xffff);
    }

    p9100_coord_write(p9100, idx, yx, val, ox, oy);
    p9100->prim = (type <= PRIM_RECT) ? type : PRIM_QUAD;

    /* A vertex is complete once its y has been loaded. */
    if (yx & 2)
        p9100->cindex = (idx + 1) % n;
}

static uint32_t
p9100_window_reg(int32_t x, int32_t y)
{
    return ((x & 0x1fff) << 16) | (y & 0x1fff);
}

static uint32_t
p9100_clip_reg(const int32_t *v, int min, int max)
{
    uint32_t ret = 0;

    for (int i = 0; i < 4; i++) {
        if (v[i] < min)
            ret |= 0x10 << i;
        if (v[i] > max)
            ret |= 0x01 << i;
    }
    return ret;
}

/* Vertex relationship: bit 0 v0:v1, 1 v1:v2, 2 v2:v3, 3 v3:v0, 4 v1:v3, 5 v0:v2. */
static uint32_t
p9100_edge_reg(const int32_t *v, int gt)
{
    static const int a[6] = { 0, 1, 2, 3, 1, 0 };
    static const int b[6] = { 1, 2, 3, 0, 3, 2 };
    uint32_t         ret  = 0;

    for (int i = 0; i < 6; i++) {
        if (gt ? (v[a[i]] < v[b[i]]) : (v[a[i]] > v[b[i]]))
            ret |= 1 << i;
    }
    return ret;
}

static uint32_t
p9100_param_read(p9100_t *p9100, int idx)
{
    switch (idx) {
        case 0x03:
            return p9100->cindex;
        case 0x04:
            return p9100->w_off;
        case 0x05:
            return p9100->p_w_min;
        case 0x06:
            return p9100->p_w_max;
        case 0x08:
            return p9100_clip_reg(p9100->y, p9100->p_w_min & 0x1fff, p9100->p_w_max & 0x1fff);
        case 0x09:
            return p9100_clip_reg(p9100->x, (p9100->p_w_min >> 16) & 0x1fff, (p9100->p_w_max >> 16) & 0x1fff);
        case 0x0a:
            return p9100_edge_reg(p9100->x, 0);
        case 0x0b:
            return p9100_edge_reg(p9100->x, 1);
        case 0x0c:
            return p9100_edge_reg(p9100->y, 0);
        case 0x0d:
            return p9100_edge_reg(p9100->y, 1);
        default:
            return 0;
    }
}

static uint32_t *
p9100_draw_reg(p9100_t *p9100, int idx)
{
    switch (idx) {
        case 0x00:
            return &p9100->color[0];
        case 0x01:
            return &p9100->color[1];
        case 0x02:
            return &p9100->pmask;
        case 0x03:
            return &p9100->draw_mode;
        case 0x04:
            return &p9100->pat_originx;
        case 0x05:
            return &p9100->pat_originy;
        case 0x06:
            return &p9100->raster;
        case 0x07:
            return &p9100->pixel8_reg;
        case 0x08:
            return &p9100->p_w_min;
        case 0x09:
            return &p9100->p_w_max;
        case 0x0e:
            return &p9100->color[2];
        case 0x0f:
            return &p9100->color[3];
        case 0x20 ... 0x27:
            return &p9100->pattern[idx & 7];
        case 0x28:
            return &p9100->b_w_min;
        case 0x29:
            return &p9100->b_w_max;
        default:
            return NULL;
    }
}

static uint32_t
p9100_video_read(p9100_t *p9100, int idx)
{
    svga_t *svga = &p9100->svga;

    switch (idx) {
        case 0x01: {
            /* Horizontal counter, free running in CRTC clocks and wrapping
               at hrzt. The drivers wait on it before changing the horizontal
               timing, even before hrzt has been set, so it has to move. */
            uint32_t period = (p9100->video_regs[0x02] & 0xfff) + 1;

            if (period < 2)
                period = 0x1000;
            if (svga->clock <= 0.0)
                return 0;
            return (uint32_t) fmod(((double) tsc * 4294967296.0) / svga->clock, (double) period);
        }
        case 0x07:
            /* The svga line counter starts at the end of vertical blanking. */
            if (!(p9100->config65 & CFG65_MODESELECT) && p9100->video_regs[0x08])
                return (svga->vc + p9100->video_regs[0x0a]) % (p9100->video_regs[0x08] & 0xfff);
            return svga->vc & 0xfff;
        default:
            return p9100->video_regs[idx];
    }
}

/* Native coprocessor register space. Values are in Power 9100 order, after
   any swapping the access asked for. */
static uint32_t
p9100_reg_read(p9100_t *p9100, uint32_t addr)
{
    svga_t   *svga = &p9100->svga;
    uint32_t  ret  = 0;
    uint32_t *reg;
    uint8_t   idx = (addr >> 2) & 0x1f;

    addr &= 0x7ffc;

    if (addr < 0x0080) {
        switch (idx) {
            case 0x01:
                /* Bits 2-0 are the chip version. */
                ret = (p9100->sysconfig & ~0x07) | 0x02;
                break;
            case 0x02:
                ret = p9100->interrupt | 0x2a;
                break;
            case 0x03:
                ret = p9100->interrupt_en | 0xaa;
                break;
            case 0x04:
                ret = p9100->alt_write_bank;
                break;
            case 0x05:
                ret = p9100->alt_read_bank;
                break;
            default:
                break;
        }
    } else if (addr < 0x0100) {
        ret = 0;
    } else if (addr < 0x0180) {
        ret = p9100_video_read(p9100, idx);
    } else if (addr < 0x0200) {
        if (idx == 0x06)
            ret = PU_CONFIG_DEFAULT;
        else
            ret = p9100->vram_regs[idx];
    } else if (addr < 0x0400) {
        uint8_t rs = (addr >> 2) & 0x07;

        /* RS 0-3 are the VGA DAC ports 3C8, 3C9, 3C6 and 3C7. */
        ret = ibm_rgb528_ramdac_in(0x03c6 + ((rs + 2) & 3), !!(rs & 4), svga->ramdac, svga);
        ret |= (ret << 8) | (ret << 16) | (ret << 24);
    } else if (addr < 0x2000) {
        /* No video coprocessor: a P9130 here ('VP' in the top half) makes the BIOS sign on as a Viper Pro. */
        ret = 0;
    } else if (addr == 0x2000) {
        ret = p9100->status | p9100_clip_status(p9100);
    } else if (addr == 0x2004) {
        /* Reading the command addresses starts the operation and returns the status. */
        p9100->status = 0;
        p9100_cmd_blit(p9100);
        p9100_engine_done(p9100);
        ret = p9100->status | p9100_clip_status(p9100);
    } else if (addr == 0x2008) {
        p9100->status = 0;
        p9100_cmd_quad(p9100);
        p9100_engine_done(p9100);
        ret = p9100->status | p9100_clip_status(p9100);
    } else if ((addr & 0x7f80) == 0x2180) {
        ret = p9100_param_read(p9100, idx);
    } else if ((addr & 0x7e00) == 0x2200) {
        reg = p9100_draw_reg(p9100, (addr >> 2) & 0x7f);
        if (reg)
            ret = *reg;
    } else if ((addr & 0x7f00) == 0x3000) {
        int i = (addr >> 6) & 3;

        switch ((addr >> 3) & 3) {
            case 1:
                ret = p9100->x[i];
                break;
            case 2:
                ret = p9100->y[i];
                break;
            case 3:
                ret = ((uint32_t) p9100->x[i] << 16) | (p9100->y[i] & 0xffff);
                break;
            default:
                break;
        }
    }

    p9100_log("P9100 native read  %04X = %08X\n", addr, ret);
    return ret;
}

static void
p9100_reg_write(p9100_t *p9100, uint32_t addr, uint32_t val, uint32_t mask)
{
    svga_t   *svga = &p9100->svga;
    uint32_t *reg  = NULL;
    uint8_t   idx  = (addr >> 2) & 0x1f;

    addr &= 0x7ffc;

    p9100_log("P9100 native write %04X = %08X (%08X)\n", addr, val, mask);

    if (addr & 0x4000) {
        /* Alternate pixel8 format, the offset is ignored. */
        p9100_cmd_pixel8(p9100, val);
        p9100_engine_done(p9100);
        return;
    }

    if (addr < 0x0080) {
        switch (idx) {
            case 0x01:
                reg = &p9100->sysconfig;
                break;
            case 0x02:
                p9100->interrupt = p9100_field_write(p9100->interrupt, val & mask, 3);
                p9100_update_irq(p9100);
                return;
            case 0x03:
                p9100->interrupt_en = p9100_field_write(p9100->interrupt_en, val & mask, 4);
                p9100_update_irq(p9100);
                return;
            case 0x04:
                reg = &p9100->alt_write_bank;
                break;
            case 0x05:
                reg = &p9100->alt_read_bank;
                break;
            default:
                return;
        }
        *reg = (*reg & ~mask) | (val & mask);
        if (idx == 0x01)
            svga_recalctimings(svga);
        return;
    }

    if ((addr >= 0x0100) && (addr < 0x0180)) {
        reg  = &p9100->video_regs[idx];
        *reg = (*reg & ~mask) | (val & mask);
        if (!(p9100->config65 & CFG65_MODESELECT))
            svga_recalctimings(svga);
        return;
    }
    if ((addr >= 0x0180) && (addr < 0x0200)) {
        reg  = &p9100->vram_regs[idx];
        *reg = (*reg & ~mask) | (val & mask);
        return;
    }
    if ((addr >= 0x0200) && (addr < 0x0400)) {
        /* The RAMDAC hangs off frame buffer data bits 23-16. */
        uint8_t rs = (addr >> 2) & 0x07;
        uint8_t dat;

        if (mask & 0x00ff0000)
            dat = val >> 16;
        else if (mask & 0x000000ff)
            dat = val;
        else if (mask & 0x0000ff00)
            dat = val >> 8;
        else
            dat = val >> 24;

        ibm_rgb528_ramdac_out(0x03c6 + ((rs + 2) & 3), !!(rs & 4), dat, svga->ramdac, svga);
        /* The RAMDAC does not recalculate for its PLL and clock divider registers. */
        if ((rs == 6) && !(p9100->config65 & CFG65_MODESELECT))
            svga_recalctimings(svga);
        return;
    }
    if (addr < 0x2000)
        return;

    if (addr == 0x200c) {
        p9100_cmd_pixel8(p9100, val);
        p9100_engine_done(p9100);
    } else if (addr == 0x2014) {
        p9100_cmd_next_pixels(p9100, val);
    } else if ((addr & 0x7f80) == 0x2080) {
        p9100_cmd_pixel1(p9100, val, idx + 1);
        p9100_engine_done(p9100);
    } else if ((addr & 0x7f80) == 0x2180) {
        if (idx == 0x03)
            p9100->cindex = val & 3;
        else if (idx == 0x04)
            p9100->w_off = (p9100->w_off & ~mask) | (val & mask);
    } else if ((addr & 0x7e00) == 0x2200) {
        int i = (addr >> 2) & 0x7f;

        if (i == 0x03) {
            p9100->draw_mode = p9100_field_write(p9100->draw_mode, val & mask, 2);
            return;
        }
        reg = p9100_draw_reg(p9100, i);
        if (reg)
            *reg = (*reg & ~mask) | (val & mask);
    } else if ((addr & 0x7f00) == 0x3000) {
        /* Device coordinates, window relative ones offset by w_off. */
        int32_t ox = 0;
        int32_t oy = 0;

        if (addr & 0x20) {
            ox = (int16_t) (p9100->w_off >> 16);
            oy = (int16_t) (p9100->w_off & 0xffff);
        }
        p9100_coord_write(p9100, (addr >> 6) & 3, (addr >> 3) & 3, val, ox, oy);
        p9100->prim = PRIM_QUAD;
    } else if ((addr & 0x7e00) == 0x3200)
        p9100_load_coord(p9100, addr, val);
}

static void
p9100_vblank_start(svga_t *svga)
{
    p9100_t *p9100 = (p9100_t *) svga->priv;

    p9100->interrupt |= 0x10;
    p9100_update_irq(p9100);
}

static uint32_t
p9100_bswap(uint32_t val)
{
    return p9100_swap(val, 1, 1, 0);
}

/* Host frame buffer address to VRAM offset, following the bank decode that
   mem_config.config selects. Banks of 256K VRAMs are interleaved per
   doubleword (bank in the low address bits, then 9 column and 9 row bits,
   the bits above ignored), so a configuration with more banks than are
   fitted leaves holes: the drivers size memory by programming the 4 bank
   layout and seeing which doublewords read back. Returns -1 for a bank that
   is not fitted. The VRAM array is kept in the fitted layout's order. */
static int32_t
p9100_fb_offset(const p9100_t *p9100, uint32_t addr)
{
    uint32_t cfg    = (p9100->vram_regs[0x01] & 0x07) | ((p9100->vram_regs[0x01] >> 26) & 0x08);
    int      fitted = p9100->vram_size >> 20;
    int      banks;
    uint32_t dword;
    uint32_t bank;
    uint32_t col;
    uint32_t row;

    switch (cfg) {
        case 0x4:
            banks = 1;
            break;
        case 0x5:
        case 0x6:
        case 0xd:
        case 0xe:
            banks = 2;
            break;
        case 0x7:
        case 0xf:
            banks = 4;
            break;
        default:
            /* Reset value or a 128K VRAM layout: take it as fitted. */
            banks = fitted;
            break;
    }

    /* The host and the drawing engine share the decode, and the engine
       works linearly here, so only a layout with banks that are not
       fitted needs remapping. */
    if (banks <= fitted)
        return addr & p9100->vram_mask;

    dword = addr >> 2;
    bank  = dword & (banks - 1);
    col   = (dword >> (banks >> 1)) & 0x1ff;
    row   = (dword >> ((banks >> 1) + 9)) & 0x1ff;
    if (bank >= (uint32_t) fitted)
        return -1;

    return (((row << (9 + (fitted >> 1))) | (col << (fitted >> 1)) | bank) << 2) | (addr & 3);
}

/* Native frame buffer: big-endian doublewords, with the swapping from sysconfig. */
static uint32_t
p9100_fb_readl(p9100_t *p9100, uint32_t addr)
{
    svga_t  *svga = &p9100->svga;
    int32_t  off  = p9100_fb_offset(p9100, addr & 0x7ffffc);
    uint32_t val;

    /* An empty bank leaves the bus to the power-up pull-ups. */
    if (off < 0)
        val = PU_CONFIG_DEFAULT;
    else
        val = p9100_bswap(*(uint32_t *) &svga->vram[off]);
    return p9100_swap(val, !!(p9100->sysconfig & 0x2000), !!(p9100->sysconfig & 0x1000), !!(p9100->sysconfig & 0x0800));
}

static void
p9100_fb_writel(p9100_t *p9100, uint32_t addr, uint32_t val, uint32_t mask)
{
    svga_t  *svga = &p9100->svga;
    int32_t  off  = p9100_fb_offset(p9100, addr & 0x7ffffc);
    uint32_t old;

    if (off < 0)
        return;
    val  = p9100_bswap(p9100_swap(val, !!(p9100->sysconfig & 0x2000), !!(p9100->sysconfig & 0x1000), !!(p9100->sysconfig & 0x0800)));
    mask = p9100_bswap(p9100_swap(mask, !!(p9100->sysconfig & 0x2000), !!(p9100->sysconfig & 0x1000), 0));
    old  = *(uint32_t *) &svga->vram[off];
    *(uint32_t *) &svga->vram[off] = (old & ~mask) | (val & mask);
    svga->changedvram[off >> 12]   = svga->monitor->mon_changeframecount;
}

/* Native accesses arrive here as a 32-bit lane: the offset within the
   doubleword picks the bytes, the swap flags apply to the whole lane. */
static uint32_t
p9100_native_readl(p9100_t *p9100, uint32_t addr, int indirect)
{
    int h;
    int b;
    int bit;

    if (indirect) {
        if (!(p9100->config65 & CFG65_NA_SELECT)) {
            uint32_t bank = (p9100->alt_read_bank & 0x007f0000);

            return p9100_fb_readl(p9100, bank | (addr & 0xffff));
        }
        h   = !!(p9100->config65 & 0x80);
        b   = !!(p9100->config65 & 0x40);
        bit = !!(p9100->config65 & 0x20);
    } else {
        if (addr & 0x800000)
            return p9100_fb_readl(p9100, addr & 0x7fffff);
        h   = !!(addr & 0x40000);
        b   = !!(addr & 0x20000);
        bit = !!(addr & 0x10000);
    }

    return p9100_swap(p9100_reg_read(p9100, addr), h, b, bit);
}

static void
p9100_native_writel(p9100_t *p9100, uint32_t addr, uint32_t val, uint32_t mask, int indirect)
{
    int h;
    int b;
    int bit;

    if (indirect) {
        if (!(p9100->config65 & CFG65_NA_SELECT)) {
            uint32_t bank = (p9100->alt_write_bank & 0x007f0000);

            p9100_fb_writel(p9100, bank | (addr & 0xffff), val, mask);
            return;
        }
        h   = !!(p9100->config65 & 0x80);
        b   = !!(p9100->config65 & 0x40);
        bit = !!(p9100->config65 & 0x20);
    } else {
        if (addr & 0x800000) {
            p9100_fb_writel(p9100, addr & 0x7fffff, val, mask);
            return;
        }
        h   = !!(addr & 0x40000);
        b   = !!(addr & 0x20000);
        bit = !!(addr & 0x10000);
    }

    p9100_reg_write(p9100, addr, p9100_swap(val, h, b, bit), p9100_swap(mask, h, b, 0));
}

/* The memory core hands accesses that are not aligned to their size to the
   handlers as they are (24 bpp drawing is full of them), so they are split
   at the doubleword boundary, each doubleword taking the bytes that fall in
   it. */
static uint32_t
p9100_native_read_n(p9100_t *p9100, uint32_t addr, int bytes, int indirect)
{
    int      shift = (addr & 3) << 3;
    uint32_t ret   = p9100_native_readl(p9100, addr & ~3, indirect) >> shift;

    if (((addr & 3) + bytes) > 4)
        ret |= p9100_native_readl(p9100, (addr & ~3) + 4, indirect) << (32 - shift);
    return ret;
}

static void
p9100_native_write_n(p9100_t *p9100, uint32_t addr, uint32_t val, int bytes, int indirect)
{
    int      shift = (addr & 3) << 3;
    uint32_t mask  = 0xffffffffU >> (32 - (bytes << 3));

    p9100_native_writel(p9100, addr & ~3, val << shift, mask << shift, indirect);
    if (((addr & 3) + bytes) > 4)
        p9100_native_writel(p9100, (addr & ~3) + 4, val >> (32 - shift), mask >> (32 - shift), indirect);
}

/* The PCI memory window: WBASE << 24, 16 MB. */
static uint8_t
p9100_bar_read(uint32_t addr, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;

    return p9100_native_read_n(p9100, addr & 0xffffff, 1, 0);
}

static uint16_t
p9100_bar_readw(uint32_t addr, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;

    return p9100_native_read_n(p9100, addr & 0xffffff, 2, 0);
}

static uint32_t
p9100_bar_readl(uint32_t addr, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;

    return p9100_native_read_n(p9100, addr & 0xffffff, 4, 0);
}

static void
p9100_bar_write(uint32_t addr, uint8_t val, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;

    p9100_native_write_n(p9100, addr & 0xffffff, val, 1, 0);
}

static void
p9100_bar_writew(uint32_t addr, uint16_t val, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;

    p9100_native_write_n(p9100, addr & 0xffffff, val, 2, 0);
}

static void
p9100_bar_writel(uint32_t addr, uint32_t val, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;

    p9100_native_write_n(p9100, addr & 0xffffff, val, 4, 0);
}

/* A0000-BFFFF: the SVGA unit in emulation mode, the indirect native window
   (A0000-AFFFF) in native mode. In emulation mode with both the A and B
   segments mapped, A uses the read bank and B the write bank for both
   reads and writes. */
static int
p9100_vga_dual_bank(p9100_t *p9100, uint32_t addr)
{
    svga_t *svga = &p9100->svga;

    if ((svga->gdcreg[6] & 0x0c) || (svga->seqregs[0x11] & MISC_BSD))
        return 0;

    /* The B segment offset already carries 10000h. */
    if (addr & 0x10000)
        svga->read_bank = svga->write_bank = ((p9100->bank & 0x0f) << 16) - 0x10000;
    else
        svga->read_bank = svga->write_bank = (p9100->bank >> 4) << 16;

    return 1;
}

static uint8_t
p9100_vga_read(uint32_t addr, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;
    uint8_t  ret;
    int      dual;

    if (!(p9100->config65 & CFG65_MODESELECT))
        return p9100_native_read_n(p9100, addr & 0xffff, 1, 1);

    dual = p9100_vga_dual_bank(p9100, addr);
    ret  = svga_read(addr, &p9100->svga);
    if (dual)
        p9100_recalcbanking(p9100);
    return ret;
}

static uint16_t
p9100_vga_readw(uint32_t addr, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;
    uint16_t ret;
    int      dual;

    if (!(p9100->config65 & CFG65_MODESELECT))
        return p9100_native_read_n(p9100, addr & 0xffff, 2, 1);

    dual = p9100_vga_dual_bank(p9100, addr);
    ret  = svga_readw(addr, &p9100->svga);
    if (dual)
        p9100_recalcbanking(p9100);
    return ret;
}

static uint32_t
p9100_vga_readl(uint32_t addr, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;
    uint32_t ret;
    int      dual;

    if (!(p9100->config65 & CFG65_MODESELECT))
        return p9100_native_read_n(p9100, addr & 0xffff, 4, 1);

    dual = p9100_vga_dual_bank(p9100, addr);
    ret  = svga_readl(addr, &p9100->svga);
    if (dual)
        p9100_recalcbanking(p9100);
    return ret;
}

static void
p9100_vga_write(uint32_t addr, uint8_t val, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;
    int      dual;

    if (!(p9100->config65 & CFG65_MODESELECT)) {
        p9100_native_write_n(p9100, addr & 0xffff, val, 1, 1);
        return;
    }

    dual = p9100_vga_dual_bank(p9100, addr);
    svga_write(addr, val, &p9100->svga);
    if (dual)
        p9100_recalcbanking(p9100);
}

static void
p9100_vga_writew(uint32_t addr, uint16_t val, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;
    int      dual;

    if (!(p9100->config65 & CFG65_MODESELECT)) {
        p9100_native_write_n(p9100, addr & 0xffff, val, 2, 1);
        return;
    }

    dual = p9100_vga_dual_bank(p9100, addr);
    svga_writew(addr, val, &p9100->svga);
    if (dual)
        p9100_recalcbanking(p9100);
}

static void
p9100_vga_writel(uint32_t addr, uint32_t val, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;
    int      dual;

    if (!(p9100->config65 & CFG65_MODESELECT)) {
        p9100_native_write_n(p9100, addr & 0xffff, val, 4, 1);
        return;
    }

    dual = p9100_vga_dual_bank(p9100, addr);
    svga_writel(addr, val, &p9100->svga);
    if (dual)
        p9100_recalcbanking(p9100);
}

static void
p9100_recalcmapping(p9100_t *p9100)
{
    svga_t *svga = &p9100->svga;

    if (!(p9100->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)) {
        mem_mapping_disable(&svga->mapping);
        mem_mapping_disable(&p9100->native_mapping);
        return;
    }

    if (p9100->native_base)
        mem_mapping_set_addr(&p9100->native_mapping, p9100->native_base, 0x1000000);
    else
        mem_mapping_disable(&p9100->native_mapping);

    if (!(p9100->config65 & CFG65_MODESELECT)) {
        if (p9100->config65 & CFG65_NA_ENABLE)
            mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
        else
            mem_mapping_disable(&svga->mapping);
        return;
    }

    switch (svga->gdcreg[6] & 0x0c) {
        case 0x0: /*128k at A0000*/
            mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
            svga->banked_mask = 0xffff;
            break;
        case 0x4: /*64k at A0000*/
            mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
            svga->banked_mask = 0xffff;
            break;
        case 0x8: /*32k at B0000*/
            mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
            svga->banked_mask = 0x7fff;
            break;
        case 0xC: /*32k at B8000*/
            mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
            svga->banked_mask = 0x7fff;
            break;

        default:
            break;
    }
}

static void
p9100_config65_write(p9100_t *p9100, uint8_t val)
{
    svga_t *svga = &p9100->svga;
    uint8_t old  = p9100->config65;

    p9100->config65 = val & 0xee;
    p9100_log("P9100 CONFIG65 = %02X\n", p9100->config65);

    if ((old ^ val) & CFG65_MODESELECT) {
        /* A mode change resets these. */
        p9100->interrupt      = 0;
        p9100->interrupt_en   = 0;
        p9100->alt_read_bank  = 0;
        p9100->alt_write_bank = 0;
        p9100_cksel_update(p9100);
        svga_recalctimings(svga);
    }

    p9100_recalcmapping(p9100);
}

static uint8_t
p9100_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    const p9100_t *p9100 = (p9100_t *) priv;
    uint8_t        ret   = 0x00;

    switch (addr) {
        case 0x00:
            ret = 0x0e; /*Weitek*/
            break;
        case 0x01:
            ret = 0x10;
            break;
        case 0x02:
            ret = 0x00; /*Power 9100*/
            break;
        case 0x03:
            ret = 0x91;
            break;

        case PCI_REG_COMMAND:
            ret = p9100->pci_regs[PCI_REG_COMMAND];
            break;
        case 0x07:
            ret = 0x02; /*Medium DEVSEL timing*/
            break;

        case 0x0a:
            ret = 0x00; /*VGA compatible*/
            break;
        case 0x0b:
            ret = 0x03;
            break;

        case 0x13:
            ret = p9100->native_base >> 24;
            break;

        case 0x30:
            ret = p9100->pci_regs[0x30] & 0x01;
            break;
        case 0x31:
            ret = p9100->pci_regs[0x31] & 0x80;
            break;
        case 0x32:
        case 0x33:
            ret = p9100->pci_regs[addr];
            break;

        case 0x3c:
            ret = p9100->pci_regs[0x3c];
            break;
        case 0x3d:
            ret = PCI_INTA;
            break;

        case 0x40:
            ret = 0x40; /*CONFIG[64]: PCI bus, configuration ports at 9100h*/
            break;
        case 0x41:
            ret = p9100->config65;
            break;
        case 0x42:
            ret = p9100->config66;
            break;

        default:
            break;
    }

    p9100_log("P9100 PCI read  %02X = %02X\n", addr, ret);
    return ret;
}

static void
p9100_rom_recalc(p9100_t *p9100)
{
    uint32_t addr = ((p9100->pci_regs[0x31] >> 7) << 15) | (p9100->pci_regs[0x32] << 16) | ((uint32_t) p9100->pci_regs[0x33] << 24);

    if ((p9100->pci_regs[0x30] & 0x01) && (p9100->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM))
        mem_mapping_set_addr(&p9100->bios_rom.mapping, addr, 0x8000);
    else
        mem_mapping_disable(&p9100->bios_rom.mapping);
}

static void
p9100_pci_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;

    p9100_log("P9100 PCI write %02X = %02X\n", addr, val);

    switch (addr) {
        case PCI_REG_COMMAND:
            p9100->pci_regs[PCI_REG_COMMAND] = val & 0x23;
            io_removehandler(0x03c0, 0x0020, p9100_in, NULL, NULL, p9100_out, NULL, NULL, p9100);
            if (val & PCI_COMMAND_IO)
                io_sethandler(0x03c0, 0x0020, p9100_in, NULL, NULL, p9100_out, NULL, NULL, p9100);
            p9100_recalcmapping(p9100);
            p9100_rom_recalc(p9100);
            break;

        case 0x13:
            p9100->native_base = (uint32_t) val << 24;
            p9100_recalcmapping(p9100);
            break;

        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
            p9100->pci_regs[addr] = val;
            p9100_rom_recalc(p9100);
            break;

        case 0x3c:
            p9100->pci_regs[0x3c] = val;
            break;

        case 0x41:
            p9100_config65_write(p9100, val);
            break;
        case 0x42:
            p9100->config66 = val & 0x1d;
            /* CKSEL picks the native pixel clock. */
            p9100_cksel_update(p9100);
            svga_recalctimings(&p9100->svga);
            break;

        default:
            break;
    }
}

static void *
p9100_init(const device_t *info)
{
    p9100_t *p9100 = calloc(1, sizeof(p9100_t));
    svga_t  *svga  = &p9100->svga;

    p9100->type      = info->local;
    p9100->vram_size = device_get_config_int("memory") << 20;
    p9100->vram_mask = p9100->vram_size - 1;

    rom_init(&p9100->bios_rom, (p9100->type == P9100_PAPILIO) ? BIOS_PAPILIO_PCI_PATH : BIOS_VIPER_SE_PCI_PATH,
             0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    mem_mapping_disable(&p9100->bios_rom.mapping);

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_p9100_pci);

    svga_init(info, svga, p9100, p9100->vram_size,
              p9100_recalctimings,
              p9100_in, p9100_out,
              NULL,
              NULL);

    svga->ramdac    = device_add(&ibm_rgb525_ramdac_device);
    svga->clock_gen = svga->ramdac;
    svga->getclock  = ibm_rgb528_getclock;
    /* The RGB525 PLL runs off a 50 MHz reference: an oscillator on the Viper,
       the ICS9161A on the Papilio, which its BIOS sets to 50 MHz at POST. */
    if (p9100->type == P9100_PAPILIO) {
        p9100->clock_chip = device_add(&ics9161_device);
        icd2061_set_clocks(p9100->clock_chip, 14318184.0f, 50000000.0f, 50000000.0f, 50000000.0f);
    }
    ibm_rgb528_ramdac_set_ref_clock(svga->ramdac, svga, 50000000.0f);
    /* The W5286 only drives the VGA port, so PORT SEL decides on its own. */
    ibm_rgb528_ramdac_set_port_sel_only(svga->ramdac, svga, 1);
    svga->dac_hwcursor_draw = ibm_rgb528_hwcursor_draw;
    svga->decode_mask       = p9100->vram_mask;
    svga->vblank_start      = p9100_vblank_start;

    mem_mapping_set_handler(&svga->mapping, p9100_vga_read, p9100_vga_readw, p9100_vga_readl,
                            p9100_vga_write, p9100_vga_writew, p9100_vga_writel);
    mem_mapping_set_p(&svga->mapping, p9100);

    mem_mapping_add(&p9100->native_mapping, 0, 0,
                    p9100_bar_read, p9100_bar_readw, p9100_bar_readl,
                    p9100_bar_write, p9100_bar_writew, p9100_bar_writel,
                    NULL, MEM_MAPPING_EXTERNAL, p9100);
    mem_mapping_disable(&p9100->native_mapping);

    io_sethandler(0x03c0, 0x0020, p9100_in, NULL, NULL, p9100_out, NULL, NULL, p9100);

    pci_add_card(PCI_ADD_NORMAL, p9100_pci_read, p9100_pci_write, p9100, &p9100->pci_slot);

    p9100->pci_regs[PCI_REG_COMMAND] = 0x03;
    p9100->pci_regs[0x32]            = 0x0c;
    p9100->config65                  = CFG65_MODESELECT;

    /* Drawing engine idle, windows and plane mask open. */
    p9100->interrupt = 0x01;
    p9100->pmask     = 0xffffffff;
    p9100->p_w_max   = p9100_window_reg(0x1fff, 0x1fff);
    p9100->b_w_max   = p9100_window_reg(0x1fff, 0x1fff);
    p9100->raster    = 0xcc;

    /* svga_init() worked out the timings while still in native mode. */
    svga_recalctimings(svga);

    /* Video RAM fitted, the rest as after reset. */
    svga->seqregs[0x05] = 0x01;
    svga->seqregs[0x06] = 0x01;
    svga->seqregs[0x10] = 0x03;
    svga->seqregs[0x11] = 0xfd;

    p9100_recalcbanking(p9100);

    return p9100;
}

static int
p9100_available(void)
{
    return rom_present(BIOS_VIPER_SE_PCI_PATH);
}

static int
p9100_papilio_available(void)
{
    return rom_present(BIOS_PAPILIO_PCI_PATH);
}

static void
p9100_close(void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;

    svga_close(&p9100->svga);

    free(p9100);
}

static void
p9100_speed_changed(void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;

    svga_recalctimings(&p9100->svga);
}

static void
p9100_force_redraw(void *priv)
{
    p9100_t *p9100 = (p9100_t *) priv;

    p9100->svga.fullchange = p9100->svga.monitor->mon_changeframecount;
}

static const device_config_t p9100_config[] = {
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "2 MB", .value = 2 },
            { .description = "4 MB", .value = 4 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t p9100_viper_se_pci_device = {
    .name          = "Weitek Power 9100 (Diamond Viper SE) (PCI)",
    .internal_name = "viper_se_pci",
    .flags         = DEVICE_PCI,
    .local         = P9100_VIPER_SE,
    .init          = p9100_init,
    .close         = p9100_close,
    .reset         = NULL,
    .available     = p9100_available,
    .speed_changed = p9100_speed_changed,
    .force_redraw  = p9100_force_redraw,
    .config        = p9100_config
};

const device_t p9100_papilio_pci_device = {
    .name          = "Weitek Power 9100 (DSystems Papilio UltraCAD Pro) (PCI)",
    .internal_name = "papilio_pci",
    .flags         = DEVICE_PCI,
    .local         = P9100_PAPILIO,
    .init          = p9100_init,
    .close         = p9100_close,
    .reset         = NULL,
    .available     = p9100_papilio_available,
    .speed_changed = p9100_speed_changed,
    .force_redraw  = p9100_force_redraw,
    .config        = p9100_config
};
