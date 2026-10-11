/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- legacy VGA passthrough.
 *
 *          The card answers the standard VGA I/O ports and memory windows
 *          as a plain VGA so the video BIOS POSTs and INT 10h text and
 *          graphics modes render; this file holds the few places where the
 *          chip's own registers reach into that VGA behavior.
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include "cpu.h"
#include <86box/vid_ati_rage128.h>
#include <86box/vid_ati_rage128_regs.h>

/* ------------------------------------------------------------------ */
/* VGA aperture at A0000. The chip pages the two 32 KB halves of the
   window independently: MEM_VGA_WP_SEL and MEM_VGA_RP_SEL carry a write
   page and a read page number for each half (RRG: MEM_VGA_WP_SEL,
   p. 3-110 / PDF 128), in force whenever the extended display path is
   on or CRTC_EXT_CNTL.VGA_MEM_PS_EN selects them for the VGA core
   (RRG: CRTC_EXT_CNTL, p. 3-65 / PDF 83). rage128_update_banking turns
   the page numbers into the bank_r / bank_w byte offsets used here, so
   a handler only picks the half, adds its bank and takes the svga
   core's linear path -- the split-bank shape of vid_ati_mach64.c. The
   GPU barrier orders the access against the optional GPU backend's
   pending work on those bytes; synctel is the optional access record.  */
/* ------------------------------------------------------------------ */
static uint8_t
rage128_aper_read(uint32_t addr, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    addr = (addr & 0x7fff) + dev->bank_r[(addr >> 15) & 1];
    if (dev->synctel)
        rage128_synctel_cpu_read(dev, addr, 1);
    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, addr, 1, 0);
    return svga_read_linear(addr, &dev->svga);
}

static uint16_t
rage128_aper_readw(uint32_t addr, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    addr = (addr & 0x7fff) + dev->bank_r[(addr >> 15) & 1];
    if (dev->synctel)
        rage128_synctel_cpu_read(dev, addr, 2);
    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, addr, 2, 0);
    return svga_readw_linear(addr, &dev->svga);
}

static uint32_t
rage128_aper_readl(uint32_t addr, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    addr = (addr & 0x7fff) + dev->bank_r[(addr >> 15) & 1];
    if (dev->synctel)
        rage128_synctel_cpu_read(dev, addr, 4);
    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, addr, 4, 0);
    return svga_readl_linear(addr, &dev->svga);
}

static void
rage128_aper_write(uint32_t addr, uint8_t val, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    addr = (addr & 0x7fff) + dev->bank_w[(addr >> 15) & 1];
    if (dev->synctel)
        rage128_synctel_cpu_write(dev, addr, 1);
    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, addr, 1, 1);
    svga_write_linear(addr, val, &dev->svga);
}

static void
rage128_aper_writew(uint32_t addr, uint16_t val, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    addr = (addr & 0x7fff) + dev->bank_w[(addr >> 15) & 1];
    if (dev->synctel)
        rage128_synctel_cpu_write(dev, addr, 2);
    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, addr, 2, 1);
    svga_writew_linear(addr, val, &dev->svga);
}

static void
rage128_aper_writel(uint32_t addr, uint32_t val, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    addr = (addr & 0x7fff) + dev->bank_w[(addr >> 15) & 1];
    if (dev->synctel)
        rage128_synctel_cpu_write(dev, addr, 4);
    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, addr, 4, 1);
    svga_writel_linear(addr, val, &dev->svga);
}

/* B0000 / B8000 windows. The svga core resolves these through its
   plane and odd/even logic, so the final VRAM address is not known
   here and the GPU barrier covers the whole of VRAM instead. With an
   idle engine and an empty texture cache that is two compares; a cached
   texture level that overlaps a text-window store is exactly the case
   the barrier's invalidate exists for. synctel is skipped: its record
   wants the resolved address, which only the core computes. */
static uint8_t
rage128_bwin_read(uint32_t addr, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, 0, dev->vram_mask + 1u, 0);
    return svga_read(addr, &dev->svga);
}

static uint16_t
rage128_bwin_readw(uint32_t addr, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, 0, dev->vram_mask + 1u, 0);
    return svga_readw(addr, &dev->svga);
}

static uint32_t
rage128_bwin_readl(uint32_t addr, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, 0, dev->vram_mask + 1u, 0);
    return svga_readl(addr, &dev->svga);
}

static void
rage128_bwin_write(uint32_t addr, uint8_t val, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, 0, dev->vram_mask + 1u, 1);
    svga_write(addr, val, &dev->svga);
}

static void
rage128_bwin_writew(uint32_t addr, uint16_t val, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, 0, dev->vram_mask + 1u, 1);
    svga_writew(addr, val, &dev->svga);
}

static void
rage128_bwin_writel(uint32_t addr, uint32_t val, void *priv)
{
    rage128_t *dev = (rage128_t *) priv;

    if (dev->gpu)
        rage128_gpu_cpu_barrier(dev, 0, dev->vram_mask + 1u, 1);
    svga_writel(addr, val, &dev->svga);
}

/* Legacy window placement from VGA graphics controller register 6 bits
   [3:2], the standard choice of 128 KB or 64 KB at A0000 and 32 KB at
   B0000 or B8000, in the shape of mach64_updatemapping in
   vid_ati_mach64.c: the A0000 cases go through the split-bank handlers,
   the B windows through the whole-VRAM barrier wrappers above.         */
void
rage128_updatemapping(rage128_t *dev)
{
    svga_t *svga = &dev->svga;

    /* A board strapped vga_disable is "not recognized as the system's
       VGA controller, but only as an extended mode controller" (GCS:
       External Straps, p. 4-32 / PDF 80); the strap reads back as
       CONFIG_XSTRAP.VGA_DISABLE and turns the VGA I/O decode off (RRG:
       CONFIG_CNTL, p. 3-10 / PDF 28). The legacy memory window goes
       with it: a second card decoding A0000 would collide with the
       primary VGA, so a secondary board never claims these segments. */
    if (dev->vga_disabled) {
        mem_mapping_disable(&svga->mapping);
        return;
    }

    switch (svga->gdcreg[6] & 0x0c) {
        case 0x0: /* 128k at A0000 */
            mem_mapping_set_handler(&svga->mapping,
                                    rage128_aper_read, rage128_aper_readw, rage128_aper_readl,
                                    rage128_aper_write, rage128_aper_writew, rage128_aper_writel);
            mem_mapping_set_p(&svga->mapping, dev);
            mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
            svga->banked_mask = 0xffff;
            break;
        case 0x4: /* 64k at A0000 */
            mem_mapping_set_handler(&svga->mapping,
                                    rage128_aper_read, rage128_aper_readw, rage128_aper_readl,
                                    rage128_aper_write, rage128_aper_writew, rage128_aper_writel);
            mem_mapping_set_p(&svga->mapping, dev);
            mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
            svga->banked_mask = 0xffff;
            break;
        case 0x8: /* 32k at B0000 */
            mem_mapping_set_handler(&svga->mapping,
                                    rage128_bwin_read, rage128_bwin_readw, rage128_bwin_readl,
                                    rage128_bwin_write, rage128_bwin_writew, rage128_bwin_writel);
            mem_mapping_set_p(&svga->mapping, dev);
            mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
            svga->banked_mask = 0x7fff;
            break;
        case 0xc: /* 32k at B8000 */
            mem_mapping_set_handler(&svga->mapping,
                                    rage128_bwin_read, rage128_bwin_readw, rage128_bwin_readl,
                                    rage128_bwin_write, rage128_bwin_writew, rage128_bwin_writel);
            mem_mapping_set_p(&svga->mapping, dev);
            mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
            svga->banked_mask = 0x7fff;
            break;

        default:
            break;
    }
}

/* DAC_VGA_ADR_EN (RRG: DAC_CNTL, p. 3-124 / PDF 142) gates the palette
   at VGA I/O 3C6-3C9 only while the CRTC is in an extended mode; VGA
   modes are never gated, and the MMIO PALETTE_INDEX/DATA path is a
   separate decode the bit does not name. A read of a port the DAC does
   not drive returns the undriven bus value, 0xff. */
static int
rage128_dac_io_gated(const rage128_t *dev, uint16_t addr)
{
    if ((addr < 0x3c6) || (addr > 0x3c9))
        return 0;

    return (dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN)
        && !(dev->dac_cntl & RAGE128_DAC_VGA_ADR_EN);
}

uint8_t
rage128_vga_in(uint16_t addr, void *priv)
{
    rage128_t *dev  = (rage128_t *) priv;
    svga_t    *svga = &dev->svga;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    if (rage128_dac_io_gated(dev, addr))
        return 0xff;

    switch (addr) {
        case 0x3c6:
            /* svga->dac_mask is the effective mask, which the extended
               display path forces fully open (rage128_dac_mask_apply);
               3C6 must still read back what software programmed. */
            return dev->dac_mask_prog;
        case 0x3d4:
            return svga->crtcreg;
        case 0x3d5:
            if (svga->crtcreg > 0x20)
                return 0xff;
            return svga->crtc[svga->crtcreg];
        default:
            return svga_in(addr, svga);
    }
}

void
rage128_vga_out(uint16_t addr, uint8_t val, void *priv)
{
    rage128_t *dev  = (rage128_t *) priv;
    svga_t    *svga = &dev->svga;
    uint8_t    old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    if (rage128_dac_io_gated(dev, addr))
        return;

    switch (addr) {
        case 0x3c6:
            /* The VGA DAC mask and DAC_CNTL [31:24] are one register: the
               guide calls the field "a mirror of the VGA DAC_MASK
               register" that acts only in VGA modes (RRG: DAC_CNTL,
               p. 3-125 / PDF 143). The programmed value is kept apart
               from the effective mask the renderer sees, which
               rage128_dac_mask_apply derives from it and the mode. */
            dev->dac_mask_prog = val;
            rage128_dac_mask_apply(dev);
            return;
        case 0x3c9:
            /* A legacy DAC data write changes the svga core's palette
               lookup inside svga_out: drop the renderer's cached palette
               (lut_dirty) and the conversion worker's frame (conv_stale),
               both rebuilt from the new entries. */
            dev->lut_dirty = 1;
            atomic_store(&dev->conv_stale, 1);
            svga_out(addr, val, svga);
            return;
        case 0x3cf:
            /* Graphics controller register 6 moves or resizes the legacy
               window: redo the placement so the A0000 cases keep the
               split-bank handlers. */
            svga_out(addr, val, svga);
            if ((svga->gdcaddr & 15) == 6)
                rage128_updatemapping(dev);
            return;
        case 0x3d4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3d5:
            if (svga->crtcreg > 0x20)
                return;
            /* CRTC[0..7] are write-protected by CRTC[0x11] bit 7, with bit 4
               of register 7 (line compare bit 8) left writable -- standard
               VGA behavior. */
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;
            /* A VGA vertical-display-end write (CRTC[0x12] with its two high
               bits in CRTC[7]) reads back through the extended register's
               CRTC_V_DISP field (RRG: CRTC_V_TOTAL_DISP, p. 3-67 / PDF 85).
               The video BIOS relies on this: its set-mode handler writes
               CRTC[0x12], reads the extended field back and skips the
               extended-to-VGA hand-off when the two disagree (RE:
               Rage128progl_zerostate.VBI @c000:50cf). Readback only:
               crtc_v_disp_active, the height the raster runs on, is left
               alone, because the X server's restore writes the VGA console
               registers after the extended ones and must not cut a live
               raster down to console height. */
            if (svga->crtcreg == 0x07 || svga->crtcreg == 0x12) {
                uint32_t vdisp = svga->crtc[0x12]
                    | ((svga->crtc[7] & 0x02) << 7)
                    | ((svga->crtc[7] & 0x40) << 3);
                dev->crtc_v_total_disp = (dev->crtc_v_total_disp & ~0x07ff0000)
                    | (vdisp << 16);
            }
            if (old != val) {
                if (svga->crtcreg < 0xe || svga->crtcreg > 0x10) {
                    if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                        svga->fullchange = 3;
                        /* Only memaddr_latch moves; scanout_seed_base is
                           left alone. rage128_scan_remap folds a change
                           of the latched CRTC_OFFSET into the running
                           fetch address mid-frame (a flip taken at a line
                           boundary, CRTC_OFFSET_CNTL.CRTC_OFFSET_FLIP_CNTL,
                           RRG: CRTC_OFFSET_CNTL, p. 3-72 / PDF 90), and a
                           VGA start-address write changes no latched
                           offset, so there is nothing for it to fold; the
                           extended-mode vsync re-seeds the base anyway. */
                        svga->memaddr_latch = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
                    } else {
                        svga->fullchange = svga->monitor->mon_changeframecount;
                        svga_recalctimings(svga);
                    }
                }
            }
            return;
        default:
            svga_out(addr, val, svga);
            return;
    }
}
