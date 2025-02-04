/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Bochs VBE SVGA emulation.
 *
 *          Uses code from libxcvt to calculate CRTC timings.
 *
 * Authors: Cacodemon345
 *          The Bochs Project
 *          Fabrice Bellard
 *          The libxcvt authors
 *
 *          Copyright 2024 Cacodemon345
 *          Copyright 2003 Fabrice Bellard
 *          Copyright 2002-2024 The Bochs Project
 *          Copyright 2002-2003 Mike Nordell
 *          Copyright 2000-2021 The libxcvt authors
 *
 *          See https://gitlab.freedesktop.org/xorg/lib/libxcvt/-/blob/master/COPYING for libxcvt license details
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/pci.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>

#define VBE_DISPI_BANK_SIZE_KB           64
#define VBE_DISPI_BANK_GRANULARITY_KB    32

#define VBE_DISPI_MAX_XRES               1920
#define VBE_DISPI_MAX_YRES               1600

#define VBE_DISPI_IOPORT_INDEX           0x01CE
#define VBE_DISPI_IOPORT_DATA            0x01CF

#define VBE_DISPI_INDEX_ID               0x0
#define VBE_DISPI_INDEX_XRES             0x1
#define VBE_DISPI_INDEX_YRES             0x2
#define VBE_DISPI_INDEX_BPP              0x3
#define VBE_DISPI_INDEX_ENABLE           0x4
#define VBE_DISPI_INDEX_BANK             0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH       0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT      0x7
#define VBE_DISPI_INDEX_X_OFFSET         0x8
#define VBE_DISPI_INDEX_Y_OFFSET         0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa
#define VBE_DISPI_INDEX_DDC              0xb

#define VBE_DISPI_ID0                    0xB0C0
#define VBE_DISPI_ID1                    0xB0C1
#define VBE_DISPI_ID2                    0xB0C2
#define VBE_DISPI_ID3                    0xB0C3
#define VBE_DISPI_ID4                    0xB0C4
#define VBE_DISPI_ID5                    0xB0C5

#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_GETCAPS                0x02
#define VBE_DISPI_BANK_GRANULARITY_32K   0x10
#define VBE_DISPI_8BIT_DAC               0x20
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80

#define VBE_DISPI_BANK_WR                0x4000
#define VBE_DISPI_BANK_RD                0x8000
#define VBE_DISPI_BANK_RW                0xc000


typedef struct vbe_mode_info_t {
    uint32_t                hdisplay;
    uint32_t                vdisplay;
    float                   vrefresh;
    float                   hsync;
    uint64_t                dot_clock;
    uint16_t                hsync_start;
    uint16_t                hsync_end;
    uint16_t                htotal;
    uint16_t                vsync_start;
    uint16_t                vsync_end;
    uint16_t                vtotal;
} vbe_mode_info_t;

static video_timings_t timing_bochs  = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };

typedef struct bochs_vbe_t {
    svga_t        svga;

    uint8_t       pci_conf_status;
    uint8_t       pci_rom_enable;
    uint8_t       pci_line_interrupt;
    uint8_t       slot;

    uint8_t       pci_regs[256];

    uint16_t      vbe_index;
    uint16_t      bank_gran;
    uint16_t      rom_addr;
    uint16_t      id5_val;

    uint16_t      vbe_regs[16];

    uint32_t      vram_size;

    rom_t         bios_rom;

    mem_mapping_t linear_mapping;
    mem_mapping_t linear_mapping_2;
    uint32_t      ma_latch_old;

    void *        i2c;
    void *        ddc;
} bochs_vbe_t;

static bochs_vbe_t *reset_state = NULL;

static void
gen_mode_info(int hdisplay, int vdisplay, float vrefresh, vbe_mode_info_t* mode_info)
{
    int   vsync;
    int   vsync_and_back_porch;

    mode_info->hdisplay = hdisplay;
    mode_info->vdisplay = vdisplay;
    mode_info->vrefresh = vrefresh;

    /* 1) top/bottom margin size (% of height) - default: 1.8 */
#define CVT_MARGIN_PERCENTAGE 1.8

    /* 2) character cell horizontal granularity (pixels) - default 8 */
#define CVT_H_GRANULARITY 8

    /* 4) Minimum vertical front porch (lines) - default 3 */
#define CVT_MIN_V_PORCH_RND 3

    /* 4) Minimum number of vertical back porch lines - default 6 */
#define CVT_MIN_V_BPORCH 6

    /* Pixel Clock step (kHz) */
#define CVT_CLOCK_STEP 250

    /* CVT default is 60.0Hz */
    if (!mode_info->vrefresh)
        mode_info->vrefresh = 60.0;

    /* 1. Required field rate */
    const float vfield_rate = mode_info->vrefresh;

    /* 2. Horizontal pixels */
    const int hdisplay_rnd  = mode_info->hdisplay - (mode_info->hdisplay % CVT_H_GRANULARITY);

    /* 3. Determine left and right borders */
    const int   hmargin     = 0;

    /* 4. Find total active pixels */
    mode_info->hdisplay     = hdisplay_rnd + (2 * hmargin);

    /* 5. Find number of lines per field */
    const int vdisplay_rnd  = mode_info->vdisplay;

    /* 6. Find top and bottom margins */
    const  int   vmargin    = 0;

    mode_info->vdisplay     = mode_info->vdisplay + 2 * vmargin;

    /* 7. interlace */
    /* Please rename this */
    const float interlace   = 0.0;

    /* Determine vsync Width from aspect ratio */
    if (!(mode_info->vdisplay % 3) && ((mode_info->vdisplay * 4 / 3) == mode_info->hdisplay))
        vsync = 4;
    else if (!(mode_info->vdisplay % 9) &&
             ((mode_info->vdisplay * 16 / 9) == mode_info->hdisplay))
        vsync = 5;
    else if (!(mode_info->vdisplay % 10) &&
             ((mode_info->vdisplay * 16 / 10) == mode_info->hdisplay))
        vsync = 6;
    else if (!(mode_info->vdisplay % 4) &&
             ((mode_info->vdisplay * 5 / 4) == mode_info->hdisplay))
        vsync = 7;
    else if (!(mode_info->vdisplay % 9) &&
             ((mode_info->vdisplay * 15 / 9) == mode_info->hdisplay))
        vsync = 7;
    else    /* Custom */
        vsync = 10;

    /* Simplified GTF calculation. */

    /* 4) Minimum time of vertical sync + back porch interval (µs)
     * default 550.0 */
#define CVT_MIN_VSYNC_BP 550.0

    /* 3) Nominal HSync width (% of line period) - default 8 */
#define CVT_HSYNC_PERCENTAGE 8

    /* 8. Estimated Horizontal period */
    const float hperiod = ((float) (1000000.0 / vfield_rate - CVT_MIN_VSYNC_BP)) /
                          (vdisplay_rnd + 2 * vmargin + CVT_MIN_V_PORCH_RND + interlace);

    /* 9. Find number of lines in sync + backporch */
    if (((int) (CVT_MIN_VSYNC_BP / hperiod) + 1) < (vsync + CVT_MIN_V_BPORCH))
        vsync_and_back_porch = vsync + CVT_MIN_V_BPORCH;
    else
        vsync_and_back_porch = (int) (CVT_MIN_VSYNC_BP / hperiod) + 1;

    /* 10. Find number of lines in back porch */

    /* 11. Find total number of lines in vertical field */
    mode_info->vtotal = vdisplay_rnd + (2 * vmargin) + vsync_and_back_porch + interlace +
                        CVT_MIN_V_PORCH_RND;

    /* 5) Definition of Horizontal blanking time limitation */
    /* Gradient (%/kHz) - default 600 */
#define CVT_M_FACTOR 600

    /* Offset (%) - default 40 */
#define CVT_C_FACTOR 40

    /* Blanking time scaling factor - default 128 */
#define CVT_K_FACTOR 128

    /* Scaling factor weighting - default 20 */
#define CVT_J_FACTOR 20

#define CVT_M_PRIME CVT_M_FACTOR * CVT_K_FACTOR / 256
#define CVT_C_PRIME (CVT_C_FACTOR - CVT_J_FACTOR) * CVT_K_FACTOR / 256 + \
        CVT_J_FACTOR

    /* 12. Find ideal blanking duty cycle from formula */
    float hblank_percentage = CVT_C_PRIME - CVT_M_PRIME * hperiod / 1000.0;

        /* 13. Blanking time */
    if (hblank_percentage < 20)
        hblank_percentage = 20;

    int hblank = mode_info->hdisplay * hblank_percentage / (100.0 - hblank_percentage);
    hblank -= hblank % (2 * CVT_H_GRANULARITY);

    /* 14. Find total number of pixels in a line. */
    mode_info->htotal = mode_info->hdisplay + hblank;

    /* Fill in HSync values */
    mode_info->hsync_end = mode_info->hdisplay + hblank / 2;

    int hsync_w = (mode_info->htotal * CVT_HSYNC_PERCENTAGE) / 100;
    hsync_w -= hsync_w % CVT_H_GRANULARITY;
    mode_info->hsync_start = mode_info->hsync_end - hsync_w;

    /* Fill in vsync values */
    mode_info->vsync_start = mode_info->vdisplay + CVT_MIN_V_PORCH_RND;
    mode_info->vsync_end = mode_info->vsync_start + vsync;

    /* 15/13. Find pixel clock frequency (kHz for xf86) */
    mode_info->dot_clock = mode_info->htotal * 1000.0 / hperiod;
    mode_info->dot_clock -= mode_info->dot_clock % CVT_CLOCK_STEP;

    /* 16/14. Find actual Horizontal Frequency (kHz) */
    mode_info->hsync = ((float) mode_info->dot_clock) / ((float) mode_info->htotal);

    /* 17/15. Find actual Field rate */
    mode_info->vrefresh = (1000.0 * ((float) mode_info->dot_clock)) /
                          ((float) (mode_info->htotal * mode_info->vtotal));

    /* 18/16. Find actual vertical frame frequency */
    /* ignore - we don't do interlace here */
}

void
bochs_vbe_recalctimings(svga_t* svga)
{
    bochs_vbe_t *dev  = (bochs_vbe_t *) svga->priv;

    if (dev->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) {
        vbe_mode_info_t mode = { 0 };
        svga->bpp = dev->vbe_regs[VBE_DISPI_INDEX_BPP];
        dev->vbe_regs[VBE_DISPI_INDEX_XRES] &= ~7;
        if (dev->vbe_regs[VBE_DISPI_INDEX_XRES] == 0) {
            dev->vbe_regs[VBE_DISPI_INDEX_XRES] = 8;
        }
        if (dev->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] > VBE_DISPI_MAX_XRES)
            dev->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] = VBE_DISPI_MAX_XRES;
        if (dev->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] < dev->vbe_regs[VBE_DISPI_INDEX_XRES])
            dev->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] = dev->vbe_regs[VBE_DISPI_INDEX_XRES];
        if (dev->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] > VBE_DISPI_MAX_XRES)
            dev->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] = VBE_DISPI_MAX_XRES;
        if (dev->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] > VBE_DISPI_MAX_YRES)
            dev->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] = VBE_DISPI_MAX_YRES;

        if (dev->vbe_regs[VBE_DISPI_INDEX_YRES] == 0)
            dev->vbe_regs[VBE_DISPI_INDEX_YRES] = 1;
        if (dev->vbe_regs[VBE_DISPI_INDEX_YRES] > VBE_DISPI_MAX_YRES)
            dev->vbe_regs[VBE_DISPI_INDEX_YRES] = VBE_DISPI_MAX_YRES;
        gen_mode_info(dev->vbe_regs[VBE_DISPI_INDEX_XRES],
                      dev->vbe_regs[VBE_DISPI_INDEX_YRES], 72.f, &mode);
        svga->char_width = 1;
        svga->dots_per_clock = 1;
        svga->clock = (cpuclock * (double) (1ULL << 32)) / (mode.dot_clock * 1000.);
        svga->dispend = mode.vdisplay;
        svga->hdisp = mode.hdisplay;
        svga->vsyncstart = mode.vsync_start;
        svga->vtotal = mode.vtotal;
        svga->htotal = mode.htotal;
        svga->hblankstart = mode.hdisplay;
        svga->hblankend = mode.hdisplay + (mode.htotal - mode.hdisplay - 1);
        svga->vblankstart = svga->dispend; /* no vertical overscan. */
        svga->rowcount = 0;
        svga->hoverride = 1;
        if (dev->vbe_regs[VBE_DISPI_INDEX_BPP] != 4) {
            svga->fb_only = 1;
            svga->adv_flags |= FLAG_NO_SHIFT3;
        } else {
            svga->fb_only = 0;
            svga->adv_flags &= ~FLAG_NO_SHIFT3;
        }

        svga->bpp = dev->vbe_regs[VBE_DISPI_INDEX_BPP];

        if (svga->bpp == 4) {
            svga->rowoffset = (dev->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] / 2) >> 3;
            svga->ma_latch  = (dev->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] * svga->rowoffset) +
                              (dev->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] >> 3);
        } else {
            svga->rowoffset = dev->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] * ((svga->bpp == 15) ? 2 : (svga->bpp / 8));
            svga->ma_latch = (dev->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] * svga->rowoffset) +
                             (dev->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] * ((svga->bpp == 15) ? 2 : (svga->bpp / 8)));            
        }
        if (svga->ma_latch != dev->ma_latch_old) {
            if (svga->bpp == 4) {
                svga->maback = (svga->maback - (dev->ma_latch_old << 2)) +
                               (svga->ma_latch << 2);
            } else {
                svga->maback = (svga->maback - (dev->ma_latch_old)) +
                                (svga->ma_latch);
                dev->ma_latch_old = svga->ma_latch;
            }
        }

        if (svga->bpp == 4)
            dev->vbe_regs[VBE_DISPI_INDEX_VIRT_HEIGHT] = (svga->vram_max * 2) / dev->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH];
        else
            dev->vbe_regs[VBE_DISPI_INDEX_VIRT_HEIGHT] = (svga->vram_max / ((svga->bpp == 15) ? 2 : (svga->bpp / 8))) / dev->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH];
        svga->split = 0xffffff;

        switch (svga->bpp) {
            case 4:
                svga->render = svga_render_4bpp_highres;
                break;
            default:
            case 8:
                svga->render = svga_render_8bpp_clone_highres;
                break;
            case 15:
                svga->render = svga_render_15bpp_highres;
                break;
            case 16:
                svga->render = svga_render_16bpp_highres;
                break;
            case 24:
                svga->render = svga_render_24bpp_highres;
               break;
            case 32:
                svga->render = svga_render_32bpp_highres;
                break;
        }
    } else {
        svga->fb_only = 0;
        svga->packed_4bpp = 0;
        svga->adv_flags &= ~FLAG_NO_SHIFT3;
        svga->hoverride = 0;
    }
}

uint16_t
bochs_vbe_inw(const uint16_t addr, void *priv)
{
    const bochs_vbe_t *dev          = (bochs_vbe_t *) priv;
    const bool         vbe_get_caps = !!(dev->vbe_regs[VBE_DISPI_INDEX_ENABLE] &
                                         VBE_DISPI_GETCAPS);
    uint16_t           ret;

    if (addr == 0x1ce)
        ret = dev->vbe_index;
    else  switch (dev->vbe_index) {
        default:
            ret = dev->vbe_regs[dev->vbe_index];
            break;
        case VBE_DISPI_INDEX_XRES:
            ret = vbe_get_caps ? VBE_DISPI_MAX_XRES : dev->vbe_regs[dev->vbe_index];
            break;
        case VBE_DISPI_INDEX_YRES:
            ret = vbe_get_caps ? VBE_DISPI_MAX_YRES : dev->vbe_regs[dev->vbe_index];
            break;
        case VBE_DISPI_INDEX_BPP:
            ret = vbe_get_caps ? 32 : dev->vbe_regs[dev->vbe_index];
            break;
        case VBE_DISPI_INDEX_VIDEO_MEMORY_64K:
            ret = dev->vram_size >> 16;
            break;
        case VBE_DISPI_INDEX_BANK:
            ret = vbe_get_caps ? (VBE_DISPI_BANK_GRANULARITY_32K << 8) :
                                 dev->vbe_regs[dev->vbe_index];
            break;
        case VBE_DISPI_INDEX_DDC:
            if (dev->vbe_regs[dev->vbe_index] & (1 << 7)) {
                ret = dev->vbe_regs[dev->vbe_index] & ((1 << 7) | 0x3);
                if ((ret & 0x01) && i2c_gpio_get_scl(dev->i2c))
                    ret |= 0x04;
                if ((ret & 0x02) && i2c_gpio_get_sda(dev->i2c))
                    ret |= 0x08;
            } else
                ret = 0x000f;
            break;
    }

    return ret;
}

uint32_t
bochs_vbe_inl(const uint16_t addr, void *priv)
{
    const bochs_vbe_t *dev          = (bochs_vbe_t *) priv;
    uint32_t           ret;

    if (addr == 0x1ce)
        ret = dev->vbe_index;
    else
        ret = dev->vram_size;

    return ret;
}

void
bochs_vbe_outw(const uint16_t addr, const uint16_t val, void *priv)
{
    bochs_vbe_t  *dev  = (bochs_vbe_t *) priv;

    if (addr == 0x1ce)
        dev->vbe_index = val;
    else if ((addr == 0x1cf) || (addr == 0x1d0))  switch (dev->vbe_index) {
        default:
            break;
        case VBE_DISPI_INDEX_ID:
            if ((val == VBE_DISPI_ID0) || (val == VBE_DISPI_ID1) ||
                (val == VBE_DISPI_ID2) || (val == VBE_DISPI_ID3) ||
                (val == VBE_DISPI_ID4))
                dev->vbe_regs[dev->vbe_index] = val;
            else if (val == VBE_DISPI_ID5)
                dev->vbe_regs[dev->vbe_index] = dev->id5_val;
            break;
        case VBE_DISPI_INDEX_XRES:
        case VBE_DISPI_INDEX_YRES:
        case VBE_DISPI_INDEX_BPP:
        case VBE_DISPI_INDEX_VIRT_WIDTH:
        case VBE_DISPI_INDEX_X_OFFSET:
        case VBE_DISPI_INDEX_Y_OFFSET:
            dev->vbe_regs[dev->vbe_index] = val;
            if (dev->vbe_index == VBE_DISPI_INDEX_X_OFFSET || dev->vbe_index == VBE_DISPI_INDEX_Y_OFFSET) {
                svga_t *svga = &dev->svga;
                if (svga->bpp == 4) {
                    svga->rowoffset = (dev->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] / 2) >> 3;
                    svga->ma_latch  = (dev->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] * svga->rowoffset) +
                                    (dev->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] >> 3);
                } else {
                    svga->rowoffset = dev->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] * ((svga->bpp == 15) ? 2 : (svga->bpp / 8));
                    svga->ma_latch = (dev->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] * svga->rowoffset) +
                                    (dev->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] * ((svga->bpp == 15) ? 2 : (svga->bpp / 8)));            
                }
                if (svga->ma_latch != dev->ma_latch_old) {
                    if (svga->bpp == 4) {
                        svga->maback = (svga->maback - (dev->ma_latch_old << 2)) +
                                    (svga->ma_latch << 2);
                    } else {
                        svga->maback = (svga->maback - (dev->ma_latch_old)) +
                                        (svga->ma_latch);
                        dev->ma_latch_old = svga->ma_latch;
                    }
                }
            }
            else
                svga_recalctimings(&dev->svga);
            break;

        case VBE_DISPI_INDEX_BANK:
            if (val & VBE_DISPI_BANK_RD)
                dev->svga.read_bank = (val & 0x1ff) * (dev->bank_gran << 10);
            if (val & VBE_DISPI_BANK_WR)
                dev->svga.write_bank = (val & 0x1ff) * (dev->bank_gran << 10);
            break;

        case VBE_DISPI_INDEX_DDC:
            if (val & (1 << 7)) {
                i2c_gpio_set(dev->i2c, !!(val & 1), !!(val & 2));
                dev->vbe_regs[dev->vbe_index] = val;
            } else
                dev->vbe_regs[dev->vbe_index] &= ~(1 << 7);
            break;

        case VBE_DISPI_INDEX_ENABLE: {
            uint32_t new_bank_gran;
            dev->vbe_regs[dev->vbe_index] = val;
            if ((val & VBE_DISPI_ENABLED) &&
                !(dev->vbe_regs[VBE_DISPI_ENABLED] & VBE_DISPI_ENABLED)) {
                dev->vbe_regs[dev->vbe_index] = val;
                dev->vbe_regs[VBE_DISPI_INDEX_X_OFFSET] = 0;
                dev->vbe_regs[VBE_DISPI_INDEX_Y_OFFSET] = 0;
                dev->vbe_regs[VBE_DISPI_INDEX_VIRT_WIDTH] = 0;
                svga_recalctimings(&dev->svga);
                if (!(val & VBE_DISPI_NOCLEARMEM)) {
                    memset(dev->svga.vram, 0,
                           dev->vbe_regs[VBE_DISPI_INDEX_YRES] * dev->svga.rowoffset);
                }
            } else
                dev->svga.read_bank = dev->svga.write_bank = 0;
            if ((val & VBE_DISPI_BANK_GRANULARITY_32K) != 0)
                new_bank_gran = 32;
            else
                new_bank_gran = 64;
            if (dev->bank_gran != new_bank_gran) {
                dev->bank_gran = new_bank_gran;
                dev->svga.read_bank = dev->svga.write_bank = 0;
            }
            if (val & VBE_DISPI_8BIT_DAC)
                dev->svga.adv_flags &= ~FLAG_RAMDAC_SHIFT;
            else
                dev->svga.adv_flags |= FLAG_RAMDAC_SHIFT;
            dev->vbe_regs[dev->vbe_index] &= ~VBE_DISPI_NOCLEARMEM;
        }
    }
}

void
bochs_vbe_outl(const uint16_t addr, const uint32_t val, void *priv)
{
    bochs_vbe_outw(addr, val & 0xffff, priv);
    bochs_vbe_outw(addr + 2, val >> 16, priv);
}

void
bochs_vbe_out(uint16_t addr, uint8_t val, void *priv)
{
    bochs_vbe_t *dev  = (bochs_vbe_t *) priv;
    svga_t *     svga = &dev->svga;
    uint8_t      old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case VBE_DISPI_IOPORT_INDEX:
            dev->vbe_index = val;
            return;
        case VBE_DISPI_IOPORT_DATA:
            bochs_vbe_outw(0x1cf, val | (bochs_vbe_inw(0x1cf, dev) & 0xFF00), dev);
            return;
        case VBE_DISPI_IOPORT_DATA + 1:
            bochs_vbe_outw(0x1cf, (val << 8) | (bochs_vbe_inw(0x1cf, dev) & 0xFF), dev);
            return;
        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if (svga->crtcreg & 0x20)
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

uint8_t
bochs_vbe_in(uint16_t addr, void *priv)
{
    bochs_vbe_t *dev  = (bochs_vbe_t *) priv;
    svga_t *     svga = &dev->svga;
    uint8_t      ret;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        default:
            ret = svga_in(addr, svga);
            break;
        case VBE_DISPI_IOPORT_INDEX:
            ret = dev->vbe_index;
            break;
        case VBE_DISPI_IOPORT_DATA:
            ret = bochs_vbe_inw(0x1cf, dev);
            break;
        case VBE_DISPI_IOPORT_DATA + 1:
            ret = bochs_vbe_inw(0x1cf, dev) >> 8;
            break;
        case 0x3D4:
            ret = svga->crtcreg;
            break;
        case 0x3D5:
            if (svga->crtcreg & 0x20)
                ret = 0xff;
            else
                ret = svga->crtc[svga->crtcreg];
            break;
    }

    return ret;
}

static uint8_t
bochs_vbe_pci_read(const int func, const int addr, void *priv)
{
    const bochs_vbe_t *dev = (bochs_vbe_t *) priv;
    uint8_t            ret = 0x00;

    if (func == 0x00)  switch (addr) {
        default:
            break;
        case 0x00:
            ret = (dev->id5_val == VBE_DISPI_ID5) ? 0x34 : 0xee;
            break;
        case 0x01:
            ret = (dev->id5_val == VBE_DISPI_ID5) ? 0x12 : 0x80;
            break;
        case 0x02:
            ret = (dev->id5_val == VBE_DISPI_ID5) ? 0x11 : 0xef;
            break;
        case 0x03:
            ret = (dev->id5_val == VBE_DISPI_ID5) ? 0x11 : 0xbe;
            break;
        case 0x04:
            ret = (dev->pci_conf_status & 0b11100011) | 0x80;
            break;
        case 0x06:
            ret = 0x80;
            break;
        case 0x07:
            ret = 0x02;
            break;
        case 0x0b:
            ret = 0x03;
            break;
        case 0x13:
            ret = dev->pci_regs[addr];
            break;
        case 0x17:
            ret = (dev->pci_regs[0x13] != 0x00) ? 0xe0 : 0x00;
            break;
        case 0x30:
            ret = dev->pci_rom_enable & 0x01;
            break;
        case 0x32:
            ret = dev->rom_addr & 0xff;
            break;
        case 0x33:
            ret = (dev->rom_addr & 0xff00) >> 8;
            break;
        case 0x3c:
            ret = dev->pci_line_interrupt;
            break;
        case 0x3d:
            ret = 0x01;
            break;
    } else
        ret = 0xff;

    return ret;
}

static void
bochs_vbe_disable_handlers(bochs_vbe_t *dev)
{
    io_removehandler(0x03c0, 0x0020, bochs_vbe_in, NULL, NULL,
                     bochs_vbe_out, NULL, NULL, dev);
    io_removehandler(0x01ce, 0x0003, bochs_vbe_in, bochs_vbe_inw,
                     bochs_vbe_inl, bochs_vbe_out, bochs_vbe_outw, bochs_vbe_outl, dev);

    mem_mapping_disable(&dev->linear_mapping_2);
    mem_mapping_disable(&dev->linear_mapping);
    mem_mapping_disable(&dev->svga.mapping);
    mem_mapping_disable(&dev->bios_rom.mapping);

    /* Save all the mappings and the timers because they are part of linked lists. */
    reset_state->linear_mapping_2 = dev->linear_mapping_2;
    reset_state->linear_mapping   = dev->linear_mapping;
    reset_state->svga.mapping     = dev->svga.mapping;
    reset_state->bios_rom.mapping = dev->bios_rom.mapping;

    reset_state->svga.timer       = dev->svga.timer;
    reset_state->svga.timer8514   = dev->svga.timer8514;
}

static void
bochs_vbe_pci_write(const int func, const int addr, const uint8_t val, void *priv)
{
    bochs_vbe_t *dev = (bochs_vbe_t *) priv;

    if (func == 0x00)  switch (addr) {
        default:
            break;
        case 0x04:
            dev->pci_conf_status = val;
            io_removehandler(0x03c0, 0x0020, bochs_vbe_in, NULL, NULL,
                             bochs_vbe_out, NULL, NULL, dev);
            io_removehandler(0x01ce, 0x0003, bochs_vbe_in, bochs_vbe_inw,
                             bochs_vbe_inl, bochs_vbe_out, bochs_vbe_outw, bochs_vbe_outl, dev);
            mem_mapping_disable(&dev->linear_mapping_2);
            mem_mapping_disable(&dev->linear_mapping);
            mem_mapping_disable(&dev->svga.mapping);
            if (dev->pci_conf_status & PCI_COMMAND_IO) {
                io_sethandler(0x03c0, 0x0020, bochs_vbe_in, NULL, NULL,
                              bochs_vbe_out, NULL, NULL, dev);
                io_sethandler(0x01ce, 0x0003, bochs_vbe_in, bochs_vbe_inw, bochs_vbe_inl,
                              bochs_vbe_out, bochs_vbe_outw, bochs_vbe_outl, dev);
            }
            if (dev->pci_conf_status & PCI_COMMAND_MEM) {
                mem_mapping_enable(&dev->svga.mapping);
                if (dev->pci_regs[0x13] != 0x00) {
                    mem_mapping_enable(&dev->linear_mapping);
                    if (dev->pci_regs[0x13] != 0xe0)
                        mem_mapping_enable(&dev->linear_mapping_2);
                }
            }
            break;
        case 0x13:
            dev->pci_regs[addr] = val;

            mem_mapping_disable(&dev->linear_mapping_2);
            mem_mapping_disable(&dev->linear_mapping);

            if ((dev->pci_conf_status & PCI_COMMAND_MEM) && (val != 0x00)) {
                mem_mapping_set_addr(&dev->linear_mapping, val << 24, 0x01000000);
                if (val != 0xe0)
                    mem_mapping_set_addr(&dev->linear_mapping_2, 0xe0000000, 0x01000000);
            }
            break;
        case 0x3c:
            dev->pci_line_interrupt = val;
            break;
        case 0x30:
            dev->pci_rom_enable = val & 0x01;
            mem_mapping_disable(&dev->bios_rom.mapping);
            if (dev->pci_rom_enable)
                mem_mapping_set_addr(&dev->bios_rom.mapping, dev->rom_addr << 16, 0x10000);
            break;
        case 0x32:
            dev->rom_addr = (dev->rom_addr & 0xff00) | (val & 0xfc);
            if (dev->pci_rom_enable)
                mem_mapping_set_addr(&dev->bios_rom.mapping, dev->rom_addr << 16, 0x10000);
            break;
         case 0x33:
            dev->rom_addr = (dev->rom_addr & 0x00ff) | (val << 8);
            if (dev->pci_rom_enable)
                mem_mapping_set_addr(&dev->bios_rom.mapping, dev->rom_addr << 16, 0x10000);
            break;
    }
}

static void
bochs_vbe_reset(void *priv)
{
    bochs_vbe_t *dev = (bochs_vbe_t *) priv;

    if (reset_state != NULL) {
        bochs_vbe_disable_handlers(dev);
        reset_state->slot = dev->slot;

        *dev = *reset_state;
    }
}

static void *
bochs_vbe_init(const device_t *info)
{
    bochs_vbe_t *dev = calloc(1, sizeof(bochs_vbe_t));
    reset_state = calloc(1, sizeof(bochs_vbe_t));

    dev->id5_val = device_get_config_int("revision");
    dev->vram_size = device_get_config_int("memory") * (1 << 20);

    rom_init(&dev->bios_rom, "roms/video/bochs/VGABIOS-lgpl-latest.bin",
             0xc0000, 0x10000, 0xffff, 0x0000,
             MEM_MAPPING_EXTERNAL);

    if (dev->id5_val == VBE_DISPI_ID4) {
        /* Patch the BIOS to match the PCI ID. */
        dev->bios_rom.rom[0x010c] = 0xee;
        dev->bios_rom.rom[0x8dff] -= (0xee - 0x34);

        dev->bios_rom.rom[0x010d] = 0x80;
        dev->bios_rom.rom[0x8dff] -= (0x80 - 0x12);

        dev->bios_rom.rom[0x010e] = 0xef;
        dev->bios_rom.rom[0x8dff] -= (0xef - 0x11);

        dev->bios_rom.rom[0x010f] = 0xbe;
        dev->bios_rom.rom[0x8dff] -= (0xbe - 0x11);
    }

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_bochs);

    svga_init(info, &dev->svga, dev, dev->vram_size,
              bochs_vbe_recalctimings,
              bochs_vbe_in, bochs_vbe_out,
              NULL,
              NULL);

    mem_mapping_add(&dev->linear_mapping, 0, 0,
                    svga_readb_linear, svga_readw_linear, svga_readl_linear,
                    svga_writeb_linear, svga_writew_linear, svga_writel_linear,
                    NULL, MEM_MAPPING_EXTERNAL, &dev->svga);
    /* Hack: If the mapping gets mapped anywhere other than at 0xe0000000,
             enable this second copy of it at 0xe0000000 so michaln's driver works. */
    mem_mapping_add(&dev->linear_mapping_2, 0, 0,
                    svga_readb_linear, svga_readw_linear, svga_readl_linear,
                    svga_writeb_linear, svga_writew_linear, svga_writel_linear,
                    NULL, MEM_MAPPING_EXTERNAL, &dev->svga);

    mem_mapping_disable(&dev->bios_rom.mapping);

    mem_mapping_disable(&dev->svga.mapping);

    mem_mapping_disable(&dev->linear_mapping);
    mem_mapping_disable(&dev->linear_mapping_2);

    dev->svga.bpp     = 8;
    dev->svga.miscout = 1;

    dev->bank_gran    = 64;

    svga_set_ramdac_type(&dev->svga, RAMDAC_8BIT);

    dev->svga.adv_flags |= FLAG_RAMDAC_SHIFT;
    dev->svga.decode_mask = 0xffffff;

    dev->i2c = i2c_gpio_init("ddc_bochs");
    dev->ddc = ddc_init(i2c_gpio_get_bus(dev->i2c));
    dev->svga.packed_chain4 = 1;

    pci_add_card(PCI_ADD_NORMAL, bochs_vbe_pci_read, bochs_vbe_pci_write, dev, &dev->slot);

    *reset_state = *dev;

    return dev;
}

static int
bochs_vbe_available(void)
{
    return rom_present("roms/video/bochs/VGABIOS-lgpl-latest.bin");
}

void
bochs_vbe_close(void *priv)
{
    bochs_vbe_t *dev = (bochs_vbe_t *) priv;

    ddc_close(dev->ddc);
    i2c_gpio_close(dev->i2c);

    svga_close(&dev->svga);

    free(reset_state);
    reset_state = NULL;

    free(dev);
}

void
bochs_vbe_speed_changed(void *priv)
{
    bochs_vbe_t *bochs_vbe = (bochs_vbe_t *) priv;

    svga_recalctimings(&bochs_vbe->svga);
}

void
bochs_vbe_force_redraw(void *priv)
{
    bochs_vbe_t *bochs_vbe = (bochs_vbe_t *) priv;

    bochs_vbe->svga.fullchange = changeframecount;
}

static const device_config_t bochs_vbe_config[] = {
    // clang-format off
    {
        .name           = "revision",
        .description    = "Revision",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = VBE_DISPI_ID5,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "VirtualBox",   .value = VBE_DISPI_ID4 },
            { .description = "Bochs latest", .value = VBE_DISPI_ID5 },
            { .description = ""                                     }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 16,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description =  "4 MB", .value =  4 },
            { .description =  "8 MB", .value =  8 },
            { .description = "16 MB", .value = 16 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t bochs_svga_device = {
    .name          = "Bochs SVGA",
    .internal_name = "bochs_svga",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = bochs_vbe_init,
    .close         = bochs_vbe_close,
    .reset         = bochs_vbe_reset,
    .available     = bochs_vbe_available,
    .speed_changed = bochs_vbe_speed_changed,
    .force_redraw  = bochs_vbe_force_redraw,
    .config        = bochs_vbe_config
};
