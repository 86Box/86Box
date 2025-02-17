/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Voodoo Banshee and 3 specific emulation.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2008-2020 Sarah Walker.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_voodoo_common.h>
#include <86box/vid_voodoo_display.h>
#include <86box/vid_voodoo_fifo.h>
#include <86box/vid_voodoo_regs.h>
#include <86box/vid_voodoo_render.h>

#define ROM_BANSHEE                 "roms/video/voodoo/Pci_sg.rom"
#define ROM_CREATIVE_BANSHEE        "roms/video/voodoo/BlasterPCI.rom"
#define ROM_VOODOO3_1000            "roms/video/voodoo/1k11sg.rom"
#define ROM_VOODOO3_2000            "roms/video/voodoo/2k11sd.rom"
#define ROM_VOODOO3_3000            "roms/video/voodoo/3k12sd.rom"
#define ROM_VOODOO3_3500_AGP_NTSC   "roms/video/voodoo/35k05n.rom"
#define ROM_VOODOO3_3500_AGP_PAL    "roms/video/voodoo/35k05p.rom"
#define ROM_VOODOO3_3500_AGP_COMPAQ "roms/video/voodoo/V3_3500_AGP_SD_2.15.05_Compaq.rom"
#define ROM_VOODOO3_3500_SE_AGP     "roms/video/voodoo/V3_3500_AGP_SD_2.15.06_NTSC_Falcon_Northwest.rom"
#define ROM_VOODOO3_3500_SI_AGP     "roms/video/voodoo/V3_3500_AGP_SD_2.15.07_PAL_3500TV-SI.rom"
#define ROM_VELOCITY_100            "roms/video/voodoo/Velocity100.VBI"
#define ROM_VELOCITY_200            "roms/video/voodoo/Velocity200sg.rom"

static video_timings_t timing_banshee     = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };
static video_timings_t timing_banshee_agp = { .type = VIDEO_AGP, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };

#ifdef CLAMP
#    undef CLAMP
#endif

static uint8_t vb_filter_v1_rb[256][256];
static uint8_t vb_filter_v1_g[256][256];

static uint8_t vb_filter_bx_rb[256][256];
static uint8_t vb_filter_bx_g[256][256];

enum {
    TYPE_BANSHEE = 0,
    TYPE_V3_1000,
    TYPE_V3_2000,
    TYPE_V3_3000,
    TYPE_V3_3500,
    TYPE_V3_3500_COMPAQ,
    TYPE_V3_3500_SI,
    TYPE_VELOCITY100,
    TYPE_VELOCITY200
};

typedef struct banshee_t {
    svga_t svga;

    rom_t bios_rom;

    uint8_t pci_regs[256];

    uint32_t memBaseAddr0;
    uint32_t memBaseAddr1;
    uint32_t ioBaseAddr;

    uint32_t agpInit0;
    uint32_t dramInit0, dramInit1;
    uint32_t lfbMemoryConfig;
    uint32_t miscInit0, miscInit1;
    uint32_t pciInit0;
    uint32_t vgaInit0, vgaInit1;

    uint32_t command_2d;
    uint32_t srcBaseAddr_2d;

    uint32_t pllCtrl0, pllCtrl1, pllCtrl2;

    uint32_t dacMode;
    int      dacAddr;

    uint32_t vidDesktopOverlayStride;
    uint32_t vidDesktopStartAddr;
    uint32_t vidProcCfg;
    uint32_t vidScreenSize;
    uint32_t vidSerialParallelPort;

    uint32_t agpReqSize;
    uint32_t agpHostAddressHigh;
    uint32_t agpHostAddressLow;
    uint32_t agpGraphicsAddress;
    uint32_t agpGraphicsStride;

    int overlay_pix_fmt;

    uint32_t hwCurPatAddr, hwCurLoc, hwCurC0, hwCurC1;

    uint32_t intrCtrl;

    uint32_t overlay_buffer[2][4096];

    mem_mapping_t linear_mapping;

    mem_mapping_t reg_mapping_low;  /*0000000-07fffff*/
    mem_mapping_t reg_mapping_high; /*0c00000-1ffffff - Windows 2000 puts the BIOS ROM in between these two areas*/

    voodoo_t *voodoo;

    uint32_t desktop_addr;
    int      desktop_y;
    uint32_t desktop_stride_tiled;

    int type, agp;
    int has_bios, vblank_irq;

    uint8_t pci_slot;
    uint8_t irq_state;

    void *i2c, *i2c_ddc, *ddc;
} banshee_t;

enum {
    Init_status          = 0x00,
    Init_pciInit0        = 0x04,
    Init_lfbMemoryConfig = 0x0c,
    Init_miscInit0       = 0x10,
    Init_miscInit1       = 0x14,
    Init_dramInit0       = 0x18,
    Init_dramInit1       = 0x1c,
    Init_agpInit0        = 0x20,
    Init_vgaInit0        = 0x28,
    Init_vgaInit1        = 0x2c,
    Init_2dCommand       = 0x30,
    Init_2dSrcBaseAddr   = 0x34,
    Init_strapInfo       = 0x38,

    PLL_pllCtrl0 = 0x40,
    PLL_pllCtrl1 = 0x44,
    PLL_pllCtrl2 = 0x48,

    DAC_dacMode = 0x4c,
    DAC_dacAddr = 0x50,
    DAC_dacData = 0x54,

    Video_vidProcCfg                   = 0x5c,
    Video_maxRgbDelta                  = 0x58,
    Video_hwCurPatAddr                 = 0x60,
    Video_hwCurLoc                     = 0x64,
    Video_hwCurC0                      = 0x68,
    Video_hwCurC1                      = 0x6c,
    Video_vidSerialParallelPort        = 0x78,
    Video_vidScreenSize                = 0x98,
    Video_vidOverlayStartCoords        = 0x9c,
    Video_vidOverlayEndScreenCoords    = 0xa0,
    Video_vidOverlayDudx               = 0xa4,
    Video_vidOverlayDudxOffsetSrcWidth = 0xa8,
    Video_vidOverlayDvdy               = 0xac,
    Video_vidOverlayDvdyOffset         = 0xe0,
    Video_vidDesktopStartAddr          = 0xe4,
    Video_vidDesktopOverlayStride      = 0xe8,
};

enum {
    cmdBaseAddr0  = 0x20,
    cmdBaseSize0  = 0x24,
    cmdBump0      = 0x28,
    cmdRdPtrL0    = 0x2c,
    cmdRdPtrH0    = 0x30,
    cmdAMin0      = 0x34,
    cmdAMax0      = 0x3c,
    cmdStatus0    = 0x40,
    cmdFifoDepth0 = 0x44,
    cmdHoleCnt0   = 0x48,

    cmdBaseAddr1  = 0x50,
    cmdBaseSize1  = 0x50 + 0x4,
    cmdBump1      = 0x50 + 0x8,
    cmdRdPtrL1    = 0x50 + 0xc,
    cmdRdPtrH1    = 0x50 + 0x10,
    cmdAMin1      = 0x50 + 0x14,
    cmdAMax1      = 0x50 + 0x1c,
    cmdStatus1    = 0x50 + 0x20,
    cmdFifoDepth1 = 0x50 + 0x24,
    cmdHoleCnt1   = 0x50 + 0x28,

    Agp_agpReqSize         = 0x00,
    Agp_agpHostAddressLow  = 0x04,
    Agp_agpHostAddressHigh = 0x08,
    Agp_agpGraphicsAddress = 0x0C,
    Agp_agpGraphicsStride  = 0x10,
};

#define VGAINIT0_RAMDAC_8BIT                (1 << 2)
#define VGAINIT0_EXTENDED_SHIFT_OUT         (1 << 12)

#define VIDPROCCFG_VIDPROC_ENABLE           (1 << 0)
#define VIDPROCCFG_CURSOR_MODE              (1 << 1)
#define VIDPROCCFG_INTERLACE                (1 << 3)
#define VIDPROCCFG_HALF_MODE                (1 << 4)
#define VIDPROCCFG_OVERLAY_ENABLE           (1 << 8)
#define VIDPROCCFG_DESKTOP_CLUT_BYPASS      (1 << 10)
#define VIDPROCCFG_OVERLAY_CLUT_BYPASS      (1 << 11)
#define VIDPROCCFG_DESKTOP_CLUT_SEL         (1 << 12)
#define VIDPROCCFG_OVERLAY_CLUT_SEL         (1 << 13)
#define VIDPROCCFG_H_SCALE_ENABLE           (1 << 14)
#define VIDPROCCFG_V_SCALE_ENABLE           (1 << 15)
#define VIDPROCCFG_FILTER_MODE_MASK         (3 << 16)
#define VIDPROCCFG_FILTER_MODE_POINT        (0 << 16)
#define VIDPROCCFG_FILTER_MODE_DITHER_2X2   (1 << 16)
#define VIDPROCCFG_FILTER_MODE_DITHER_4X4   (2 << 16)
#define VIDPROCCFG_FILTER_MODE_BILINEAR     (3 << 16)
#define VIDPROCCFG_DESKTOP_PIX_FORMAT       ((banshee->vidProcCfg >> 18) & 7)
#define VIDPROCCFG_OVERLAY_PIX_FORMAT       ((banshee->vidProcCfg >> 21) & 7)
#define VIDPROCCFG_OVERLAY_PIX_FORMAT_SHIFT (21)
#define VIDPROCCFG_OVERLAY_PIX_FORMAT_MASK  (7 << VIDPROCCFG_OVERLAY_PIX_FORMAT_SHIFT)
#define VIDPROCCFG_DESKTOP_TILE             (1 << 24)
#define VIDPROCCFG_OVERLAY_TILE             (1 << 25)
#define VIDPROCCFG_2X_MODE                  (1 << 26)
#define VIDPROCCFG_HWCURSOR_ENA             (1 << 27)

#define OVERLAY_FMT_565                     (1)
#define OVERLAY_FMT_YUYV422                 (5)
#define OVERLAY_FMT_UYVY422                 (6)
#define OVERLAY_FMT_565_DITHER              (7)

#define OVERLAY_START_X_MASK                (0xfff)
#define OVERLAY_START_Y_SHIFT               (12)
#define OVERLAY_START_Y_MASK                (0xfff << OVERLAY_START_Y_SHIFT)

#define OVERLAY_END_X_MASK                  (0xfff)
#define OVERLAY_END_Y_SHIFT                 (12)
#define OVERLAY_END_Y_MASK                  (0xfff << OVERLAY_END_Y_SHIFT)

#define OVERLAY_SRC_WIDTH_SHIFT             (19)
#define OVERLAY_SRC_WIDTH_MASK              (0x1fff << OVERLAY_SRC_WIDTH_SHIFT)

#define VID_STRIDE_OVERLAY_SHIFT            (16)
#define VID_STRIDE_OVERLAY_MASK             (0x7fff << VID_STRIDE_OVERLAY_SHIFT)

#define VID_DUDX_MASK                       (0xffffff)
#define VID_DVDY_MASK                       (0xffffff)

#define PIX_FORMAT_8                        0
#define PIX_FORMAT_RGB565                   1
#define PIX_FORMAT_RGB24                    2
#define PIX_FORMAT_RGB32                    3

#define VIDSERIAL_DDC_EN                    (1 << 18)
#define VIDSERIAL_DDC_DCK_W                 (1 << 19)
#define VIDSERIAL_DDC_DDA_W                 (1 << 20)
#define VIDSERIAL_DDC_DCK_R                 (1 << 21)
#define VIDSERIAL_DDC_DDA_R                 (1 << 22)
#define VIDSERIAL_I2C_EN                    (1 << 23)
#define VIDSERIAL_I2C_SCK_W                 (1 << 24)
#define VIDSERIAL_I2C_SDA_W                 (1 << 25)
#define VIDSERIAL_I2C_SCK_R                 (1 << 26)
#define VIDSERIAL_I2C_SDA_R                 (1 << 27)

#define MISCINIT0_Y_ORIGIN_SWAP_SHIFT       (18)
#define MISCINIT0_Y_ORIGIN_SWAP_MASK        (0xfff << MISCINIT0_Y_ORIGIN_SWAP_SHIFT)

#ifdef ENABLE_BANSHEE_LOG
int banshee_do_log = ENABLE_BANSHEE_LOG;

static void
banshee_log(const char *fmt, ...)
{
    va_list ap;

    if (banshee_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define banshee_log(fmt, ...)
#endif

static uint32_t banshee_status(banshee_t *banshee);

static int
banshee_vga_vsync_enabled(banshee_t *banshee)
{
    if (!(banshee->svga.crtc[0x11] & 0x20) && (banshee->svga.crtc[0x11] & 0x10) && ((banshee->pciInit0 >> 18) & 1) != 0)
        return 1;
    return 0;
}

static void
banshee_update_irqs(banshee_t *banshee)
{
    if (banshee->vblank_irq > 0 && banshee_vga_vsync_enabled(banshee)) {
        pci_set_irq(banshee->pci_slot, PCI_INTA, &banshee->irq_state);
    } else {
        pci_clear_irq(banshee->pci_slot, PCI_INTA, &banshee->irq_state);
    }
}

static void
banshee_vblank_start(svga_t *svga)
{
    banshee_t *banshee = (banshee_t *) svga->priv;
    if (banshee->vblank_irq >= 0) {
        banshee->vblank_irq = 1;
        banshee_update_irqs(banshee);
    }
}

static void
banshee_out(uint16_t addr, uint8_t val, void *priv)
{
    banshee_t *banshee = (banshee_t *) priv;
    svga_t    *svga    = &banshee->svga;
    uint8_t    old;

#if 0
    if (addr != 0x3c9)
        banshee_log("banshee_out : %04X %02X  %04X:%04X\n", addr, val, CS,cpu_state.pc);
#endif
    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;
            if (old != val) {
                if (svga->crtcreg == 0x11) {
                    if (!(val & 0x10)) {
                        if (banshee->vblank_irq > 0)
                            banshee->vblank_irq = -1;
                    } else if (banshee->vblank_irq < 0) {
                        banshee->vblank_irq = 0;
                    }
                    banshee_update_irqs(banshee);
                    if ((val & ~0x30) == (old & ~0x30))
                        old = val;
                }
                if (svga->crtcreg < 0xe || svga->crtcreg > 0x11 || (svga->crtcreg == 0x11 && old != val)) {
                    if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                        svga->fullchange = 3;
                        svga->ma_latch   = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
                    } else {
                        svga->fullchange = changeframecount;
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

static uint8_t
banshee_in(uint16_t addr, void *priv)
{
    banshee_t *banshee = (banshee_t *) priv;
    svga_t    *svga    = &banshee->svga;
    uint8_t    temp;

#if 0
    if (addr != 0x3da)
        banshee_log("banshee_in : %04X ", addr);
#endif

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c2:
            if ((svga->vgapal[0].r + svga->vgapal[0].g + svga->vgapal[0].b) >= 0x40)
                temp = 0;
            else
                temp = 0x10;
            if (banshee->vblank_irq > 0)
                temp |= 0x80;
            break;
        case 0x3D4:
            temp = svga->crtcreg;
            break;
        case 0x3D5:
            temp = svga->crtc[svga->crtcreg];
            break;
        default:
            temp = svga_in(addr, svga);
            break;
    }
#if 0
    if (addr != 0x3da)
        banshee_log("%02X  %04X:%04X %i\n", temp, CS,cpu_state.pc, ins);
#endif
    return temp;
}

static void
banshee_updatemapping(banshee_t *banshee)
{
    svga_t *svga = &banshee->svga;

    if (!(banshee->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)) {
#if 0
        banshee_log("Update mapping - PCI disabled\n");
#endif
        mem_mapping_disable(&svga->mapping);
        mem_mapping_disable(&banshee->linear_mapping);
        mem_mapping_disable(&banshee->reg_mapping_low);
        mem_mapping_disable(&banshee->reg_mapping_high);
        return;
    }

    banshee_log("Update mapping - bank %02X ", svga->gdcreg[6] & 0xc);
    switch (svga->gdcreg[6] & 0xc) { /*Banked framebuffer*/
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

    banshee_log("Linear framebuffer %08X  ", banshee->memBaseAddr1);
    mem_mapping_set_addr(&banshee->linear_mapping, banshee->memBaseAddr1, 32 << 20);
    banshee_log("registers %08X\n", banshee->memBaseAddr0);
    mem_mapping_set_addr(&banshee->reg_mapping_low, banshee->memBaseAddr0, 8 << 20);
    mem_mapping_set_addr(&banshee->reg_mapping_high, banshee->memBaseAddr0 + 0xc00000, 20 << 20);
}

uint32_t
banshee_conv_16to32(svga_t* svga, uint16_t color, UNUSED(uint8_t bpp))
{
    banshee_t *banshee = (banshee_t *) svga->priv;
    uint32_t ret = 0x00000000;
    uint16_t src_b = (color & 0x1f) << 3;
    uint16_t src_g = (color & 0x7e0) >> 3;
    uint16_t src_r = (color & 0xf800) >> 8;

    if (banshee->vidProcCfg & VIDPROCCFG_DESKTOP_CLUT_SEL) {
        src_b += 256;
        src_g += 256;
        src_r += 256;
    }

    if (svga->lut_map) {
        uint8_t b = getcolr(svga->pallook[src_b]);
        uint8_t g = getcolg(svga->pallook[src_g]);
        uint8_t r = getcolb(svga->pallook[src_r]);
        ret = (video_16to32[color] & 0xFF000000) | makecol(r, g, b);
    } else
        ret = video_16to32[color];

    return ret;
}

static void
banshee_render_16bpp_tiled(svga_t *svga)
{
    banshee_t *banshee = (banshee_t *) svga->priv;
    uint32_t  *p = &(svga->monitor->target_buffer->line[svga->displine + svga->y_add])[svga->x_add];
    uint32_t   addr;
    int        drawn = 0;

    if ((svga->displine + svga->y_add) < 0)
        return;

    if (banshee->vidProcCfg & VIDPROCCFG_HALF_MODE)
        addr = banshee->desktop_addr + ((banshee->desktop_y >> 1) & 31) * 128 + ((banshee->desktop_y >> 6) * banshee->desktop_stride_tiled);
    else
        addr = banshee->desktop_addr + (banshee->desktop_y & 31) * 128 + ((banshee->desktop_y >> 5) * banshee->desktop_stride_tiled);

    if (addr >= svga->vram_max)
        return;

    for (int x = 0; x < svga->hdisp; x += 64) {
        if (svga->hwcursor_on || svga->overlay_on)
            svga->changedvram[addr >> 12] = 2;
        if (svga->changedvram[addr >> 12] || svga->fullchange) {
            const uint16_t *vram_p = (uint16_t *) &svga->vram[addr & svga->vram_display_mask];

            for (uint8_t xx = 0; xx < 64; xx++)
                *p++ = banshee_conv_16to32(svga, *vram_p++, 16);

            drawn = 1;
        } else
            p += 64;
        addr += 128 * 32;
    }

    if (drawn) {
        if (svga->firstline_draw == 2000)
            svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
    }

    banshee->desktop_y++;
}

static void
banshee_recalctimings(svga_t *svga)
{
    banshee_t      *banshee = (banshee_t *) svga->priv;
    const voodoo_t *voodoo  = banshee->voodoo;

    /*7 R/W Horizontal Retrace End bit 5. -
      6 R/W Horizontal Retrace Start bit 8 0x4
      5 R/W Horizontal Blank End bit 6. -
      4 R/W Horizontal Blank Start bit 8. 0x3 ---- Erratum: Actually, 0x02!
      3 R/W Reserved. -
      2 R/W Horizontal Display Enable End bit 8. 0x1
      1 R/W Reserved. -
      0 R/W Horizontal Total bit 8. 0x0*/
    if (svga->crtc[0x1a] & 0x01)
        svga->htotal += 0x100;
    if (svga->crtc[0x1a] & 0x04)
        svga->hdisp += 0x100;

     if (banshee->vidProcCfg & VIDPROCCFG_VIDPROC_ENABLE) {
        /* Video processing mode - assume timings akin to Cirrus' special blanking mode,
           that is, no overscan and relying on display end to blank. */
        if (banshee->vgaInit0 & 0x40) {
            svga->hblankstart     = svga->crtc[1]/* + ((svga->crtc[3] >> 5) & 3)*/ +
                                   (((svga->crtc[0x1a] & 0x04) >> 2) << 8);
            svga->hblank_end_mask = 0x0000007f;
        } else {
            svga->hblankstart     = svga->crtc[1]/* + ((svga->crtc[3] >> 5) & 3)*/;
            svga->hblank_end_mask = 0x0000003f;
        }
        svga->hblank_end_val = svga->htotal - 1 /* + ((svga->crtc[3] >> 5) & 3)*/;

        /* In this mode, the dots per clock are always 8 or 16, never 9 or 18. */
        if (!svga->scrblank && svga->attr_palette_enable)
            svga->dots_per_clock = (svga->seqregs[1] & 8) ? 16 : 8;

        svga->monitor->mon_overscan_y = 0;
        svga->monitor->mon_overscan_x = 0;

        /* Also make sure vertical blanking starts on display end. */
        svga->vblankstart = svga->dispend;

        svga->linedbl     = 0;
     } else {
        if (banshee->vgaInit0 & 0x40) {
            svga->hblankstart     = (((svga->crtc[0x1a] & 0x10) >> 4) << 8) + svga->crtc[2];
            svga->hblank_end_val  = (svga->crtc[3] & 0x1f) | (((svga->crtc[5] & 0x80) >> 7) << 5) |
                                    (((svga->crtc[0x1a] & 0x20) >> 5) << 6);
            svga->hblank_end_mask = 0x0000007f;
        } else {
            svga->hblankstart     =  svga->crtc[2];
            svga->hblank_end_val  =  (svga->crtc[3] & 0x1f) | (((svga->crtc[5] & 0x80) >> 7) << 5);
            svga->hblank_end_mask =  0x0000003f;
        }
    }

    /*6 R/W Vertical Retrace Start bit 10 0x10
      5 R/W Reserved. -
      4 R/W Vertical Blank Start bit 10. 0x15
      3 R/W Reserved. -
      2 R/W Vertical Display Enable End bit 10 0x12
      1 R/W Reserved. -
      0 R/W Vertical Total bit 10. 0x6*/
    if (svga->crtc[0x1b] & 0x01)
        svga->vtotal += 0x400;
    if (svga->crtc[0x1b] & 0x04)
        svga->dispend += 0x400;
    if (svga->crtc[0x1b] & 0x10)
        svga->vblankstart += 0x400;
    if (svga->crtc[0x1b] & 0x40)
        svga->vsyncstart += 0x400;

#if 0
    banshee_log("svga->hdisp=%i\n", svga->hdisp);
#endif

    svga->interlace = 0;

    if (banshee->vgaInit0 & VGAINIT0_EXTENDED_SHIFT_OUT) {
        switch (VIDPROCCFG_DESKTOP_PIX_FORMAT) {
            case PIX_FORMAT_8:
                svga->render = svga_render_8bpp_highres;
                svga->bpp    = 8;
                break;
            case PIX_FORMAT_RGB565:
                svga->render = (banshee->vidProcCfg & VIDPROCCFG_DESKTOP_TILE) ? banshee_render_16bpp_tiled : svga_render_16bpp_highres;
                svga->bpp    = 16;
                break;
            case PIX_FORMAT_RGB24:
                svga->render = svga_render_24bpp_highres;
                svga->bpp    = 24;
                break;
            case PIX_FORMAT_RGB32:
                svga->render = svga_render_32bpp_highres;
                svga->bpp    = 32;
                break;
            default:
                fatal("Unknown pixel format %08x (vgaInit0=%08x)\n", VIDPROCCFG_DESKTOP_PIX_FORMAT, banshee->vgaInit0);
        }
        if (!(banshee->vidProcCfg & VIDPROCCFG_DESKTOP_TILE) && (banshee->vidProcCfg & VIDPROCCFG_HALF_MODE))
            svga->rowcount = 1;
        else
            svga->rowcount = 0;
        if (banshee->vidProcCfg & VIDPROCCFG_DESKTOP_TILE)
            svga->rowoffset = ((banshee->vidDesktopOverlayStride & 0x3fff) * 128) >> 3;
        else
            svga->rowoffset = (banshee->vidDesktopOverlayStride & 0x3fff) >> 3;
        svga->ma_latch                = banshee->vidDesktopStartAddr >> 2;
        banshee->desktop_stride_tiled = (banshee->vidDesktopOverlayStride & 0x3fff) * 128 * 32;
#if 0
        banshee_log("Extended shift out %i rowoffset=%i %02x\n", VIDPROCCFG_DESKTOP_PIX_FORMAT, svga->rowoffset, svga->crtc[1]);
#endif

        svga->char_width = 8;
        svga->split      = 99999;

        if (banshee->vidProcCfg & VIDPROCCFG_2X_MODE) {
            svga->hdisp *= 2;
            svga->dots_per_clock *= 2;
        }

        svga->interlace = !!(banshee->vidProcCfg & VIDPROCCFG_INTERLACE);

        svga->overlay.ena = banshee->vidProcCfg & VIDPROCCFG_OVERLAY_ENABLE;

        svga->overlay.x         = voodoo->overlay.start_x;
        svga->overlay.y         = voodoo->overlay.start_y;
        svga->overlay.cur_xsize = voodoo->overlay.size_x;
        svga->overlay.cur_ysize = voodoo->overlay.size_y;
        svga->overlay.pitch     = (banshee->vidDesktopOverlayStride & VID_STRIDE_OVERLAY_MASK) >> VID_STRIDE_OVERLAY_SHIFT;
        if (banshee->vidProcCfg & VIDPROCCFG_OVERLAY_TILE)
            svga->overlay.pitch *= 128 * 32;
        if (svga->overlay.cur_xsize <= 0 || svga->overlay.cur_ysize <= 0)
            svga->overlay.ena = 0;
        if (svga->overlay.ena) {
#if 0
            banshee_log("Overlay enabled : start=%i,%i end=%i,%i size=%i,%i pitch=%x\n",
                        voodoo->overlay.start_x, voodoo->overlay.start_y,
                        voodoo->overlay.end_x, voodoo->overlay.end_y,
                        voodoo->overlay.size_x, voodoo->overlay.size_y,
                        svga->overlay.pitch);
#endif
            if (!voodoo->overlay.start_x && !voodoo->overlay.start_y && svga->hdisp == voodoo->overlay.size_x && svga->dispend == voodoo->overlay.size_y) {
                /*Overlay is full screen, so don't bother rendering the desktop
                  behind it*/
                svga->render = svga_render_null;
                svga->bpp    = 0;
            }
        }
    } else {
#if 0
        banshee_log("Normal shift out\n");
#endif
        svga->bpp = 8;
    }

    svga->fb_only = (banshee->vidProcCfg & VIDPROCCFG_VIDPROC_ENABLE);

    if (((svga->miscout >> 2) & 3) == 3) {
        int    k    = banshee->pllCtrl0 & 3;
        int    m    = (banshee->pllCtrl0 >> 2) & 0x3f;
        int    n    = (banshee->pllCtrl0 >> 8) & 0xff;
        double freq = (((double) n + 2) / (((double) m + 2) * (double) (1 << k))) * 14318184.0;

        svga->clock = (cpuclock * (float) (1ULL << 32)) / freq;
#if 0
        svga->clock = cpuclock / freq;
#endif

#if 0
        banshee_log("svga->clock = %g %g  m=%i k=%i n=%i\n", freq, freq / 1000000.0, m, k, n);
#endif
    }
}

static void
banshee_ext_out(uint16_t addr, uint8_t val, void *priv)
{
#if 0
    banshee_t *banshee = (banshee_t *)priv;
    svga_t *svga = &banshee->svga;
#endif

#if 0
    banshee_log("banshee_ext_out: addr=%04x val=%02x\n", addr, val);
#endif

    switch (addr & 0xff) {
        case 0xb0:
        case 0xb1:
        case 0xb2:
        case 0xb3:
        case 0xb4:
        case 0xb5:
        case 0xb6:
        case 0xb7:
        case 0xb8:
        case 0xb9:
        case 0xba:
        case 0xbb:
        case 0xbc:
        case 0xbd:
        case 0xbe:
        case 0xbf:
        case 0xc0:
        case 0xc1:
        case 0xc2:
        case 0xc3:
        case 0xc4:
        case 0xc5:
        case 0xc6:
        case 0xc7:
        case 0xc8:
        case 0xc9:
        case 0xca:
        case 0xcb:
        case 0xcc:
        case 0xcd:
        case 0xce:
        case 0xcf:
        case 0xd0:
        case 0xd1:
        case 0xd2:
        case 0xd3:
        case 0xd4:
        case 0xd5:
        case 0xd6:
        case 0xd7:
        case 0xd8:
        case 0xd9:
        case 0xda:
        case 0xdb:
        case 0xdc:
        case 0xdd:
        case 0xde:
        case 0xdf:
            banshee_out((addr & 0xff) + 0x300, val, priv);
            break;

        default:
            banshee_log("bad banshee_ext_out: addr=%04x val=%02x\n", addr, val);
    }
}
static void
banshee_ext_outl(uint16_t addr, uint32_t val, void *priv)
{
    banshee_t *banshee = (banshee_t *) priv;
    voodoo_t  *voodoo  = banshee->voodoo;
    svga_t    *svga    = &banshee->svga;

#if 0
    banshee_log("banshee_ext_outl: addr=%04x val=%08x %04x(%08x):%08x\n", addr, val, CS,cs,cpu_state.pc);
#endif

    switch (addr & 0xff) {
        case Init_pciInit0:
            banshee->pciInit0  = val;
            voodoo->read_time  = (banshee->agp ? agp_nonburst_time : pci_nonburst_time) + (banshee->agp ? agp_burst_time : pci_burst_time) * ((val & 0x100) ? 2 : 1);
            voodoo->burst_time = (banshee->agp ? agp_burst_time : pci_burst_time) * ((val & 0x200) ? 1 : 0);
            voodoo->write_time = (banshee->agp ? agp_nonburst_time : pci_nonburst_time) + voodoo->burst_time;
            break;

        case Init_lfbMemoryConfig:
            banshee->lfbMemoryConfig = val;
#if 0
            banshee_log("lfbMemoryConfig=%08x\n", val);
#endif
            voodoo->tile_base         = (val & 0x1fff) << 12;
            voodoo->tile_stride       = 1024 << ((val >> 13) & 7);
            voodoo->tile_stride_shift = 10 + ((val >> 13) & 7);
            voodoo->tile_x            = ((val >> 16) & 0x7f) * 128;
            voodoo->tile_x_real       = ((val >> 16) & 0x7f) * 128 * 32;
            break;

        case Init_miscInit0:
            banshee->miscInit0    = val;
            voodoo->y_origin_swap = (val & MISCINIT0_Y_ORIGIN_SWAP_MASK) >> MISCINIT0_Y_ORIGIN_SWAP_SHIFT;
            break;
        case Init_miscInit1:
            banshee->miscInit1 = val;
            break;
        case Init_dramInit0:
            banshee->dramInit0 = val;
            break;
        case Init_dramInit1:
            banshee->dramInit1 = val;
            break;
        case Init_agpInit0:
            banshee->agpInit0 = val;
            break;

        case Init_2dCommand:
            banshee->command_2d = val;
            break;
        case Init_2dSrcBaseAddr:
            banshee->srcBaseAddr_2d = val;
            break;
        case Init_vgaInit0:
            banshee->vgaInit0 = val;
            svga_set_ramdac_type(svga, (val & VGAINIT0_RAMDAC_8BIT ? RAMDAC_8BIT : RAMDAC_6BIT));
            svga_recalctimings(svga);
            break;
        case Init_vgaInit1:
            banshee->vgaInit1   = val;
            svga->write_bank    = (val & 0x3ff) << 15;
            svga->read_bank     = ((val >> 10) & 0x3ff) << 15;
            svga->packed_chain4 = !!(val & 0x00100000);
            break;

        case PLL_pllCtrl0:
            banshee->pllCtrl0 = val;
            break;
        case PLL_pllCtrl1:
            banshee->pllCtrl1 = val;
            break;
        case PLL_pllCtrl2:
            banshee->pllCtrl2 = val;
            break;

        case DAC_dacMode:
            banshee->dacMode = val;
            svga->dpms       = !!(val & 0x0a);
            svga_recalctimings(svga);
            break;
        case DAC_dacAddr:
            banshee->dacAddr = val & 0x1ff;
            break;
        case DAC_dacData:
            svga->pallook[banshee->dacAddr] = val & 0xffffff;
            svga->fullchange                = changeframecount;
            break;

        case Video_vidProcCfg:
            banshee->vidProcCfg = val;
#if 0
            banshee_log("vidProcCfg=%08x\n", val);
#endif
            banshee->overlay_pix_fmt = (val & VIDPROCCFG_OVERLAY_PIX_FORMAT_MASK) >> VIDPROCCFG_OVERLAY_PIX_FORMAT_SHIFT;
            svga->hwcursor.ena       = val & VIDPROCCFG_HWCURSOR_ENA;
            svga->fullchange         = changeframecount;
            svga->lut_map            = !(val & VIDPROCCFG_DESKTOP_CLUT_BYPASS) && (svga->bpp < 24);
            svga_recalctimings(svga);
            break;

        case Video_maxRgbDelta:
            banshee->voodoo->scrfilterThreshold = val;
            if (val > 0x00)
                banshee->voodoo->scrfilterEnabled = 1;
            else
                banshee->voodoo->scrfilterEnabled = 0;
            voodoo_threshold_check(banshee->voodoo);
            banshee_log("Banshee Filter: %06x\n", val);
            break;

        case Video_hwCurPatAddr:
            banshee->hwCurPatAddr = val;
            svga->hwcursor.addr   = (val & 0xfffff0) + (svga->hwcursor.yoff * 16);
            break;
        case Video_hwCurLoc:
            banshee->hwCurLoc = val;
            svga->hwcursor.x  = (val & 0x7ff) - 64;
            svga->hwcursor.y  = ((val >> 16) & 0x7ff) - 64;
            if (svga->hwcursor.y < 0) {
                svga->hwcursor.yoff = -svga->hwcursor.y;
                svga->hwcursor.y    = 0;
            } else
                svga->hwcursor.yoff = 0;
            svga->hwcursor.addr      = (banshee->hwCurPatAddr & 0xfffff0) + (svga->hwcursor.yoff * 16);
            svga->hwcursor.cur_xsize = 64;
            svga->hwcursor.cur_ysize = 64;
#if 0
            banshee_log("hwCurLoc %08x %i\n", val, svga->hwcursor.y);
#endif
            break;
        case Video_hwCurC0:
            banshee->hwCurC0 = val;
            break;
        case Video_hwCurC1:
            banshee->hwCurC1 = val;
            break;

        case Video_vidSerialParallelPort:
            banshee->vidSerialParallelPort = val;
#if 0
            banshee_log("vidSerialParallelPort: write %08x %08x %04x(%08x):%08x\n", val, val & (VIDSERIAL_DDC_DCK_W | VIDSERIAL_DDC_DDA_W), CS,cs,cpu_state.pc);
#endif
            i2c_gpio_set(banshee->i2c_ddc, !!(val & VIDSERIAL_DDC_DCK_W), !!(val & VIDSERIAL_DDC_DDA_W));
            i2c_gpio_set(banshee->i2c, !!(val & VIDSERIAL_I2C_SCK_W), !!(val & VIDSERIAL_I2C_SDA_W));
            break;

        case Video_vidScreenSize:
            banshee->vidScreenSize = val;
            voodoo->h_disp         = (val & 0xfff) + 1;
            voodoo->v_disp         = (val >> 12) & 0xfff;
            break;
        case Video_vidOverlayStartCoords:
            voodoo->overlay.vidOverlayStartCoords = val;
            voodoo->overlay.start_x               = val & OVERLAY_START_X_MASK;
            voodoo->overlay.start_y               = (val & OVERLAY_START_Y_MASK) >> OVERLAY_START_Y_SHIFT;
            voodoo->overlay.size_x                = voodoo->overlay.end_x - voodoo->overlay.start_x;
            voodoo->overlay.size_y                = voodoo->overlay.end_y - voodoo->overlay.start_y;
            svga_recalctimings(svga);
            break;
        case Video_vidOverlayEndScreenCoords:
            voodoo->overlay.vidOverlayEndScreenCoords = val;
            voodoo->overlay.end_x                     = val & OVERLAY_END_X_MASK;
            voodoo->overlay.end_y                     = (val & OVERLAY_END_Y_MASK) >> OVERLAY_END_Y_SHIFT;
            voodoo->overlay.size_x                    = (voodoo->overlay.end_x - voodoo->overlay.start_x) + 1;
            voodoo->overlay.size_y                    = (voodoo->overlay.end_y - voodoo->overlay.start_y) + 1;
            svga_recalctimings(svga);
            break;
        case Video_vidOverlayDudx:
            voodoo->overlay.vidOverlayDudx = val & VID_DUDX_MASK;
#if 0
            banshee_log("vidOverlayDudx=%08x\n", val);
#endif
            break;
        case Video_vidOverlayDudxOffsetSrcWidth:
            voodoo->overlay.vidOverlayDudxOffsetSrcWidth = val;
            voodoo->overlay.overlay_bytes                = (val & OVERLAY_SRC_WIDTH_MASK) >> OVERLAY_SRC_WIDTH_SHIFT;
#if 0
            banshee_log("vidOverlayDudxOffsetSrcWidth=%08x\n", val);
#endif
            break;
        case Video_vidOverlayDvdy:
            voodoo->overlay.vidOverlayDvdy = val & VID_DVDY_MASK;
#if 0
            banshee_log("vidOverlayDvdy=%08x\n", val);
#endif
            break;
        case Video_vidOverlayDvdyOffset:
            voodoo->overlay.vidOverlayDvdyOffset = val;
            break;

        case Video_vidDesktopStartAddr:
            banshee->vidDesktopStartAddr = val & 0xffffff;
#if 0
            banshee_log("vidDesktopStartAddr=%08x\n", val);
#endif
            svga->fullchange = changeframecount;
            svga_recalctimings(svga);
            break;
        case Video_vidDesktopOverlayStride:
            banshee->vidDesktopOverlayStride = val;
#if 0
            banshee_log("vidDesktopOverlayStride=%08x\n", val);
#endif
            svga->fullchange = changeframecount;
            svga_recalctimings(svga);
            break;
        default:
#if 0
            fatal("bad banshee_ext_outl: addr=%04x val=%08x\n", addr, val);
#endif
            break;
    }
}

static uint8_t
banshee_ext_in(uint16_t addr, void *priv)
{
    banshee_t *banshee = (banshee_t *) priv;
#if 0
    svga_t *svga = &banshee->svga;
#endif
    uint8_t ret = 0xff;

    switch (addr & 0xff) {
        case Init_status:
        case Init_status + 1:
        case Init_status + 2:
        case Init_status + 3:
            ret = (banshee_status(banshee) >> ((addr & 3) * 8)) & 0xff;
#if 0
            banshee_log("Read status reg! %04x(%08x):%08x\n", CS, cs, cpu_state.pc);
#endif
            break;

        case 0xb0:
        case 0xb1:
        case 0xb2:
        case 0xb3:
        case 0xb4:
        case 0xb5:
        case 0xb6:
        case 0xb7:
        case 0xb8:
        case 0xb9:
        case 0xba:
        case 0xbb:
        case 0xbc:
        case 0xbd:
        case 0xbe:
        case 0xbf:
        case 0xc0:
        case 0xc1:
        case 0xc2:
        case 0xc3:
        case 0xc4:
        case 0xc5:
        case 0xc6:
        case 0xc7:
        case 0xc8:
        case 0xc9:
        case 0xca:
        case 0xcb:
        case 0xcc:
        case 0xcd:
        case 0xce:
        case 0xcf:
        case 0xd0:
        case 0xd1:
        case 0xd2:
        case 0xd3:
        case 0xd4:
        case 0xd5:
        case 0xd6:
        case 0xd7:
        case 0xd8:
        case 0xd9:
        case 0xda:
        case 0xdb:
        case 0xdc:
        case 0xdd:
        case 0xde:
        case 0xdf:
            ret = banshee_in((addr & 0xff) + 0x300, priv);
            break;

        default:
            banshee_log("bad banshee_ext_in: addr=%04x\n", addr);
            break;
    }

#if 0
    banshee_log("banshee_ext_in: addr=%04x val=%02x\n", addr, ret);
#endif

    return ret;
}

static uint32_t
banshee_status(banshee_t *banshee)
{
    voodoo_t     *voodoo       = banshee->voodoo;
    const svga_t *svga         = &banshee->svga;
    int           fifo_entries = FIFO_ENTRIES;
    int           swap_count   = voodoo->swap_count;
    int           written      = voodoo->cmd_written + voodoo->cmd_written_fifo;
    int           busy         = (written - voodoo->cmd_read) || (voodoo->cmdfifo_depth_rd != voodoo->cmdfifo_depth_wr) || (voodoo->cmdfifo_depth_rd_2 != voodoo->cmdfifo_depth_wr_2) || voodoo->render_voodoo_busy[0] || voodoo->render_voodoo_busy[1] || voodoo->render_voodoo_busy[2] || voodoo->render_voodoo_busy[3] || voodoo->voodoo_busy;
    uint32_t      ret          = 0;

    if (fifo_entries < 0x20)
        ret |= 0x1f - fifo_entries;
    else
        ret |= 0x1f;
    if (fifo_entries)
        ret |= 0x20;
    if (swap_count < 7)
        ret |= (swap_count << 28);
    else
        ret |= (7 << 28);
    if (!(svga->cgastat & 8))
        ret |= 0x40;

    if (busy)
        ret |= 0x780; /*Busy*/

    if (voodoo->cmdfifo_depth_rd != voodoo->cmdfifo_depth_wr)
        ret |= (1 << 11);

    if (voodoo->cmdfifo_depth_rd_2 != voodoo->cmdfifo_depth_wr_2)
        ret |= (1 << 12);

    if (!voodoo->voodoo_busy)
        voodoo_wake_fifo_thread(voodoo);

#if 0
    banshee_log("banshee_status: busy %i  %i (%i %i)  %i   %i %i  %04x(%08x):%08x %08x\n", busy, written, voodoo->cmd_written, voodoo->cmd_written_fifo, voodoo->cmd_read, voodoo->cmdfifo_depth_rd, voodoo->cmdfifo_depth_wr, CS,cs,cpu_state.pc, ret);
#endif

    return ret;
}

static uint32_t
banshee_ext_inl(uint16_t addr, void *priv)
{
    banshee_t      *banshee = (banshee_t *) priv;
    const voodoo_t *voodoo  = banshee->voodoo;
    const svga_t   *svga    = &banshee->svga;
    uint32_t        ret     = 0xffffffff;

    cycles -= voodoo->read_time;

    switch (addr & 0xff) {
        case Init_status:
            ret = banshee_status(banshee);
#if 0
            banshee_log("Read status reg! %04x(%08x):%08x\n", CS, cs, cpu_state.pc);
#endif
            break;
        case Init_pciInit0:
            ret = banshee->pciInit0;
            break;
        case Init_lfbMemoryConfig:
            ret = banshee->lfbMemoryConfig;
            break;

        case Init_miscInit0:
            ret = banshee->miscInit0;
            break;
        case Init_miscInit1:
            ret = banshee->miscInit1;
            break;
        case Init_dramInit0:
            ret = banshee->dramInit0;
            break;
        case Init_dramInit1:
            ret = banshee->dramInit1;
            break;
        case Init_agpInit0:
            ret = banshee->agpInit0;
            break;

        case Init_vgaInit0:
            ret = banshee->vgaInit0;
            break;
        case Init_vgaInit1:
            ret = banshee->vgaInit1;
            break;

        case Init_2dCommand:
            ret = banshee->command_2d;
            break;
        case Init_2dSrcBaseAddr:
            ret = banshee->srcBaseAddr_2d;
            break;
        case Init_strapInfo:
            ret = 0x00000040; /*8 MB SGRAM, PCI, IRQ enabled, 32kB BIOS*/
            break;

        case PLL_pllCtrl0:
            ret = banshee->pllCtrl0;
            break;
        case PLL_pllCtrl1:
            ret = banshee->pllCtrl1;
            break;
        case PLL_pllCtrl2:
            ret = banshee->pllCtrl2;
            break;

        case DAC_dacMode:
            ret = banshee->dacMode;
            break;
        case DAC_dacAddr:
            ret = banshee->dacAddr;
            break;
        case DAC_dacData:
            ret = svga->pallook[banshee->dacAddr];
            break;

        case Video_vidProcCfg:
            ret = banshee->vidProcCfg;
            break;

        case Video_hwCurPatAddr:
            ret = banshee->hwCurPatAddr;
            break;
        case Video_hwCurLoc:
            ret = banshee->hwCurLoc;
            break;
        case Video_hwCurC0:
            ret = banshee->hwCurC0;
            break;
        case Video_hwCurC1:
            ret = banshee->hwCurC1;
            break;

        case Video_vidSerialParallelPort:
            ret = banshee->vidSerialParallelPort & ~(VIDSERIAL_DDC_DCK_R | VIDSERIAL_DDC_DDA_R | VIDSERIAL_I2C_SCK_R | VIDSERIAL_I2C_SDA_R);
            if (banshee->vidSerialParallelPort & VIDSERIAL_DDC_EN) {
                if (i2c_gpio_get_scl(banshee->i2c_ddc))
                    ret |= VIDSERIAL_DDC_DCK_R;
                if (i2c_gpio_get_sda(banshee->i2c_ddc))
                    ret |= VIDSERIAL_DDC_DDA_R;
            }
            if (banshee->vidSerialParallelPort & VIDSERIAL_I2C_EN) {
                if (i2c_gpio_get_scl(banshee->i2c))
                    ret |= VIDSERIAL_I2C_SCK_R;
                if (i2c_gpio_get_sda(banshee->i2c))
                    ret |= VIDSERIAL_I2C_SDA_R;
            }
#if 0
            banshee_log("vidSerialParallelPort: read %08x %08x  %04x(%08x):%08x\n", ret, ret & (VIDSERIAL_DDC_DCK_R | VIDSERIAL_DDC_DDA_R), CS,cs,cpu_state.pc);
#endif
            break;

        case Video_vidScreenSize:
            ret = banshee->vidScreenSize;
            break;
        case Video_vidOverlayStartCoords:
            ret = voodoo->overlay.vidOverlayStartCoords;
            break;
        case Video_vidOverlayEndScreenCoords:
            ret = voodoo->overlay.vidOverlayEndScreenCoords;
            break;
        case Video_vidOverlayDudx:
            ret = voodoo->overlay.vidOverlayDudx;
            break;
        case Video_vidOverlayDudxOffsetSrcWidth:
            ret = voodoo->overlay.vidOverlayDudxOffsetSrcWidth;
            break;
        case Video_vidOverlayDvdy:
            ret = voodoo->overlay.vidOverlayDvdy;
            break;
        case Video_vidOverlayDvdyOffset:
            ret = voodoo->overlay.vidOverlayDvdyOffset;
            break;

        case Video_vidDesktopStartAddr:
            ret = banshee->vidDesktopStartAddr;
            break;
        case Video_vidDesktopOverlayStride:
            ret = banshee->vidDesktopOverlayStride;
            break;

        default:
#if 0
            fatal("bad banshee_ext_inl: addr=%04x\n", addr);
#endif
            break;
    }

#if 0
    if (addr)
        banshee_log("banshee_ext_inl: addr=%04x val=%08x\n", addr, ret);
#endif

    return ret;
}

static uint32_t banshee_reg_readl(uint32_t addr, void *priv);

static uint8_t
banshee_reg_read(uint32_t addr, void *priv)
{
#if 0
    banshee_log("banshee_reg_read: addr=%08x\n", addr);
#endif
    return banshee_reg_readl(addr & ~3, priv) >> (8 * (addr & 3));
}

static uint16_t
banshee_reg_readw(uint32_t addr, void *priv)
{
#if 0
    banshee_log("banshee_reg_readw: addr=%08x\n", addr);
#endif
    return banshee_reg_readl(addr & ~3, priv) >> (8 * (addr & 2));
}

static uint32_t
banshee_cmd_read(banshee_t *banshee, uint32_t addr)
{
    const voodoo_t *voodoo = banshee->voodoo;
    uint32_t        ret    = 0xffffffff;

    switch (addr & 0x1fc) {
        case Agp_agpHostAddressLow:
            ret = banshee->agpHostAddressLow;
            break;

        case Agp_agpHostAddressHigh:
            ret = banshee->agpHostAddressHigh;
            break;

        case Agp_agpGraphicsAddress:
            ret = banshee->agpGraphicsAddress;
            break;

        case Agp_agpGraphicsStride:
            ret = banshee->agpGraphicsStride;
            break;

        case Agp_agpReqSize:
            ret = banshee->agpReqSize;
            break;

        case cmdBaseAddr0:
            ret = voodoo->cmdfifo_base >> 12;
#if 0
            banshee_log("Read cmdfifo_base %08x\n", ret);
#endif
            break;

        case cmdRdPtrL0:
            ret = voodoo->cmdfifo_rp;
#if 0
            banshee_log("Read cmdfifo_rp %08x\n", ret);
#endif
            break;

        case cmdFifoDepth0:
            ret = voodoo->cmdfifo_depth_wr - voodoo->cmdfifo_depth_rd;
#if 0
            banshee_log("Read cmdfifo_depth %08x\n", ret);
#endif
            break;

        case cmdStatus0:
            ret = voodoo->cmd_status;
            break;

        case cmdBaseSize0:
            ret = voodoo->cmdfifo_size;
            break;

        case cmdBaseAddr1:
            ret = voodoo->cmdfifo_base_2 >> 12;
#if 0
            banshee_log("Read cmdfifo_base %08x\n", ret);
#endif
            break;

        case cmdRdPtrL1:
            ret = voodoo->cmdfifo_rp_2;
#if 0
            banshee_log("Read cmdfifo_rp %08x\n", ret);
#endif
            break;

        case cmdFifoDepth1:
            ret = voodoo->cmdfifo_depth_wr_2 - voodoo->cmdfifo_depth_rd_2;
#if 0
            banshee_log("Read cmdfifo_depth %08x\n", ret);
#endif
            break;

        case cmdStatus1:
            ret = voodoo->cmd_status_2;
            break;

        case cmdBaseSize1:
            ret = voodoo->cmdfifo_size_2;
            break;

        case 0x108:
            break;

        default:
            fatal("Unknown banshee_cmd_read 0x%08x (reg 0x%03x)\n", addr, addr & 0x1fc);
    }

    return ret;
}

static uint32_t
banshee_reg_readl(uint32_t addr, void *priv)
{
    banshee_t *banshee = (banshee_t *) priv;
    voodoo_t  *voodoo  = banshee->voodoo;
    uint32_t   ret     = 0xffffffff;

    cycles -= voodoo->read_time;

    switch (addr & 0x1f00000) {
        case 0x0000000: /*IO remap*/
            if (!(addr & 0x80000))
                ret = banshee_ext_inl(addr & 0xff, banshee);
            else
                ret = banshee_cmd_read(banshee, addr);
            break;

        case 0x0100000: /*2D registers*/
            voodoo_flush(voodoo);
            switch (addr & 0x1fc) {
                case SST_status:
                    ret = banshee_status(banshee);
                    break;

                case SST_intrCtrl:
                    ret = banshee->intrCtrl & 0x0030003f;
                    break;

                case 0x08:
                    ret = voodoo->banshee_blt.clip0Min;
                    break;
                case 0x0c:
                    ret = voodoo->banshee_blt.clip0Max;
                    break;
                case 0x10:
                    ret = voodoo->banshee_blt.dstBaseAddr;
                    break;
                case 0x14:
                    ret = voodoo->banshee_blt.dstFormat;
                    break;
                case 0x34:
                    ret = voodoo->banshee_blt.srcBaseAddr;
                    break;
                case 0x38:
                    ret = voodoo->banshee_blt.commandExtra;
                    break;
                case 0x5c:
                    ret = voodoo->banshee_blt.srcXY;
                    break;
                case 0x60:
                    ret = voodoo->banshee_blt.colorBack;
                    break;
                case 0x64:
                    ret = voodoo->banshee_blt.colorFore;
                    break;
                case 0x68:
                    ret = voodoo->banshee_blt.dstSize;
                    break;
                case 0x6c:
                    ret = voodoo->banshee_blt.dstXY;
                    break;
                case 0x70:
                    ret = voodoo->banshee_blt.command;
                    break;
                default:
                    banshee_log("banshee_reg_readl: addr=%08x\n", addr);
            }
            break;

        case 0x0200000:
        case 0x0300000:
        case 0x0400000:
        case 0x0500000: /*3D registers*/
            switch (addr & 0x3fc) {
                case SST_status:
                    ret = banshee_status(banshee);
                    break;

                case SST_intrCtrl:
                    ret = banshee->intrCtrl & 0x0030003f;
                    break;

                case SST_fbzColorPath:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.fbzColorPath;
                    break;
                case SST_fogMode:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.fogMode;
                    break;
                case SST_alphaMode:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.alphaMode;
                    break;
                case SST_fbzMode:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.fbzMode;
                    break;
                case SST_lfbMode:
                    voodoo_flush(voodoo);
                    ret = voodoo->lfbMode;
                    break;
                case SST_clipLeftRight:
                    ret = voodoo->params.clipRight | (voodoo->params.clipLeft << 16);
                    break;
                case SST_clipLowYHighY:
                    ret = voodoo->params.clipHighY | (voodoo->params.clipLowY << 16);
                    break;

                case SST_clipLeftRight1:
                    ret = voodoo->params.clipRight1 | (voodoo->params.clipLeft1 << 16);
                    break;
                case SST_clipTopBottom1:
                    ret = voodoo->params.clipHighY1 | (voodoo->params.clipLowY1 << 16);
                    break;

                case SST_stipple:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.stipple;
                    break;
                case SST_color0:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.color0;
                    break;
                case SST_color1:
                    voodoo_flush(voodoo);
                    ret = voodoo->params.color1;
                    break;

                case SST_fbiPixelsIn:
                    ret = voodoo->fbiPixelsIn & 0xffffff;
                    break;
                case SST_fbiChromaFail:
                    ret = voodoo->fbiChromaFail & 0xffffff;
                    break;
                case SST_fbiZFuncFail:
                    ret = voodoo->fbiZFuncFail & 0xffffff;
                    break;
                case SST_fbiAFuncFail:
                    ret = voodoo->fbiAFuncFail & 0xffffff;
                    break;
                case SST_fbiPixelsOut:
                    ret = voodoo->fbiPixelsOut & 0xffffff;
                    break;

                default:
                    banshee_log("banshee_reg_readl: 3D addr=%08x\n", addr);
                    break;
            }
            break;

        default:
            break;
    }

#if 0
    if (addr != 0xe0000000)
        banshee_log("banshee_reg_readl: addr=%08x ret=%08x %04x(%08x):%08x\n", addr, ret, CS,cs,cpu_state.pc);
#endif

    return ret;
}

static void
banshee_reg_write(UNUSED(uint32_t addr), UNUSED(uint8_t val), UNUSED(void *priv))
{
#if 0
    banshee_log("banshee_reg_writeb: addr=%08x val=%02x\n", addr, val);
#endif
}

static void
banshee_reg_writew(uint32_t addr, uint16_t val, void *priv)
{
    banshee_t *banshee = (banshee_t *) priv;
    voodoo_t  *voodoo  = banshee->voodoo;

    cycles -= voodoo->write_time;

#if 0
    banshee_log("banshee_reg_writew: addr=%08x val=%04x\n", addr, val);
#endif
    switch (addr & 0x1f00000) {
        case 0x1000000:
        case 0x1100000:
        case 0x1200000:
        case 0x1300000: /*3D LFB*/
        case 0x1400000:
        case 0x1500000:
        case 0x1600000:
        case 0x1700000:
        case 0x1800000:
        case 0x1900000:
        case 0x1a00000:
        case 0x1b00000:
        case 0x1c00000:
        case 0x1d00000:
        case 0x1e00000:
        case 0x1f00000:
            voodoo_queue_command(voodoo, (addr & 0xffffff) | FIFO_WRITEW_FB, val);
            break;

        default:
            break;
    }
}

static void
banshee_cmd_write(banshee_t *banshee, uint32_t addr, uint32_t val)
{
    voodoo_t *voodoo = banshee->voodoo;
#if 0
    banshee_log("banshee_cmd_write: addr=%03x val=%08x\n", addr & 0x1fc, val);
#endif
    switch (addr & 0x1fc) {
        case Agp_agpHostAddressLow:
            banshee->agpHostAddressLow = val;
            break;

        case Agp_agpHostAddressHigh:
            banshee->agpHostAddressHigh = val;
            break;

        case Agp_agpGraphicsAddress:
            banshee->agpGraphicsAddress = val;
            break;

        case Agp_agpGraphicsStride:
            banshee->agpGraphicsStride = val;
            break;

        case Agp_agpReqSize:
            banshee->agpReqSize = val;
            break;

        case cmdBaseAddr0:
            voodoo->cmdfifo_base = (val & 0xfff) << 12;
            voodoo->cmdfifo_end  = voodoo->cmdfifo_base + (((voodoo->cmdfifo_size & 0xff) + 1) << 12);
#if 0
            banshee_log("cmdfifo_base=%08x  cmdfifo_end=%08x %08x\n", voodoo->cmdfifo_base, voodoo->cmdfifo_end, val);
#endif
            break;

        case cmdBaseSize0:
            voodoo->cmdfifo_size    = val;
            voodoo->cmdfifo_end     = voodoo->cmdfifo_base + (((voodoo->cmdfifo_size & 0xff) + 1) << 12);
            voodoo->cmdfifo_enabled = val & 0x100;
            if (!voodoo->cmdfifo_enabled)
                voodoo->cmdfifo_in_sub = 0; /*Not sure exactly when this should be reset*/
#if 0
            banshee_log("cmdfifo_base=%08x  cmdfifo_end=%08x\n", voodoo->cmdfifo_base, voodoo->cmdfifo_end);
#endif
            break;

#if 0
            voodoo->cmdfifo_end = ((val >> 16) & 0x3ff) << 12;
            banshee_log("CMDFIFO base=%08x end=%08x\n", voodoo->cmdfifo_base, voodoo->cmdfifo_end);
            break;
#endif

        case cmdRdPtrL0:
            voodoo->cmdfifo_rp = val;
            break;
        case cmdAMin0:
            voodoo->cmdfifo_amin = val;
            break;
        case cmdAMax0:
            voodoo->cmdfifo_amax = val;
            break;
        case cmdFifoDepth0:
            voodoo->cmdfifo_depth_rd = 0;
            voodoo->cmdfifo_depth_wr = val & 0xffff;
            break;

        case cmdBaseAddr1:
            voodoo->cmdfifo_base_2 = (val & 0xfff) << 12;
            voodoo->cmdfifo_end_2  = voodoo->cmdfifo_base_2 + (((voodoo->cmdfifo_size_2 & 0xff) + 1) << 12);
#if 0
            banshee_log("cmdfifo_base=%08x  cmdfifo_end=%08x %08x\n", voodoo->cmdfifo_base, voodoo->cmdfifo_end, val);
#endif
            break;

        case cmdBaseSize1:
            voodoo->cmdfifo_size_2    = val;
            voodoo->cmdfifo_end_2     = voodoo->cmdfifo_base_2 + (((voodoo->cmdfifo_size_2 & 0xff) + 1) << 12);
            voodoo->cmdfifo_enabled_2 = val & 0x100;
            if (!voodoo->cmdfifo_enabled_2)
                voodoo->cmdfifo_in_sub_2 = 0; /*Not sure exactly when this should be reset*/
#if 0
            banshee_log("cmdfifo_base=%08x  cmdfifo_end=%08x\n", voodoo->cmdfifo_base, voodoo->cmdfifo_end);
#endif
            break;

#if 0
            voodoo->cmdfifo_end = ((val >> 16) & 0x3ff) << 12;
            banshee_log("CMDFIFO base=%08x end=%08x\n", voodoo->cmdfifo_base, voodoo->cmdfifo_end);
            break;
#endif

        case cmdRdPtrL1:
            voodoo->cmdfifo_rp_2 = val;
            break;
        case cmdAMin1:
            voodoo->cmdfifo_amin_2 = val;
            break;
        case cmdAMax1:
            voodoo->cmdfifo_amax_2 = val;
            break;
        case cmdFifoDepth1:
            voodoo->cmdfifo_depth_rd_2 = 0;
            voodoo->cmdfifo_depth_wr_2 = val & 0xffff;
            break;

        default:
            banshee_log("Unknown banshee_cmd_write: addr=%08x val=%08x reg=0x%03x\n", addr, val, addr & 0x1fc);
            break;
    }

#if 0
    cmdBaseSize0  = 0x24,
    cmdBump0      = 0x28,
    cmdRdPtrL0    = 0x2c,
    cmdRdPtrH0    = 0x30,
    cmdAMin0      = 0x34,
    cmdAMax0      = 0x3c,
    cmdFifoDepth0 = 0x44,
    cmdHoleCnt0   = 0x48
    }
#endif
}

static void
banshee_reg_writel(uint32_t addr, uint32_t val, void *priv)
{
    banshee_t *banshee = (banshee_t *) priv;
    voodoo_t  *voodoo  = banshee->voodoo;

    if (addr == voodoo->last_write_addr + 4)
        cycles -= voodoo->burst_time;
    else
        cycles -= voodoo->write_time;
    voodoo->last_write_addr = addr;

#if 0
    banshee_log("banshee_reg_writel: addr=%08x val=%08x\n", addr, val);
#endif

    switch (addr & 0x1f00000) {
        case 0x0000000: /*IO remap*/
            if (!(addr & 0x80000))
                banshee_ext_outl(addr & 0xff, val, banshee);
            else
                banshee_cmd_write(banshee, addr, val);
#if 0
            banshee_log("CMD!!! write %08x %08x\n", addr, val);
#endif
            break;

        case 0x0100000: /*2D registers*/
            if ((addr & 0x3fc) == SST_intrCtrl) {
                banshee->intrCtrl = val & 0x0030003f;
            } else {
                voodoo_queue_command(voodoo, (addr & 0x1fc) | FIFO_WRITEL_2DREG, val);
            }
            break;

        case 0x0200000:
        case 0x0300000:
        case 0x0400000:
        case 0x0500000: /*3D registers*/
            switch (addr & 0x3fc) {
                case SST_intrCtrl:
                    banshee->intrCtrl = val & 0x0030003f;
#if 0
                    banshee_log("intrCtrl=%08x\n", val);
#endif
                    break;

                case SST_userIntrCMD:
                    fatal("userIntrCMD write %08x\n", val);
                    break;

                case SST_swapbufferCMD:
                    voodoo->cmd_written++;
                    voodoo_queue_command(voodoo, (addr & 0x3fc) | FIFO_WRITEL_REG, val);
                    if (!voodoo->voodoo_busy)
                        voodoo_wake_fifo_threads(voodoo->set, voodoo);
#if 0
                    banshee_log("SST_swapbufferCMD write: %i %i\n", voodoo->cmd_written, voodoo->cmd_written_fifo);
#endif
                    break;
                case SST_triangleCMD:
                    voodoo->cmd_written++;
                    voodoo_queue_command(voodoo, (addr & 0x3fc) | FIFO_WRITEL_REG, val);
                    if (!voodoo->voodoo_busy)
                        voodoo_wake_fifo_threads(voodoo->set, voodoo);
                    break;
                case SST_ftriangleCMD:
                    voodoo->cmd_written++;
                    voodoo_queue_command(voodoo, (addr & 0x3fc) | FIFO_WRITEL_REG, val);
                    if (!voodoo->voodoo_busy)
                        voodoo_wake_fifo_threads(voodoo->set, voodoo);
                    break;
                case SST_fastfillCMD:
                    voodoo->cmd_written++;
                    voodoo_queue_command(voodoo, (addr & 0x3fc) | FIFO_WRITEL_REG, val);
                    if (!voodoo->voodoo_busy)
                        voodoo_wake_fifo_threads(voodoo->set, voodoo);
                    break;
                case SST_nopCMD:
                    voodoo->cmd_written++;
                    voodoo_queue_command(voodoo, (addr & 0x3fc) | FIFO_WRITEL_REG, val);
                    if (!voodoo->voodoo_busy)
                        voodoo_wake_fifo_threads(voodoo->set, voodoo);
                    break;

                case SST_swapPending:
                    thread_wait_mutex(voodoo->swap_mutex);
                    voodoo->swap_count++;
                    thread_release_mutex(voodoo->swap_mutex);
#if 0
                    voodoo->cmd_written++;
#endif
                    break;

                default:
                    voodoo_queue_command(voodoo, (addr & 0x3ffffc) | FIFO_WRITEL_REG, val);
                    break;
            }
            break;

        case 0x0600000:
        case 0x0700000: /*TMU0 Texture download*/
            voodoo->tex_count++;
            voodoo_queue_command(voodoo, (addr & 0x1ffffc) | FIFO_WRITEL_TEX, val);
            break;

        case 0x1000000:
        case 0x1100000:
        case 0x1200000:
        case 0x1300000: /*3D LFB*/
        case 0x1400000:
        case 0x1500000:
        case 0x1600000:
        case 0x1700000:
        case 0x1800000:
        case 0x1900000:
        case 0x1a00000:
        case 0x1b00000:
        case 0x1c00000:
        case 0x1d00000:
        case 0x1e00000:
        case 0x1f00000:
            voodoo_queue_command(voodoo, (addr & 0xfffffc) | FIFO_WRITEL_FB, val);
            break;

        default:
            break;
    }
}

static uint8_t
banshee_read_linear(uint32_t addr, void *priv)
{
    banshee_t      *banshee = (banshee_t *) priv;
    const voodoo_t *voodoo  = banshee->voodoo;
    const svga_t   *svga    = &banshee->svga;

    cycles -= voodoo->read_time;

    if ((banshee->pci_regs[0x30] & 0x01) && addr >= banshee->bios_rom.mapping.base && addr < (banshee->bios_rom.mapping.base + banshee->bios_rom.sz)) {
        return rom_read(addr & (banshee->bios_rom.sz - 1), &banshee->bios_rom);
    }
    addr &= svga->decode_mask;
    if (addr >= voodoo->tile_base) {
        int x;
        int y;

        addr -= voodoo->tile_base;
        x = addr & (voodoo->tile_stride - 1);
        y = addr >> voodoo->tile_stride_shift;

        addr = voodoo->tile_base + (x & 127) + ((x >> 7) * 128 * 32) + ((y & 31) * 128) + (y >> 5) * voodoo->tile_x_real;
#if 0
        banshee_log("  Tile rb %08x->%08x %i %i\n", old_addr, addr, x, y);
#endif
    }
    if (addr >= svga->vram_max)
        return 0xff;

    cycles -= svga->monitor->mon_video_timing_read_b;

#if 0
    banshee_log("read_linear: addr=%08x val=%02x\n", addr, svga->vram[addr & svga->vram_mask]);
#endif

    return svga->vram[addr & svga->vram_mask];
}

static uint16_t
banshee_read_linear_w(uint32_t addr, void *priv)
{
    banshee_t      *banshee = (banshee_t *) priv;
    const voodoo_t *voodoo  = banshee->voodoo;
    svga_t         *svga    = &banshee->svga;

    if (addr & 1)
        return banshee_read_linear(addr, priv) | (banshee_read_linear(addr + 1, priv) << 8);

    cycles -= voodoo->read_time;
    if ((banshee->pci_regs[0x30] & 0x01) && addr >= banshee->bios_rom.mapping.base && addr < (banshee->bios_rom.mapping.base + banshee->bios_rom.sz)) {
        return rom_readw(addr & (banshee->bios_rom.sz - 1), &banshee->bios_rom);
    }
    addr &= svga->decode_mask;
    if (addr >= voodoo->tile_base) {
        int x;
        int y;

        addr -= voodoo->tile_base;
        x = addr & (voodoo->tile_stride - 1);
        y = addr >> voodoo->tile_stride_shift;

        addr = voodoo->tile_base + (x & 127) + ((x >> 7) * 128 * 32) + ((y & 31) * 128) + (y >> 5) * voodoo->tile_x_real;
#if 0
        banshee_log("  Tile rb %08x->%08x %i %i\n", old_addr, addr, x, y);
#endif
    }
    if (addr >= svga->vram_max)
        return 0xff;

    cycles -= svga->monitor->mon_video_timing_read_w;

#if 0
    banshee_log("read_linear: addr=%08x val=%02x\n", addr, svga->vram[addr & svga->vram_mask]);
#endif

    return *(uint16_t *) &svga->vram[addr & svga->vram_mask];
}

static uint32_t
banshee_read_linear_l(uint32_t addr, void *priv)
{
    banshee_t      *banshee = (banshee_t *) priv;
    const voodoo_t *voodoo  = banshee->voodoo;
    svga_t         *svga    = &banshee->svga;

    if (addr & 3)
        return banshee_read_linear_w(addr, priv) | (banshee_read_linear_w(addr + 2, priv) << 16);

    cycles -= voodoo->read_time;

    if ((banshee->pci_regs[0x30] & 0x01) && addr >= banshee->bios_rom.mapping.base && addr < (banshee->bios_rom.mapping.base + banshee->bios_rom.sz)) {
        return rom_readl(addr & (banshee->bios_rom.sz - 1), &banshee->bios_rom);
    }
    addr &= svga->decode_mask;
    if (addr >= voodoo->tile_base) {
        int x;
        int y;

        addr -= voodoo->tile_base;
        x = addr & (voodoo->tile_stride - 1);
        y = addr >> voodoo->tile_stride_shift;

        addr = voodoo->tile_base + (x & 127) + ((x >> 7) * 128 * 32) + ((y & 31) * 128) + (y >> 5) * voodoo->tile_x_real;
#if 0
        banshee_log("  Tile rb %08x->%08x %i %i\n", old_addr, addr, x, y);
#endif
    }
    if (addr >= svga->vram_max)
        return 0xff;

    cycles -= svga->monitor->mon_video_timing_read_l;

#if 0
    banshee_log("read_linear: addr=%08x val=%02x\n", addr, svga->vram[addr & svga->vram_mask]);
#endif

    return *(uint32_t *) &svga->vram[addr & svga->vram_mask];
}

static void
banshee_write_linear(uint32_t addr, uint8_t val, void *priv)
{
    banshee_t      *banshee = (banshee_t *) priv;
    const voodoo_t *voodoo  = banshee->voodoo;
    svga_t         *svga    = &banshee->svga;

    cycles -= voodoo->write_time;

#if 0
    banshee_log("write_linear: addr=%08x val=%02x\n", addr, val);
#endif
    addr &= svga->decode_mask;
    if (addr >= voodoo->tile_base) {
        int x;
        int y;

        addr -= voodoo->tile_base;
        x = addr & (voodoo->tile_stride - 1);
        y = addr >> voodoo->tile_stride_shift;

        addr = voodoo->tile_base + (x & 127) + ((x >> 7) * 128 * 32) + ((y & 31) * 128) + (y >> 5) * voodoo->tile_x_real;
#if 0
        banshee_log("  Tile b %08x->%08x %i %i\n", old_addr, addr, x, y);
#endif
    }
    if (addr >= svga->vram_max)
        return;

    cycles -= svga->monitor->mon_video_timing_write_b;

    svga->changedvram[addr >> 12]      = changeframecount;
    svga->vram[addr & svga->vram_mask] = val;
}

static void
banshee_write_linear_w(uint32_t addr, uint16_t val, void *priv)
{
    banshee_t      *banshee = (banshee_t *) priv;
    const voodoo_t *voodoo  = banshee->voodoo;
    svga_t         *svga    = &banshee->svga;

    if (addr & 1) {
        banshee_write_linear(addr, val, priv);
        banshee_write_linear(addr + 1, val >> 8, priv);
        return;
    }

    cycles -= voodoo->write_time;
#if 0
    banshee_log("write_linear: addr=%08x val=%02x\n", addr, val);
#endif
    addr &= svga->decode_mask;
    if (addr >= voodoo->tile_base) {
        int x;
        int y;

        addr -= voodoo->tile_base;
        x = addr & (voodoo->tile_stride - 1);
        y = addr >> voodoo->tile_stride_shift;

        addr = voodoo->tile_base + (x & 127) + ((x >> 7) * 128 * 32) + ((y & 31) * 128) + (y >> 5) * voodoo->tile_x_real;
#if 0
        banshee_log("  Tile b %08x->%08x %i %i\n", old_addr, addr, x, y);
#endif
    }
    if (addr >= svga->vram_max)
        return;

    cycles -= svga->monitor->mon_video_timing_write_w;

    svga->changedvram[addr >> 12]                     = changeframecount;
    *(uint16_t *) &svga->vram[addr & svga->vram_mask] = val;
}

static void
banshee_write_linear_l(uint32_t addr, uint32_t val, void *priv)
{
    banshee_t *banshee = (banshee_t *) priv;
    voodoo_t  *voodoo  = banshee->voodoo;
    svga_t    *svga    = &banshee->svga;
    int        timing;

    if (addr & 3) {
        banshee_write_linear_w(addr, val, priv);
        banshee_write_linear_w(addr + 2, val >> 16, priv);
        return;
    }

    if (addr == voodoo->last_write_addr + 4)
        timing = voodoo->burst_time;
    else
        timing = voodoo->write_time;
    cycles -= timing;
    voodoo->last_write_addr = addr;

#if 0
    if (val)
        banshee_log("write_linear_l: addr=%08x val=%08x  %08x\n", addr, val, voodoo->tile_base);
#endif
    addr &= svga->decode_mask;
    if (addr >= voodoo->tile_base) {
        int x;
        int y;

        addr -= voodoo->tile_base;
        x = addr & (voodoo->tile_stride - 1);
        y = addr >> voodoo->tile_stride_shift;

        addr = voodoo->tile_base + (x & 127) + ((x >> 7) * 128 * 32) + ((y & 31) * 128) + (y >> 5) * voodoo->tile_x_real;
#if 0
        banshee_log("  Tile %08x->%08x->%08x->%08x %i %i  tile_x=%i\n", old_addr, addr_off, addr2, addr, x, y, voodoo->tile_x_real);
#endif
    }

    if (addr >= svga->vram_max)
        return;

    cycles -= svga->monitor->mon_video_timing_write_l;

    svga->changedvram[addr >> 12]                     = changeframecount;
    *(uint32_t *) &svga->vram[addr & svga->vram_mask] = val;
    if (voodoo->cmdfifo_enabled && addr >= voodoo->cmdfifo_base && addr < voodoo->cmdfifo_end) {
#if 0
        banshee_log("CMDFIFO write %08x %08x  old amin=%08x amax=%08x hlcnt=%i depth_wr=%i rp=%08x\n", addr, val, voodoo->cmdfifo_amin, voodoo->cmdfifo_amax, voodoo->cmdfifo_holecount, voodoo->cmdfifo_depth_wr, voodoo->cmdfifo_rp);
#endif
        if (addr == voodoo->cmdfifo_base && !voodoo->cmdfifo_holecount) {
#if 0
            if (voodoo->cmdfifo_holecount)
                fatal("CMDFIFO reset pointers while outstanding holes\n");
#endif
            /*Reset pointers*/
            voodoo->cmdfifo_amin = voodoo->cmdfifo_base;
            voodoo->cmdfifo_amax = voodoo->cmdfifo_base;
            voodoo->cmdfifo_depth_wr++;
            voodoo_wake_fifo_thread(voodoo);
        } else if (voodoo->cmdfifo_holecount) {
#if 0
            if ((addr <= voodoo->cmdfifo_amin && voodoo->cmdfifo_amin != -4) || addr >= voodoo->cmdfifo_amax)
                fatal("CMDFIFO holecount write outside of amin/amax - amin=%08x amax=%08x holecount=%i\n", voodoo->cmdfifo_amin, voodoo->cmdfifo_amax, voodoo->cmdfifo_holecount);
            banshee_log("holecount %i\n", voodoo->cmdfifo_holecount);
#endif
            voodoo->cmdfifo_holecount--;
            if (!voodoo->cmdfifo_holecount) {
                /*Filled in holes, resume normal operation*/
                voodoo->cmdfifo_depth_wr += ((voodoo->cmdfifo_amax - voodoo->cmdfifo_amin) >> 2);
                voodoo->cmdfifo_amin = voodoo->cmdfifo_amax;
                voodoo_wake_fifo_thread(voodoo);
#if 0
                banshee_log("hole filled! amin=%08x amax=%08x added %i words\n", voodoo->cmdfifo_amin, voodoo->cmdfifo_amax, words_to_add);
#endif
            }
        } else if (addr == voodoo->cmdfifo_amax + 4) {
            /*In-order write*/
            voodoo->cmdfifo_amin = addr;
            voodoo->cmdfifo_amax = addr;
            voodoo->cmdfifo_depth_wr++;
            voodoo_wake_fifo_thread(voodoo);
        } else {
            /*Out-of-order write*/
            if (addr < voodoo->cmdfifo_amin) {
                /*Reset back to start. Note that write is still out of order!*/
                voodoo->cmdfifo_amin = voodoo->cmdfifo_base - 4;
            }
#if 0
            else if (addr < voodoo->cmdfifo_amax)
                fatal("Out-of-order write really out of order\n");
#endif
            voodoo->cmdfifo_amax      = addr;
            voodoo->cmdfifo_holecount = ((voodoo->cmdfifo_amax - voodoo->cmdfifo_amin) >> 2) - 1;
#if 0
            banshee_log("CMDFIFO out of order: amin=%08x amax=%08x holecount=%i\n", voodoo->cmdfifo_amin, voodoo->cmdfifo_amax, voodoo->cmdfifo_holecount);
#endif
        }
    }

    if (voodoo->cmdfifo_enabled_2 && addr >= voodoo->cmdfifo_base_2 && addr < voodoo->cmdfifo_end_2) {
#if 0
        banshee_log("CMDFIFO write %08x %08x  old amin=%08x amax=%08x hlcnt=%i depth_wr=%i rp=%08x\n", addr, val, voodoo->cmdfifo_amin, voodoo->cmdfifo_amax, voodoo->cmdfifo_holecount, voodoo->cmdfifo_depth_wr, voodoo->cmdfifo_rp);
#endif
        if (addr == voodoo->cmdfifo_base_2 && !voodoo->cmdfifo_holecount_2) {
#if 0
            if (voodoo->cmdfifo_holecount)
                fatal("CMDFIFO reset pointers while outstanding holes\n");
#endif
            /*Reset pointers*/
            voodoo->cmdfifo_amin_2 = voodoo->cmdfifo_base_2;
            voodoo->cmdfifo_amax_2 = voodoo->cmdfifo_base_2;
            voodoo->cmdfifo_depth_wr_2++;
            voodoo_wake_fifo_thread(voodoo);
        } else if (voodoo->cmdfifo_holecount_2) {
#if 0
            if ((addr <= voodoo->cmdfifo_amin && voodoo->cmdfifo_amin != -4) || addr >= voodoo->cmdfifo_amax)
                fatal("CMDFIFO holecount write outside of amin/amax - amin=%08x amax=%08x holecount=%i\n", voodoo->cmdfifo_amin, voodoo->cmdfifo_amax, voodoo->cmdfifo_holecount);
            banshee_log("holecount %i\n", voodoo->cmdfifo_holecount);
#endif
            voodoo->cmdfifo_holecount_2--;
            if (!voodoo->cmdfifo_holecount_2) {
                /*Filled in holes, resume normal operation*/
                voodoo->cmdfifo_depth_wr_2 += ((voodoo->cmdfifo_amax_2 - voodoo->cmdfifo_amin_2) >> 2);
                voodoo->cmdfifo_amin_2 = voodoo->cmdfifo_amax_2;
                voodoo_wake_fifo_thread(voodoo);
#if 0
                banshee_log("hole filled! amin=%08x amax=%08x added %i words\n", voodoo->cmdfifo_amin, voodoo->cmdfifo_amax, words_to_add);
#endif
            }
        } else if (addr == voodoo->cmdfifo_amax_2 + 4) {
            /*In-order write*/
            voodoo->cmdfifo_amin_2 = addr;
            voodoo->cmdfifo_amax_2 = addr;
            voodoo->cmdfifo_depth_wr_2++;
            voodoo_wake_fifo_thread(voodoo);
        } else {
            /*Out-of-order write*/
            if (addr < voodoo->cmdfifo_amin_2) {
                /*Reset back to start. Note that write is still out of order!*/
                voodoo->cmdfifo_amin_2 = voodoo->cmdfifo_base_2 - 4;
            }
#if 0
            else if (addr < voodoo->cmdfifo_amax)
                fatal("Out-of-order write really out of order\n");
#endif
            voodoo->cmdfifo_amax_2      = addr;
            voodoo->cmdfifo_holecount_2 = ((voodoo->cmdfifo_amax_2 - voodoo->cmdfifo_amin_2) >> 2) - 1;
#if 0
            banshee_log("CMDFIFO out of order: amin=%08x amax=%08x holecount=%i\n", voodoo->cmdfifo_amin, voodoo->cmdfifo_amax, voodoo->cmdfifo_holecount);
#endif
        }
    }
}

void
banshee_hwcursor_draw(svga_t *svga, int displine)
{
    const banshee_t *banshee = (banshee_t *) svga->priv;
    int              x;
    int              x_off;
    int              xx;
    uint32_t         col0 = banshee->hwCurC0;
    uint32_t         col1 = banshee->hwCurC1;
    uint8_t          plane0[8];
    uint8_t          plane1[8];

    for (uint8_t c = 0; c < 8; c++)
        plane0[c] = svga->vram[svga->hwcursor_latch.addr + c];
    for (uint8_t c = 0; c < 8; c++)
        plane1[c] = svga->vram[svga->hwcursor_latch.addr + c + 8];
    svga->hwcursor_latch.addr += 16;

    x_off = svga->hwcursor_latch.x;

    if (banshee->vidProcCfg & VIDPROCCFG_CURSOR_MODE) {
        /*X11 mode*/
        for (x = 0; x < 64; x += 8) {
            if (x_off > -8) {
                for (xx = 0; xx < 8; xx++) {
                    if (plane0[x >> 3] & (1 << 7))
                        (svga->monitor->target_buffer->line[displine])[x_off + xx + svga->x_add] = (plane1[x >> 3] & (1 << 7)) ? col1 : col0;

                    plane0[x >> 3] <<= 1;
                    plane1[x >> 3] <<= 1;
                }
            }

            x_off += 8;
        }
    } else {
        /*Windows mode*/
        for (x = 0; x < 64; x += 8) {
            if (x_off > -8) {
                for (xx = 0; xx < 8; xx++) {
                    if (((x_off + xx + svga->x_add) >= 0) && ((x_off + xx + svga->x_add) <= 2047)) {
                        if (!(plane0[x >> 3] & (1 << 7)))
                            (svga->monitor->target_buffer->line[displine])[x_off + xx + svga->x_add] = (plane1[x >> 3] & (1 << 7)) ? col1 : col0;
                        else if (plane1[x >> 3] & (1 << 7))
                            (svga->monitor->target_buffer->line[displine])[x_off + xx + svga->x_add] ^= 0xffffff;
                    }

                    plane0[x >> 3] <<= 1;
                    plane1[x >> 3] <<= 1;
                }
            }

            x_off += 8;
        }
    }
}

#define CLAMP(x)                      \
    do {                              \
        if ((x) & ~0xff)              \
            x = ((x) < 0) ? 0 : 0xff; \
    } while (0)

#define DECODE_RGB565(buf)                                                                                     \
    do {                                                                                                       \
        int c;                                                                                                 \
        int wp = 0;                                                                                            \
                                                                                                               \
        for (c = 0; c < voodoo->overlay.overlay_bytes; c += 2) {                                               \
            uint16_t data = *(uint16_t *) src;                                                                 \
            int      r    = data & 0x1f;                                                                       \
            int      g    = (data >> 5) & 0x3f;                                                                \
            int      b    = data >> 11;                                                                        \
                                                                                                               \
            if (banshee->vidProcCfg & VIDPROCCFG_OVERLAY_CLUT_BYPASS)                                          \
                buf[wp++] = (r << 3) | (g << 10) | (b << 19);                                                  \
            else                                                                                               \
                buf[wp++] = (clut[r << 3] & 0x0000ff) | (clut[g << 2] & 0x00ff00) | (clut[b << 3] & 0xff0000); \
            src += 2;                                                                                          \
        }                                                                                                      \
    } while (0)

#define DECODE_RGB565_TILED(buf)                                                                                        \
    do {                                                                                                                \
        int      c;                                                                                                     \
        int      wp        = 0;                                                                                         \
        uint32_t base_addr = (buf == banshee->overlay_buffer[1]) ? src_addr2 : src_addr;                                \
                                                                                                                        \
        for (c = 0; c < voodoo->overlay.overlay_bytes; c += 2) {                                                        \
            uint16_t data = *(uint16_t *) &svga->vram[(base_addr + (c & 127) + (c >> 7) * 128 * 32) & svga->vram_mask]; \
            int      r    = data & 0x1f;                                                                                \
            int      g    = (data >> 5) & 0x3f;                                                                         \
            int      b    = data >> 11;                                                                                 \
                                                                                                                        \
            if (banshee->vidProcCfg & VIDPROCCFG_OVERLAY_CLUT_BYPASS)                                                   \
                buf[wp++] = (r << 3) | (g << 10) | (b << 19);                                                           \
            else                                                                                                        \
                buf[wp++] = (clut[r << 3] & 0x0000ff) | (clut[g << 2] & 0x00ff00) | (clut[b << 3] & 0xff0000);          \
        }                                                                                                               \
    } while (0)

#define DECODE_YUYV422(buf)                                      \
    do {                                                         \
        int c;                                                   \
        int wp = 0;                                              \
                                                                 \
        for (c = 0; c < voodoo->overlay.overlay_bytes; c += 4) { \
            uint8_t y1, y2;                                      \
            int8_t  Cr, Cb;                                      \
            int     dR, dG, dB;                                  \
            int     r, g, b;                                     \
                                                                 \
            y1 = src[0];                                         \
            Cr = src[1] - 0x80;                                  \
            y2 = src[2];                                         \
            Cb = src[3] - 0x80;                                  \
            src += 4;                                            \
                                                                 \
            dR = (359 * Cr) >> 8;                                \
            dG = (88 * Cb + 183 * Cr) >> 8;                      \
            dB = (453 * Cb) >> 8;                                \
                                                                 \
            r = y1 + dR;                                         \
            CLAMP(r);                                            \
            g = y1 - dG;                                         \
            CLAMP(g);                                            \
            b = y1 + dB;                                         \
            CLAMP(b);                                            \
            buf[wp++] = r | (g << 8) | (b << 16);                \
                                                                 \
            r = y2 + dR;                                         \
            CLAMP(r);                                            \
            g = y2 - dG;                                         \
            CLAMP(g);                                            \
            b = y2 + dB;                                         \
            CLAMP(b);                                            \
            buf[wp++] = r | (g << 8) | (b << 16);                \
        }                                                        \
    } while (0)

#define DECODE_UYUV422(buf)                                      \
    do {                                                         \
        int c;                                                   \
        int wp = 0;                                              \
                                                                 \
        for (c = 0; c < voodoo->overlay.overlay_bytes; c += 4) { \
            uint8_t y1, y2;                                      \
            int8_t  Cr, Cb;                                      \
            int     dR, dG, dB;                                  \
            int     r, g, b;                                     \
                                                                 \
            Cr = src[0] - 0x80;                                  \
            y1 = src[1];                                         \
            Cb = src[2] - 0x80;                                  \
            y2 = src[3];                                         \
            src += 4;                                            \
                                                                 \
            dR = (359 * Cr) >> 8;                                \
            dG = (88 * Cb + 183 * Cr) >> 8;                      \
            dB = (453 * Cb) >> 8;                                \
                                                                 \
            r = y1 + dR;                                         \
            CLAMP(r);                                            \
            g = y1 - dG;                                         \
            CLAMP(g);                                            \
            b = y1 + dB;                                         \
            CLAMP(b);                                            \
            buf[wp++] = r | (g << 8) | (b << 16);                \
                                                                 \
            r = y2 + dR;                                         \
            CLAMP(r);                                            \
            g = y2 - dG;                                         \
            CLAMP(g);                                            \
            b = y2 + dB;                                         \
            CLAMP(b);                                            \
            buf[wp++] = r | (g << 8) | (b << 16);                \
        }                                                        \
    } while (0)

#define OVERLAY_SAMPLE(buf)                                                      \
    do {                                                                         \
        switch (banshee->overlay_pix_fmt) {                                      \
            case 0:                                                              \
                break;                                                           \
                                                                                 \
            case OVERLAY_FMT_YUYV422:                                            \
                DECODE_YUYV422(buf);                                             \
                break;                                                           \
                                                                                 \
            case OVERLAY_FMT_UYVY422:                                            \
                DECODE_UYUV422(buf);                                             \
                break;                                                           \
                                                                                 \
            case OVERLAY_FMT_565:                                                \
            case OVERLAY_FMT_565_DITHER:                                         \
                if (banshee->vidProcCfg & VIDPROCCFG_OVERLAY_TILE)               \
                    DECODE_RGB565_TILED(buf);                                    \
                else                                                             \
                    DECODE_RGB565(buf);                                          \
                break;                                                           \
                                                                                 \
            default:                                                             \
                fatal("Unknown overlay pix fmt %i\n", banshee->overlay_pix_fmt); \
        }                                                                        \
    } while (0)

/* generate both filters for the static table here */
void
voodoo_generate_vb_filters(voodoo_t *voodoo, int fcr, int fcg)
{
    float difference;
    float diffg;
    float thiscol;
    float thiscolg;
    float clr;
    float clg = 0;
    float hack = 1.0f;
    // pre-clamping

    fcr *= hack;
    fcg *= hack;

    /* box prefilter */
    for (uint16_t g = 0; g < 256; g++) { // pixel 1 - our target pixel we want to bleed into
        for (uint16_t h = 0; h < 256; h++) { // pixel 2 - our main pixel
            float avg;
            float avgdiff;

            difference = (float) (g - h);
            avg        = g;
            avgdiff    = avg - h;

            avgdiff = avgdiff * 0.75f;
            if (avgdiff < 0)
                avgdiff *= -1;
            if (difference < 0)
                difference *= -1;

            thiscol = thiscolg = g;

            if (h > g) {
                clr = clg = avgdiff;

                if (clr > fcr)
                    clr = fcr;
                if (clg > fcg)
                    clg = fcg;

                thiscol  = g;
                thiscolg = g;

                if (thiscol > g + fcr)
                    thiscol = g + fcr;
                if (thiscolg > g + fcg)
                    thiscolg = g + fcg;

                if (thiscol > g + difference)
                    thiscol = g + difference;
                if (thiscolg > g + difference)
                    thiscolg = g + difference;

                // hmm this might not be working out..
                int ugh = g - h;
                if (ugh < fcr)
                    thiscol = h;
                if (ugh < fcg)
                    thiscolg = h;
            }

            if (difference > fcr)
                thiscol = g;
            if (difference > fcg)
                thiscolg = g;

            // clamp
            if (thiscol < 0)
                thiscol = 0;
            if (thiscolg < 0)
                thiscolg = 0;

            if (thiscol > 255)
                thiscol = 255;
            if (thiscolg > 255)
                thiscolg = 255;

            vb_filter_bx_rb[g][h] = thiscol;
            vb_filter_bx_g[g][h]  = thiscolg;
        }
        float lined = g + 4;
        if (lined > 255)
            lined = 255;
        voodoo->purpleline[g][0] = lined;
        voodoo->purpleline[g][2] = lined;

        lined = g + 0;
        if (lined > 255)
            lined = 255;
        voodoo->purpleline[g][1] = lined;
    }

    /* 4x1 and 2x2 filter */
#if 0
    fcr *= 5;
    fcg *= 6;
#endif

    for (uint16_t g = 0; g < 256; g++) { // pixel 1
        for (uint16_t h = 0; h < 256; h++) { // pixel 2
            difference = (float) (h - g);
            diffg      = difference;

            thiscol = thiscolg = g;

            if (difference > fcr)
                difference = fcr;
            if (difference < -fcr)
                difference = -fcr;

            if (diffg > fcg)
                diffg = fcg;
            if (diffg < -fcg)
                diffg = -fcg;

            if ((difference < fcr) || (-difference > -fcr))
                thiscol = g + (difference / 2);
            if ((diffg < fcg) || (-diffg > -fcg))
                thiscolg = g + (diffg / 2);

            if (thiscol < 0)
                thiscol = 0;
            if (thiscol > 255)
                thiscol = 255;

            if (thiscolg < 0)
                thiscolg = 0;
            if (thiscolg > 255)
                thiscolg = 255;

            vb_filter_v1_rb[g][h] = thiscol;
            vb_filter_v1_g[g][h]  = thiscolg;
        }
    }
}

static void
banshee_overlay_draw(svga_t *svga, int displine)
{
    banshee_t      *banshee = (banshee_t *) svga->priv;
    voodoo_t       *voodoo  = banshee->voodoo;
    uint32_t       *p;
    int             x;
    int             y         = voodoo->overlay.src_y >> 20;
    uint32_t        src_addr  = svga->overlay_latch.addr + ((banshee->vidProcCfg & VIDPROCCFG_OVERLAY_TILE) ? ((y & 31) * 128 + (y >> 5) * svga->overlay_latch.pitch) : y * svga->overlay_latch.pitch);
    uint32_t        src_addr2 = svga->overlay_latch.addr + ((banshee->vidProcCfg & VIDPROCCFG_OVERLAY_TILE) ? (((y + 1) & 31) * 128 + ((y + 1) >> 5) * svga->overlay_latch.pitch) : (y + 1) * svga->overlay_latch.pitch);
    uint8_t        *src       = &svga->vram[src_addr & svga->vram_mask];
    uint32_t        src_x     = 0;
    unsigned int    y_coeff   = (voodoo->overlay.src_y & 0xfffff) >> 4;
    int             skip_filtering;
    const uint32_t *clut = &svga->pallook[(banshee->vidProcCfg & VIDPROCCFG_OVERLAY_CLUT_SEL) ? 256 : 0];

    if (svga->render == svga_render_null && !svga->changedvram[src_addr >> 12] && !svga->changedvram[src_addr2 >> 12] && !svga->fullchange && ((voodoo->overlay.src_y >> 20) < 2048 && !voodoo->dirty_line[voodoo->overlay.src_y >> 20]) && !(banshee->vidProcCfg & VIDPROCCFG_V_SCALE_ENABLE)) {
        voodoo->overlay.src_y += (1 << 20);
        return;
    }

    if ((voodoo->overlay.src_y >> 20) < 2048)
        voodoo->dirty_line[voodoo->overlay.src_y >> 20] = 0;
#if 0
    pclog("displine=%i addr=%08x %08x  %08x  %08x\n", displine, svga->overlay_latch.addr, src_addr, voodoo->overlay.vidOverlayDvdy, *(uint32_t *)src);
    if (src_addr >= 0x800000)
        fatal("overlay out of range!\n");
#endif
    p = &(svga->monitor->target_buffer->line[displine])[svga->overlay_latch.x + svga->x_add];

    if (banshee->voodoo->scrfilter && banshee->voodoo->scrfilterEnabled)
        skip_filtering = ((banshee->vidProcCfg & VIDPROCCFG_FILTER_MODE_MASK) != VIDPROCCFG_FILTER_MODE_BILINEAR && !(banshee->vidProcCfg & VIDPROCCFG_H_SCALE_ENABLE) && !(banshee->vidProcCfg & VIDPROCCFG_FILTER_MODE_DITHER_4X4) && !(banshee->vidProcCfg & VIDPROCCFG_FILTER_MODE_DITHER_2X2));
    else
        skip_filtering = ((banshee->vidProcCfg & VIDPROCCFG_FILTER_MODE_MASK) != VIDPROCCFG_FILTER_MODE_BILINEAR && !(banshee->vidProcCfg & VIDPROCCFG_H_SCALE_ENABLE));

    if (skip_filtering) {
        /*No scaling or filtering required, just write straight to output buffer*/
        OVERLAY_SAMPLE(p);
    } else {
        OVERLAY_SAMPLE(banshee->overlay_buffer[0]);

        switch (banshee->vidProcCfg & VIDPROCCFG_FILTER_MODE_MASK) {
            case VIDPROCCFG_FILTER_MODE_BILINEAR:
                src = &svga->vram[src_addr2 & svga->vram_mask];
                OVERLAY_SAMPLE(banshee->overlay_buffer[1]);
                if (banshee->vidProcCfg & VIDPROCCFG_H_SCALE_ENABLE) {
                    for (x = 0; x < svga->overlay_latch.cur_xsize; x++) {
                        unsigned int x_coeff   = (src_x & 0xfffff) >> 4;
                        unsigned int coeffs[4] = {
                            ((0x10000 - x_coeff) * (0x10000 - y_coeff)) >> 16,
                            (x_coeff * (0x10000 - y_coeff)) >> 16,
                            ((0x10000 - x_coeff) * y_coeff) >> 16,
                            (x_coeff * y_coeff) >> 16
                        };
                        uint32_t samp0 = banshee->overlay_buffer[0][src_x >> 20];
                        uint32_t samp1 = banshee->overlay_buffer[0][(src_x >> 20) + 1];
                        uint32_t samp2 = banshee->overlay_buffer[1][src_x >> 20];
                        uint32_t samp3 = banshee->overlay_buffer[1][(src_x >> 20) + 1];
                        int      r     = (((samp0 >> 16) & 0xff) * coeffs[0] + ((samp1 >> 16) & 0xff) * coeffs[1] + ((samp2 >> 16) & 0xff) * coeffs[2] + ((samp3 >> 16) & 0xff) * coeffs[3]) >> 16;
                        int      g     = (((samp0 >> 8) & 0xff) * coeffs[0] + ((samp1 >> 8) & 0xff) * coeffs[1] + ((samp2 >> 8) & 0xff) * coeffs[2] + ((samp3 >> 8) & 0xff) * coeffs[3]) >> 16;
                        int      b     = ((samp0 & 0xff) * coeffs[0] + (samp1 & 0xff) * coeffs[1] + (samp2 & 0xff) * coeffs[2] + (samp3 & 0xff) * coeffs[3]) >> 16;
                        p[x]           = (r << 16) | (g << 8) | b;

                        src_x += voodoo->overlay.vidOverlayDudx;
                    }
                } else {
                    for (x = 0; x < svga->overlay_latch.cur_xsize; x++) {
                        uint32_t samp0 = banshee->overlay_buffer[0][src_x >> 20];
                        uint32_t samp1 = banshee->overlay_buffer[1][src_x >> 20];
                        int      r     = (((samp0 >> 16) & 0xff) * (0x10000 - y_coeff) + ((samp1 >> 16) & 0xff) * y_coeff) >> 16;
                        int      g     = (((samp0 >> 8) & 0xff) * (0x10000 - y_coeff) + ((samp1 >> 8) & 0xff) * y_coeff) >> 16;
                        int      b     = ((samp0 & 0xff) * (0x10000 - y_coeff) + (samp1 & 0xff) * y_coeff) >> 16;
                        p[x]           = (r << 16) | (g << 8) | b;
                    }
                }
                break;

            case VIDPROCCFG_FILTER_MODE_DITHER_4X4:
                if (banshee->voodoo->scrfilter && banshee->voodoo->scrfilterEnabled) {
                    uint8_t fil[2048 * 3];
                    uint8_t fil3[2048 * 3];

                    if (banshee->vidProcCfg & VIDPROCCFG_H_SCALE_ENABLE) /* leilei HACK - don't know of real 4x1 hscaled behavior yet, double for now */
                    {
                        for (x = 0; x < svga->overlay_latch.cur_xsize; x++) {
                            fil[x * 3]      = (banshee->overlay_buffer[0][src_x >> 20]);
                            fil[x * 3 + 1]  = (banshee->overlay_buffer[0][src_x >> 20] >> 8);
                            fil[x * 3 + 2]  = (banshee->overlay_buffer[0][src_x >> 20] >> 16);
                            fil3[x * 3 + 0] = fil[x * 3 + 0];
                            fil3[x * 3 + 1] = fil[x * 3 + 1];
                            fil3[x * 3 + 2] = fil[x * 3 + 2];
                            src_x += voodoo->overlay.vidOverlayDudx;
                        }
                    } else {
                        for (x = 0; x < svga->overlay_latch.cur_xsize; x++) {
                            fil[x * 3]      = (banshee->overlay_buffer[0][x]);
                            fil[x * 3 + 1]  = (banshee->overlay_buffer[0][x] >> 8);
                            fil[x * 3 + 2]  = (banshee->overlay_buffer[0][x] >> 16);
                            fil3[x * 3 + 0] = fil[x * 3 + 0];
                            fil3[x * 3 + 1] = fil[x * 3 + 1];
                            fil3[x * 3 + 2] = fil[x * 3 + 2];
                        }
                    }
                    if (y % 2 == 0) {
                        for (x = 0; x < svga->overlay_latch.cur_xsize; x++) {
                            fil[x * 3]     = banshee->voodoo->purpleline[fil[x * 3 + 0]][0];
                            fil[x * 3 + 1] = banshee->voodoo->purpleline[fil[x * 3 + 1]][1];
                            fil[x * 3 + 2] = banshee->voodoo->purpleline[fil[x * 3 + 2]][2];
                        }
                    }

                    for (x = 1; x < svga->overlay_latch.cur_xsize; x++) {
                        fil3[x * 3]     = vb_filter_v1_rb[fil[x * 3]][fil[(x - 1) * 3]];
                        fil3[x * 3 + 1] = vb_filter_v1_g[fil[x * 3 + 1]][fil[(x - 1) * 3 + 1]];
                        fil3[x * 3 + 2] = vb_filter_v1_rb[fil[x * 3 + 2]][fil[(x - 1) * 3 + 2]];
                    }
                    for (x = 1; x < svga->overlay_latch.cur_xsize; x++) {
                        fil[x * 3]     = vb_filter_v1_rb[fil[x * 3]][fil3[(x - 1) * 3]];
                        fil[x * 3 + 1] = vb_filter_v1_g[fil[x * 3 + 1]][fil3[(x - 1) * 3 + 1]];
                        fil[x * 3 + 2] = vb_filter_v1_rb[fil[x * 3 + 2]][fil3[(x - 1) * 3 + 2]];
                    }
                    for (x = 1; x < svga->overlay_latch.cur_xsize; x++) {
                        fil3[x * 3]     = vb_filter_v1_rb[fil[x * 3]][fil[(x - 1) * 3]];
                        fil3[x * 3 + 1] = vb_filter_v1_g[fil[x * 3 + 1]][fil[(x - 1) * 3 + 1]];
                        fil3[x * 3 + 2] = vb_filter_v1_rb[fil[x * 3 + 2]][fil[(x - 1) * 3 + 2]];
                    }
                    for (x = 0; x < svga->overlay_latch.cur_xsize; x++) {
                        fil[x * 3]     = vb_filter_v1_rb[fil[x * 3]][fil3[(x + 1) * 3]];
                        fil[x * 3 + 1] = vb_filter_v1_g[fil[x * 3 + 1]][fil3[(x + 1) * 3 + 1]];
                        fil[x * 3 + 2] = vb_filter_v1_rb[fil[x * 3 + 2]][fil3[(x + 1) * 3 + 2]];
                        p[x]            = (fil[x * 3 + 2] << 16) | (fil[x * 3 + 1] << 8) | fil[x * 3];
                    }
                } else /* filter disabled by emulator option */
                {
                    if (banshee->vidProcCfg & VIDPROCCFG_H_SCALE_ENABLE) {
                        for (x = 0; x < svga->overlay_latch.cur_xsize; x++) {
                            p[x] = banshee->overlay_buffer[0][src_x >> 20];
                            src_x += voodoo->overlay.vidOverlayDudx;
                        }
                    } else {
                        for (x = 0; x < svga->overlay_latch.cur_xsize; x++)
                            p[x] = banshee->overlay_buffer[0][x];
                    }
                }
                break;

            case VIDPROCCFG_FILTER_MODE_DITHER_2X2:
                if (banshee->voodoo->scrfilter && banshee->voodoo->scrfilterEnabled) {
                    uint8_t fil[2048 * 3];
                    uint8_t soak[2048 * 3];
                    uint8_t soak2[2048 * 3];

                    uint8_t samp1[2048 * 3];
                    uint8_t samp2[2048 * 3];
                    uint8_t samp3[2048 * 3];
                    uint8_t samp4[2048 * 3];

                    src = &svga->vram[src_addr2 & svga->vram_mask];
                    OVERLAY_SAMPLE(banshee->overlay_buffer[1]);
                    for (x = 0; x < svga->overlay_latch.cur_xsize; x++) {
                        samp1[x * 3]     = (banshee->overlay_buffer[0][x]);
                        samp1[x * 3 + 1] = (banshee->overlay_buffer[0][x] >> 8);
                        samp1[x * 3 + 2] = (banshee->overlay_buffer[0][x] >> 16);

                        samp2[x * 3 + 0] = (banshee->overlay_buffer[0][x + 1]);
                        samp2[x * 3 + 1] = (banshee->overlay_buffer[0][x + 1] >> 8);
                        samp2[x * 3 + 2] = (banshee->overlay_buffer[0][x + 1] >> 16);

                        samp3[x * 3 + 0] = (banshee->overlay_buffer[1][x]);
                        samp3[x * 3 + 1] = (banshee->overlay_buffer[1][x] >> 8);
                        samp3[x * 3 + 2] = (banshee->overlay_buffer[1][x] >> 16);

                        samp4[x * 3 + 0] = (banshee->overlay_buffer[1][x + 1]);
                        samp4[x * 3 + 1] = (banshee->overlay_buffer[1][x + 1] >> 8);
                        samp4[x * 3 + 2] = (banshee->overlay_buffer[1][x + 1] >> 16);

                        /* sample two lines */

                        soak[x * 3 + 0] = vb_filter_bx_rb[samp1[x * 3 + 0]][samp2[x * 3 + 0]];
                        soak[x * 3 + 1] = vb_filter_bx_g[samp1[x * 3 + 1]][samp2[x * 3 + 1]];
                        soak[x * 3 + 2] = vb_filter_bx_rb[samp1[x * 3 + 2]][samp2[x * 3 + 2]];

                        soak2[x * 3 + 0] = vb_filter_bx_rb[samp3[x * 3 + 0]][samp4[x * 3 + 0]];
                        soak2[x * 3 + 1] = vb_filter_bx_g[samp3[x * 3 + 1]][samp4[x * 3 + 1]];
                        soak2[x * 3 + 2] = vb_filter_bx_rb[samp3[x * 3 + 2]][samp4[x * 3 + 2]];

                        /* then pour it on the rest */

                        fil[x * 3 + 0] = vb_filter_v1_rb[soak[x * 3 + 0]][soak2[x * 3 + 0]];
                        fil[x * 3 + 1] = vb_filter_v1_g[soak[x * 3 + 1]][soak2[x * 3 + 1]];
                        fil[x * 3 + 2] = vb_filter_v1_rb[soak[x * 3 + 2]][soak2[x * 3 + 2]];
                    }

                    if (banshee->vidProcCfg & VIDPROCCFG_H_SCALE_ENABLE) /* 2x2 on a scaled low res */
                    {
                        for (x = 0; x < svga->overlay_latch.cur_xsize; x++) {
                            p[x] = (fil[(src_x >> 20) * 3 + 2] << 16) | (fil[(src_x >> 20) * 3 + 1] << 8) | fil[(src_x >> 20) * 3];
                            src_x += voodoo->overlay.vidOverlayDudx;
                        }
                    } else {
                        for (x = 0; x < svga->overlay_latch.cur_xsize; x++) {
                            p[x] = (fil[x * 3 + 2] << 16) | (fil[x * 3 + 1] << 8) | fil[x * 3];
                        }
                    }
                } else /* filter disabled by emulator option */
                {
                    if (banshee->vidProcCfg & VIDPROCCFG_H_SCALE_ENABLE) {
                        for (x = 0; x < svga->overlay_latch.cur_xsize; x++) {
                            p[x] = banshee->overlay_buffer[0][src_x >> 20];

                            src_x += voodoo->overlay.vidOverlayDudx;
                        }
                    } else {
                        for (x = 0; x < svga->overlay_latch.cur_xsize; x++)
                            p[x] = banshee->overlay_buffer[0][x];
                    }
                }
                break;

            case VIDPROCCFG_FILTER_MODE_POINT:
            default:
                if (banshee->vidProcCfg & VIDPROCCFG_H_SCALE_ENABLE) {
                    for (x = 0; x < svga->overlay_latch.cur_xsize; x++) {
                        p[x] = banshee->overlay_buffer[0][src_x >> 20];

                        src_x += voodoo->overlay.vidOverlayDudx;
                    }
                } else {
                    for (x = 0; x < svga->overlay_latch.cur_xsize; x++)
                        p[x] = banshee->overlay_buffer[0][x];
                }
                break;
        }
    }

    if (banshee->vidProcCfg & VIDPROCCFG_V_SCALE_ENABLE)
        voodoo->overlay.src_y += voodoo->overlay.vidOverlayDvdy;
    else
        voodoo->overlay.src_y += (1 << 20);
}

void
banshee_set_overlay_addr(void *priv, UNUSED(uint32_t addr))
{
    banshee_t *banshee = (banshee_t *) priv;
    voodoo_t  *voodoo  = banshee->voodoo;

    banshee->svga.overlay.addr       = banshee->voodoo->leftOverlayBuf & 0xfffffff;
    banshee->svga.overlay_latch.addr = banshee->voodoo->leftOverlayBuf & 0xfffffff;
    memset(voodoo->dirty_line, 1, sizeof(voodoo->dirty_line));
}

static void
banshee_vsync_callback(svga_t *svga)
{
    banshee_t *banshee = (banshee_t *) svga->priv;
    voodoo_t  *voodoo  = banshee->voodoo;

    voodoo->retrace_count++;
    thread_wait_mutex(voodoo->swap_mutex);
    if (voodoo->swap_pending && (voodoo->retrace_count > voodoo->swap_interval)) {
        if (voodoo->swap_count > 0)
            voodoo->swap_count--;
        voodoo->swap_pending = 0;
        thread_release_mutex(voodoo->swap_mutex);

        memset(voodoo->dirty_line, 1, sizeof(voodoo->dirty_line));
        voodoo->retrace_count = 0;
        banshee_set_overlay_addr(banshee, voodoo->swap_offset);
        thread_set_event(voodoo->wake_fifo_thread);
        voodoo->frame_count++;
    } else
        thread_release_mutex(voodoo->swap_mutex);

    voodoo->overlay.src_y = 0;
    banshee->desktop_addr = banshee->vidDesktopStartAddr;
    banshee->desktop_y    = 0;
}

static uint8_t
banshee_pci_read(int func, int addr, void *priv)
{
    const banshee_t *banshee = (banshee_t *) priv;
#if 0
    svga_t *svga = &banshee->svga;
#endif
    uint8_t ret = 0;

    if (func)
        return 0xff;
#if 0
    banshee_log("Banshee PCI read %08X  ", addr);
#endif
    switch (addr) {
        case 0x00:
            ret = 0x1a;
            break; /*3DFX*/
        case 0x01:
            ret = 0x12;
            break;

        case 0x02:
            ret = (banshee->type == TYPE_BANSHEE) ? 0x03 : 0x05;
            break;
        case 0x03:
            ret = 0x00;
            break;

        case 0x04:
            ret = banshee->pci_regs[0x04] & 0x27;
            break;

        case 0x07:
            ret = banshee->pci_regs[0x07] & 0x36;
            break;

        case 0x08:
            ret = (banshee->type == TYPE_BANSHEE) ? 3 : 1;
            break; /*Revision ID*/
        case 0x09:
            ret = 0;
            break; /*Programming interface*/

        case 0x0a:
            ret = 0x00;
            break; /*Supports VGA interface*/
        case 0x0b:
            ret = 0x03;
            break;

        case 0x0d:
            ret = banshee->pci_regs[0x0d] & 0xf8;
            break;

        case 0x10:
            ret = 0x00;
            break; /*memBaseAddr0*/
        case 0x11:
            ret = 0x00;
            break;
        case 0x12:
            ret = 0x00;
            break;
        case 0x13:
            ret = banshee->memBaseAddr0 >> 24;
            break;

        case 0x14:
            ret = 0x00;
            break; /*memBaseAddr1*/
        case 0x15:
            ret = 0x00;
            break;
        case 0x16:
            ret = 0x00;
            break;
        case 0x17:
            ret = banshee->memBaseAddr1 >> 24;
            break;

        case 0x18:
            ret = 0x01;
            break; /*ioBaseAddr*/
        case 0x19:
            ret = banshee->ioBaseAddr >> 8;
            break;
        case 0x1a:
            ret = banshee->ioBaseAddr >> 16;
            break;
        case 0x1b:
            ret = banshee->ioBaseAddr >> 24;
            break;

        /*Subsystem vendor ID*/
        case 0x2c:
            ret = banshee->pci_regs[0x2c];
            break;
        case 0x2d:
            ret = banshee->pci_regs[0x2d];
            break;
        case 0x2e:
            ret = banshee->pci_regs[0x2e];
            break;
        case 0x2f:
            ret = banshee->pci_regs[0x2f];
            break;

        case 0x30:
            ret = banshee->pci_regs[0x30] & 0x01;
            break; /*BIOS ROM address*/
        case 0x31:
            ret = 0x00;
            break;
        case 0x32:
            ret = banshee->pci_regs[0x32];
            break;
        case 0x33:
            ret = banshee->pci_regs[0x33];
            break;

        case 0x34:
            ret = banshee->agp ? 0x54 : 0x60;
            break;

        case 0x3c:
            ret = banshee->pci_regs[0x3c];
            break;

        case 0x3d:
            ret = 0x01;
            break; /*INTA*/

        case 0x3e:
            ret = 0x04;
            break;
        case 0x3f:
            ret = 0xff;
            break;

        case 0x40:
            ret = 0x01;
            break;

        case 0x50:
            ret = banshee->pci_regs[0x50];
            break;

        case 0x54:
            ret = 0x02;
            break;
        case 0x55:
            ret = 0x60;
            break;
        case 0x56:
            ret = 0x10;
            break; /* assumed AGP 1.0 */

        case 0x58:
            ret = (banshee->type == TYPE_BANSHEE) ? 0x21 : 0x23;
            break;
        case 0x59:
            ret = 0x02;
            break;
        case 0x5b:
            ret = 0x07;
            break;

        case 0x5c:
            ret = banshee->pci_regs[0x5c];
            break;
        case 0x5d:
            ret = banshee->pci_regs[0x5d];
            break;
        case 0x5e:
            ret = banshee->pci_regs[0x5e];
            break;
        case 0x5f:
            ret = banshee->pci_regs[0x5f];
            break;

        case 0x60:
            ret = 0x01;
            break;
        case 0x62:
            ret = 0x21;
            break;

        case 0x64:
            ret = banshee->pci_regs[0x64];
            break;
        case 0x65:
            ret = banshee->pci_regs[0x65];
            break;
        case 0x66:
            ret = banshee->pci_regs[0x66];
            break;
        case 0x67:
            ret = banshee->pci_regs[0x67];
            break;

        default:
            break;
    }
#if 0
    banshee_log("%02X\n", ret);
#endif
    return ret;
}

static void
banshee_pci_write(int func, int addr, uint8_t val, void *priv)
{
    banshee_t *banshee = (banshee_t *) priv;
#if 0
    svga_t *svga = &banshee->svga;
#endif

    if (func)
        return;
#if 0
    banshee_log("Banshee write %08X %02X %04X:%08X\n", addr, val, CS, cpu_state.pc);
#endif
    switch (addr) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x3d:
        case 0x3e:
        case 0x3f:
            return;

        case PCI_REG_COMMAND:
            if (val & PCI_COMMAND_IO) {
                io_removehandler(0x03c0, 0x0020, banshee_in, NULL, NULL, banshee_out, NULL, NULL, banshee);
                if (banshee->ioBaseAddr)
                    io_removehandler(banshee->ioBaseAddr, 0x0100, banshee_ext_in, NULL, banshee_ext_inl, banshee_ext_out, NULL, banshee_ext_outl, banshee);

                io_sethandler(0x03c0, 0x0020, banshee_in, NULL, NULL, banshee_out, NULL, NULL, banshee);
                if (banshee->ioBaseAddr)
                    io_sethandler(banshee->ioBaseAddr, 0x0100, banshee_ext_in, NULL, banshee_ext_inl, banshee_ext_out, NULL, banshee_ext_outl, banshee);
            } else {
                io_removehandler(0x03c0, 0x0020, banshee_in, NULL, NULL, banshee_out, NULL, NULL, banshee);
                io_removehandler(banshee->ioBaseAddr, 0x0100, banshee_ext_in, NULL, banshee_ext_inl, banshee_ext_out, NULL, banshee_ext_outl, banshee);
            }
            banshee->pci_regs[PCI_REG_COMMAND] = val & 0x27;
            banshee_updatemapping(banshee);
            return;
        case 0x07:
            banshee->pci_regs[0x07] = val & 0x3e;
            return;
        case 0x0d:
            banshee->pci_regs[0x0d] = val & 0xf8;
            return;

        case 0x13:
            banshee->memBaseAddr0 = (val & 0xfe) << 24;
            banshee_updatemapping(banshee);
            return;

        case 0x17:
            banshee->memBaseAddr1 = (val & 0xfe) << 24;
            banshee_updatemapping(banshee);
            return;

        case 0x19:
            if (banshee->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
                io_removehandler(banshee->ioBaseAddr, 0x0100, banshee_ext_in, NULL, banshee_ext_inl, banshee_ext_out, NULL, banshee_ext_outl, banshee);
            banshee->ioBaseAddr &= 0xffff00ff;
            banshee->ioBaseAddr |= val << 8;
            if ((banshee->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO) && banshee->ioBaseAddr)
                io_sethandler(banshee->ioBaseAddr, 0x0100, banshee_ext_in, NULL, banshee_ext_inl, banshee_ext_out, NULL, banshee_ext_outl, banshee);
            banshee_log("Banshee ioBaseAddr=%08x\n", banshee->ioBaseAddr);
            return;

        case 0x1a:
            banshee->ioBaseAddr &= 0xff00ffff;
            banshee->ioBaseAddr |= val << 16;
            break;

        case 0x1b:
            banshee->ioBaseAddr &= 0x00ffffff;
            banshee->ioBaseAddr |= val << 24;
            break;

        case 0x30:
        case 0x32:
        case 0x33:
            if (!banshee->has_bios)
                return;
            banshee->pci_regs[addr] = val;
            if (banshee->pci_regs[0x30] & 0x01) {
                uint32_t biosaddr = (banshee->pci_regs[0x32] << 16) | (banshee->pci_regs[0x33] << 24);
                banshee_log("Banshee bios_rom enabled at %08x\n", biosaddr);
                mem_mapping_set_addr(&banshee->bios_rom.mapping, biosaddr, 0x10000);
                mem_mapping_enable(&banshee->bios_rom.mapping);
            } else {
                banshee_log("Banshee bios_rom disabled\n");
                mem_mapping_disable(&banshee->bios_rom.mapping);
            }
            return;
        case 0x3c:
        case 0x50:
        case 0x65:
        case 0x67:
            banshee->pci_regs[addr] = val;
            return;

        case 0x5c:
            banshee->pci_regs[0x5c] = val & 0x27;
            return;

        case 0x5d:
            banshee->pci_regs[0x5d] = val & 0x03;
            return;

        case 0x5f:
            banshee->pci_regs[0x5e] = val;
            return;

        case 0x64:
            banshee->pci_regs[0x64] = val & 0x03;
            return;

        case 0x66:
            banshee->pci_regs[0x66] = val & 0xc0;
            return;

        default:
            break;
    }
}

static void *
banshee_init_common(const device_t *info, char *fn, int has_sgram, int type, int voodoo_type, int agp)
{
    int        mem_size;
    banshee_t *banshee = malloc(sizeof(banshee_t));
    memset(banshee, 0, sizeof(banshee_t));

    banshee->type     = type;
    banshee->agp      = agp;
    banshee->has_bios = !!fn;

    if (banshee->has_bios) {
        rom_init(&banshee->bios_rom, fn, 0xc0000, 0x10000, 0xffff, 0, MEM_MAPPING_EXTERNAL);
        mem_mapping_disable(&banshee->bios_rom.mapping);
    }

    if (!banshee->has_bios)
#if 0
        mem_size = info->local; /* fixed size for on-board chips */
#endif
        mem_size = device_get_config_int("memory"); /* MS-6168 / Bora Pro can do both 8 and 16 MB. */
    else if (has_sgram) {
        if ((banshee->type == TYPE_V3_1000) || (banshee->type == TYPE_VELOCITY200))
            mem_size = 16; /* Our Voodoo 3 1000 and Velocity 200 bios'es are hardcoded to 16 MB. */
        else if (banshee->type == TYPE_VELOCITY100)
            mem_size = 8; /* Velocity 100 only supports 8 MB */
        else
            mem_size = device_get_config_int("memory");
    } else
        mem_size = 16; /* SDRAM Banshee only supports 16 MB */

    svga_init(info, &banshee->svga, banshee, mem_size << 20,
              banshee_recalctimings,
              banshee_in, banshee_out,
              banshee_hwcursor_draw,
              banshee_overlay_draw);
    banshee->svga.vsync_callback = banshee_vsync_callback;

    mem_mapping_add(&banshee->linear_mapping, 0, 0, banshee_read_linear,
                    banshee_read_linear_w,
                    banshee_read_linear_l,
                    banshee_write_linear,
                    banshee_write_linear_w,
                    banshee_write_linear_l,
                    NULL,
                    MEM_MAPPING_EXTERNAL,
                    &banshee->svga);
    mem_mapping_add(&banshee->reg_mapping_low, 0, 0, banshee_reg_read,
                    banshee_reg_readw,
                    banshee_reg_readl,
                    banshee_reg_write,
                    banshee_reg_writew,
                    banshee_reg_writel,
                    NULL,
                    MEM_MAPPING_EXTERNAL,
                    banshee);
    mem_mapping_add(&banshee->reg_mapping_high, 0, 0, banshee_reg_read,
                    banshee_reg_readw,
                    banshee_reg_readl,
                    banshee_reg_write,
                    banshee_reg_writew,
                    banshee_reg_writel,
                    NULL,
                    MEM_MAPPING_EXTERNAL,
                    banshee);

    banshee->svga.vblank_start = banshee_vblank_start;

#if 0
    io_sethandler(0x03c0, 0x0020, banshee_in, NULL, NULL, banshee_out, NULL, NULL, banshee);
#endif

    banshee->svga.bpp     = 8;
    banshee->svga.miscout = 1;

    banshee->dramInit0 = 1 << 27;
    if (has_sgram && mem_size == 16)
        banshee->dramInit0 |= (1 << 26); /*2xSGRAM = 16 MB*/
    if (!has_sgram)
        banshee->dramInit1 = 1 << 30; /*SDRAM*/
    banshee->svga.decode_mask = 0x1ffffff;

    if (banshee->has_bios)
        pci_add_card(banshee->agp ? PCI_ADD_AGP : PCI_ADD_NORMAL, banshee_pci_read, banshee_pci_write, banshee, &banshee->pci_slot);
    else
        pci_add_card(banshee->agp ? PCI_ADD_AGP : PCI_ADD_VIDEO, banshee_pci_read, banshee_pci_write, banshee, &banshee->pci_slot);

    banshee->voodoo               = voodoo_2d3d_card_init(voodoo_type);
    banshee->voodoo->priv         = banshee;
    banshee->voodoo->vram         = banshee->svga.vram;
    banshee->voodoo->changedvram  = banshee->svga.changedvram;
    banshee->voodoo->fb_mem       = banshee->svga.vram;
    banshee->voodoo->fb_mask      = banshee->svga.vram_mask;
    banshee->voodoo->tex_mem[0]   = banshee->svga.vram;
    banshee->voodoo->tex_mem_w[0] = (uint16_t *) banshee->svga.vram;
    banshee->voodoo->tex_mem[1]   = banshee->svga.vram;
    banshee->voodoo->tex_mem_w[1] = (uint16_t *) banshee->svga.vram;
    banshee->voodoo->texture_mask = banshee->svga.vram_mask;
    banshee->voodoo->cmd_status   = (1 << 28);
    banshee->voodoo->cmd_status_2 = (1 << 28);
    voodoo_generate_filter_v1(banshee->voodoo);

    banshee->vidSerialParallelPort = VIDSERIAL_DDC_DCK_W | VIDSERIAL_DDC_DDA_W;

    banshee->i2c     = i2c_gpio_init("i2c_voodoo_banshee");
    banshee->i2c_ddc = i2c_gpio_init("ddc_voodoo_banshee");
    banshee->ddc     = ddc_init(i2c_gpio_get_bus(banshee->i2c_ddc));

    banshee->svga.conv_16to32 = banshee_conv_16to32;

    switch (type) {
        case TYPE_BANSHEE:
            if (has_sgram) {
                banshee->pci_regs[0x2c] = 0x1a;
                banshee->pci_regs[0x2d] = 0x12;
                banshee->pci_regs[0x2e] = 0x04;
                banshee->pci_regs[0x2f] = 0x00;
            } else {
                banshee->pci_regs[0x2c] = 0x02;
                banshee->pci_regs[0x2d] = 0x11;
                banshee->pci_regs[0x2e] = 0x17;
                banshee->pci_regs[0x2f] = 0x10;
            }
            break;

        case TYPE_V3_1000:
            banshee->pci_regs[0x2c] = 0x1a;
            banshee->pci_regs[0x2d] = 0x12;
            banshee->pci_regs[0x2e] = 0x52;
            banshee->pci_regs[0x2f] = 0x00;
            break;

        case TYPE_V3_2000:
            banshee->pci_regs[0x2c] = 0x1a;
            banshee->pci_regs[0x2d] = 0x12;
            banshee->pci_regs[0x2e] = 0x30;
            banshee->pci_regs[0x2f] = 0x00;
            break;

        case TYPE_V3_3000:
            banshee->pci_regs[0x2c] = 0x1a;
            banshee->pci_regs[0x2d] = 0x12;
            banshee->pci_regs[0x2e] = 0x3a;
            banshee->pci_regs[0x2f] = 0x00;
            break;

        case TYPE_V3_3500:
            banshee->pci_regs[0x2c] = 0x1a;
            banshee->pci_regs[0x2d] = 0x12;
            banshee->pci_regs[0x2e] = 0x60;
            banshee->pci_regs[0x2f] = 0x00;
            break;

        case TYPE_V3_3500_COMPAQ:
            banshee->pci_regs[0x2c] = 0x1a;
            banshee->pci_regs[0x2d] = 0x12;
            banshee->pci_regs[0x2e] = 0x4f;
            banshee->pci_regs[0x2f] = 0x12;
            break;

        case TYPE_V3_3500_SI:
            banshee->pci_regs[0x2c] = 0x1a;
            banshee->pci_regs[0x2d] = 0x12;
            banshee->pci_regs[0x2e] = 0x61;
            banshee->pci_regs[0x2f] = 0x00;
            break;

        case TYPE_VELOCITY100:
            banshee->pci_regs[0x2c] = 0x1a;
            banshee->pci_regs[0x2d] = 0x12;
            banshee->pci_regs[0x2e] = 0x4b;
            banshee->pci_regs[0x2f] = 0x00;
            break;

        case TYPE_VELOCITY200:
            banshee->pci_regs[0x2c] = 0x1a;
            banshee->pci_regs[0x2d] = 0x12;
            banshee->pci_regs[0x2e] = 0x54;
            banshee->pci_regs[0x2f] = 0x00;
            break;

        default:
            break;
    }

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, banshee->agp ? &timing_banshee_agp : &timing_banshee);

    return banshee;
}

static void *
banshee_init(const device_t *info)
{
    return banshee_init_common(info, ROM_BANSHEE, 1, TYPE_BANSHEE, VOODOO_BANSHEE, 0);
}

static void *
creative_banshee_init(const device_t *info)
{
    return banshee_init_common(info, ROM_CREATIVE_BANSHEE, 0, TYPE_BANSHEE, VOODOO_BANSHEE, 0);
}

static void *
v3_1000_init(const device_t *info)
{
    return banshee_init_common(info, ROM_VOODOO3_1000, 1, TYPE_V3_1000, VOODOO_3, 0);
}

static void *
v3_1000_agp_init(const device_t *info)
{
    return banshee_init_common(info, ROM_VOODOO3_1000, 1, TYPE_V3_1000, VOODOO_3, 1);
}

static void *
v3_2000_init(const device_t *info)
{
    return banshee_init_common(info, ROM_VOODOO3_2000, 0, TYPE_V3_2000, VOODOO_3, 0);
}

static void *
v3_2000_agp_init(const device_t *info)
{
    return banshee_init_common(info, ROM_VOODOO3_2000, 0, TYPE_V3_2000, VOODOO_3, 1);
}

static void *
v3_2000_agp_onboard_init(const device_t *info)
{
    return banshee_init_common(info, NULL, 1, TYPE_V3_2000, VOODOO_3, 1);
}

static void *
v3_3000_init(const device_t *info)
{
    return banshee_init_common(info, ROM_VOODOO3_3000, 0, TYPE_V3_3000, VOODOO_3, 0);
}

static void *
v3_3000_agp_init(const device_t *info)
{
    return banshee_init_common(info, ROM_VOODOO3_3000, 0, TYPE_V3_3000, VOODOO_3, 1);
}

static void *
v3_3500_agp_ntsc_init(const device_t *info)
{
    return banshee_init_common(info, ROM_VOODOO3_3500_AGP_NTSC, 0, TYPE_V3_3500, VOODOO_3, 1);
}

static void *
v3_3500_agp_pal_init(const device_t *info)
{
    return banshee_init_common(info, ROM_VOODOO3_3500_AGP_PAL, 0, TYPE_V3_3500, VOODOO_3, 1);
}

static void *
compaq_v3_3500_agp_init(const device_t *info)
{
    return banshee_init_common(info, ROM_VOODOO3_3500_AGP_COMPAQ, 0, TYPE_V3_3500_COMPAQ, VOODOO_3, 1);
}

static void *
v3_3500_se_agp_init(const device_t *info)
{
    return banshee_init_common(info, ROM_VOODOO3_3500_SE_AGP, 0, TYPE_V3_3500, VOODOO_3, 1);
}

static void *
v3_3500_si_agp_init(const device_t *info)
{
    return banshee_init_common(info, ROM_VOODOO3_3500_SI_AGP, 0, TYPE_V3_3500_SI, VOODOO_3, 1);
}

static void *
velocity_100_agp_init(const device_t *info)
{
    return banshee_init_common(info, ROM_VELOCITY_100, 1, TYPE_VELOCITY100, VOODOO_3, 1);
}

static void *
velocity_200_agp_init(const device_t *info)
{
    return banshee_init_common(info, ROM_VELOCITY_200, 1, TYPE_VELOCITY200, VOODOO_3, 1);
}

static int
banshee_available(void)
{
    return rom_present(ROM_BANSHEE);
}

static int
creative_banshee_available(void)
{
    return rom_present(ROM_CREATIVE_BANSHEE);
}

static int
v3_1000_available(void)
{
    return rom_present(ROM_VOODOO3_1000);
}
#define v3_1000_agp_available v3_1000_available

static int
v3_2000_available(void)
{
    return rom_present(ROM_VOODOO3_2000);
}
#define v3_2000_agp_available v3_2000_available

static int
v3_3000_available(void)
{
    return rom_present(ROM_VOODOO3_3000);
}
#define v3_3000_agp_available v3_3000_available

static int
v3_3500_agp_ntsc_available(void)
{
    return rom_present(ROM_VOODOO3_3500_AGP_NTSC);
}

static int
v3_3500_agp_pal_available(void)
{
    return rom_present(ROM_VOODOO3_3500_AGP_PAL);
}

static int
compaq_v3_3500_agp_available(void)
{
    return rom_present(ROM_VOODOO3_3500_AGP_COMPAQ);
}

static int
v3_3500_se_agp_available(void)
{
    return rom_present(ROM_VOODOO3_3500_SE_AGP);
}

static int
v3_3500_si_agp_available(void)
{
    return rom_present(ROM_VOODOO3_3500_SI_AGP);
}

static int
velocity_100_available(void)
{
    return rom_present(ROM_VELOCITY_100);
}

static int
velocity_200_available(void)
{
    return rom_present(ROM_VELOCITY_200);
}

static void
banshee_close(void *priv)
{
    banshee_t *banshee = (banshee_t *) priv;

    voodoo_card_close(banshee->voodoo);
    svga_close(&banshee->svga);
    ddc_close(banshee->ddc);
    i2c_gpio_close(banshee->i2c_ddc);
    i2c_gpio_close(banshee->i2c);

    free(banshee);
}

static void
banshee_speed_changed(void *priv)
{
    banshee_t *banshee = (banshee_t *) priv;

    svga_recalctimings(&banshee->svga);
}

static void
banshee_force_redraw(void *priv)
{
    banshee_t *banshee = (banshee_t *) priv;

    banshee->svga.fullchange = changeframecount;
}

// clang-format off
static const device_config_t banshee_sgram_config[] = {
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 16,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description =  "8 MB", .value =  8 },
            { .description = "16 MB", .value = 16 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "bilinear",
        .description    = "Bilinear filtering",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "dithersub",
        .description    = "Dither subtraction",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "dacfilter",
        .description    = "Screen Filter",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
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
            { .description = "4", .value = 4 },
            { .description = ""              }
        },
        .bios           = { { 0 } }
    },
#ifndef NO_CODEGEN
    {
        .name           = "recompiler",
        .description    = "Dynamic Recompiler",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
#endif
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t banshee_sgram_16mbonly_config[] = {
    {
        .name           = "bilinear",
        .description    = "Bilinear filtering",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "dithersub",
        .description    = "Dither subtraction",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "dacfilter",
        .description    = "Screen Filter",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
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
            { .description = "4", .value = 4 },
            { .description = ""              }
        },
        .bios           = { { 0 } }
    },
#ifndef NO_CODEGEN
    {
        .name           = "recompiler",
        .description    = "Dynamic Recompiler",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
#endif
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t banshee_sdram_config[] = {
    {
        .name           = "bilinear",
        .description    = "Bilinear filtering",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "dithersub",
        .description    = "Dither subtraction",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "dacfilter",
        .description    = "Screen Filter",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
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
            { .description = "4", .value = 4 },
            { .description = ""              }
        },
        .bios           = { { 0 } }
    },
#ifndef NO_CODEGEN
    {
        .name           = "recompiler",
        .description    = "Dynamic Recompiler",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
#endif
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t voodoo_banshee_device = {
    .name          = "3Dfx Voodoo Banshee",
    .internal_name = "voodoo_banshee_pci",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = banshee_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = banshee_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sgram_config
};

const device_t creative_voodoo_banshee_device = {
    .name          = "Creative 3D Blaster Banshee",
    .internal_name = "ctl3d_banshee_pci",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = creative_banshee_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = creative_banshee_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sdram_config
};

const device_t voodoo_3_1000_device = {
    .name          = "3dfx Voodoo3 1000",
    .internal_name = "voodoo3_1k_pci",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = v3_1000_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = v3_1000_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sgram_config
};

const device_t voodoo_3_1000_agp_device = {
    .name          = "3dfx Voodoo3 1000",
    .internal_name = "voodoo3_1k_agp",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = v3_1000_agp_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = v3_1000_agp_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sgram_16mbonly_config
};

const device_t voodoo_3_2000_device = {
    .name          = "3dfx Voodoo3 2000",
    .internal_name = "voodoo3_2k_pci",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = v3_2000_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = v3_2000_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sdram_config
};

const device_t voodoo_3_2000_agp_device = {
    .name          = "3dfx Voodoo3 2000",
    .internal_name = "voodoo3_2k_agp",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = v3_2000_agp_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = v3_2000_agp_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sdram_config
};

const device_t voodoo_3_2000_agp_onboard_8m_device = {
    .name          = "3dfx Voodoo3 2000 (On-Board 8MB SGRAM)",
    .internal_name = "voodoo3_2k_agp_onboard_8m",
    .flags         = DEVICE_AGP,
    .local         = 8,
    .init          = v3_2000_agp_onboard_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sgram_config
};

const device_t voodoo_3_3000_device = {
    .name          = "3dfx Voodoo3 3000",
    .internal_name = "voodoo3_3k_pci",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = v3_3000_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = v3_3000_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sdram_config
};

const device_t voodoo_3_3000_agp_device = {
    .name          = "3dfx Voodoo3 3000",
    .internal_name = "voodoo3_3k_agp",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = v3_3000_agp_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = v3_3000_agp_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sdram_config
};

const device_t voodoo_3_3500_agp_ntsc_device = {
    .name          = "3dfx Voodoo3 3500 TV (NTSC)",
    .internal_name = "voodoo3_3500_agp_ntsc",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = v3_3500_agp_ntsc_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = v3_3500_agp_ntsc_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sdram_config
};

const device_t voodoo_3_3500_agp_pal_device = {
    .name          = "3dfx Voodoo3 3500 TV (PAL)",
    .internal_name = "voodoo3_3500_agp_pal",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = v3_3500_agp_pal_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = v3_3500_agp_pal_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sdram_config
};

const device_t compaq_voodoo_3_3500_agp_device = {
    .name          = "Compaq Voodoo3 3500 TV",
    .internal_name = "compaq_voodoo3_3500_agp",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = compaq_v3_3500_agp_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = compaq_v3_3500_agp_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sdram_config
};

const device_t voodoo_3_3500_se_agp_device = {
    .name          = "Falcon Northwest Voodoo3 3500 SE",
    .internal_name = "voodoo3_3500_se_agp",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = v3_3500_se_agp_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = v3_3500_se_agp_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sdram_config
};

const device_t voodoo_3_3500_si_agp_device = {
    .name          = "3dfx Voodoo3 3500 SI",
    .internal_name = "voodoo3_3500_si_agp",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = v3_3500_si_agp_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = v3_3500_si_agp_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sdram_config
};

const device_t velocity_100_agp_device = {
    .name          = "3dfx Velocity 100",
    .internal_name = "velocity100_agp",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = velocity_100_agp_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = velocity_100_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sdram_config
};

const device_t velocity_200_agp_device = {
    .name          = "3dfx Velocity 200",
    .internal_name = "velocity200_agp",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = velocity_200_agp_init,
    .close         = banshee_close,
    .reset         = NULL,
    .available     = velocity_200_available,
    .speed_changed = banshee_speed_changed,
    .force_redraw  = banshee_force_redraw,
    .config        = banshee_sgram_16mbonly_config
};
