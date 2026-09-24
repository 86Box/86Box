/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATi Mach64 graphics card emulation.
 *          The ATi Mach64 is a 1994 Windows 2D accelerator.
 *          Technical information is available at: https://bitsavers.org/components/ati/RRG-S00700-05_mach64_Register_Reference_Guide_1999410.pdf
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Connor Hyde, <mario64crashed@gmail.com> <https://starfrost.net>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2026 Connor Hyde.
 */

#include "vid_ati_mach64.h"

video_timings_t timing_mach64_isa = { .type = VIDEO_ISA, .write_b = 3, .write_w = 3, .write_l = 6, .read_b = 5, .read_w = 5, .read_l = 10 };
video_timings_t timing_mach64_vlb = { .type = VIDEO_BUS, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };
video_timings_t timing_mach64_pci = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };

mach64_t *reset_state[2] = { NULL, NULL };

/* DP_*_PIX_WIDTH: 0 mono, 1 4 bpp, 2 8 bpp, 3 15 bpp, 4 16 bpp, 6 32 bpp (RRG 3-39). */
int mach64_width[8] = { WIDTH_1BIT, WIDTH_4BIT, 0, 1, 1, 2, 2, 0 };

#ifdef ENABLE_MACH64_LOG
int mach64_do_log = ENABLE_MACH64_LOG;

void
mach64_log(const char *fmt, ...);
{
    va_list ap;

    if (mach64_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#endif

// x86 I/O port output function
/* In accelerator mode (CRTC_EXT_DISP_EN) the DAC answers at the VGA
   addresses 3C6-3C9 only while DAC_VGA_ADR_EN (DAC_CNTL bit 13) is set;
   DAC_REGS reaches it always (RRG 3-32). */
static int
mach64_vga_dac_decoded(const mach64_t *mach64)
{
    return !(mach64->crtc_gen_cntl & (1 << 24)) || (mach64->dac_cntl & (1 << 13));
}

/* The VGA's CPU paging (VGA Register Guide 5-17, 5-26, 5-27): ATI32 bits
   4:1 page writes, and reads as well unless ATI3E bit 3 splits them, when
   bit 0 over bits 7:5 pages reads. The pages are 64K, or 128K while
   ATI3D bit 2 opens A0000-BFFFF. */
static void
mach64_update_vga_banks(mach64_t *mach64)
{
    svga_t        *svga  = &mach64->svga;
    const uint8_t  ati32 = mach64->regs[0x32];
    const uint32_t size  = (mach64->regs[0x3d] & 0x04) ? 0x20000 : 0x10000;
    const uint32_t wpage = (ati32 >> 1) & 0x0f;
    const uint32_t rpage = (mach64->regs[0x3e] & 0x08) ? (((ati32 & 0x01) << 3) | (ati32 >> 5)) : wpage;

    svga->write_bank = wpage * size;
    svga->read_bank  = rpage * size;
}

void
mach64_out(uint16_t addr, uint8_t val, void *priv)
{
    mach64_t *mach64 = priv;
    svga_t   *svga   = &mach64->svga;
    uint8_t   old;

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x1ce:
            mach64->index = val;
            break;
        case 0x1cf:
            mach64->regs[mach64->index & 0x3f] = val;
            switch (mach64->index & 0x3f) {
                case 0x32: /* ATI32: CPU paging */
                case 0x3d: /* ATI3D bit 2: 128K pages */
                case 0x3e: /* ATI3E bit 3: separate read and write pages */
                    mach64_update_vga_banks(mach64);
                    if ((mach64->index & 0x3f) == 0x3d)
                        mach64_updatemapping(mach64);
                    if ((mach64->index & 0x3f) == 0x3e)
                        svga_recalctimings(svga); /* ATI3E bit 1: interlace */
                    break;
                case 0x23: /* ATI23 bit 4: start address bit 17 */
                case 0x30: /* ATI30 bit 6: start address bit 16; bit 5: 256 colours */
                case 0x36:
                    svga_recalctimings(svga);
                    break;
                default:
                    break;
            }
            break;
        case 0x3C6 ... 0x3C9:
            if (!mach64_vga_dac_decoded(mach64))
                return;
            if (mach64->type == MACH64_GX)
                ati68860_ramdac_out((addr & 3) | ((mach64->dac_cntl & 3) << 2), val, 0, svga->ramdac, svga);
            else
                svga_out(addr, val, svga);
            return;
        case 0x3cf:
            if (svga->gdcaddr == 6) {
                uint8_t old_val = svga->gdcreg[6];
                svga->gdcreg[6] = val;
                if ((svga->gdcreg[6] & 0xc) != (old_val & 0xc))
                    mach64_updatemapping(mach64);
                return;
            }
            break;
        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if (svga->crtcreg > 0x20)
                return;
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;

            if (old != val) {
                if (svga->crtcreg < 0xe || svga->crtcreg > 0x10) {
                    if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                        svga->fullchange = 3;
                        svga->memaddr_latch   = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
                    } else {
                        svga->fullchange = svga->monitor->mon_changeframecount;
                        svga_recalctimings(svga);
                    }
                }
            }
            break;

        default:
            break;
    }
    svga_out(addr, val, svga);
}

uint8_t
mach64_in(uint16_t addr, void *priv)
{
    mach64_t *mach64 = priv;
    svga_t   *svga   = &mach64->svga;

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x1ce:
            return mach64->index;
        case 0x1cf:
            /* ATI28: bits 9:8 of the vertical line counter, read only
               (VGA Register Guide 5-12). */
            if ((mach64->index & 0x3f) == 0x28)
                return (svga->vc >> 8) & 3;
            return mach64->regs[mach64->index & 0x3f];
        case 0x3C6 ... 0x3C9:
            if (!mach64_vga_dac_decoded(mach64))
                return 0xff;
            if (mach64->type == MACH64_GX)
                return ati68860_ramdac_in((addr & 3) | ((mach64->dac_cntl & 3) << 2), 0, svga->ramdac, svga);
            return svga_in(addr, svga);
        case 0x3D4:
            return svga->crtcreg;
        case 0x3D5:
            if (svga->crtcreg > 0x20)
                return 0xff;
            return svga->crtc[svga->crtcreg];

        default:
            break;
    }
    return svga_in(addr, svga);
}

/* CRTC_PIX_WIDTH 4 bpp (CRTC_GEN_CNTL bits 10:8): two pixels in each byte,
   the high nibble first, or the low one first when CRTC_BYTE_PIX_ORDER
   (bit 11) is set (RRG 3-20). The accelerator CRTC reads memory linearly;
   the VGA planar shifter takes no part. */
static void
mach64_render_4bpp(svga_t *svga)
{
    const mach64_t *mach64   = (mach64_t *) svga->priv;
    const int       lo_first = !!(mach64->crtc_gen_cntl & (1 << 11));
    uint32_t       *p;

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

    for (int x = 0; x < svga->hdisp; x += 2) {
        uint8_t dat = svga->vram[svga->memaddr & svga->vram_display_mask];

        p[x]     = svga->map8[(lo_first ? (dat & 0x0f) : (dat >> 4)) & svga->dac_mask];
        p[x + 1] = svga->map8[(lo_first ? (dat >> 4) : (dat & 0x0f)) & svga->dac_mask];
        svga->memaddr++;
    }
    svga->memaddr &= svga->vram_display_mask;
}

/* The accelerator CRTC's border (RRG 3-72 to 3-74): OVR_WID_LEFT (3:0)
   and OVR_WID_RIGHT (19:16) characters, OVR_WID_TOP (7:0) and
   OVR_WID_BOTTOM (23:16) lines, in OVR_CLR -- the palette entry
   OVR_CLR_8 (7:0) at 4 and 8 bpp, OVR_CLR_B/G/R (15:8, 23:16, 31:24)
   above that. Outside it the display is blank. */
static void
mach64_update_overscan(mach64_t *mach64)
{
    svga_t *svga = &mach64->svga;

    if (((mach64->crtc_gen_cntl >> 24) & 3) != 3) {
        svga->border_override = 0;
        svga->overscan_color  = svga->pallook[svga->attrregs[0x11]];
        return;
    }

    if (svga->bpp <= 8)
        svga->overscan_color = svga->pallook[mach64->ovr_clr & 0xff];
    else
        svga->overscan_color = makecol32((mach64->ovr_clr >> 24) & 0xff, (mach64->ovr_clr >> 16) & 0xff, (mach64->ovr_clr >> 8) & 0xff);
}

void
mach64_recalctimings(svga_t *svga)
{
    mach64_t *mach64 = (mach64_t *) svga->priv;

    if (((mach64->crtc_gen_cntl >> 24) & 3) == 3) {
        /* CRTC_INTERLACE_EN (CRTC_GEN_CNTL bit 1): the vertical registers
           count the lines of the whole frame, and each field scans half of
           them -- ATI's 800x600 89 Hz interlaced mode programs V_DISP 257h
           and V_TOTAL 2BCh (Programmer's Guide C-3). */
        const int ilace = !!(mach64->crtc_gen_cntl & 2);

        /* CRTC_PIX_BY_2_EN (bit 5): the CRTC advances two pixels on each
           pixel clock, so a character of 8 pixels takes 4 clocks. */
        svga->char_width = (mach64->crtc_gen_cntl & 0x20) ? 4 : 8;
        svga->interlace  = ilace;
        svga->vtotal     = ((mach64->crtc_v_total_disp & 2047) + 1) >> ilace;
        svga->dispend    = (((mach64->crtc_v_total_disp >> 16) & 2047) + 1) >> ilace;
        svga->htotal     = (mach64->crtc_h_total_disp & 255) + 1;
        svga->hdisp_time = svga->hdisp = ((mach64->crtc_h_total_disp >> 16) & 255) + 1;
        /* CRTC_H_SYNC_STRT (7:0) is in characters; CRTC_H_SYNC_DLY (10:8)
           delays the sync by pixels within that character (RRG 3-22), finer
           than the character counter this blanking runs on. */
        svga->hblankstart              = mach64->crtc_h_sync_strt_wid & 255;
        svga->hblank_end_val           = (svga->hblankstart +
                                         ((mach64->crtc_h_sync_strt_wid >> 16) & 31) - 1) & 63;
        svga->vsyncstart               = ((mach64->crtc_v_sync_strt_wid & 2047) + 1) >> ilace;
        svga->rowoffset                = (mach64->crtc_off_pitch >> 22);
        {
            double freq = ics2595_getclock(svga->clock_gen);

            /* CLOCK_DIV (bits 5:4): divide by 1, 2 or 4 (RRG 3-4). A clock
               entry nothing has programmed is 0 Hz: keep the last timing. */
            if ((mach64->type == MACH64_GX) && (((mach64->clock_cntl >> 4) & 3) < 3))
                freq /= (double) (1 << ((mach64->clock_cntl >> 4) & 3));
            if (freq > 0.0)
                svga->clock = (cpuclock * (double) (1ULL << 32)) / freq;
        }
        /* CRTC_OFFSET is 19:0, in qwords (RRG 3-23). */
        svga->memaddr_latch            = (mach64->crtc_off_pitch & 0xfffff) * 2;
        svga->linedbl = svga->rowcount = 0;
        svga->split                    = 0xffffff;
        svga->vblankstart              = svga->dispend;
        svga->rowcount                 = mach64->crtc_gen_cntl & 1;
        svga->lut_map                  = (mach64->type >= MACH64_VT);
        svga->rowoffset <<= 1;
        svga->attrregs[0x13]          &= ~0x0f;

        if (mach64->type == MACH64_GX)
            ati68860_ramdac_set_render(svga->ramdac, svga);

        svga->packed_4bpp = !!(((mach64->crtc_gen_cntl >> 8) & 7) == BPP_4);

        switch ((mach64->crtc_gen_cntl >> 8) & 7) {
            case BPP_4:
                svga->render = mach64_render_4bpp;
                svga->hdisp <<= 3;
                svga->bpp = 4;
                break;
            case BPP_8:
                if (mach64->type != MACH64_GX)
                    svga->render = svga_render_8bpp_clone_highres;
                svga->hdisp <<= 3;
                svga->rowoffset >>= 1;
                svga->bpp = 8;
                break;
            case BPP_15:
                if (mach64->type != MACH64_GX)
                    svga->render = svga_render_15bpp_highres;
                svga->hdisp <<= 3;
                svga->bpp = 15;
                break;
            case BPP_16:
                if (mach64->type != MACH64_GX)
                    svga->render = svga_render_16bpp_highres;
                svga->hdisp <<= 3;
                svga->bpp = 16;
                break;
            case BPP_24:
                if (mach64->type != MACH64_GX)
                    svga->render = svga_render_24bpp_highres;
                svga->hdisp <<= 3;
                svga->rowoffset = (svga->rowoffset * 3) >> 1;
                svga->bpp = 24;
                break;
            case BPP_32:
                if (mach64->type != MACH64_GX)
                    svga->render = svga_render_32bpp_highres;
                svga->hdisp <<= 3;
                svga->rowoffset <<= 1;
                svga->bpp = 32;
                break;

            default:
                break;
        }

        svga->vram_display_mask = mach64->vram_mask;

        svga->border_override         = 1;
        svga->border_left             = (mach64->ovr_wid_left_right & 0x0f) * 8;
        svga->border_top              = mach64->ovr_wid_top_bottom & 0xff;
        svga->monitor->mon_overscan_x = svga->border_left + ((mach64->ovr_wid_left_right >> 16) & 0x0f) * 8;
        svga->monitor->mon_overscan_y = svga->border_top + ((mach64->ovr_wid_top_bottom >> 16) & 0xff);
    } else {
        svga->vram_display_mask = (mach64->regs[0x36] & 0x01) ? mach64->vram_mask : 0x3ffff;
        svga->lut_map           = 0;
        svga->bpp               = 8;

        /* ATI extended modes: start address bits 16 (ATI30 bit 6) and 17
           (ATI23 bit 4), the 256-colour mode (ATI30 bit 5) and interlace
           (ATI3E bit 1) (VGA Register Guide 5-8, 5-15, 5-27). */
        if (mach64->regs[0x30] & 0x40)
            svga->memaddr_latch |= 0x10000;
        if (mach64->regs[0x23] & 0x10)
            svga->memaddr_latch |= 0x20000;
        if ((mach64->regs[0x30] & 0x20) && ((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1)) &&
            (svga->render != svga_render_blank) && !(svga->gdcreg[5] & 0x40)) {
            svga->map8   = svga->pallook;
            svga->render = svga->lowres ? svga_render_8bpp_lowres : svga_render_8bpp_highres;
        }
        svga->interlace = !!(mach64->regs[0x3e] & 0x02);
        if (svga->interlace)
            svga->dispend >>= 1;
    }

    mach64_update_overscan(mach64);

    /* CRTC_DISPLAY_DIS (CRTC_GEN_CNTL bit 6) holds the blanking signal
       active, whichever CRTC mode is running (RRG 3-20). */
    if (mach64->crtc_gen_cntl & 0x40)
        svga->render = svga_render_blank;
}

/* The VLB card's EEPROM as ATI's INSTALL utility (mach64 driver CD, release
   435) leaves it when it first sets the card up, taken word for word from
   the nvr file it wrote: the ATI88800CX EEPROM data structure (BIOS Kit
   BIO-888GX0-02, appendix B) with a write count of 0, checksum 0BEh in
   word 1 (the bytes of all the words sum to 0), table revision 2 in word
   3, word 9 = 0040h, and no aperture location, monitor, refresh rates or
   CRT tables chosen. Written when the card has no file, instead of an
   erased part: an erased one reads aperture location 4095 MB. */
static const uint16_t mach64_vlb_eeprom_default[256] = {
    [1] = 0x00be,
    [3] = 0x0002,
    [9] = 0x0040,
};

/* Places a piece of the linear aperture, `off` bytes into it.
   CFG_MEM_AP_LOC gives the aperture's location in 4 MB steps, and "with
   8M apertures, bit 4 is ignored" (Register Reference Guide, CONFIG_CNTL):
   an 8 MB aperture sits on 8 MB, whatever that bit says. The card decodes
   32 address lines, so a piece at or past 4 GB -- the second of the 2 x 8 MB
   pair at the top -- is on no address and is not mapped. Taking the field
   as it stood put an 8 MB aperture at FFC00000h through 1003FC000h: the
   memory tables were indexed past 4 GB, and the register window at its
   end, added in 32 bits, wrapped round to 003FC000h over guest RAM. */
static void
mach64_map_aperture(mach64_t *mach64, mem_mapping_t *map, int ap_8m, uint32_t off, uint32_t size)
{
    uint32_t base  = mach64->linear_base & (ap_8m ? ~((8u << 20) - 1) : ~((4u << 20) - 1));
    uint64_t start = (uint64_t) base + off;

    if ((start + size) > 0x100000000ULL) {
        mem_mapping_disable(map);
        return;
    }
    mem_mapping_set_addr(map, (uint32_t) start, size);
}

/* The video BIOS ROM: at the PCI ROM BAR while it is enabled on PCI, at
   C0000 otherwise, and on no address at all while BUS_ROM_DIS (BUS_CNTL
   bit 12, "ROM disabled", RRG 3-2) is set -- how a second card's BIOS gets
   out of the first one's way. BUS_ROM_PAGE (11:8) selects a page of a ROM
   larger than the window; the ROMs here are the window's 32K, so it is
   stored and nothing more. */
static void
mach64_update_rom(mach64_t *mach64)
{
    if (mach64->on_board)
        return;
    if (mach64->bus_cntl & (1u << 12)) {
        mem_mapping_disable(&mach64->bios_rom.mapping);
        return;
    }
    if (mach64->pci) {
        if (mach64->pci_regs[PCI_REG_ROM_BAR_BYTE0] & 0x01) {
            uint32_t biosaddr = ((mach64->pci_regs[0x31] & 0x80) << 8) | (mach64->pci_regs[0x32] << 16) | (mach64->pci_regs[0x33] << 24);

            mach64_log("Mach64 bios_rom enabled at %08x\n", biosaddr);
            mem_mapping_set_addr(&mach64->bios_rom.mapping, biosaddr, 0x8000);
        } else {
            mach64_log("Mach64 bios_rom disabled\n");
            mem_mapping_disable(&mach64->bios_rom.mapping);
        }
    } else
        mem_mapping_set_addr(&mach64->bios_rom.mapping, 0xc0000, 0x8000);
}

/* CFG_MEM_AP_LOC (CONFIG_CNTL 13:4) reads back where the aperture is: in
   4M units, or 16M on the VT and VT2. The I/O and MMIO reads agree. */
static void
mach64_sync_ap_loc(mach64_t *mach64)
{
    const int shift = ((mach64->type == MACH64_VT) || (mach64->type == MACH64_VT2)) ? 24 : 22;

    mach64->config_cntl = (mach64->config_cntl & ~0x3ff0) | (((mach64->linear_base >> shift) << 4) & 0x3ff0);
}

void
mach64_updatemapping(mach64_t *mach64)
{
    svga_t *svga = &mach64->svga;
    xga_t *xga   = (xga_t *) svga->xga;

    if (mach64->pci && !(mach64->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)) {
        mach64_log("Update mapping - PCI disabled\n");
        mem_mapping_disable(&svga->mapping);
        mem_mapping_disable(&mach64->linear_mapping);
        mem_mapping_disable(&mach64->linear_mapping_big_endian);
        mem_mapping_disable(&mach64->mmio_mapping);
        mem_mapping_disable(&mach64->mmio_linear_mapping);
        mem_mapping_disable(&mach64->mmio_linear_mapping_2);
        return;
    }

    switch (svga->gdcreg[6] & 0xc) {
        case 0x0: /*128k at A0000*/
            mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
            /* ATI3D bit 2 pages all 128K at once (VGA Register Guide 5-26). */
            svga->banked_mask = (mach64->regs[0x3d] & 0x04) ? 0x1ffff : 0xffff;
            break;
        case 0x4: /*64k at A0000*/
            mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
            svga->banked_mask = 0xffff;
            if (xga_active && (svga->xga != NULL))
                xga->on = 0;
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

    /*
       The small apertures, with the registers in the 1K at B000:FC00, are
       enabled by CFG_MEM_VGA_AP_EN (CONFIG_CNTL bit 2), in any display mode:
       the BIOS clears video memory through them on a mode set before the
       CRTC is in accelerator mode (VLB BIOS 113-26900-103, 0BDFh).
     */
    if (mach64->config_cntl & 4) {
        mem_mapping_set_handler(&svga->mapping, mach64_read, mach64_readw, mach64_readl, mach64_write, mach64_writew, mach64_writel);
        mem_mapping_set_p(&svga->mapping, mach64);
        mem_mapping_enable(&mach64->mmio_mapping);
    } else {
        mem_mapping_set_handler(&svga->mapping, svga_read, svga_readw, svga_readl, svga_write, svga_writew, svga_writel);
        mem_mapping_set_p(&svga->mapping, svga);
        mem_mapping_disable(&mach64->mmio_mapping);
    }

    if (mach64->linear_base) {
        if (mach64->type == MACH64_GX) {
            /* CFG_MEM_AP_SIZE: 0 = disabled, 1 = 4M, 2 = 8M, 3 reserved (RRG
               3-9). On PCI the BAR places the aperture and 8M it stays unless
               4M is asked for, as before. */
            int ap = mach64->config_cntl & 3;

            if (mach64->pci)
                ap = (ap == 1) ? 1 : 2;
            if ((ap != 1) && (ap != 2)) {
                mem_mapping_disable(&mach64->linear_mapping);
                mem_mapping_disable(&mach64->mmio_linear_mapping);
            } else {
                uint32_t size = (ap == 2) ? (8 << 20) : (4 << 20);

                /* Video memory is at the offset into the aperture, wherever
                   the aperture sits; the registers are its top 1K, "at
                   +0x3FFC00 for a 4M aperture or 0x7FFC00 for an 8M" (RRG
                   1-3). */
                svga->decode_mask = size - 1;
                mach64_log("Mach64 linear aperture=%08x size %i MB, VGAAP=%x.\n", mach64->linear_base, size >> 20, mach64->config_cntl & 4);
                mach64_map_aperture(mach64, &mach64->linear_mapping, ap == 2, 0, size - 0x400);
                mach64_map_aperture(mach64, &mach64->mmio_linear_mapping, ap == 2, size - 0x400, 0x400);
            }
        } else {
            /*2*8 MB aperture*/
            mach64_map_aperture(mach64, &mach64->linear_mapping, 1, 0, (8 << 20) - 4096);
            mach64_map_aperture(mach64, &mach64->mmio_linear_mapping, 1, (8 << 20) - 4096, 4096);
            mach64_map_aperture(mach64, &mach64->linear_mapping_big_endian, 1, 8 << 20, (8 << 20) - 0x1000);
            mach64_map_aperture(mach64, &mach64->mmio_linear_mapping_2, 1, (16 << 20) - 0x1000, 0x1000);
        }
    } else {
        mem_mapping_disable(&mach64->linear_mapping);
        mem_mapping_disable(&mach64->mmio_linear_mapping);
        mem_mapping_disable(&mach64->mmio_linear_mapping_2);
        mem_mapping_disable(&mach64->linear_mapping_big_endian);
    }
}

/* CRTC_INT_CNTL (RRG 3-21): each interrupt's status bit sits one above its
   enable bit, and writing 1 to a status bit acknowledges it. The GX has the
   vertical blank (enable 1, status 2) and the vertical line (3, 4); the CT
   and later add the snapshot, I2C, capture, overlay and one-shot ones. */
static uint32_t
mach64_crtc_int_en(const mach64_t *mach64)
{
    return (mach64->type == MACH64_GX) ? 0x0000000a : 0x0055028a;
}

/* The line of the frame the CRTC is on: in an interlaced mode each field
   scans every other one (CRTC_CRNT_VLINE, RRG 3-25). */
static uint32_t
mach64_crnt_vline(const mach64_t *mach64)
{
    const svga_t *svga = &mach64->svga;

    if (svga->interlace)
        return ((svga->vc << 1) | (svga->oddeven & 1)) & 0x7ff;
    return svga->vc & 0x7ff;
}

static uint32_t
mach64_crtc_int_cntl_read(const mach64_t *mach64)
{
    uint32_t ret = mach64->crtc_int_cntl & ~0x61;

    if (!mach64->svga.dispon)
        ret |= 0x01; /* CRTC_VBLANK */
    if (mach64_crnt_vline(mach64) & 1)
        ret |= 0x20; /* CRTC_VLINE_SYNC: odd scan line */
    if (mach64->svga.interlace && (mach64->svga.oddeven & 1))
        ret |= 0x40; /* CRTC_FRAME: odd frame */
    return ret;
}

static void
mach64_update_irqs(mach64_t *mach64)
{
    const uint32_t crtc_en = mach64->crtc_int_cntl & mach64_crtc_int_en(mach64);
    /* BUS_CNTL: FIFO error 20/21, host data error 22/23 (RRG 3-2). */
    const uint32_t bus_en  = mach64->bus_cntl & 0x00500000;
    const int      pending = !!((mach64->crtc_int_cntl & (crtc_en << 1)) || (mach64->bus_cntl & (bus_en << 1)));

    if (mach64->pci) {
        if (pending)
            pci_set_irq(mach64->pci_slot, PCI_INTA, &mach64->irq_state);
        else
            pci_clear_irq(mach64->pci_slot, PCI_INTA, &mach64->irq_state);
        return;
    }

    /* ISA and VLB boards take their interrupt from a jumper: 2, 3, 5 or 10
       on ISA, 2, 3 or 5 on the VLB (mach64 User's Guide, Reference). */
    if (!mach64->isa_irq || (pending == mach64->isa_irq_raised))
        return;
    if (pending)
        picint(1 << mach64->isa_irq);
    else
        picintc(1 << mach64->isa_irq);
    mach64->isa_irq_raised = pending;
}

/* CRTC_VLINE_INT (CRTC_INT_CNTL bit 4) sets when the CRTC reaches the line
   in CRTC_VLINE (RRG 3-25). */
static void
mach64_line_callback(svga_t *svga)
{
    mach64_t *mach64 = (mach64_t *) svga->priv;

    if (mach64_crnt_vline(mach64) != (mach64->crtc_vline & 0x7ff))
        return;
    mach64->crtc_int_cntl |= 0x10;
    mach64_update_irqs(mach64);
}


#define PLL_REF_DIV   0x2
#define VCLK_POST_DIV 0x6
#define VCLK0_FB_DIV  0x7

static void
pll_write(mach64_t *mach64, uint32_t addr, uint8_t val)
{
    switch (addr & 3) {
        case 0: /*Clock sel*/
            break;
        case 1: /*Addr*/
            mach64->pll_addr = (val >> 2) & 0xf;
            break;
        case 2: /*Data*/
            mach64->pll_regs[mach64->pll_addr] = val;
            mach64_log("pll_write %02x,%02x\n", mach64->pll_addr, val);

            for (uint8_t c = 0; c < 4; c++) {
                double m = (double) mach64->pll_regs[PLL_REF_DIV];
                double n = (double) mach64->pll_regs[VCLK0_FB_DIV + c];
                double r = 14318184.0;
                double p = (double) (1 << ((mach64->pll_regs[VCLK_POST_DIV] >> (c * 2)) & 3));

                mach64_log("PLLfreq %i = %g  %g m=%02x n=%02x p=%02x\n", c, (2.0 * r * n) / (m * p), p, mach64->pll_regs[PLL_REF_DIV], mach64->pll_regs[VCLK0_FB_DIV + c], mach64->pll_regs[VCLK_POST_DIV]);
                mach64->pll_freq[c] = (2.0 * r * n) / (m * p);
                mach64_log(" %g\n", mach64->pll_freq[c]);
            }
            break;

        default:
            break;
    }
}



/* CRTC_VBLANK_INT (CRTC_INT_CNTL bit 2) sets as the display ends. */
static void
mach64gx_vblank_start(svga_t *svga)
{
    mach64_t *mach64 = (mach64_t *) svga->priv;

    mach64->crtc_int_cntl |= 4;
    mach64_update_irqs(mach64);
    mach64_update_overscan(mach64);
}

#define OVERLAY_EN (1 << 30)
static void
mach64_vblank_start(svga_t *svga)
{
    mach64_t *mach64          = (mach64_t *) svga->priv;
    int       overlay_cmp_mix = (mach64->overlay_key_cntl >> 8) & 0xf;

    mach64->crtc_int_cntl |= 4;
    mach64_update_irqs(mach64);
    mach64_update_overscan(mach64);

    svga->overlay.x = (mach64->overlay_y_x_start >> 16) & 0x7ff;
    svga->overlay.y = mach64->overlay_y_x_start & 0x7ff;

    svga->overlay.cur_xsize = ((mach64->overlay_y_x_end >> 16) & 0x7ff) - svga->overlay.x;
    svga->overlay.cur_ysize = (mach64->overlay_y_x_end & 0x7ff) - svga->overlay.y;

    if (mach64->type >= MACH64_VT3) {
        svga->overlay.addr  = mach64->scaler_buf_offset[0] & 0x3fffff;
        svga->overlay.pitch = mach64->scaler_buf_pitch & 0xfff;
    } else {
        svga->overlay.addr  = mach64->buf_offset[0] & 0x3ffff8;
        svga->overlay.pitch = mach64->buf_pitch[0] & 0xfff;
    }

    svga->overlay.ena = (mach64->overlay_scale_cntl & OVERLAY_EN) && (overlay_cmp_mix != 1);

    mach64->overlay_v_acc   = 0;
    mach64->scaler_update   = 1;
    mach64->overlay_uv_addr = svga->overlay.addr;
    mach64->overlay_cur_y   = 0;
    mach64->overlay_base    = svga->overlay.addr;
}

uint8_t
mach64_ext_readb(uint32_t addr, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    uint8_t   ret    = 0xff;

    svga_t   *svga   = &mach64->svga;

    if ((addr >= 0x000a0000) && (addr < 0x000bf800))
        ret = svga->mapping.read_b(addr, svga->mapping.priv);
    else if ((addr < 0x000a0000) || ((addr >= 0x000bf800) && (addr <= 0x000bffff)) || (addr >= 0x00100000)) {
        if (!(addr & 0x400)) {
            mach64_log("mach64_ext_readb: addr=%04x\n", addr);
            switch (addr & 0x3ff) {
                case 0x00 ... 0x03:
                    READ8(addr, mach64->overlay_y_x_start);
                    break;
                case 0x04 ... 0x07:
                    READ8(addr, mach64->overlay_y_x_end);
                    break;
                case 0x08 ... 0x0b:
                    READ8(addr, mach64->overlay_video_key_clr);
                    break;
                case 0x0c ... 0x0f:
                    READ8(addr, mach64->overlay_video_key_msk);
                    break;
                case 0x10 ... 0x13:
                    READ8(addr, mach64->overlay_graphics_key_clr);
                    break;
                case 0x14 ... 0x17:
                    READ8(addr, mach64->overlay_graphics_key_msk);
                    break;
                case 0x18 ... 0x1b:
                    READ8(addr, mach64->overlay_key_cntl);
                    break;
                case 0x20 ... 0x23:
                    READ8(addr, mach64->overlay_scale_inc);
                    break;
                case 0x24 ... 0x27:
                    READ8(addr, mach64->overlay_scale_cntl);
                    break;
                case 0x28 ... 0x2b:
                    READ8(addr, mach64->scaler_height_width);
                    break;
                case 0x34 ... 0x37:
                    READ8(addr, mach64->scaler_buf_offset[0]);
                    break;
                case 0x38 ... 0x3b: // optimise
                    READ8(addr, mach64->scaler_buf_offset[1]);
                    break;
                case 0x3c ... 0x3f:
                    READ8(addr, mach64->scaler_buf_pitch);
                    break;
                case 0x58 ... 0x5b:
                    READ8(addr, mach64->overlay_exclusive_horz);
                    break;
                case 0x5c ... 0x5f:
                    READ8(addr, mach64->overlay_exclusive_vert);
                    break;
                case 0x4a:
                    ret = mach64->scaler_format;
                    break;
                case 0x4b:
                    ret = mach64->scaler_yuv_aper;
                    break;
                default:
                    ret = 0xff;
                    break;
            }
        } else {
            switch (addr & 0x3ff) {
                case 0x00 ... 0x03:
                    READ8(addr, mach64->crtc_h_total_disp);
                    break;
                case 0x04 ... 0x07:
                    READ8(addr, mach64->crtc_h_sync_strt_wid);
                    break;
                case 0x08 ... 0x0b:
                    READ8(addr, mach64->crtc_v_total_disp);
                    break;
                case 0x0c ... 0x0f:
                    READ8(addr, mach64->crtc_v_sync_strt_wid);
                    break;
                case 0x10 ... 0x11:
                    READ8(addr, mach64->crtc_vline); /* CRTC_VLINE, 10:0 RW (RRG 3-25) */
                    break;
                case 0x12 ... 0x13: {
                    uint32_t vline = mach64_crnt_vline(mach64);

                    READ8(addr - 2, vline);
                    break;
                }
                case 0x14 ... 0x17:
                    READ8(addr, mach64->crtc_off_pitch);
                    break;
                case 0x18 ... 0x1b: {
                    uint32_t cntl = mach64_crtc_int_cntl_read(mach64);

                    READ8(addr, cntl);
                    break;
                }
                case 0x1c ... 0x1f:
                    READ8(addr, mach64->crtc_gen_cntl);
                    break;
                case 0x20 ... 0x23:
                    READ8(addr, mach64->dsp_config);
                    break;
                case 0x24 ... 0x27:
                    READ8(addr, mach64->dsp_on_off);
                    break;
                case 0x40 ... 0x43:
                    READ8(addr, mach64->ovr_clr);
                    break;
                case 0x44 ... 0x47:
                    READ8(addr, mach64->ovr_wid_left_right);
                    break;
                case 0x48 ... 0x4b:
                    READ8(addr, mach64->ovr_wid_top_bottom);
                    break;
                case 0x4c ... 0x4f:
                    READ8(addr, mach64->vga_dsp_config);
                    break;
                case 0x50 ... 0x53:
                    READ8(addr, mach64->vga_dsp_on_off);
                    break;
                case 0x60 ... 0x63:
                    READ8(addr, mach64->cur_clr0);
                    break;
                case 0x64 ... 0x67:
                    READ8(addr, mach64->cur_clr1);
                    break;
                case 0x68 ... 0x6b:
                    READ8(addr, mach64->cur_offset);
                    break;
                case 0x6c ... 0x6f:
                    READ8(addr, mach64->cur_horz_vert_posn);
                    break;
                case 0x70 ... 0x73:
                    READ8(addr, mach64->cur_horz_vert_off);
                    break;
                case 0x79:
                    ret = 0x30;
                    if (mach64->type == MACH64_VT3)
                    {
                        ret = (((i2c_gpio_get_scl(mach64->i2c_tv) << 3)) & (~(mach64->gp_io >> 24) & 0xFF)) | ((mach64->gp_io >> 8) & (mach64->gp_io >> 24) & 0xFF);
                        ret &= ~((1 << 4) | (1 << 5));
                        ret |= (i2c_gpio_get_scl(mach64->i2c) << 5) | (i2c_gpio_get_sda(mach64->i2c) << 4);
                        break;
                    }
                    break;
                case 0x78:
                    if (mach64->type == MACH64_VT3)
                    {
                        ret = ((i2c_gpio_get_sda(mach64->i2c_tv) << 4) & (~(mach64->gp_io >> 16) & 0xFF)) | ((mach64->gp_io & 0xFF) & ((mach64->gp_io >> 16) & 0xFF));
                        break;
                    }
                case 0x7A ... 0x7B:
                    if (mach64->type == MACH64_VT3)
                        READ8(addr, mach64->gp_io);
                    //                pclog("GPIO READ 0x%X, 0x00\n", addr & 0x3ff);
                    break;
                case 0x80 ... 0x83:
                    READ8(addr, mach64->scratch_reg0);
                    break;
                case 0x84 ... 0x87:
                    READ8(addr, mach64->scratch_reg1);
                    break;
                case 0x90 ... 0x93:
                    READ8(addr, mach64->clock_cntl);
                    break;
                case 0xb0 ... 0xb3:
                    READ8(addr, mach64->mem_cntl);
                    break;
                case 0xb4:
                    ret = (mach64->bank_w[0] >> 15);
                    break;
                case 0xb6:
                    ret = (mach64->bank_w[1] >> 15);
                    break;
                case 0xb8:
                    ret = (mach64->bank_r[0] >> 15);
                    break;
                case 0xba:
                    ret = (mach64->bank_r[1] >> 15); /* MEM_VGA_RP_SEL's RPS1 */
                    break;
                case 0xc0 ... 0xc3:
                    if (mach64->type == MACH64_GX)
                        ret = ati68860_ramdac_in((addr & 3) | ((mach64->dac_cntl & 3) << 2), 0, mach64->svga.ramdac, &mach64->svga);
                    else {
                        uint16_t port_list[4] = { 0x3c8, 0x3c9, 0x3c6, 0x3c7 };
                        ret = svga_in(port_list[addr & 3], svga);
                    }
                    break;
                case 0xc4 ... 0xc6: // optimise
                    READ8(addr, mach64->dac_cntl);
                    break;
                case 0xc7:
                    READ8(addr, mach64->dac_cntl);
                    if (mach64->type >= MACH64_CT && mach64->type != MACH64_VT3) {
                        ret &= 0xf9;
                        if (i2c_gpio_get_scl(mach64->i2c))
                            ret |= 0x04;
                        if (i2c_gpio_get_sda(mach64->i2c))
                            ret |= 0x02;
                    }
                    break;
                case 0xa0 ... 0xa3:
                    READ8(addr, mach64->bus_cntl);
                    break;
                case 0xd0 ... 0xd3:
                    READ8(addr, mach64->gen_test_cntl);
                    break;
                case 0xe8 ... 0xeb:
                    /* CONFIG_STAT1: nothing set on this board (CFG_PCI_DAC_CFG 0). */
                    ret = 0x00;
                    break;
                case 0xdc ... 0xdf:
                    /* "All except CONFIG_CNTL have memory mapped register
                       aliases" on the GX (RRG 1-3): reached through I/O only. */
                    if (mach64->type == MACH64_GX)
                        break;
                    mach64_sync_ap_loc(mach64);
                    READ8(addr, mach64->config_cntl);
                    break;
                case 0xe0 ... 0xe3:
                    READ8(addr, mach64->config_chip_id);
                    break;
                case 0xe4 ... 0xe7:
                    READ8(addr, mach64->config_stat0);
                    break;
                case 0x100 ... 0x103:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dst_off_pitch);
                    break;
                /* DST_Y_X: Y in 14:0, X in 28:16 (RRG 3-55). */
                case 0x104 ... 0x105: /* DST_X */
                case 0x11c ... 0x11d: /* DST_X_WIDTH's X */
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr + 2, mach64->dst_y_x);
                    break;
                case 0x108 ... 0x109: /* DST_Y */
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dst_y_x);
                    break;
                case 0x10c ... 0x10f:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dst_y_x);
                    break;
                case 0x2e8 ... 0x2eb:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr ^ 2, mach64->dst_y_x);
                    break;
                case 0x2ec ... 0x2ef:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr ^ 2, mach64->dst_height_width);
                    break;
                case 0x110 ... 0x111: // optimise
                    addr += 2;
                    fallthrough;
                    // probably can be optimised but might be a little slower?
                case 0x114 ... 0x115:
                case 0x118 ... 0x119:
                case 0x11a ... 0x11b:
                case 0x11e ... 0x11f:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dst_height_width);
                    break;
                case 0x120 ... 0x123:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dst_bres_lnth);
                    break;
                case 0x124 ... 0x127:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dst_bres_err);
                    break;
                case 0x128 ... 0x12b:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dst_bres_inc);
                    break;
                case 0x12c ... 0x12f:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dst_bres_dec);
                    break;
                case 0x130 ... 0x133:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dst_cntl);
                    break;
                case 0x180 ... 0x183:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->src_off_pitch);
                    break;
                case 0x184 ... 0x185: /* SRC_X: SRC_Y_X 28:16 (RRG 3-99) */
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr + 2, mach64->src_y_x);
                    break;
                case 0x188 ... 0x189: /* SRC_Y: 14:0 */
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->src_y_x);
                    break;
                case 0x18c ... 0x18f:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->src_y_x);
                    break;
                case 0x190 ... 0x191:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr + 2, mach64->src_height1_width1);
                    break;
                case 0x194 ... 0x195:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->src_height1_width1);
                    break;
                case 0x198 ... 0x19b:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->src_height1_width1);
                    break;
                case 0x19c ... 0x19d: /* SRC_X_START: SRC_Y_X_START 28:16 (RRG 3-100) */
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr + 2, mach64->src_y_x_start);
                    break;
                case 0x1a0 ... 0x1a1: /* SRC_Y_START: SRC_Y_X_START 14:0 */
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->src_y_x_start);
                    break;
                case 0x1a4 ... 0x1a7:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->src_y_x_start);
                    break;
                case 0x1a8 ... 0x1a9:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr + 2, mach64->src_height2_width2);
                    break;
                case 0x1ac ... 0x1ad:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->src_height2_width2);
                    break;
                case 0x1b0 ... 0x1b3:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->src_height2_width2);
                    break;
                case 0x1b4 ... 0x1b7:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->src_cntl);
                    break;
                case 0x240 ... 0x243:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->host_cntl);
                    break;
                case 0x280 ... 0x283:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->pat_reg0);
                    break;
                case 0x284 ... 0x287:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->pat_reg1);
                    break;
                case 0x288 ... 0x28b:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->pat_cntl);
                    break;
                case 0x2a0 ... 0x2a1:
                case 0x2a8 ... 0x2a9: // optimise
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->sc_left_right);
                    break;
                case 0x2a4 ... 0x2a5:
                    addr += 2;
                    fallthrough;
                case 0x2aa ... 0x2ab:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->sc_left_right);
                    break;
                case 0x2ac ... 0x2ad:
                case 0x2b4 ... 0x2b5:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->sc_top_bottom);
                    break;
                case 0x2b0 ... 0x2b1:
                    addr += 2;
                    fallthrough;
                case 0x2b6 ... 0x2b7:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->sc_top_bottom);
                    break;
                case 0x2c0 ... 0x2c3:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dp_bkgd_clr);
                    break;
                case 0x2c4 ... 0x2c7:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dp_frgd_clr);
                    break;
                case 0x2c8 ... 0x2cb:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->write_mask);
                    break;
                case 0x2cc ... 0x2cf:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->chain_mask);
                    break;
                case 0x2d0 ... 0x2d3:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dp_pix_width);
                    break;
                case 0x2d4 ... 0x2d7:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dp_mix);
                    break;
                case 0x2d8 ... 0x2db:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dp_src);
                    break;
                case 0x300 ... 0x303:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->clr_cmp_clr);
                    break;
                case 0x304 ... 0x307:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->clr_cmp_mask);
                    break;
                case 0x308 ... 0x30b:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->clr_cmp_cntl);
                    break;
                case 0x310 ... 0x311:
                    /* FIFO_STAT: a '1' for each filled entry of the 16-deep
                       command FIFO (RRG 3-56). */
                    if (!mach64->blitter_busy)
                        mach64_wake_fifo_thread(mach64);
                    {
                        int      n    = FIFO_ENTRIES;
                        uint32_t bits = (n >= 16) ? 0xffff : ((1u << n) - 1);

                        ret = (addr & 1) ? (bits >> 8) : (bits & 0xff);
                    }
                    break;
                case 0x320 ... 0x323:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->context_mask);
                    break;
                case 0x330 ... 0x331:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr, mach64->dst_cntl);
                    break;
                case 0x332:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr - 2, mach64->src_cntl);
                    break;
                case 0x333:
                    mach64_wait_fifo_idle(mach64);
                    READ8(addr - 3, mach64->pat_cntl);
                    break;
                case 0x338:
                    /* GUI_ACTIVE: the FIFO, or an operation still running --
                       one waiting on host data included (RRG 3-61). */
                    if (!mach64->blitter_busy)
                        mach64_wake_fifo_thread(mach64);
                    ret = (!FIFO_EMPTY || mach64->blitter_busy || mach64->accel.busy) ? 1 : 0;
                    break;
                case 0x339: {
                    /* DSTX/DSTY against the scissors, bits 8-11. */
                    int x  = ((int) (mach64->dst_y_x << 3)) >> 19;
                    int y  = ((int) (mach64->dst_y_x << 17)) >> 17;
                    int sl = ((int) (mach64->sc_left_right << 19)) >> 19;
                    int sr = ((int) (mach64->sc_left_right << 3)) >> 19;
                    int st = ((int) (mach64->sc_top_bottom << 17)) >> 17;
                    int sb = ((int) (mach64->sc_top_bottom << 1)) >> 17;

                    mach64_wait_fifo_idle(mach64);
                    ret = ((x < sl) ? 1 : 0) | ((x > sr) ? 2 : 0) | ((y < st) ? 4 : 0) | ((y > sb) ? 8 : 0);
                    break;
                }
                case 0x33a:
                    /* The CT's FIFO count; nothing there on the GX (RRG 3-61). */
                    ret = (mach64->type >= MACH64_CT) ? (FIFO_EMPTY ? 32 : 31) : 0;
                    break;
                default:
                    ret = 0;
                    break;
            }
        }
        if ((addr & 0x3fc) != 0x018)
            mach64_log("mach64_ext_readb : addr %08X ret %02X\n", addr, ret);
    }
    return ret;
}
uint16_t
mach64_ext_readw(uint32_t addr, void *priv)
{
    const mach64_t *mach64 = (mach64_t *) priv;
    uint16_t        ret    = 0xffff;

    const svga_t   *svga   = &mach64->svga;

    if ((addr >= 0x000a0000) && (addr < 0x000bf800))
        ret = svga->mapping.read_w(addr, svga->mapping.priv);
    else if ((addr < 0x000a0000) || ((addr >= 0x000bf800) && (addr <= 0x000bffff)) || (addr >= 0x00100000)) {
        if (!(addr & 0x400)) {
            mach64_log("mach64_ext_readw: addr=%04x\n", addr);
            ret = mach64_ext_readb(addr, priv);
            ret |= mach64_ext_readb(addr + 1, priv) << 8;
        } else // optimise
            switch (addr & 0x3fe) {
            case 0xb4: case 0xb6:
                    ret = (mach64->bank_w[(addr & 2) >> 1] >> 15);
                    break;
            case 0xb8: case 0xba:
                    ret = (mach64->bank_r[(addr & 2) >> 1] >> 15);
                    break;
            default:
                    ret = mach64_ext_readb(addr, priv);
                    ret |= mach64_ext_readb(addr + 1, priv) << 8;
                    break;
            }
        if ((addr & 0x3fc) != 0x018)
            mach64_log("mach64_ext_readw : addr %08X ret %04X\n", addr, ret);
    }
    return ret;
}
uint32_t
mach64_ext_readl(uint32_t addr, void *priv)
{
    const mach64_t *mach64 = (mach64_t *) priv;
    uint32_t        ret    = 0xffffffff;

    const svga_t   *svga   = &mach64->svga;

    if ((addr >= 0x000a0000) && (addr < 0x000bf800))
        ret = svga->mapping.read_l(addr, svga->mapping.priv);
    else if ((addr < 0x000a0000) || ((addr >= 0x000bf800) && (addr <= 0x000bffff)) || (addr >= 0x00100000)) {
        if (!(addr & 0x400)) {
            mach64_log("mach64_ext_readl: addr=%04x\n", addr);
            ret = mach64_ext_readw(addr, priv);
            ret |= mach64_ext_readw(addr + 2, priv) << 16;
        } else
            switch (addr & 0x3fc) {
            case 0x18:
                    ret = mach64_crtc_int_cntl_read(mach64);
                    break;
            case 0xb4:
                    ret = (mach64->bank_w[0] >> 15) | ((mach64->bank_w[1] >> 15) << 16);
                    break;
            case 0xb8:
                    ret = (mach64->bank_r[0] >> 15) | ((mach64->bank_r[1] >> 15) << 16);
                    break;
            default:
                    ret = mach64_ext_readw(addr, priv);
                    ret |= mach64_ext_readw(addr + 2, priv) << 16;
                    break;
            }
        if ((addr & 0x3fc) != 0x018)
            mach64_log("mach64_ext_readl : addr %08X ret %08X\n", addr, ret);
    }
    return ret;
}

void
mach64_ext_writeb(uint32_t addr, uint8_t val, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    svga_t   *svga   = &mach64->svga;


    if ((addr >= 0x000a0000) && (addr < 0x000bf800))
        svga->mapping.write_b(addr, val, svga->mapping.priv);
    else if ((addr < 0x000a0000) || ((addr >= 0x000bf800) && (addr <= 0x000bffff)) || (addr >= 0x00100000)) {
        mach64_log("mach64_ext_writeb : addr %08X val %02X\n", addr, val);

        if (!(addr & 0x400)) {
            switch (addr & 0x3ff) {
                case 0x00 ... 0x03:
                    WRITE8(addr, mach64->overlay_y_x_start, val);
                    break;
                case 0x04 ... 0x07:
                    WRITE8(addr, mach64->overlay_y_x_end, val);
                    break;
                case 0x08 ... 0x0b:
                    WRITE8(addr, mach64->overlay_video_key_clr, val);
                    break;
                case 0x0c ... 0x0f:
                    WRITE8(addr, mach64->overlay_video_key_msk, val);
                    break;
                case 0x10 ... 0x13:
                    WRITE8(addr, mach64->overlay_graphics_key_clr, val);
                    break;
                case 0x14 ... 0x17:
                    WRITE8(addr, mach64->overlay_graphics_key_msk, val);
                    break;
                case 0x18 ... 0x1b:
                    WRITE8(addr, mach64->overlay_key_cntl, val);
                    break;
                case 0x20 ... 0x23:
                    WRITE8(addr, mach64->overlay_scale_inc, val);
                    break;
                case 0x24 ... 0x27:
                    WRITE8(addr, mach64->overlay_scale_cntl, val);
                    break;
                case 0x28 ... 0x2b:
                    WRITE8(addr, mach64->scaler_height_width, val);
                    break;
                case 0x34 ... 0x37:
                    WRITE8(addr, mach64->scaler_buf_offset[0], val);
                    break;
                case 0x38 ... 0x3b: // optimise
                    WRITE8(addr, mach64->scaler_buf_offset[1], val);
                    break;
                case 0x3c ... 0x3f:
                    WRITE8(addr, mach64->scaler_buf_pitch, val);
                    break;
                case 0x4a:
                    mach64->scaler_format = val & 0xf;
                    break;
                case 0x4b:
                    mach64->scaler_yuv_aper = val;
                    break;
                case 0x58 ... 0x5b:
                    WRITE8(addr, mach64->overlay_exclusive_horz, val);
                    break;
                case 0x5c ... 0x5f:
                    WRITE8(addr, mach64->overlay_exclusive_vert, val);
                    break;
                case 0x80 ... 0x83:
                    WRITE8(addr, mach64->buf_offset[0], val);
                    break;
                case 0x8c ... 0x8f:
                    WRITE8(addr, mach64->buf_pitch[0], val);
                    break;
                case 0x98 ... 0x9b:
                    WRITE8(addr, mach64->buf_offset[1], val);
                    break;
                case 0xa4 ... 0xa7:
                    WRITE8(addr, mach64->buf_pitch[1], val);
                    break;
                default:
                    break;
            }

            mach64_log("mach64_ext_writeb: addr=%04x val=%02x\n", addr, val);
        } else if (addr & 0x300) {
            mach64_queue(mach64, addr & 0x3ff, val, FIFO_WRITE_BYTE);
        } else {
            mach64_log("mach64_ext_writeb: addr=%04x val=%02x\n", addr & 0x3ff, val);
            switch (addr & 0x3ff) {
                case 0x00 ... 0x03:
                    WRITE8(addr, mach64->crtc_h_total_disp, val);
                    svga_recalctimings(&mach64->svga);
                    svga->fullchange = svga->monitor->mon_changeframecount;
                    break;
                case 0x04 ... 0x07:
                    WRITE8(addr, mach64->crtc_h_sync_strt_wid, val);
                    svga_recalctimings(&mach64->svga);
                    svga->fullchange = svga->monitor->mon_changeframecount;
                    break;
                case 0x08 ... 0x0b:
                    WRITE8(addr, mach64->crtc_v_total_disp, val);
                    svga_recalctimings(&mach64->svga);
                    svga->fullchange = svga->monitor->mon_changeframecount;
                    break;
                case 0x0c ... 0x0f:
                    WRITE8(addr, mach64->crtc_v_sync_strt_wid, val);
                    svga_recalctimings(&mach64->svga);
                    svga->fullchange = svga->monitor->mon_changeframecount;
                    break;
                case 0x10 ... 0x11:
                    WRITE8(addr, mach64->crtc_vline, val); /* CRTC_VLINE */
                    mach64->crtc_vline &= 0x7ff;
                    break;
                case 0x14 ... 0x17:
                    WRITE8(addr, mach64->crtc_off_pitch, val);
                    svga_recalctimings(&mach64->svga);
                    svga->fullchange = svga->monitor->mon_changeframecount;
                    break;
                case 0x18 ... 0x1b: {
                    const uint32_t en   = mach64_crtc_int_en(mach64);
                    const uint32_t lane = 0xffu << ((addr & 3) * 8);
                    const uint32_t v    = (uint32_t) val << ((addr & 3) * 8);

                    mach64->crtc_int_cntl = (mach64->crtc_int_cntl & ~(en & lane)) | (v & en);
                    mach64->crtc_int_cntl &= ~(v & (en << 1)); /* the acknowledges */
                    mach64_update_irqs(mach64);
                    break;
                }
                case 0x1c ... 0x1f:
                    WRITE8(addr, mach64->crtc_gen_cntl, val);
                    if (((mach64->crtc_gen_cntl >> 24) & 3) == 3)
                        svga->fb_only = 1;
                    else
                        svga->fb_only = 0;
                    svga->dpms = !!(mach64->crtc_gen_cntl & 0x0c);
                    svga_recalctimings(&mach64->svga);
                    svga->fullchange = svga->monitor->mon_changeframecount;
                    break;
                case 0x20 ... 0x23:
                    WRITE8(addr, mach64->dsp_config, val);
                    break;
                case 0x24 ... 0x27:
                    WRITE8(addr, mach64->dsp_on_off, val);
                    break;
                case 0x4c ... 0x4f:
                    WRITE8(addr, mach64->vga_dsp_config, val);
                    break;
                case 0x50 ... 0x53:
                    WRITE8(addr, mach64->vga_dsp_on_off, val);
                    break;
                case 0x40 ... 0x43:
                    WRITE8(addr, mach64->ovr_clr, val);
                    mach64_update_overscan(mach64);
                    break;
                case 0x44 ... 0x47:
                    WRITE8(addr, mach64->ovr_wid_left_right, val);
                    svga_recalctimings(svga);
                    break;
                case 0x48 ... 0x4b:
                    WRITE8(addr, mach64->ovr_wid_top_bottom, val);
                    svga_recalctimings(svga);
                    break;
                case 0x60 ... 0x63:
                    WRITE8(addr, mach64->cur_clr0, val);
                    break;
                case 0x64 ... 0x67:
                    WRITE8(addr, mach64->cur_clr1, val);
                    break;
                case 0x68 ... 0x6b:
                    WRITE8(addr, mach64->cur_offset, val);
                    if (mach64->type == MACH64_GX)
                        svga->dac_hwcursor.addr = (mach64->cur_offset & 0xfffff) << 3;
                    else
                        svga->hwcursor.addr = (mach64->cur_offset & 0xfffff) << 3;
                    break;
                case 0x6c ... 0x6f:
                    WRITE8(addr, mach64->cur_horz_vert_posn, val);
                    if (mach64->type == MACH64_GX) {
                        svga->dac_hwcursor.x = mach64->cur_horz_vert_posn & 0x7ff;
                        svga->dac_hwcursor.y = (mach64->cur_horz_vert_posn >> 16) & 0x7ff;
                    } else {
                        svga->hwcursor.x = mach64->cur_horz_vert_posn & 0x7ff;
                        svga->hwcursor.y = (mach64->cur_horz_vert_posn >> 16) & 0x7ff;
                    }
                    break;
                case 0x70 ... 0x73:
                    WRITE8(addr, mach64->cur_horz_vert_off, val);
                    if (mach64->type == MACH64_GX) {
                        svga->dac_hwcursor.xoff = mach64->cur_horz_vert_off & 0x3f;
                        svga->dac_hwcursor.yoff = (mach64->cur_horz_vert_off >> 16) & 0x3f;
                    } else {
                        svga->hwcursor.xoff = mach64->cur_horz_vert_off & 0x3f;
                        svga->hwcursor.yoff = (mach64->cur_horz_vert_off >> 16) & 0x3f;
                    }
                    break;
                case 0x78 ... 0x7b:
                    if (mach64->type == MACH64_VT3) {
                        WRITE8(addr, mach64->gp_io, val);
                        {
                            i2c_gpio_set(mach64->i2c_tv, !!(mach64->gp_io & (1 << 11)) || !(mach64->gp_io & (1 << (11 + 16))), !!(mach64->gp_io & (1 << 4)) || !(mach64->gp_io & (1 << (4 + 16))));
                            i2c_gpio_set(mach64->i2c, !!(mach64->gp_io & (1 << 13)) || !(mach64->gp_io & (1 << (13 + 16))), !!(mach64->gp_io & (1 << 12)) || !(mach64->gp_io & (1 << (12 + 16))));
                        }
                    }
                    break;
                case 0x80 ... 0x83:
                    WRITE8(addr, mach64->scratch_reg0, val);
                    break;
                case 0x84 ... 0x87:
                    WRITE8(addr, mach64->scratch_reg1, val);
                    break;
                case 0x90 ... 0x93:
                    WRITE8(addr, mach64->clock_cntl, val);
                    /* CLOCK_SEL, CLOCK_DIV and CLOCK_STROBE are all byte 0
                       (RRG 3-4): the other lanes would select clock 0. */
                    if (mach64->type == MACH64_GX) {
                        if ((addr & 3) == 0)
                            ics2595_write(svga->clock_gen, val & 0x40, val & 0xf);
                    } else {
                        pll_write(mach64, addr, val);
                        ics2595_setclock(svga->clock_gen, mach64->pll_freq[mach64->clock_cntl & 3]);
                    }
                    svga_recalctimings(&mach64->svga);
                    break;
                case 0xb0 ... 0xb3:
                    WRITE8(addr, mach64->mem_cntl, val);
                    break;
                    // optimise
                case 0xb4:
                    mach64->bank_w[0] = val << 15; // *32768
                    mach64_log("mach64 : write bank A0000-A7FFF set to %08X\n", mach64->bank_w[0]);
                    break;
                case 0xb6:
                    mach64->bank_w[1] = val << 15; // *32768
                    mach64_log("mach64 : write bank A8000-AFFFF set to %08X\n", mach64->bank_w[1]);
                    break;
                case 0xb8:
                    mach64->bank_r[0] = val << 15; // *32768
                    mach64_log("mach64 :  read bank A0000-A7FFF set to %08X\n", mach64->bank_r[0]);
                    break;
                case 0xba:
                    mach64->bank_r[1] = val << 15; // *32768
                    mach64_log("mach64 :  read bank A8000-AFFFF set to %08X\n", mach64->bank_r[1]);
                    break;
                case 0xc0 ... 0xc3:
                    if (mach64->type == MACH64_GX)
                        ati68860_ramdac_out((addr & 3) | ((mach64->dac_cntl & 3) << 2), val, 0, svga->ramdac, svga);
                    else {
                        uint16_t port_list[4] = { 0x3c8, 0x3c9, 0x3c6, 0x3c7 };
                        svga_out(port_list[addr & 3], val, svga);
                    }
                    break;
                case 0xc4 ... 0xc7:
                    WRITE8(addr, mach64->dac_cntl, val);
                    mach64_log("Ext RAMDAC TYPE write=%x, bit set=%03x.\n", addr & 0x3ff, mach64->dac_cntl & 0x100);
                    if ((addr & 3) >= 1) {
                        svga_set_ramdac_type(svga, !!(mach64->dac_cntl & 0x100));
                        if (mach64->type == MACH64_GX)
                            ati68860_set_ramdac_type(svga->ramdac, !!(mach64->dac_cntl & 0x100));
                    }
                    if (mach64->type != MACH64_VT3)
                        i2c_gpio_set(mach64->i2c, !(mach64->dac_cntl & 0x20000000) || (mach64->dac_cntl & 0x04000000), !(mach64->dac_cntl & 0x10000000) || (mach64->dac_cntl & 0x02000000));
                    break;
                case 0xa0 ... 0xa3:
                    /* BUS_CNTL (RRG 3-2): bits 21 and 23 read as the FIFO and
                       host data error interrupts, which nothing raises here,
                       and a write to them acknowledges. */
                    WRITE8(addr, mach64->bus_cntl, val);
                    mach64->bus_cntl &= ~((1u << 21) | (1u << 23));
                    mach64_update_irqs(mach64);
                    if ((addr & 3) == 1)
                        mach64_update_rom(mach64); /* BUS_ROM_DIS */
                    break;
                case 0xe8 ... 0xeb:
                    break; /* CONFIG_STAT1 is read-only */
                case 0xd0 ... 0xd3:
                    /* GEN_GUI_EN (bit 8): "0 = Resets draw engine" (RRG 3-57). */
                    if (((addr & 3) == 1) && (mach64->gen_test_cntl & 0x100) && !(val & 0x01)) {
                        mach64_wait_fifo_idle(mach64);
                        mach64->accel.busy = 0;
                    }
                    WRITE8(addr, mach64->gen_test_cntl, val);
                    /* GEN_EE_CHIP_SEL (bit 2) is the EEPROM's chip select and
                       GEN_EE_CLOCK (bit 1) its clock; the part sees either only
                       while GEN_EE_EN (bit 4) enables the interface, its pins
                       being shared with the display (Register Reference Guide,
                       GEN_TEST_CNTL). Bit 4 was taken for the chip select. */
                    {
                        int ee_en = !!(mach64->gen_test_cntl & 0x10);

                        ati_eeprom_write(&mach64->eeprom, ee_en && (mach64->gen_test_cntl & 0x04),
                                         ee_en && (mach64->gen_test_cntl & 0x02), mach64->gen_test_cntl & 0x01);
                    }
                    mach64->gen_test_cntl  = (mach64->gen_test_cntl & ~8) | (ati_eeprom_read(&mach64->eeprom) ? 8 : 0);
                    if (mach64->type == MACH64_GX)
                        svga->dac_hwcursor.ena = !!(mach64->gen_test_cntl & 0x80);
                    else
                        svga->hwcursor.ena = !!(mach64->gen_test_cntl & 0x80);
                    break;
                case 0xdc ... 0xdf:
                    if (mach64->type == MACH64_GX)
                        break; /* no memory mapped alias on the GX (RRG 1-3) */
                    WRITE8(addr, mach64->config_cntl, val);
                    mach64_updatemapping(mach64);
                    break;
                case 0xe4 ... 0xe7:
                    if (mach64->type != MACH64_GX)
                        WRITE8(addr, mach64->config_stat0, val);
                    break;
                default:
                    break;
            }
        }
    }
}

void
mach64_ext_writew(uint32_t addr, uint16_t val, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    svga_t   *svga   = &mach64->svga;

    if ((addr >= 0x000a0000) && (addr < 0x000bf800))
        svga->mapping.write_w(addr, val, svga->mapping.priv);
    else if ((addr < 0x000a0000) || ((addr >= 0x000bf800) && (addr <= 0x000bffff)) || (addr >= 0x00100000)) {
        mach64_log("mach64_ext_writew : addr %08X val %04X\n", addr, val);
        if (!(addr & 0x400)) {
            mach64_log("mach64_ext_writew: addr=%04x val=%04x\n", addr, val);
            mach64_ext_writeb(addr, val, priv);
            mach64_ext_writeb(addr + 1, val >> 8, priv);
        } else if (addr & 0x300) {
            mach64_queue(mach64, addr & 0x3fe, val, FIFO_WRITE_WORD);
        } else {
            switch (addr & 0x3fe) {
                case 0xb4:
                case 0xb6:
                    mach64->bank_w[(addr & 2) >> 1] = val << 15;
                    break;
                case 0xb8:
                case 0xba:
                    mach64->bank_r[(addr & 2) >> 1] = val << 15;
                    break;
                default:
                    mach64_ext_writeb(addr, val, priv);
                    mach64_ext_writeb(addr + 1, val >> 8, priv);
                    break;
            }
        }
    }
}
void
mach64_ext_writel(uint32_t addr, uint32_t val, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    svga_t   *svga   = &mach64->svga;

    if ((addr >= 0x000a0000) && (addr < 0x000bf800))
        svga->mapping.write_l(addr, val, svga->mapping.priv);
    else if ((addr < 0x000a0000) || ((addr >= 0x000bf800) && (addr <= 0x000bffff)) || (addr >= 0x00100000)) {
        if ((addr & 0x3c0) != 0x200)
            mach64_log("mach64_ext_writel : addr %08X val %08X\n", addr, val);
        if (!(addr & 0x400)) {
            mach64_log("mach64_ext_writel: addr=%04x val=%08x\n", addr, val);

            mach64_ext_writew(addr, val, priv);
            mach64_ext_writew(addr + 2, val >> 16, priv);
        } else if (addr & 0x300) {
            mach64_queue(mach64, addr & 0x3fc, val, FIFO_WRITE_DWORD);
        } else {
            switch (addr & 0x3fc) {
                case 0xb4:
                    mach64->bank_w[0] = val << 15;
                    mach64->bank_w[1] = ((val >> 16) << 15);
                    break;
                case 0xb8:
                    mach64->bank_r[0] = val << 15;
                    mach64->bank_r[1] = ((val >> 16) << 15);
                    break;
                default:
                    mach64_ext_writew(addr, val, priv);
                    mach64_ext_writew(addr + 2, val >> 16, priv);
                    break;
            }
        }
    }
}

uint8_t
mach64_ext_inb(uint16_t port, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    svga_t   *svga   = &mach64->svga;
    uint8_t   ret    = 0x00; // default value based on invaid port

    // Code is written with the assumption that IO_BASE = 2cc. so we can rewrite it liek this
    /* io_base is the PCI 40h select, 0-2; the decode below is for 2ECh. */
    if (mach64->io_base == 1)
        port += MACH64_IO_BASE_2EC - MACH64_IO_BASE_1CC;
    else if (mach64->io_base == 2)
        port += MACH64_IO_BASE_2EC - MACH64_IO_BASE_1C8;

    uint8_t port_high = (port >> 8) & 0xFE; // we only care about the upper 5 bits
    uint8_t port_low = port & 0xFF;

    uint8_t lane = port & 3;

    // the value to or the final address for write into space
    uint8_t addr_or_value = 0;
    // we only care about (ec...ef)
    if ((port_low >= 0xEC) && (port_low <= 0xEF))
    {
        // exclude everything we don't want
        switch (port_high)
        {
            // some special cases
            case 0x56: // 56ec-56ef
            case 0x5a: // 5aec-5aef

                addr_or_value = 0xb4;

                if (port_high == 0x5a)
                    addr_or_value = 0xb8;

                if (port_low == 0xEC)
                    ret = mach64_ext_readb(0x400 | addr_or_value, priv);
                else if (port_low == 0xEE)
                    ret = mach64_ext_readb(0x400 | addr_or_value | 2, priv); /* WPS1 / RPS1 */
                break;
            case 0x5e: // 5eec-5eef
                if (mach64->type == MACH64_GX)
                    ret = ati68860_ramdac_in((lane) | ((mach64->dac_cntl & 3) << 2), 0, mach64->svga.ramdac, &mach64->svga);
                else {
                    uint16_t port_list[4] = { 0x3c8, 0x3c9, 0x3c6, 0x3c7 };
                    ret = svga_in(port_list[lane], svga);
                }
                break;
            case 0x6a: // 6eec-6eef
                mach64_sync_ap_loc(mach64);
                READ8(port, mach64->config_cntl);
                break;
            default:  // general case
                // there must be a more rational rule here
                if (port_high <= 0x1E)
                    addr_or_value = port_high - 2;
                else if ((port_high >= 0x22) && (port_high <= 0x2A))
                    addr_or_value = port_high + 0x1E; // 0x40 - 0x48
                else if ((port_high >= 0x2E) && (port_high <= 0x3E))
                    addr_or_value = port_high + 0x32;
                else if ((port_high >= 0x42) && (port_high <= 0x46))
                    addr_or_value = port_high + 0x3E;
                else if (port_high == 0x4A)
                    addr_or_value = 0x90;
                else if (port_high == 0x52)
                    addr_or_value = 0xb0;
                else if (port_high == 0x62)
                    addr_or_value = 0xc4;
                else if (port_high == 0x66)
                    addr_or_value = 0xd0;
                else if ((port_high >= 0x6e) && (port_high <= 0x72))
                    addr_or_value = port_high + 0x72;
                else if (port_high == 0x7e)
                    addr_or_value = 0x00; // must be 0
                /* BUS_CNTL is I/O select 13h (4EECh) and CONFIG_STAT1 1Dh
                   (76ECh); select 1Eh (7AECh) is no register (RRG 2-6). All
                   three fell through to CRTC_H_TOTAL_DISP. */
                else if (port_high == 0x4E)
                    addr_or_value = 0xa0;
                else if (port_high == 0x76)
                    addr_or_value = 0xe8;
                else if (port_high == 0x7A)
                    break;

                ret = mach64_ext_readb(0x400 | addr_or_value | (lane), priv);
                break;
        }
    }

    mach64_log("mach64_ext_inb : port %04X ret %02X\n", port, ret);
    return ret;
}
uint16_t
mach64_ext_inw(uint16_t port, void *priv)
{
    uint16_t ret;

    ret = mach64_ext_inb(port, priv);
    ret |= (mach64_ext_inb(port + 1, priv) << 8);

    mach64_log("mach64_ext_inw : port %04X ret %04X\n", port, ret);
    return ret;
}

uint32_t
mach64_ext_inl(uint16_t port, void *priv)
{
    uint32_t ret;

    ret = mach64_ext_inw(port, priv);
    ret |= (mach64_ext_inw(port + 2, priv) << 16);

    mach64_log("mach64_ext_inl : port %04X ret %08X\n", port, ret);
    return ret;
}

void
mach64_ext_outb(uint16_t port, uint8_t val, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    svga_t *svga = &mach64->svga;

    // Code is written with the assumption that IO_BASE = 2cc. so we can rewrite it for other I/O bases like this
    /* io_base is the PCI 40h select, 0-2; the decode below is for 2ECh. */
    if (mach64->io_base == 1)
        port += MACH64_IO_BASE_2EC - MACH64_IO_BASE_1CC;
    else if (mach64->io_base == 2)
        port += MACH64_IO_BASE_2EC - MACH64_IO_BASE_1C8;

    uint8_t port_high = (port >> 8) & 0xFE; // we only care about the upper 5 bits
    uint8_t port_low = port & 0xFF;

    // the value to or the final address for write into

    uint8_t addr_or_value = 0;

    // we only care about (ec...ef)
    if ((port_low >= 0xEC) && (port_low <= 0xEF))
    {
        switch (port_high)
        {
             case 0x56: // 56ec-56ef
                mach64_log("BankWrite portlow=%02x, val=%02x.\n", port_low, val);
                if (port_low == 0xEC)
                    mach64_ext_writeb(0x400 | 0xb4, val, priv);
                else if (port_low == 0xEE)
                    mach64_ext_writeb(0x400 | 0xb6, val, priv);
                break;
            case 0x5a: // 5aec-5aef
                mach64_log("BankRead portlow=%02x, val=%02x.\n", port_low, val);
                if (port_low == 0xEC)
                    mach64_ext_writeb(0x400 | 0xb8, val, priv);
                else if (port_low == 0xEE)
                    mach64_ext_writeb(0x400 | 0xba, val, priv);
                break;
            case 0x5e: // 5eec-5eef
                if (mach64->type == MACH64_GX)
                    ati68860_ramdac_out((port & 3) | ((mach64->dac_cntl & 3) << 2), val, 0, svga->ramdac, svga);
                else {
                    uint16_t port_list[4] = { 0x3c8, 0x3c9, 0x3c6, 0x3c7 };
                    svga_out(port_list[port & 3], val, svga);
                }
                break;
            case 0x6a: // 6eec-6eef
                WRITE8(port, mach64->config_cntl, val);
                if (!mach64->pci)
                    mach64->linear_base = (mach64->config_cntl & 0x3ff0) << 18;

                mach64_updatemapping(mach64);
                break;
            default:

                 // there must be a more rational rule here
                if (port_high <= 0x1E)
                    addr_or_value = port_high - 2;
                else if ((port_high >= 0x22) && (port_high <= 0x2A))
                    addr_or_value = port_high + 0x1E; // 0x40 - 0x48
                else if ((port_high >= 0x2E) && (port_high <= 0x3E))
                    addr_or_value = port_high + 0x32;
                else if ((port_high >= 0x42) && (port_high <= 0x46))
                    addr_or_value = port_high + 0x3E;
                else if (port_high == 0x4A)
                    addr_or_value = 0x90;
                else if (port_high == 0x52)
                    addr_or_value = 0xb0;
                else if (port_high == 0x62)
                    addr_or_value = 0xc4;
                else if (port_high == 0x66)
                    addr_or_value = 0xd0;
                else if ((port_high >= 0x6e) && (port_high <= 0x72))
                    addr_or_value = port_high + 0x72;
                else if (port_high == 0x7e)
                    addr_or_value = 0x00; // must be 0
                /* BUS_CNTL (13h, 4EECh), CONFIG_STAT1 (1Dh, 76ECh), and 1Eh
                   (7AECh), which is no register (RRG 2-6). */
                else if (port_high == 0x4E)
                    addr_or_value = 0xa0;
                else if (port_high == 0x76)
                    addr_or_value = 0xe8;
                else if (port_high == 0x7A)
                    break;

                mach64_ext_writeb(0x400 | addr_or_value | (port & 3), val, priv);

                break;
        }
    }

    mach64_log("mach64_ext_outb : port %04X val %02X\n", port, val);
}
void
mach64_ext_outw(uint16_t port, uint16_t val, void *priv)
{
    mach64_log("mach64_ext_outw : port %04X val %04X\n", port, val);
    mach64_ext_outb(port, val, priv);
    mach64_ext_outb(port + 1, val >> 8, priv);
}
void
mach64_ext_outl(uint16_t port, uint32_t val, void *priv)
{
    mach64_log("mach64_ext_outl : port %04X val %08X\n", port, val);
    mach64_ext_outw(port, val, priv);
    mach64_ext_outw(port + 2, val >> 16, priv);
}

static uint8_t
mach64_block_inb(uint16_t port, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    uint8_t   ret;

    ret = mach64_ext_readb(0x400 | (port & 0x3ff), mach64);
    mach64_log("mach64_block_inb : port %04X ret %02X\n", port, ret);
    return ret;
}
static uint16_t
mach64_block_inw(uint16_t port, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    uint16_t  ret;

    ret = mach64_ext_readw(0x400 | (port & 0x3ff), mach64);
    mach64_log("mach64_block_inw : port %04X ret %04X\n", port, ret);
    return ret;
}
static uint32_t
mach64_block_inl(uint16_t port, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    uint32_t  ret;

    ret = mach64_ext_readl(0x400 | (port & 0x3ff), mach64);
    mach64_log("mach64_block_inl : port %04X ret %08X\n", port, ret);
    return ret;
}

static void
mach64_block_outb(uint16_t port, uint8_t val, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;

    mach64_log("mach64_block_outb : port %04X val %02X\n ", port, val);
    mach64_ext_writeb(0x400 | (port & 0x3ff), val, mach64);
}
static void
mach64_block_outw(uint16_t port, uint16_t val, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;

    mach64_log("mach64_block_outw : port %04X val %04X\n ", port, val);
    mach64_ext_writew(0x400 | (port & 0x3ff), val, mach64);
}
static void
mach64_block_outl(uint16_t port, uint32_t val, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;

    mach64_log("mach64_block_outl : port %04X val %08X\n ", port, val);
    mach64_ext_writel(0x400 | (port & 0x3ff), val, mach64);
}

static uint32_t
mach64_decode_addr(mach64_t *mach64, uint32_t addr, int write)
{
    const svga_t * svga            = &mach64->svga;
    const int      memory_map_mode = (svga->gdcreg[6] >> 2) & 3;

    addr &= 0x1ffff;

    switch (memory_map_mode) {
        case 0:
            break;
        case 1:
            if (addr >= 0x10000)
                return 0xffffffff;
            break;
        case 2:
            addr -= 0x10000;
            if (addr >= 0x8000)
                return 0xffffffff;
            break;
        default:
        case 3:
            addr -= 0x18000;
            if (addr >= 0x8000)
                return 0xffffffff;
            break;
    }

    if (write)
        addr = (addr & 0x7fff) + mach64->bank_w[(addr >> 15) & 1];
    else
        addr = (addr & 0x7fff) + mach64->bank_r[(addr >> 15) & 1];

    return addr;
}

/* MEM_BNDRY_EN (MEM_CNTL bit 18): the VGA apertures, the standard one and
   the two small ones, reach only the memory below MEM_BNDRY; the linear
   aperture is not affected (Programmer's Guide 2-18, 2-65). An address
   past the end of video memory is one svga drops. */
static uint32_t
mach64_vga_translate(uint32_t addr, void *priv)
{
    const svga_t   *svga   = (svga_t *) priv;
    const mach64_t *mach64 = (mach64_t *) svga->priv;

    if ((mach64->mem_cntl & (1 << 18)) && (addr >= mach64_mem_bndry(mach64)))
        return 0xffffffff;
    return addr;
}

void
mach64_write(uint32_t addr, uint8_t val, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    svga_t   *svga   = &mach64->svga;
    addr = mach64_decode_addr(mach64, addr, 1);
    if (addr != 0xffffffff)
        svga_write_linear(addr, val, svga);
}
void
mach64_writew(uint32_t addr, uint16_t val, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    svga_t   *svga   = &mach64->svga;
    addr = mach64_decode_addr(mach64, addr, 1);
    if (addr != 0xffffffff)
        svga_writew_linear(addr, val, svga);
}
void
mach64_writel(uint32_t addr, uint32_t val, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    svga_t   *svga   = &mach64->svga;
    addr = mach64_decode_addr(mach64, addr, 1);
    if (addr != 0xffffffff)
        svga_writel_linear(addr, val, svga);
}

uint8_t
mach64_read(uint32_t addr, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    svga_t   *svga   = &mach64->svga;
    uint8_t   ret    = 0xff;
    addr = mach64_decode_addr(mach64, addr, 0);
    if (addr != 0xffffffff)
        ret  = svga_read_linear(addr, svga);
    return ret;
}
uint16_t
mach64_readw(uint32_t addr, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    svga_t   *svga   = &mach64->svga;
    uint16_t  ret    = 0xffff;
    addr = mach64_decode_addr(mach64, addr, 0);
    if (addr != 0xffffffff)
        ret  = svga_readw_linear(addr, svga);
    return ret;
}
uint32_t
mach64_readl(uint32_t addr, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;
    svga_t   *svga   = &mach64->svga;
    uint32_t  ret    = 0xffffffff;
    addr = mach64_decode_addr(mach64, addr, 0);
    if (addr != 0xffffffff)
        ret  = svga_readl_linear(addr, svga);
    return ret;
}

uint32_t
mach64_conv_16to32(svga_t* svga, uint16_t color, uint8_t bpp)
{
    uint32_t ret = 0x00000000;

    if (svga->lut_map) {
        if (bpp == 15) {
            uint8_t b = getcolr(svga->pallook[(color & 0x1f) << 3]);
            uint8_t g = getcolg(svga->pallook[(color & 0x3e0) >> 2]);
            uint8_t r = getcolb(svga->pallook[(color & 0x7c00) >> 7]);
            ret = (video_15to32[color] & 0xFF000000) | makecol(r, g, b);
        } else {
            uint8_t b = getcolr(svga->pallook[(color & 0x1f) << 3]);
            uint8_t g = getcolg(svga->pallook[(color & 0x7e0) >> 3]);
            uint8_t r = getcolb(svga->pallook[(color & 0xf800) >> 8]);
            ret = (video_16to32[color] & 0xFF000000) | makecol(r, g, b);
        }
    } else
        ret = (bpp == 15) ? video_15to32[color] : video_16to32[color];

    return ret;
}

void
mach64_int_hwcursor_draw(svga_t *svga, int displine)
{
    const mach64_t          *mach64 = (mach64_t *) svga->priv;
    int                      comb;
    int                      offset;
    int                      x_pos;
    int                      y_pos;
    int                      shift = 0;
    uint16_t                 dat;
    uint32_t                 col0 = makecol32((mach64->cur_clr0 >> 24) & 0xff, (mach64->cur_clr0 >> 16) & 0xff, (mach64->cur_clr0 >> 8) & 0xff);
    uint32_t                 col1 = makecol32((mach64->cur_clr1 >> 24) & 0xff, (mach64->cur_clr1 >> 16) & 0xff, (mach64->cur_clr1 >> 8) & 0xff);
    uint32_t                *p;

    offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;
    if (svga->packed_4bpp)
        shift = 1;

    for (int x = 0; x < svga->hwcursor_latch.cur_xsize; x += (8 >> shift)) {
        if (shift) {
            dat = svga->vram[(svga->hwcursor_latch.addr) & svga->vram_mask] & 0x0f;
            dat |= (svga->vram[(svga->hwcursor_latch.addr + 1) & svga->vram_mask] << 4);
            dat |= (svga->vram[(svga->hwcursor_latch.addr + 2) & svga->vram_mask] << 8);
            dat |= (svga->vram[(svga->hwcursor_latch.addr + 3) & svga->vram_mask] << 12);
        } else {
            dat = svga->vram[svga->hwcursor_latch.addr & svga->vram_mask];
            dat |= (svga->vram[(svga->hwcursor_latch.addr + 1) & svga->vram_mask] << 8);
        }
        for (int xx = 0; xx < (8 >> shift); xx++) {
            comb = (dat >> (xx << 1)) & 0x03;

            y_pos = displine;
            x_pos = offset + svga->x_add;
            p     = buffer32->line[y_pos];

            if (offset >= svga->hwcursor_latch.x) {
                switch (comb) {
                    case 0:
                        p[x_pos] = col0;
                        break;
                    case 1:
                        p[x_pos] = col1;
                        break;
                    case 3:
                        p[x_pos] ^= 0xffffff;
                        break;
                    default:
                        break;
                }
            }
            offset++;
        }
        svga->hwcursor_latch.addr += 2;
    }
}


static void
mach64_io_unmap(mach64_t *mach64)
{
    uint16_t io_base = MACH64_IO_BASE_2EC;

    switch (mach64->io_base) {
        default:
        case 0:
            io_base = MACH64_IO_BASE_2EC;
            break;
        case 1:
            io_base = MACH64_IO_BASE_1CC;
            break;
        case 2:
            io_base = MACH64_IO_BASE_1C8;
            break;
        case 3:
            fatal("Attempting to use the reserved value for I/O Base\n");
            return;
    }

    io_removehandler(0x03a0, 0x0040, mach64_in, NULL, NULL, mach64_out, NULL, NULL, mach64);

    for (uint8_t c = 0; c < 32; c++) // *0x400
        io_removehandler((c << 10) + io_base, 0x0004, mach64_ext_inb, mach64_ext_inw, mach64_ext_inl, mach64_ext_outb, mach64_ext_outw, mach64_ext_outl, mach64);

    io_removehandler(0x01ce, 0x0002, mach64_in, NULL, NULL, mach64_out, NULL, NULL, mach64);

    if (mach64->block_decoded_io && mach64->block_decoded_io < 0x10000)
        io_removehandler(mach64->block_decoded_io, 0x0100, mach64_block_inb, mach64_block_inw, mach64_block_inl, mach64_block_outb, mach64_block_outw, mach64_block_outl, mach64);
}

static void
mach64_io_map(mach64_t *mach64)
{
    uint16_t io_base = MACH64_IO_BASE_2EC;

    mach64_io_unmap(mach64);

    switch (mach64->io_base) {
        default:
        case 0:
            io_base = MACH64_IO_BASE_2EC;
            break;
        case 1:
            io_base = MACH64_IO_BASE_1CC;
            break;
        case 2:
            io_base = MACH64_IO_BASE_1C8;
            break;
        case 3:
            fatal("Attempting to use the reserved value for I/O Base\n");
            return;
    }

    if (!(mach64->svga.miscout & 0x01))
        io_sethandler(0x03a0, 0x0020, mach64_in, NULL, NULL, mach64_out, NULL, NULL, mach64);
    io_sethandler(0x03c0, 0x0020, mach64_in, NULL, NULL, mach64_out, NULL, NULL, mach64);

    if (!mach64->use_block_decoded_io) {

        for (uint8_t c = 0; c < 32; c++) // *0x400
            io_sethandler((c << 10) + io_base, 0x0004, mach64_ext_inb, mach64_ext_inw, mach64_ext_inl, mach64_ext_outb, mach64_ext_outw, mach64_ext_outl, mach64);
    }

    io_sethandler(0x01ce, 0x0002, mach64_in, NULL, NULL, mach64_out, NULL, NULL, mach64);

    if (mach64->use_block_decoded_io && mach64->block_decoded_io && mach64->block_decoded_io < 0x10000)
        io_sethandler(mach64->block_decoded_io, 0x0100, mach64_block_inb, mach64_block_inw, mach64_block_inl, mach64_block_outb, mach64_block_outw, mach64_block_outl, mach64);
}

static uint8_t
mach64_read_linear(uint32_t addr, void *priv)
{
    const svga_t *svga = (svga_t *) priv;

    cycles -= svga->monitor->mon_video_timing_read_b;

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return 0xff;

    return svga->vram[addr & svga->vram_mask];
}

static uint16_t
mach64_readw_linear(uint32_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    cycles -= svga->monitor->mon_video_timing_read_w;

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return 0xffff;

    return *(uint16_t *) &svga->vram[addr & svga->vram_mask];
}

static uint32_t
mach64_readl_linear(uint32_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    cycles -= svga->monitor->mon_video_timing_read_l;

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return 0xffffffff;

    return *(uint32_t *) &svga->vram[addr & svga->vram_mask];
}

static void
mach64_write_linear(uint32_t addr, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    cycles -= svga->monitor->mon_video_timing_write_b;

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return;
    addr &= svga->vram_mask;
    svga->changedvram[addr >> 12] = svga->monitor->mon_changeframecount;
    svga->vram[addr]              = val;
}

static void
mach64_writew_linear(uint32_t addr, uint16_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    cycles -= svga->monitor->mon_video_timing_write_w;

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return;
    addr &= svga->vram_mask;
    svga->changedvram[addr >> 12]   = svga->monitor->mon_changeframecount;
    *(uint16_t *) &svga->vram[addr] = val;
}

static void
mach64_writel_linear(uint32_t addr, uint32_t val, void *priv)
{
    svga_t   *svga   = (svga_t *) priv;
    mach64_t *mach64 = (mach64_t *) svga->priv;

    cycles -= svga->monitor->mon_video_timing_write_l;

    if (((mach64->scaler_yuv_aper >> 4) & 0xc) && !!(addr & 0x800000) == !(mach64->scaler_yuv_aper & 0x20)) {
        uint32_t offset_from_base = addr & 0x7FFFFF;
        if (addr & 0x800000)
            bswap32s(&val);
        if (((mach64->scaler_yuv_aper >> 4) & 0xc) == 0x4) { // Y plane
            offset_from_base <<= 1;
            svga->vram[offset_from_base & svga->vram_mask] = (val & 0xFF);
            svga->vram[(offset_from_base + 1) & svga->vram_mask] = ((val >> 8) & 0xFF);
            svga->vram[(offset_from_base + 4) & svga->vram_mask] = ((val >> 16) & 0xFF);
            svga->vram[(offset_from_base + 5) & svga->vram_mask] = ((val >> 24) & 0xFF);
        }
        else if (((mach64->scaler_yuv_aper >> 4) & 0xc) == 0x8 || ((mach64->scaler_yuv_aper >> 4) & 0xc) == 0xc) {
            offset_from_base <<= 2;
            if (((mach64->scaler_yuv_aper >> 4) & 0xc) == 0x8) { // U plane
                svga->vram[(offset_from_base + 3) & svga->vram_mask] = (val & 0xFF);
                svga->vram[(offset_from_base + 7) & svga->vram_mask] = ((val >> 8) & 0xFF);
                svga->vram[(offset_from_base + 11) & svga->vram_mask] = ((val >> 16) & 0xFF);
                svga->vram[(offset_from_base + 15) & svga->vram_mask] = ((val >> 24) & 0xFF);
            } else { // V plane
                svga->vram[(offset_from_base + 2) & svga->vram_mask] = (val & 0xFF);
                svga->vram[(offset_from_base + 6) & svga->vram_mask] = ((val >> 8) & 0xFF);
                svga->vram[(offset_from_base + 10) & svga->vram_mask] = ((val >> 16) & 0xFF);
                svga->vram[(offset_from_base + 14) & svga->vram_mask] = ((val >> 24) & 0xFF);
            }
        }
        return;
    }

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return;
    addr &= svga->vram_mask;
    svga->changedvram[addr >> 12]   = svga->monitor->mon_changeframecount;
    *(uint32_t *) &svga->vram[addr] = val;
}

uint8_t
mach64_readb_be(uint32_t addr, void *priv)
{
    return mach64_read_linear(addr, priv);
}

uint16_t
mach64_readw_be(uint32_t addr, void *priv)
{
    return bswap16(mach64_readw_linear(addr, priv));
}

uint32_t
mach64_readl_be(uint32_t addr, void *priv)
{
    return bswap32(mach64_readl_linear(addr, priv));
}

void
mach64_writeb_be(uint32_t addr, uint8_t val, void *priv)
{
    return mach64_write_linear(addr, val, priv);
}

void
mach64_writew_be(uint32_t addr, uint16_t val, void *priv)
{
    return mach64_writew_linear(addr, bswap16(val), priv);
}

void
mach64_writel_be(uint32_t addr, uint32_t val, void *priv)
{
    return mach64_writel_linear(addr, bswap32(val), priv);
}

// PCI config space I/O read function
uint8_t
mach64_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    const mach64_t *mach64 = (mach64_t *) priv;

    switch (addr) {
        case PCI_REG_VENDOR_ID_L:
            return 0x02; /*ATi*/
        case PCI_REG_VENDOR_ID_H:
            return 0x10;
        case PCI_REG_DEVICE_ID_L:
            return mach64->pci_id & 0xff;
        case PCI_REG_DEVICE_ID_H:
            return mach64->pci_id >> 8;
        case PCI_REG_COMMAND:
            return mach64->pci_regs[PCI_REG_COMMAND]; /*Respond to IO and memory accesses*/
        case PCI_REG_STATUS_H:
            return 1 << 1; /*Medium DEVSEL timing*/
        case PCI_REG_REVISION: /*Revision ID*/
            if (mach64->type == MACH64_GX)
                return 0;
            return 0x40;
        case PCI_REG_PROG_IF:
            return 0; /*Programming interface*/
        case PCI_REG_SUBCLASS:
            return 0x01; /*Supports VGA interface, XGA compatible*/
        case PCI_REG_CLASS:
            return 0x03;
        case PCI_REG_BAR0_BYTE0:
            return 0x00; /*Linear frame buffer address*/
        case PCI_REG_BAR0_BYTE1:
            return 0x00;
        case PCI_REG_BAR0_BYTE2:
            return mach64->linear_base >> 16;
        case PCI_REG_BAR0_BYTE3:
            return mach64->linear_base >> 24;
        case PCI_REG_BAR1_BYTE0:
            if (mach64->type >= MACH64_CT)
                return 0x01; /*Block decoded IO address*/
            return 0x00;
        case PCI_REG_BAR1_BYTE1:
            if (mach64->type >= MACH64_CT)
                return mach64->block_decoded_io >> 8;
            return 0x00;
        case PCI_REG_BAR1_BYTE2:
            if (mach64->type >= MACH64_CT)
                return mach64->block_decoded_io >> 16;
            return 0x00;
        case PCI_REG_BAR1_BYTE3:
            if (mach64->type >= MACH64_CT)
                return mach64->block_decoded_io >> 24;
            return 0x00;
        case PCI_REG_ROM_BAR_BYTE0:
            return (mach64->on_board) ? 0 : (mach64->pci_regs[0x30] & 0x01); /*BIOS ROM address*/
        case PCI_REG_ROM_BAR_BYTE1:
            /* The ROM is 32K: address bits 31:15 (PCI 2.1, 6.2.5.2). */
            return (mach64->on_board) ? 0 : (mach64->pci_regs[0x31] & 0x80);
        case PCI_REG_ROM_BAR_BYTE2:
            return (mach64->on_board) ? 0 : mach64->pci_regs[0x32];
        case PCI_REG_ROM_BAR_BYTE3:
            return (mach64->on_board) ? 0 : mach64->pci_regs[0x33];
        case PCI_REG_INT_LINE:
            return mach64->int_line;
        case PCI_REG_INT_PIN:
            return PCI_INTA;
        case MACH64_PCI_IOCONFIG:
            return mach64->use_block_decoded_io | mach64->io_base;
        default:
            break;
    }
    return 0;
}

// PCI config space I/O write function
void
mach64_pci_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;

    // Addresses that DON'T need to
    bool dont_remap_io = (addr == PCI_REG_COMMAND // controls the mapping so we don't use the default behaviour
    || addr == PCI_REG_BAR0_BYTE2
    || addr == PCI_REG_BAR0_BYTE3
    || (addr >= PCI_REG_ROM_BAR_BYTE0 && addr <= PCI_REG_ROM_BAR_BYTE3));

    if (!dont_remap_io
    && (mach64->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO))
    {
        mach64_io_unmap(mach64);
    }

    switch (addr) {
        case PCI_REG_COMMAND:
            mach64->pci_regs[PCI_REG_COMMAND] = val & 0x27;
            if (val & PCI_COMMAND_IO)
                mach64_io_map(mach64);
            else
                mach64_io_unmap(mach64);
            mach64_updatemapping(mach64);
            break;
        case PCI_REG_BAR0_BYTE2:
            if (mach64->type >= MACH64_CT)
                val = 0;
            mach64->linear_base = (mach64->linear_base & 0xff000000) | ((val & 0x80) << 16);
            mach64_updatemapping(mach64);
            break;
        case PCI_REG_BAR0_BYTE3:
            mach64->linear_base = (mach64->linear_base & 0x800000) | (val << 24);
            mach64_updatemapping(mach64);
            break;
        case PCI_REG_BAR1_BYTE1:
            if (mach64->type >= MACH64_CT)
                mach64->block_decoded_io = (mach64->block_decoded_io & 0xffff00ff) | (val << 8);
            break;
        case PCI_REG_BAR1_BYTE2:
            if (mach64->type >= MACH64_CT)
                mach64->block_decoded_io = (mach64->block_decoded_io & 0xff00ffff) | (val << 16);
            break;
        case PCI_REG_BAR1_BYTE3:
            if (mach64->type >= MACH64_CT)
                mach64->block_decoded_io = (mach64->block_decoded_io & 0x00ffffff) | (val << 24);
            break;
        case PCI_REG_ROM_BAR_BYTE0 ... PCI_REG_ROM_BAR_BYTE3:
            if (mach64->on_board)
                return;
            mach64->pci_regs[addr] = val;
            mach64_update_rom(mach64);
            return;
        case PCI_REG_INT_LINE:
            mach64->int_line = val;
            break;
        case MACH64_PCI_IOCONFIG:
            mach64->io_base = val & 0x03;
            if (mach64->type >= MACH64_CT)
                mach64->use_block_decoded_io = val & 0x04;
            break;
        default:
            break;
    }

    if (!dont_remap_io
    && (mach64->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO))
    {
        mach64_io_map(mach64);
    }
}

static void
mach64_disable_handlers(mach64_t *dev)
{
    mach64_io_unmap(dev);

    mem_mapping_disable(&dev->linear_mapping);
    mem_mapping_disable(&dev->linear_mapping_big_endian);
    mem_mapping_disable(&dev->mmio_mapping);
    mem_mapping_disable(&dev->mmio_linear_mapping);
    mem_mapping_disable(&dev->mmio_linear_mapping_2);
    mem_mapping_disable(&dev->svga.mapping);
    if (dev->pci && !dev->on_board)
        mem_mapping_disable(&dev->bios_rom.mapping);

    /* Save all the mappings and the timers because they are part of linked lists. */
    reset_state[dev->svga.monitor_index]->linear_mapping            = dev->linear_mapping;
    reset_state[dev->svga.monitor_index]->linear_mapping_big_endian = dev->linear_mapping_big_endian;
    reset_state[dev->svga.monitor_index]->mmio_mapping              = dev->mmio_mapping;
    reset_state[dev->svga.monitor_index]->mmio_linear_mapping       = dev->mmio_linear_mapping;
    reset_state[dev->svga.monitor_index]->mmio_linear_mapping_2     = dev->mmio_linear_mapping_2;
    reset_state[dev->svga.monitor_index]->svga.mapping              = dev->svga.mapping;
    reset_state[dev->svga.monitor_index]->bios_rom.mapping          = dev->bios_rom.mapping;

    reset_state[dev->svga.monitor_index]->svga.timer      = dev->svga.timer;
    reset_state[dev->svga.monitor_index]->svga.timer_8514 = dev->svga.timer_8514;
    reset_state[dev->svga.monitor_index]->svga.timer_xga  = dev->svga.timer_xga;
}

static void
mach64_reset(void *priv)
{
    mach64_t *dev = (mach64_t *) priv;

    if (reset_state[dev->svga.monitor_index] != NULL) {
        mach64_disable_handlers(dev);
        dev->blitter_busy                              = 0;
        dev->fifo_write_idx                            = 0;
        dev->fifo_read_idx                             = 0;
        reset_state[dev->svga.monitor_index]->eeprom   = dev->eeprom;
        reset_state[dev->svga.monitor_index]->pci_slot = dev->pci_slot;

        *dev = *reset_state[dev->svga.monitor_index];
        mach64_io_map(dev);
        memset(dev->svga.vram, 0, dev->svga.vram_max);
        memset(dev->svga.changedvram, 0, (dev->svga.vram_max >> 12) + 1);
        dev->svga.dpms = 1;
        svga_recalctimings(&dev->svga);
        dev->svga.dpms = 0;
    }
}

static void *
mach64_common_init(const device_t *info)
{
    svga_t   *svga;
    mach64_t *mach64 = calloc(1, sizeof(mach64_t));
    reset_state[monitor_index_global] = calloc(1, sizeof(mach64_t));

    svga = &mach64->svga;

    mach64->type = info->local & 0xff;
    mach64->vram_size = (mach64->type == MACH64_CT || mach64->type == MACH64_VT || mach64->type == MACH64_VT3) ? 2 : ((info->local & (1 << 20)) ? 4 : device_get_config_int("memory"));
    mach64->vram_mask = (mach64->vram_size << 20) - 1;
    mach64->io_base = 0; /* PCI 40h select: 0 = 2ECh */

    if (mach64->type > MACH64_GX)
        svga_init(info, svga, mach64, mach64->vram_size << 20,
                  mach64_recalctimings,
                  mach64_in, mach64_out,
                  mach64_int_hwcursor_draw,
                  mach64_overlay_draw);
    else
        svga_init(info, svga, mach64, mach64->vram_size << 20,
                  mach64_recalctimings,
                  mach64_in, mach64_out,
                  NULL,
                  mach64_overlay_draw);

    mem_mapping_add(&mach64->linear_mapping, 0, 0, mach64_read_linear, mach64_readw_linear, mach64_readl_linear, mach64_write_linear, mach64_writew_linear, mach64_writel_linear, NULL, MEM_MAPPING_EXTERNAL, svga);
    mem_mapping_add(&mach64->linear_mapping_big_endian, 0, 0, mach64_readb_be, mach64_readw_be, mach64_readl_be, mach64_writeb_be, mach64_writew_be, mach64_writel_be, NULL, MEM_MAPPING_EXTERNAL, svga);
    mem_mapping_add(&mach64->mmio_linear_mapping, 0, 0, mach64_ext_readb, mach64_ext_readw, mach64_ext_readl, mach64_ext_writeb, mach64_ext_writew, mach64_ext_writel, NULL, MEM_MAPPING_EXTERNAL, mach64);
    mem_mapping_add(&mach64->mmio_linear_mapping_2, 0, 0, mach64_ext_readb, mach64_ext_readw, mach64_ext_readl, mach64_ext_writeb, mach64_ext_writew, mach64_ext_writel, NULL, MEM_MAPPING_EXTERNAL, mach64);
    /* The GX has only its 1K of registers there; the video memory below
       them stays in the aperture. The CT adds a second block at BF800. */
    if (mach64->type == MACH64_GX)
        mem_mapping_add(&mach64->mmio_mapping, 0xbfc00, 0x400, mach64_ext_readb, mach64_ext_readw, mach64_ext_readl, mach64_ext_writeb, mach64_ext_writew, mach64_ext_writel, NULL, MEM_MAPPING_EXTERNAL, mach64);
    else
        mem_mapping_add(&mach64->mmio_mapping, 0xbf800, 0x800, mach64_ext_readb, mach64_ext_readw, mach64_ext_readl, mach64_ext_writeb, mach64_ext_writew, mach64_ext_writel, NULL, MEM_MAPPING_EXTERNAL, mach64);
    mem_mapping_disable(&mach64->mmio_mapping);

    mach64_io_map(mach64);

    if (info->flags & DEVICE_PCI)
        pci_add_card((info->local & MACH64_FLAG_ONBOARD) ? PCI_ADD_VIDEO : PCI_ADD_NORMAL, mach64_pci_read, mach64_pci_write, mach64, &mach64->pci_slot);

    mach64->pci_regs[PCI_REG_COMMAND]       = 3;
    mach64->pci_regs[PCI_REG_ROM_BAR_BYTE0] = 0x00;
    mach64->pci_regs[PCI_REG_ROM_BAR_BYTE2] = 0x0c;
    mach64->pci_regs[PCI_REG_ROM_BAR_BYTE3] = 0x00;

    svga->clock_gen         = device_add(&ics2595_device);
    svga->line_callback     = mach64_line_callback;
    svga->translate_address = mach64_vga_translate;

    if (mach64->type >= MACH64_VT)
        svga->conv_16to32 = mach64_conv_16to32;

    mach64->dst_cntl = 3;

    mach64->thread_run = 1;
    mach64->wake_fifo_thread = thread_create_event();
    mach64->fifo_not_full_event = thread_create_event();
    mach64->fifo_thread = thread_create(mach64_fifo_thread, mach64);
    mach64->on_board = !!(info->local & MACH64_FLAG_ONBOARD);

    mach64->i2c = i2c_gpio_init("ddc_ati_mach64");
    mach64->i2c_tv = i2c_gpio_init("tv_ati_mach64");
    mach64->ddc = ddc_init(i2c_gpio_get_bus(mach64->i2c));

#ifdef DMA_BM
    mach64->dma.lock = thread_create_mutex();
#endif

    return mach64;
}

static void *
mach64gx_init(const device_t *info)
{
    mach64_t *mach64 = mach64_common_init(info);
    svga_t   *svga   = &mach64->svga;

    svga->ramdac            = device_add(&ati68860_ramdac_device);
    svga->dac_hwcursor_draw = ati68860_hwcursor_draw;

    svga->dac_hwcursor.cur_ysize = 64;
    svga->dac_hwcursor.cur_xsize = 64;
    svga->vblank_start           = mach64gx_vblank_start;

    if (!(info->flags & DEVICE_PCI))
        mach64->isa_irq = device_get_config_int("irq");

    if (info->flags & DEVICE_ISA16)
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_mach64_isa);
    else if (info->flags & DEVICE_PCI)
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_mach64_pci);
    else
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_mach64_vlb);

    mach64->pci            = !!(info->flags & DEVICE_PCI);
    mach64->vlb            = !!(info->flags & DEVICE_VLB);
    mach64->pci_id         = 'X' | ('G' << 8);
    mach64->config_chip_id = 0x000000d7;
    mach64->dac_cntl       = 5 << 16;             /*ATI 68860 RAMDAC*/
    /* CFG_MEM_TYPE (CONFIG_STAT0 bits 5:3, RRG 3-10) is the board's memory:
       1 VRAM (256Kx4, x8, x16) on the Graphics Pro Turbo, 3 DRAM (256Kx16)
       on the Graphics Xpression (mach64 User's Guide). The BIOS takes each
       mode's memory requirement from the VRAM or the DRAM column of its
       table by it. */
    mach64->config_stat0   = (5 << 9) | (((info->local & MACH64_FLAG_DRAM) ? 3 : 1) << 3); /*ATI 68860*/
    /* The board's straps (RRG 3-11): VGA and the chip enabled, the ROM at
       C0000. VLB: local bus DAC writes enabled. ISA: no 4 GB aperture. */
    mach64->config_stat0  |= (1u << 23) | (1u << 25) | (1u << 27);
    if (info->flags & DEVICE_VLB)
        mach64->config_stat0 |= (1u << 29);
    else if (!(info->flags & DEVICE_PCI))
        mach64->config_stat0 |= (1u << 31);
    mach64->bus_cntl = 0x000000ff; /* BUS_WS and BUS_ROM_WS default Fh (RRG 3-2) */
    mach64->mem_cntl = 0x00000400; /* MEM_CYC_LNTH default 2, MEM_SIZE 512K (RRG 3-67) */
    if (info->flags & DEVICE_PCI) {
        mach64->config_stat0 |= 7; /*PCI*/
        ati_eeprom_load(&mach64->eeprom, "mach64_pci.nvr", 1);
        rom_init(&mach64->bios_rom, BIOS_ROM_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        mem_mapping_disable(&mach64->bios_rom.mapping);
    } else if (info->flags & DEVICE_VLB) {
        mach64->config_stat0 |= 6; /*VLB*/
        ati_eeprom_load_default(&mach64->eeprom, (info->local & MACH64_FLAG_DRAM) ? "mach64_xpression_vlb.nvr" : "mach64_vlb.nvr", 1,
                                mach64_vlb_eeprom_default, sizeof(mach64_vlb_eeprom_default) / sizeof(mach64_vlb_eeprom_default[0]));
        if (info->local & MACH64_FLAG_DRAM)
            rom_init(&mach64->bios_rom, (char *) device_get_bios_file(info, device_get_config_bios("bios_ver"), 0),
                     0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL); /* Graphics Xpression */
        else
            rom_init(&mach64->bios_rom, BIOS_VLB_ROM_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    } else if (info->flags & DEVICE_ISA16) {
        mach64->config_stat0 |= 0; /*ISA 16-bit*/
        ati_eeprom_load(&mach64->eeprom, "mach64.nvr", 1);
        rom_init(&mach64->bios_rom, BIOS_ISA_ROM_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    }

    *reset_state[monitor_index_global] = *mach64;
    return mach64;
}
static void *
mach64ct_init(const device_t *info)
{
    mach64_t *mach64 = mach64_common_init(info);
    svga_t   *svga   = &mach64->svga;

    svga->dac_hwcursor_draw = NULL;

    svga->hwcursor.cur_ysize = 64;
    svga->hwcursor.cur_xsize = 64;

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_mach64_pci);

    mach64->pci                  = 1;
    mach64->vlb                  = 0;
    mach64->pci_id               = 'T' | ('C' << 8);
    mach64->config_chip_id       = mach64->pci_id;
    mach64->dac_cntl             = 1 << 16; /*Internal 24-bit DAC*/
    mach64->config_stat0         = 1; /* CFG_MEM_TYPE 1 = DRAM; 4 is reserved on the CT (RRG 3-12) */
    mach64->use_block_decoded_io = 4;

    ati_eeprom_load(&mach64->eeprom, "mach64ct.nvr", 1);

    if (!(info->local & MACH64_FLAG_ONBOARD))
        rom_init(&mach64->bios_rom, BIOS_ROMCT_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    mem_mapping_disable(&mach64->bios_rom.mapping);

    svga->vblank_start = mach64_vblank_start;
    svga->adv_flags |= FLAG_PANNING_ATI;

    *reset_state[monitor_index_global] = *mach64;
    return mach64;
}
static void *
mach64vt_init(const device_t *info)
{
    mach64_t *mach64 = mach64_common_init(info);
    svga_t   *svga   = &mach64->svga;

    svga->dac_hwcursor_draw = NULL;

    svga->hwcursor.cur_ysize = 64;
    svga->hwcursor.cur_xsize = 64;

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_mach64_pci);

    mach64->pci                  = 1;
    mach64->vlb                  = 0;
    mach64->pci_id               = 0x5654;
    mach64->config_chip_id       = 0x08005654;
    mach64->dac_cntl             = 1 << 16; /*Internal 24-bit DAC*/
    mach64->config_stat0         = 4;
    mach64->use_block_decoded_io = 4;

    ati_eeprom_load(&mach64->eeprom, "mach64vt1.nvr", 1);
    rom_init(&mach64->bios_rom, BIOS_ROMVT_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    mem_mapping_disable(&mach64->bios_rom.mapping);

    svga->vblank_start = mach64_vblank_start;
    svga->adv_flags   |= FLAG_PANNING_ATI;

    *reset_state[monitor_index_global] = *mach64;
    return mach64;
}
static void *
mach64vt2_init(const device_t *info)
{
    mach64_t *mach64 = mach64_common_init(info);
    svga_t   *svga   = &mach64->svga;

    svga->dac_hwcursor_draw = NULL;

    svga->hwcursor.cur_ysize = 64;
    svga->hwcursor.cur_xsize = 64;

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_mach64_pci);

    mach64->pci                  = 1;
    mach64->vlb                  = 0;
    mach64->pci_id               = 0x5654;
    mach64->config_chip_id       = 0x40005654;
    mach64->dac_cntl             = 1 << 16; /*Internal 24-bit DAC*/
    mach64->config_stat0         = 4;
    mach64->use_block_decoded_io = 4;

    ati_eeprom_load(&mach64->eeprom, "mach64vt.nvr", 1);
    rom_init(&mach64->bios_rom, BIOS_ROMVT2_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    mem_mapping_disable(&mach64->bios_rom.mapping);

    svga->vblank_start = mach64_vblank_start;
    svga->adv_flags   |= FLAG_PANNING_ATI;

    *reset_state[monitor_index_global] = *mach64;
    return mach64;
}

static void *
mach64vt3_onboard_init(const device_t *info)
{
    mach64_t *mach64 = mach64_common_init(info);
    svga_t   *svga   = &mach64->svga;

    svga->dac_hwcursor_draw = NULL;

    svga->hwcursor.cur_ysize = 64;
    svga->hwcursor.cur_xsize = 64;

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_mach64_pci);

    mach64->pci                  = 1;
    mach64->vlb                  = 0;
    mach64->pci_id               = 0x5655;
    mach64->config_chip_id       = 0x9A005655;
    mach64->dac_cntl             = 1 << 16; /*Internal 24-bit DAC*/
    mach64->config_stat0         = 4;
    mach64->use_block_decoded_io = 4;

    mem_mapping_disable(&mach64->bios_rom.mapping);

    svga->vblank_start = mach64_vblank_start;
    svga->adv_flags   |= FLAG_PANNING_ATI;

    *reset_state[monitor_index_global] = *mach64;
    return mach64;
}

int
mach64gx_available(void)
{
    return rom_present(BIOS_ROM_PATH);
}
int
mach64gx_isa_available(void)
{
    return rom_present(BIOS_ISA_ROM_PATH);
}
int
mach64gx_xpression_vlb_available(void)
{
    return rom_present(BIOS_XPRESSION_VLB_27803_PATH);
}

int
mach64gx_vlb_available(void)
{
    return rom_present(BIOS_VLB_ROM_PATH);
}
int
mach64ct_available(void)
{
    return rom_present(BIOS_ROMCT_PATH);
}
int
mach64vt_available(void)
{
    return rom_present(BIOS_ROMVT_PATH);
}
int
mach64vt2_available(void)
{
    return rom_present(BIOS_ROMVT2_PATH);
}

void
mach64_close(void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;

#ifdef DMA_BM
    mach64->dma.state = 0;
#endif
    mach64->thread_run = 0;
    thread_set_event(mach64->wake_fifo_thread);
    thread_wait(mach64->fifo_thread);
    thread_destroy_event(mach64->fifo_not_full_event);
    thread_destroy_event(mach64->wake_fifo_thread);
#ifdef DMA_BM
    thread_close_mutex(mach64->dma.lock);
#endif

    svga_close(&mach64->svga);

    ddc_close(mach64->ddc);
    i2c_gpio_close(mach64->i2c);
    i2c_gpio_close(mach64->i2c_tv);

    free(reset_state[mach64->svga.monitor_index]);
    free(mach64);
}

void
mach64_speed_changed(void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;

    svga_recalctimings(&mach64->svga);
}

void
mach64_force_redraw(void *priv)
{
    mach64_t *mach64 = (mach64_t *) priv;

    mach64->svga.fullchange = mach64->svga.monitor->mon_changeframecount;
}

// clang-format off
/* The Graphics Pro Turbo (ISA and VLB BIOSes): 2 or 4 MB VRAM, and the
   interrupt jumper: 2, 3 or 5 on the VLB, and 10 as well on ISA, where
   IRQ 2 is IRQ 9 on an AT (mach64 User's Guide). Left unfitted, as ATI
   reserved it for future use. */
static const device_config_t mach64gx_vram_config[] = {
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 4,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "2 MB", .value = 2 },
            { .description = "4 MB", .value = 4 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0  },
            { .description = "IRQ 2",    .value = 9  },
            { .description = "IRQ 3",    .value = 3  },
            { .description = "IRQ 5",    .value = 5  },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t mach64gx_vram_isa_config[] = {
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 4,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "2 MB", .value = 2 },
            { .description = "4 MB", .value = 4 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0  },
            { .description = "IRQ 2",    .value = 9  },
            { .description = "IRQ 3",    .value = 3  },
            { .description = "IRQ 5",    .value = 5  },
            { .description = "IRQ 10",   .value = 10 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

/* The Graphics Xpression VLB: 1 or 2 MB DRAM, and its BIOS revisions. */
static const device_config_t mach64gx_xpression_config[] = {
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "1 MB", .value = 1 },
            { .description = "2 MB", .value = 2 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_ver",
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "113_27803_102",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "113-27803-102 (1995/03/16)",
                .internal_name = "113_27803_102",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 32768,
                .files         = { BIOS_XPRESSION_VLB_27803_PATH, "" }
            },
            {
                .name          = "113-27804-101 (1995/01/20)",
                .internal_name = "113_27804_101",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 32768,
                .files         = { BIOS_XPRESSION_VLB_27804_PATH, "" }
            },
            {
                .name          = "113-27802-101 (1994/08/24)",
                .internal_name = "113_27802_101",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 32768,
                .files         = { BIOS_XPRESSION_VLB_27802_PATH, "" }
            },
            { .files_no = 0 }
        }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0  },
            { .description = "IRQ 2",    .value = 9  },
            { .description = "IRQ 3",    .value = 3  },
            { .description = "IRQ 5",    .value = 5  },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

/* PCI: a BIOS for more than one board; DRAM, as before. */
static const device_config_t mach64gx_config[] = {
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 4,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "1 MB", .value = 1 },
            { .description = "2 MB", .value = 2 },
            { .description = "4 MB", .value = 4 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t mach64vt2_config[] = {
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 4,
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
// clang-format on

const device_t mach64gx_isa_device = {
    .name          = "ATI Graphics Pro Turbo (Mach64GX) ISA",
    .internal_name = "mach64gx_isa",
    .flags         = DEVICE_ISA16,
    .local         = MACH64_GX,
    .init          = mach64gx_init,
    .close         = mach64_close,
    .reset         = NULL,
    .available     = mach64gx_isa_available,
    .speed_changed = mach64_speed_changed,
    .force_redraw  = mach64_force_redraw,
    .config        = mach64gx_vram_isa_config
};

const device_t mach64gx_vlb_device = {
    .name          = "ATI Graphics Pro Turbo (Mach64GX) VLB",
    .internal_name = "mach64gx_vlb",
    .flags         = DEVICE_VLB,
    .local         = MACH64_GX,
    .init          = mach64gx_init,
    .close         = mach64_close,
    .reset         = NULL,
    .available     = mach64gx_vlb_available,
    .speed_changed = mach64_speed_changed,
    .force_redraw  = mach64_force_redraw,
    .config        = mach64gx_vram_config
};

const device_t mach64gx_xpression_vlb_device = {
    .name          = "ATI Graphics Xpression (Mach64GX) VLB",
    .internal_name = "mach64gx_xpression_vlb",
    .flags         = DEVICE_VLB,
    .local         = MACH64_GX | MACH64_FLAG_DRAM,
    .init          = mach64gx_init,
    .close         = mach64_close,
    .reset         = NULL,
    .available     = mach64gx_xpression_vlb_available,
    .speed_changed = mach64_speed_changed,
    .force_redraw  = mach64_force_redraw,
    .config        = mach64gx_xpression_config
};

const device_t mach64gx_pci_device = {
    .name          = "ATI Mach64GX PCI",
    .internal_name = "mach64gx_pci",
    .flags         = DEVICE_PCI,
    .local         = MACH64_GX | MACH64_FLAG_DRAM,
    .init          = mach64gx_init,
    .close         = mach64_close,
    .reset         = NULL,
    .available     = mach64gx_available,
    .speed_changed = mach64_speed_changed,
    .force_redraw  = mach64_force_redraw,
    .config        = mach64gx_config
};

const device_t mach64ct_device = {
    .name          = "ATI Mach64CT",
    .internal_name = "mach64ct",
    .flags         = DEVICE_PCI,
    .local         = MACH64_CT,
    .init          = mach64ct_init,
    .close         = mach64_close,
    .reset         = NULL,
    .available     = mach64ct_available,
    .speed_changed = mach64_speed_changed,
    .force_redraw  = mach64_force_redraw,
    .config        = NULL
};

const device_t mach64ct_device_onboard = {
    .name          = "ATI Mach64CT (On-Board)",
    .internal_name = "mach64ct_onboard",
    .flags         = DEVICE_PCI,
    .local         = MACH64_CT | MACH64_FLAG_ONBOARD,
    .init          = mach64ct_init,
    .close         = mach64_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = mach64_speed_changed,
    .force_redraw  = mach64_force_redraw,
    .config        = NULL
};

const device_t mach64vt_device = {
    .name          = "ATI Mach64VT",
    .internal_name = "mach64vt",
    .flags         = DEVICE_PCI,
    .local         = MACH64_VT,
    .init          = mach64vt_init,
    .close         = mach64_close,
    .reset         = NULL,
    .available     = mach64vt_available,
    .speed_changed = mach64_speed_changed,
    .force_redraw  = mach64_force_redraw,
    .config        = NULL
};

const device_t mach64vt2_device = {
    .name          = "ATI Mach64VT2",
    .internal_name = "mach64vt2",
    .flags         = DEVICE_PCI,
    .local         = MACH64_VT2,
    .init          = mach64vt2_init,
    .close         = mach64_close,
    .reset         = NULL,
    .available     = mach64vt2_available,
    .speed_changed = mach64_speed_changed,
    .force_redraw  = mach64_force_redraw,
    .config        = mach64vt2_config
};

const device_t mach64vt3_onboard_device = {
    .name          = "ATI Mach64VT3 (On-Board)",
    .internal_name = "mach64vt3_onboard",
    .flags         = DEVICE_PCI,
    .local         = MACH64_VT3 | MACH64_FLAG_ONBOARD,
    .init          = mach64vt3_onboard_init,
    .close         = mach64_close,
    .reset         = mach64_reset,
    .available     = NULL,
    .speed_changed = mach64_speed_changed,
    .force_redraw  = mach64_force_redraw,
    .config        = NULL
};
