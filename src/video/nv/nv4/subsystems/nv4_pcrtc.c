/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PCRTC and the display timings.
 *
 *          The PCRTC registers (vertical blank interrupt, the start
 *          address that replaces the VGA one, the raster position), the
 *          extended CRTC timing and addressing bits the SVGA core does
 *          not know, the scanout format of CR28 and the 32x32 hardware
 *          cursor.
 *
 *          The extended bits, as the TNT's register descriptions lay
 *          them out (and as Linux rivafb programs them):
 *            CR19 7:5   row offset 10:8          CR19 4:0  start 20:16
 *            CR25 5     row offset 11            CR25 4    hblank end 6
 *            CR25 3:0   bit 10 of vblank start, vsync start, vdisplay
 *                       end, vtotal
 *            CR2D 5     start 21                 CR2D 3:0  bit 8 of
 *                       hsync start, hblank start, hdisplay end, htotal
 *            CR28 1:0   VGA, 8, 16 or 32 bpp     CR1A 0    VGA wrap
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include "cpu.h"
#include <86box/nv/vid_nv4.h>

#define NV4_CURSOR_SIZE 32

void
nv4_pcrtc_reset(nv4_t *nv4)
{
    nv4->pcrtc_intr    = 0;
    nv4->pcrtc_intr_en = 0;
    nv4->pcrtc_start   = 0;
    nv4->pcrtc_config  = 0;
}

uint32_t
nv4_pcrtc_read(nv4_t *nv4, uint32_t addr)
{
    svga_t *svga = &nv4->svga;

    switch (addr) {
        case NV4_PCRTC_INTR_0:
            return nv4->pcrtc_intr;
        case NV4_PCRTC_INTR_EN_0:
            return nv4->pcrtc_intr_en;
        case NV4_PCRTC_START_ADDR:
            return nv4->pcrtc_start;
        case NV4_PCRTC_CONFIG:
            return nv4->pcrtc_config;
        case NV4_PCRTC_RASTER:
            /* position 10:0, vertical blank 16 */
            return (svga->displine & 0x7ff) | ((svga->cgastat & 8) ? 0x10000 : 0);
        default:
            nv4_log("NV4 PCRTC: read %06x\n", addr);
            return 0;
    }
}

void
nv4_pcrtc_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    switch (addr) {
        case NV4_PCRTC_INTR_0:
            nv4->pcrtc_intr &= ~val;
            nv4_pmc_update_irq(nv4);
            break;
        case NV4_PCRTC_INTR_EN_0:
            nv4->pcrtc_intr_en = val & 1;
            nv4_pmc_update_irq(nv4);
            break;
        case NV4_PCRTC_START_ADDR:
            nv4->pcrtc_start = val & 0x01fffffc;
            svga_recalctimings(&nv4->svga);
            break;
        case NV4_PCRTC_CONFIG:
            nv4->pcrtc_config = val & 1;
            svga_recalctimings(&nv4->svga);
            break;
        default:
            nv4_log("NV4 PCRTC: write %06x = %08x\n", addr, val);
            break;
    }
}

/* The CRTC's start address registers and PCRTC_START are one latch:
   CR0C/CR0D (byte address 17:2), CR19 4:0 (22:18) and CR2D bit 5 (23)
   write their bits of it. PCRTC_CONFIG only picks when a new start takes
   effect (the driver sets it to latch flips at the end of vertical sync).
   Windows goes back to text mode (a DOS box, the blue screen) by
   writing the VGA registers only: a game that had flipped to a back
   buffer through PCRTC_START must not leave text mode scanning it.
   Scan-out always takes this latch (nv4_recalctimings). */
void
nv4_pcrtc_crtc_start_write(nv4_t *nv4, uint8_t idx, uint8_t val)
{
    uint32_t s = nv4->pcrtc_start;

    switch (idx) {
        case 0x0c:
            s = (s & ~(0xffu << 10)) | ((uint32_t) val << 10);
            break;
        case 0x0d:
            s = (s & ~(0xffu << 2)) | ((uint32_t) val << 2);
            break;
        case NV4_CR_RPC0:
            s = (s & ~(0x1fu << 18)) | ((uint32_t) (val & 0x1f) << 18);
            break;
        case NV4_CR_HEB:
            s = (s & ~(1u << 23)) | ((uint32_t) ((val >> 5) & 1) << 23);
            break;
        default:
            return;
    }
    if (s != nv4->pcrtc_start) {
        nv4->pcrtc_start = s;
        svga_recalctimings(&nv4->svga);
    }
}

/* Start of vertical blank: the PCRTC interrupt. */
void
nv4_vblank_start(svga_t *svga)
{
    nv4_t *nv4 = (nv4_t *) svga->priv;

    /* what the 3D engine has queued is on screen by the next frame
       (waited for only when it draws into the displayed bytes) */
    {
        uint32_t lo    = svga->memaddr_latch << 2;
        uint32_t pitch = svga->rowoffset << 3;
        uint32_t len;

        if (!pitch)
            pitch = (uint32_t) svga->hdisp * ((svga->bpp + 7) >> 3);
        len = pitch * svga->dispend;

        nv4_3d_vblank(nv4, lo, lo + len);
    }
    nv4_pvideo_vblank(nv4);
    nv4->dbg_vblanks++;
    nv4->vblank_count++;
    nv4->pcrtc_intr |= 1;
    nv4_pmc_update_irq(nv4);
}

extern uint32_t svga_conv_16to32(svga_t *svga, uint16_t color, uint8_t bpp); /* vid_svga.c */

/* 15/16 bpp through the palette as a gamma ramp */
static uint32_t
nv4_conv_16to32(svga_t *svga, uint16_t color, uint8_t bpp)
{
    return svga_lookup_lut_ram(svga, svga_conv_16to32(svga, color, bpp));
}

void
nv4_recalctimings(svga_t *svga)
{
    nv4_t  *nv4    = (nv4_t *) svga->priv;
    uint8_t format = svga->crtc[NV4_CR_PIXEL] & 0x03;
    uint8_t lsr    = svga->crtc[NV4_CR_LSR];
    uint8_t heb    = svga->crtc[NV4_CR_HEB];

    /* Start address and row offset. The start is the latch PCRTC_START
       and the CRTC start registers share (nv4_pcrtc_crtc_start_write),
       whatever PCRTC_CONFIG says: that only picks when a new start takes
       effect. NVIDIA's later drivers write PCRTC_START with it clear. */
    svga->memaddr_latch = nv4->pcrtc_start >> 2;
    svga->rowoffset += (svga->crtc[NV4_CR_RPC0] & 0xe0) << 3;
    if (lsr & 0x20)
        svga->rowoffset += 0x800;

    /* Vertical bit 10s and horizontal bit 8s */
    if (lsr & 0x01)
        svga->vtotal += 0x400;
    if (lsr & 0x02)
        svga->dispend += 0x400;
    if (lsr & 0x04)
        svga->vsyncstart += 0x400;
    if (lsr & 0x08)
        svga->vblankstart += 0x400;
    if (heb & 0x01)
        svga->htotal += 0x100;
    if (heb & 0x02) {
        svga->hdisp += 0x100 * svga->dots_per_clock;
        svga->hdisp_time += 0x100;
    }
    if (heb & 0x04)
        svga->hblankstart += 0x100;
    /* CR1A bit 2 clear (screens of 1280x1024 and over) enables bit 6 of
       the horizontal blank end. */
    if (!(svga->crtc[NV4_CR_RPC1] & 0x04)) {
        svga->hblank_end_val |= (lsr & 0x10) << 2;
        svga->hblank_end_mask = 0x7f;
    }

    /* Line compare has no bit 10 (CR25 extends the other vertical
       values only), so the drivers' "no split" 0x3FF would restart the
       scan-out at line 1024 and repeat the top of a taller screen: at its
       maximum it never cuts one. */
    if ((svga->split == 0x400) && (svga->dispend > 0x400))
        svga->split = 0x7fffffff;

    /* Horizontal blank end is compared from the character after blank
       start, so an end equal to the start in the compared bits blanks a
       whole 64 (or 128) characters; the SVGA core would match at once and
       make a huge overscan. Give it the end with the 7-bit compare. */
    {
        uint32_t mask = (svga->hblank_end_val & ~0x3f) ? svga->hblank_end_mask : 0x3f;

        if (((svga->hblankstart ^ svga->hblank_end_val) & mask) == 0) {
            svga->hblank_end_val  = 0x80 | ((svga->hblankstart + mask + 1) & 0x7f);
            svga->hblank_end_mask = 0x7f;
        }
    }

    /* Interlace: CR39 is the half-line point, 0xff disables it. */
    svga->interlace = (svga->crtc[NV4_CR_ILACE] != 0xff);

    /* CR1A bits 7/6 stop hsync/vsync (DPMS); bit 0 wraps the CRTC at
       256 KB for the VGA modes. */
    svga->dpms              = !!(svga->crtc[NV4_CR_RPC1] & 0xc0);
    svga->vram_display_mask = (!format && (svga->crtc[NV4_CR_RPC1] & 0x01)) ? (svga->vram_mask & 0x3ffff) : svga->vram_mask;

    nv4_vga_update_banks(nv4);
    svga->override = 0;

    switch (format) {
        case 1:
            svga->bpp    = 8;
            svga->lowres = 0;
            svga->map8   = svga->pallook;
            svga->render = svga_render_8bpp_highres;
            break;
        case 2:
            svga->lowres = 0;
            if (nv4->general_control & NV4_PRAMDAC_GC_ALT_MODE) {
                svga->bpp    = 16;
                svga->render = svga_render_16bpp_highres;
            } else {
                svga->bpp    = 15;
                svga->render = svga_render_15bpp_highres;
            }
            break;
        case 3:
            svga->bpp    = 32;
            svga->lowres = 0;
            svga->render = svga_render_32bpp_highres;
            break;
        default:
            /* VGA: the core chose everything, but with the bpp of the
               last extended mode; VGA has 8 bits per pixel at most. */
            if (svga->bpp > 8) {
                svga->bpp = 8;
                if (svga->attr_palette_enable && ((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1))
                    && ((svga->gdcreg[5] & 0x60) >= 0x40)) {
                    svga->map8   = svga->pallook;
                    svga->render = svga->lowres ? svga_render_8bpp_lowres : svga_render_8bpp_highres;
                }
            }
            break;
    }

    /* Direct colour modes go through the palette as a gamma ramp. */
    svga->lut_map     = (format >= 2);
    svga->conv_16to32 = svga->lut_map ? nv4_conv_16to32 : svga_conv_16to32;

    /* Pixel clock: the VPLL once it is programmed, else the VGA clock
       the core picked from MISC. */
    if (nv4->vclk_hz > 0.0)
        svga->clock = (cpuclock * (double) (1ULL << 32)) / nv4->vclk_hz;

    /* Hardware cursor: CR31 bit 0 shows it; its 2 KB image is at
       CR2F (address 31:24), CR30 6:0 (23:17) and CR31 7:2 (16:11), in
       instance memory or, with CR30 bit 7, in the framebuffer. The
       TNT2's later drivers keep it in the top 16 MB of a 32 MB card. */
    svga->hwcursor.ena       = (svga->crtc[NV4_CR_HCUR_ADDR1] & 0x01) && format;
    svga->hwcursor.cur_xsize = NV4_CURSOR_SIZE;
    svga->hwcursor.cur_ysize = NV4_CURSOR_SIZE;
    svga->hwcursor.xoff      = (svga->hwcursor.x < 0) ? -svga->hwcursor.x : 0;
    svga->hwcursor.yoff      = (svga->hwcursor.y < 0) ? -svga->hwcursor.y : 0;
    svga->hwcursor.addr      = ((uint32_t) svga->crtc[NV4_CR_HCUR_ADDR2] << 24)
                             | ((svga->crtc[NV4_CR_HCUR_ADDR0] & 0x7f) << 17)
                             | ((svga->crtc[NV4_CR_HCUR_ADDR1] & 0xfc) << 9);
    svga->hwcursor.addr += svga->hwcursor.yoff * NV4_CURSOR_SIZE * 2;
}

/* One line of the cursor: 32 pixels of A1R5G5B5. A set alpha bit
   replaces the screen pixel, a clear one XORs it (0 is transparent). */
void
nv4_hwcursor_draw(svga_t *svga, int displine)
{
    nv4_t    *nv4   = (nv4_t *) svga->priv;
    uint32_t *line  = svga->monitor->target_buffer->line[displine];
    uint32_t  addr  = svga->hwcursor_latch.addr;
    int       x_pos = svga->hwcursor_latch.x + svga->x_add;
    int       in_fb = !!(svga->crtc[NV4_CR_HCUR_ADDR0] & 0x80);

    for (int x = 0; x < NV4_CURSOR_SIZE; x++, addr += 2) {
        uint16_t pixel;
        int      px = x_pos + x;
        uint32_t rgb;

        if (in_fb)
            pixel = *(uint16_t *) &svga->vram[addr & nv4->vram_mask & ~1];
        else
            pixel = nv4_ramin_read16(nv4, addr);

        if ((px < 0) || (px > 2047))
            continue;

        rgb = ((pixel & 0x7c00) << 9) | ((pixel & 0x7000) << 4)
            | ((pixel & 0x03e0) << 6) | ((pixel & 0x0380) << 1)
            | ((pixel & 0x001f) << 3) | ((pixel & 0x001c) >> 2);

        if (pixel & 0x8000)
            line[px] = rgb;
        else
            line[px] ^= rgb;
    }

    svga->hwcursor_latch.addr += NV4_CURSOR_SIZE * 2;
}
