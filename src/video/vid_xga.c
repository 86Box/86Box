/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM XGA emulation.
 *
 *
 *
 * Authors: TheCollector1995.
 *
 *          Copyright 2022 TheCollector1995.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
//#include <86box/bswap.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/dma.h>
#include <86box/rom.h>
#include <86box/mca.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_xga.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_xga_device.h>
#include "cpu.h"
#include <86box/plat_unused.h>

#define XGA_BIOS_PATH       "roms/video/xga/XGA_37F9576_Ver200.BIN"
#define XGA2_BIOS_PATH      "roms/video/xga/xga2_v300.bin"
#define INMOS_XGA_BIOS_PATH "roms/video/xga/InMOS XGA - Fairchild NM27C256Q-150.BIN"

static video_timings_t timing_xga_isa = { .type = VIDEO_ISA, .write_b = 3, .write_w = 3, .write_l =  6, .read_b = 5, .read_w = 5, .read_l = 10 };
static video_timings_t timing_xga_mca = { .type = VIDEO_MCA, .write_b = 4, .write_w = 5, .write_l = 10, .read_b = 5, .read_w = 5, .read_l = 10 };

static void    xga_ext_outb(uint16_t addr, uint8_t val, void *priv);
static uint8_t xga_ext_inb(uint16_t addr, void *priv);

static void     xga_writew(uint32_t addr, uint16_t val, void *priv);
static uint16_t xga_readw(uint32_t addr, void *priv);

static void xga_render_4bpp(svga_t *svga);
static void xga_render_8bpp(svga_t *svga);
static void xga_render_16bpp(svga_t *svga);

static void xga_write(uint32_t addr, uint8_t val, void *priv);
static void xga_writew(uint32_t addr, uint16_t val, void *priv);
static void xga_writel(uint32_t addr, uint32_t val, void *priv);
static uint8_t xga_read(uint32_t addr, void *priv);
static uint16_t xga_readw(uint32_t addr, void *priv);
static uint32_t xga_readl(uint32_t addr, void *priv);

int xga_active = 0;

#ifdef ENABLE_XGA_LOG
int xga_do_log = ENABLE_XGA_LOG;

static void
xga_log(const char *fmt, ...)
{
    va_list ap;

    if (xga_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define xga_log(fmt, ...)
#endif

void
svga_xga_out(uint16_t addr, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    uint8_t old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
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
svga_xga_in(uint16_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    uint8_t temp;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3D4:
            temp = svga->crtcreg;
            break;
        case 0x3D5:
            if (svga->crtcreg & 0x20)
                temp = 0xff;
            else
                temp = svga->crtc[svga->crtcreg];
            break;
        default:
            temp = svga_in(addr, svga);
            break;
    }
    return temp;
}

void
xga_updatemapping(svga_t *svga)
{
    xga_t *xga = (xga_t *) svga->xga;

    xga_log("OpMode = %x, linear base = %08x, aperture cntl = %d, access mode = %x, map = %x, "
            "endian reverse = %d, a5test = %d, XGA on = %d.\n", xga->op_mode, xga->linear_base,
            xga->aperture_cntl, xga->access_mode, svga->gdcreg[6] & 0x0c,
            xga->linear_endian_reverse, xga->a5_test, xga->on);

    switch (xga->op_mode & 7) {
        case 0:
            xga_log("XGA: VGA mode address decode disabled.\n");
            break;
        case 1:
            xga_log("XGA: VGA mode address decode enabled.\n");
            if (xga->base_addr_1mb) {
                mem_mapping_set_addr(&xga->linear_mapping, xga->base_addr_1mb, 0x100000);
                mem_mapping_enable(&xga->linear_mapping);
            } else if (xga->linear_base) {
                mem_mapping_set_addr(&xga->linear_mapping, xga->linear_base, 0x400000);
                mem_mapping_enable(&xga->linear_mapping);
            } else
                mem_mapping_disable(&xga->linear_mapping);
            break;
        case 2:
            xga_log("XGA: 132-Column mode address decode disabled.\n");
            break;
        case 3:
            xga_log("XGA: 132-Column mode address decode enabled.\n");
            if (xga->base_addr_1mb) {
                mem_mapping_set_addr(&xga->linear_mapping, xga->base_addr_1mb, 0x100000);
                mem_mapping_enable(&xga->linear_mapping);
            } else if (xga->linear_base) {
                mem_mapping_set_addr(&xga->linear_mapping, xga->linear_base, 0x400000);
                mem_mapping_enable(&xga->linear_mapping);
            } else
                mem_mapping_disable(&xga->linear_mapping);
            break;
        default:
            xga_log("XGA: Extended Graphics mode, ap=%d.\n", xga->aperture_cntl);
            switch (xga->aperture_cntl) {
                case 0:
                    xga_log("XGA: No 64KB aperture: 1MB=%x, 4MB=%x, SVGA Mapping Base=%x.\n", xga->base_addr_1mb, xga->linear_base, svga->mapping.base);
                    if (xga->base_addr_1mb) {
                        mem_mapping_set_addr(&xga->linear_mapping, xga->base_addr_1mb, 0x100000);
                        mem_mapping_enable(&xga->linear_mapping);
                    } else if (xga->linear_base) {
                        mem_mapping_set_addr(&xga->linear_mapping, xga->linear_base, 0x400000);
                        mem_mapping_enable(&xga->linear_mapping);
                    } else
                        mem_mapping_disable(&xga->linear_mapping);

                    mem_mapping_set_handler(&svga->mapping, svga->read, svga->readw, svga->readl, svga->write, svga->writew, svga->writel);
                    switch (svga->gdcreg[6] & 0xc) {
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
                    break;
                case 1:
                    xga_log("XGA: 64KB aperture at A0000.\n");
                    mem_mapping_set_handler(&svga->mapping, xga_read, xga_readw, xga_readl, xga_write, xga_writew, xga_writel);
                    mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                    xga->banked_mask = 0xffff;
                    break;
                case 2:
                    xga_log("XGA: 64KB aperture at B0000.\n");
                    mem_mapping_set_handler(&svga->mapping, xga_read, xga_readw, xga_readl, xga_write, xga_writew, xga_writel);
                    mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x10000);
                    xga->banked_mask = 0xffff;
                    break;
                default:
                    break;
            }
            break;
    }
}

static void
xga_render_blank(svga_t *svga)
{
    xga_t *xga = (xga_t *) svga->xga;

    if ((xga->displine + svga->y_add) < 0)
        return;

    if (xga->firstline_draw == 2000)
        xga->firstline_draw = xga->displine;

    xga->lastline_draw = xga->displine;

    uint32_t *line_ptr   = &svga->monitor->target_buffer->line[xga->displine + svga->y_add][svga->x_add];
    uint32_t  line_width = (uint32_t)(xga->h_disp) * sizeof(uint32_t);

    if (xga->h_disp > 0)
        memset(line_ptr, 0, line_width);
}

void
xga_recalctimings(svga_t *svga)
{
    xga_t *xga = (xga_t *) svga->xga;
    if (xga->on) {
        xga->h_total      = xga->htotal + 1;
        xga->v_total      = xga->vtotal + 1;
        xga->dispend      = xga->vdispend + 1;
        xga->v_syncstart  = xga->vsyncstart + 1;
        xga->split        = xga->linecmp + 1;
        xga->v_blankstart = xga->vblankstart + 1;

        xga->h_disp = (xga->hdisp + 1) << 3;

        xga->rowoffset = xga->pix_map_width;

        xga->interlace = !!(xga->disp_cntl_1 & 0x08);
        xga->rowcount  = (xga->disp_cntl_2 & 0xc0) >> 6;

        if (xga->interlace) {
            xga->v_total >>= 1;
            xga->dispend >>= 1;
            xga->v_syncstart >>= 1;
            xga->split >>= 1;
            xga->v_blankstart >>= 1;
        }

        xga->ma_latch = xga->disp_start_addr;


        xga_log("XGA ClkSel1 = %d, ClkSel2 = %02x, dispcntl2=%02x.\n", (xga->clk_sel_1 >> 2) & 3, xga->clk_sel_2 & 0x80, xga->disp_cntl_2 & 0xc0);
        switch ((xga->clk_sel_1 >> 2) & 3) {
            case 0:
                xga_log("HDISP VGA0 = %d, XGA = %d.\n", svga->hdisp, xga->h_disp);
                if (xga->clk_sel_2 & 0x80)
                    svga->clock_xga = (cpuclock * (double) (1ULL << 32)) / 41539000.0;
                else
                    svga->clock_xga = (cpuclock * (double) (1ULL << 32)) / 25175000.0;
                break;
            case 1:
                xga_log("HDISP VGA1 = %d, XGA = %d.\n", svga->hdisp, xga->h_disp);
                svga->clock_xga = (cpuclock * (double) (1ULL << 32)) / 28322000.0;
                break;
            case 3:
                svga->clock_xga = (cpuclock * (double) (1ULL << 32)) / 44900000.0;
                break;

            default:
                break;
        }

        switch (xga->disp_cntl_2 & 7) {
            case 2:
                svga->render_xga = xga_render_4bpp;
                break;
            case 3:
                svga->render_xga = xga_render_8bpp;
                break;
            case 4:
                svga->render_xga = xga_render_16bpp;
                break;

            default:
                svga->render_xga = xga_render_blank;
                break;
        }
    }
}

static void
xga_ext_out_reg(xga_t *xga, svga_t *svga, uint8_t idx, uint8_t val)
{
    uint8_t index;

    switch (idx) {
        case 0x10:
            xga->htotal = (xga->htotal & 0xff00) | val;
            break;
        case 0x11:
            xga->htotal = (xga->htotal & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x12:
            xga->hdisp = (xga->hdisp & 0xff00) | val;
            break;
        case 0x13:
            xga->hdisp = (xga->hdisp & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x20:
            xga->vtotal = (xga->vtotal & 0xff00) | val;
            break;
        case 0x21:
            xga->vtotal = (xga->vtotal & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x22:
            xga->vdispend = (xga->vdispend & 0xff00) | val;
            break;
        case 0x23:
            xga->vdispend = (xga->vdispend & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x24:
            xga->vblankstart = (xga->vblankstart & 0xff00) | val;
            break;
        case 0x25:
            xga->vblankstart = (xga->vblankstart & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x28:
            xga->vsyncstart = (xga->vsyncstart & 0xff00) | val;
            break;
        case 0x29:
            xga->vsyncstart = (xga->vsyncstart & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x2c:
            xga->linecmp = (xga->linecmp & 0xff00) | val;
            break;
        case 0x2d:
            xga->linecmp = (xga->linecmp & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x30:
            xga->hwc_pos_x  = (xga->hwc_pos_x & 0x0700) | val;
            xga->hwcursor.x = xga->hwc_pos_x;
            break;
        case 0x31:
            xga->hwc_pos_x  = (xga->hwc_pos_x & 0xff) | ((val & 0x07) << 8);
            xga->hwcursor.x = xga->hwc_pos_x;
            break;

        case 0x32:
            xga->hwc_hotspot_x = val & 0x3f;
            xga->hwcursor.xoff = val & 0x3f;
            break;

        case 0x33:
            xga->hwc_pos_y  = (xga->hwc_pos_y & 0x0700) | val;
            xga->hwcursor.y = xga->hwc_pos_y;
            break;
        case 0x34:
            xga->hwc_pos_y  = (xga->hwc_pos_y & 0xff) | ((val & 0x07) << 8);
            xga->hwcursor.y = xga->hwc_pos_y;
            break;

        case 0x35:
            xga->hwc_hotspot_y = val & 0x3f;
            xga->hwcursor.yoff = val & 0x3f;
            break;

        case 0x36:
            xga->hwc_control  = val;
            xga->hwcursor.ena = xga->hwc_control & 1;
            break;

        case 0x38:
            xga->hwc_color0 = (xga->hwc_color0 & 0xffff00) | val;
            break;
        case 0x39:
            xga->hwc_color0 = (xga->hwc_color0 & 0xff00ff) | (val << 8);
            break;
        case 0x3a:
            xga->hwc_color0 = (xga->hwc_color0 & 0x00ffff) | (val << 16);
            break;

        case 0x3b:
            xga->hwc_color1 = (xga->hwc_color1 & 0xffff00) | val;
            break;
        case 0x3c:
            xga->hwc_color1 = (xga->hwc_color1 & 0xff00ff) | (val << 8);
            break;
        case 0x3d:
            xga->hwc_color1 = (xga->hwc_color1 & 0x00ffff) | (val << 16);
            break;

        case 0x40:
            xga->disp_start_addr = (xga->disp_start_addr & 0x7ff00) | val;
            xga_log("DISPSTARTADDR0=%x.\n", xga->disp_start_addr);
            break;
        case 0x41:
            xga->disp_start_addr = (xga->disp_start_addr & 0x700ff) | (val << 8);
            xga_log("DISPSTARTADDR8=%x.\n", xga->disp_start_addr);
            break;
        case 0x42:
            xga->disp_start_addr = (xga->disp_start_addr & 0x0ffff) | ((val & 0x07) << 16);
            xga_log("DISPSTARTADDR16=%x.\n", xga->disp_start_addr);
            svga_recalctimings(svga);
            break;

        case 0x43:
            xga->pix_map_width = (xga->pix_map_width & 0x700) | val;
            break;
        case 0x44:
            xga->pix_map_width = (xga->pix_map_width & 0xff) | ((val & 0x07) << 8);
            svga_recalctimings(svga);
            break;

        case 0x50:
            xga_log("Reg50 write=%02x.\n", val);
            xga->disp_cntl_1 = val;
            svga_recalctimings(svga);
            break;

        case 0x51:
            xga_log("Reg51 write=%02x.\n", val & 0x07);
            xga->disp_cntl_2 = val;
            xga->on          = ((val & 0x07) >= 0x02);
            svga_recalctimings(svga);
            break;

        case 0x54:
            xga_log("Reg54 write = %02x.\n", val);
            xga->clk_sel_1 = val;
            svga_recalctimings(svga);
            break;

        case 0x55:
            xga->border_color = val;
            break;

        case 0x59:
            xga->direct_color = val;
            break;

        case 0x60:
            xga->sprite_pal_addr_idx = (xga->sprite_pal_addr_idx & 0x3f00) | val;
            xga->dac_pos             = 0;
            xga->dac_addr            = val & 0xff;
            break;
        case 0x61:
            xga->sprite_pal_addr_idx = (xga->sprite_pal_addr_idx & 0xff) | ((val & 0x3f) << 8);
            xga->sprite_pos          = xga->sprite_pal_addr_idx & 0x1ff;

            xga_log("Sprite POS = %d, data on = %d, idx = %d, apcntl = %d\n", xga->sprite_pos,
                    xga->cursor_data_on, xga->sprite_pal_addr_idx, xga->aperture_cntl);
            break;

        case 0x64:
            xga->dac_mask = val;
            xga_log("DAC mask=%02x.\n", val);
            break;

        case 0x65:
            svga->fullchange = svga->monitor->mon_changeframecount;
            switch (xga->dac_pos) {
                case 0:
                    xga->dac_r = val;
                    xga->dac_pos++;
                    break;
                case 1:
                    xga->dac_g = val;
                    xga->dac_pos++;
                    break;
                case 2:
                    xga->pal_b            = val;
                    index                 = xga->dac_addr & 0xff;
                    xga->xgapal[index].r = xga->dac_r;
                    xga->xgapal[index].g = xga->dac_g;
                    xga->xgapal[index].b = xga->pal_b;
                    xga->pallook[index]  = makecol32(xga->xgapal[index].r, xga->xgapal[index].g, xga->xgapal[index].b);
                    xga_log("XGA Pallook=%06x, idx=%d.\n", xga->pallook[index], index);
                    xga->dac_pos         = 0;
                    xga->dac_addr        = (xga->dac_addr + 1) & 0xff;
                    break;

                default:
                    break;
            }
            break;

        case 0x66:
            xga_log("Palette Sequence=%02x.\n", val);
            xga->pal_seq = val;
            break;

        case 0x6a:
            xga->sprite_data[xga->sprite_pos] = val;
            xga->sprite_pos                   = (xga->sprite_pos + 1) & 0x3ff;
            break;

        case 0x70:
            xga_log("Reg70 write = %02x.\n", val);
            xga->clk_sel_2 = val;
            svga_recalctimings(svga);
            break;

        default:
            break;
    }
}

static void
xga_ext_outb(uint16_t addr, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;

    xga_log("[%04X:%08X]: EXT OUTB = %02x, val = %02x\n", CS, cpu_state.pc, addr, val);
    switch (addr & 0x0f) {
        case 0:
            xga_log("[%04X:%08X]: EXT OUTB = %02x, val = %02x\n", CS, cpu_state.pc, addr, val);
            xga->op_mode = val;
            break;
        case 1:
            xga_log("[%04X:%08X]: EXT OUTB = %02x, val = %02x\n", CS, cpu_state.pc, addr, val);
            xga->aperture_cntl = val & 3;
            xga_updatemapping(svga);
            break;
        case 8:
            xga->ap_idx = val;
            xga_log("Aperture CNTL = %d, val = %02x, up to bit6 = %02x\n", xga->aperture_cntl,
                    val, val & 0x3f);
            if ((xga->op_mode & 7) < 4) {
                xga->write_bank = xga->read_bank = 0;
            } else {
                if (xga->base_addr_1mb) {
                    if (xga->aperture_cntl) {
                        xga->write_bank = (xga->ap_idx & 0x3f) << 16;
                        xga->read_bank  = xga->write_bank;
                    } else {
                        xga->write_bank = (xga->ap_idx & 0x30) << 16;
                        xga->read_bank  = xga->write_bank;
                    }
                } else {
                    xga->write_bank = (xga->ap_idx & 0x3f) << 16;
                    xga->read_bank  = xga->write_bank;
                }
            }
            break;
        case 9:
            xga->access_mode = val;
            break;
        case 0x0a:
            xga->regs_idx = val;
            break;
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            xga->regs[xga->regs_idx] = val;
            xga_log("EXT OUT Reg=%02x, val=%02x.\n", xga->regs_idx, val);
            xga_ext_out_reg(xga, svga, xga->regs_idx, xga->regs[xga->regs_idx]);
            break;

        default:
            xga_log("[%04X:%08X]: EXT OUTB = %02x, val = %02x\n", CS, cpu_state.pc, addr, val);
            break;
    }
}

static uint8_t
xga_ext_inb(uint16_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;
    uint8_t ret  = 0;
    uint8_t index;

    switch (addr & 0x0f) {
        case 0:
            ret = xga->op_mode;
            break;
        case 1:
            ret = xga->aperture_cntl;
            break;
        case 8:
            ret = xga->ap_idx;
            break;
        case 9:
            ret = xga->access_mode;
            break;
        case 0x0a:
            ret = xga->regs_idx;
            break;
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            switch (xga->regs_idx) {
                case 0:
                    ret = (xga->bus & DEVICE_MCA) ? 0x02 : 0x01;
                    break;
                case 4:
                    if (xga->bus & DEVICE_MCA)
                        ret = 0x01; /*32-bit MCA*/
                    else
                        ret = 0x10; /*16-bit ISA*/
                    break;
                case 0x10:
                    ret = xga->htotal & 0xff;
                    break;
                case 0x11:
                    ret = xga->htotal >> 8;
                    break;
                case 0x12:
                    ret = xga->hdisp & 0xff;
                    break;
                case 0x13:
                    ret = xga->hdisp >> 8;
                    break;
                case 0x20:
                    ret = xga->vtotal & 0xff;
                    break;
                case 0x21:
                    ret = xga->vtotal >> 8;
                    break;
                case 0x22:
                    ret = xga->vdispend & 0xff;
                    break;
                case 0x23:
                    ret = xga->vdispend >> 8;
                    break;
                case 0x24:
                    ret = xga->vblankstart & 0xff;
                    break;
                case 0x25:
                    ret = xga->vblankstart >> 8;
                    break;
                case 0x28:
                    ret = xga->vsyncstart & 0xff;
                    break;
                case 0x29:
                    ret = xga->vsyncstart >> 8;
                    break;
                case 0x2c:
                    ret = xga->linecmp & 0xff;
                    break;
                case 0x2d:
                    ret = xga->linecmp >> 8;
                    break;
                case 0x30:
                    ret = xga->hwc_pos_x & 0xff;
                    break;
                case 0x31:
                    ret = xga->hwc_pos_x >> 8;
                    break;
                case 0x32:
                    ret = xga->hwc_hotspot_x;
                    break;
                case 0x33:
                    ret = xga->hwc_pos_y & 0xff;
                    break;
                case 0x34:
                    ret = xga->hwc_pos_y >> 8;
                    break;
                case 0x35:
                    ret = xga->hwc_hotspot_y;
                    break;
                case 0x36:
                    ret = xga->hwc_control;
                    break;
                case 0x38:
                    ret = xga->hwc_color0 & 0xff;
                    break;
                case 0x39:
                    ret = xga->hwc_color0 >> 8;
                    break;
                case 0x3a:
                    ret = xga->hwc_color0 >> 16;
                    break;
                case 0x3b:
                    ret = xga->hwc_color1 & 0xff;
                    break;
                case 0x3c:
                    ret = xga->hwc_color1 >> 8;
                    break;
                case 0x3d:
                    ret = xga->hwc_color1 >> 16;
                    break;
                case 0x40:
                    ret = xga->disp_start_addr & 0xff;
                    break;
                case 0x41:
                    ret = xga->disp_start_addr >> 8;
                    break;
                case 0x42:
                    ret = xga->disp_start_addr >> 16;
                    break;
                case 0x43:
                    ret = xga->pix_map_width & 0xff;
                    break;
                case 0x44:
                    ret = xga->pix_map_width >> 8;
                    break;
                case 0x50:
                    ret = xga->disp_cntl_1 | 0x20;
                    break;
                case 0x51:
                    ret = xga->disp_cntl_2;
                    break;
                case 0x52:
                    ret = xga->type ? 0xfa : 0xea;
                    break;
                case 0x53:
                    ret = xga->type ? 0x53 : 0x30;
                    break;
                case 0x54:
                    ret = xga->clk_sel_1;
                    break;
                case 0x55:
                    ret = xga->border_color;
                    break;

                case 0x59:
                    ret = xga->direct_color;
                    break;

                case 0x60:
                    ret = xga->sprite_pal_addr_idx & 0xff;
                    break;
                case 0x61:
                    ret = xga->sprite_pal_addr_idx >> 8;
                    break;

                case 0x64:
                    ret = xga->dac_mask;
                    break;

                case 0x65:
                    index = xga->dac_addr & 0xff;
                    switch (xga->dac_pos) {
                        case 0:
                            xga->dac_pos++;
                            ret = xga->xgapal[index].r;
                            break;
                        case 1:
                            xga->dac_pos++;
                            ret = xga->xgapal[index].g;
                            break;
                        case 2:
                            xga->dac_pos  = 0;
                            xga->dac_addr = (xga->dac_addr + 1) & 0xff;
                            ret           = xga->xgapal[index].b;
                            break;

                        default:
                            break;
                    }
                    break;

                case 0x66:
                    ret = xga->pal_seq;
                    break;

                case 0x6a:
                    xga_log("Sprite POS Read=%d.\n", xga->sprite_pos);
                    ret                      = xga->sprite_data[xga->sprite_pos];
                    xga->sprite_pos = (xga->sprite_pos + 1) & 0x3ff;
                    break;

                case 0x70:
                    ret = xga->clk_sel_2;
                    break;

                case 0x74:
                    if (xga->bus & DEVICE_MCA)
                        ret = xga->regs[xga->regs_idx];
                    else {
                        ret = (xga->dma_channel << 1);
                        if (xga->dma_channel)
                            ret |= 1;
                    }
                    break;

                default:
                    ret = xga->regs[xga->regs_idx];
                    if ((xga->regs_idx == 0x0c) || (xga->regs_idx == 0x0d))
                        xga_log("EXT IN Reg=%02x, val=%02x.\n", xga->regs_idx, ret);
                    break;
            }
            break;

        default:
            xga_log("[%04X:%08X]: EXT INB = %02x, ret = %02x.\n\n", CS, cpu_state.pc, addr, ret);
            break;
    }

    xga_log("[%04X:%08X]: EXT INB = %02x, ret = %02x.\n\n", CS, cpu_state.pc, addr, ret);

    return ret;
}

#define READ(addr, dat)                         \
    dat = xga->vram[(addr) & (xga->vram_mask)];

#define WRITE(addr, dat)                                                                         \
    xga->vram[((addr)) & (xga->vram_mask)]                = dat;                                 \
    xga->changedvram[(((addr)) & (xga->vram_mask)) >> 12] = svga->monitor->mon_changeframecount;

#define READW(addr, dat)                                       \
    dat = *(uint16_t *) &xga->vram[(addr) & (xga->vram_mask)];

#define WRITEW(addr, dat)                                                                        \
    *(uint16_t *) &xga->vram[((addr)) & (xga->vram_mask)] = dat;                                 \
    xga->changedvram[(((addr)) & (xga->vram_mask)) >> 12] = svga->monitor->mon_changeframecount;

#define ROP(mix, d, s)                                                                 \
    {                                                                                  \
        switch ((mix) ? (xga->accel.frgd_mix & 0x1f) : (xga->accel.bkgd_mix & 0x1f)) { \
            case 0x00:                                                                 \
                d = 0;                                                                 \
                break;                                                                 \
            case 0x01:                                                                 \
                d = s & d;                                                             \
                break;                                                                 \
            case 0x02:                                                                 \
                d = s & ~d;                                                            \
                break;                                                                 \
            case 0x03:                                                                 \
                d = s;                                                                 \
                break;                                                                 \
            case 0x04:                                                                 \
                d = ~s & d;                                                            \
                break;                                                                 \
            case 0x05:                                                                 \
                d = d;                                                                 \
                break;                                                                 \
            case 0x06:                                                                 \
                d = s ^ d;                                                             \
                break;                                                                 \
            case 0x07:                                                                 \
                d = s | d;                                                             \
                break;                                                                 \
            case 0x08:                                                                 \
                d = ~s & ~d;                                                           \
                break;                                                                 \
            case 0x09:                                                                 \
                d = s ^ ~d;                                                            \
                break;                                                                 \
            case 0x0a:                                                                 \
                d = ~d;                                                                \
                break;                                                                 \
            case 0x0b:                                                                 \
                d = s | ~d;                                                            \
                break;                                                                 \
            case 0x0c:                                                                 \
                d = ~s;                                                                \
                break;                                                                 \
            case 0x0d:                                                                 \
                d = ~s | d;                                                            \
                break;                                                                 \
            case 0x0e:                                                                 \
                d = ~s | ~d;                                                           \
                break;                                                                 \
            case 0x0f:                                                                 \
                d = ~0;                                                                \
                break;                                                                 \
            case 0x10:                                                                 \
                d = MAX(s, d);                                                         \
                break;                                                                 \
            case 0x11:                                                                 \
                d = MIN(s, d);                                                         \
                break;                                                                 \
            case 0x12:                                                                 \
                d = MIN(~0, s + d);                                                    \
                break;                                                                 \
            case 0x13:                                                                 \
                d = MAX(0, d - s);                                                     \
                break;                                                                 \
            case 0x14:                                                                 \
                d = MAX(0, s - d);                                                     \
                break;                                                                 \
            case 0x15:                                                                 \
                d = (s + d) >> 1;                                                      \
                break;                                                                 \
        }                                                                              \
    }

static uint32_t
xga_accel_read_pattern_map_pixel(svga_t *svga, int x, int y, uint32_t base, int width)
{
    const xga_t *xga  = (xga_t *) svga->xga;
    uint32_t     addr = base;
    int          bits;
    uint8_t      byte;
    uint8_t      px;
    int          skip = 0;

    if ((addr < xga->linear_base) || (addr > (xga->linear_base + 0xfffff)))
        skip = 1;

    addr += (y * (width >> 3));
    addr += (x >> 3);
    if (!skip) {
        READ(addr, byte);
    } else
        byte = mem_readb_phys(addr);

    bits = 7 - (x & 7);

    xga_log("0. AccessMode=%02x, SRCMAP=%02x, DSTMAP=%02x, PAT=%02x.\n", xga->access_mode & 0x0f, (xga->accel.px_map_format[xga->accel.src_map] & 0x0f), (xga->accel.px_map_format[xga->accel.dst_map] & 0x0f), (xga->accel.px_map_format[xga->accel.pat_src] & 0x08));
    if (!(xga->accel.px_map_format[xga->accel.src_map] & 0x08) && !(xga->accel.px_map_format[xga->accel.dst_map] & 0x08)) {
        if (((xga->accel.px_map_format[xga->accel.src_map] & 0x07) >= 0x02) && ((xga->accel.px_map_format[xga->accel.dst_map] & 0x07) >= 0x02) && (xga->accel.pat_src <= 2))
            bits ^= 7;
    }

    px = (byte >> bits) & 1;
    return px;
}

static uint32_t
xga_accel_read_area_map_pixel(svga_t *svga, int x, int y, uint32_t base, int width)
{
    const xga_t *xga  = (xga_t *) svga->xga;
    uint32_t     addr = base;
    int          bits;
    uint8_t      byte;
    uint8_t      px;
    int          skip = 0;

    if ((addr < xga->linear_base) || (addr > (xga->linear_base + 0xfffff)))
        skip = 1;

    addr += (y * (width >> 3));
    addr += (x >> 3);
    if (!skip) {
        READ(addr, byte);
    } else
        byte = mem_readb_phys(addr);

    bits = 7 - (x & 7);

    px = (byte >> bits) & 1;
    return px;
}

static uint32_t
xga_accel_read_map_pixel(svga_t *svga, int x, int y, int map, uint32_t base, int width)
{
    xga_t   *xga  = (xga_t *) svga->xga;
    uint32_t addr = base;
    int      bits;
    uint32_t byte;
    uint8_t  px;
    int      skip = 0;

    if ((addr < xga->linear_base) || (addr > (xga->linear_base + 0xfffff)))
        skip = 1;

    switch (xga->accel.px_map_format[map] & 0x07) {
        case 0: /*1-bit*/
            addr += (y * (width >> 3));
            addr += (x >> 3);
            if (!skip) {
                READ(addr, byte);
            } else
                byte = mem_readb_phys(addr);

            xga_log("1. AccessMode=%02x, SRCMAP=%02x, DSTMAP=%02x, PAT=%02x.\n", xga->access_mode & 0x0f, (xga->accel.px_map_format[xga->accel.src_map] & 0x0f), (xga->accel.px_map_format[xga->accel.dst_map] & 0x0f), xga->accel.pat_src);
            if ((xga->accel.px_map_format[xga->accel.src_map] & 0x08) && !(xga->access_mode & 0x08))
                bits = (x & 7);
            else
                bits = 7 - (x & 7);

            px = (byte >> bits) & 1;
            return px;
        case 2: /*4-bit*/
            addr += (y * (width >> 1));
            addr += (x >> 1);
            if (!skip) {
                READ(addr, byte);
            } else
                byte = mem_readb_phys(addr);

            xga_log("4bpp read: OPMODEBIG=%02x, SRC Map=%02x, DST Map=%02x, AccessMode=%02x, SRCPIX=%02x, DSTPIX=%02x, wordpix=%04x, x=%d, y=%d, skip=%d.\n", xga->op_mode & 0x08, (xga->accel.px_map_format[xga->accel.src_map] & 0x0f), (xga->accel.px_map_format[xga->accel.dst_map] & 0x0f), xga->access_mode & 0x0f, xga->accel.src_map, xga->accel.dst_map, byte, x, y, skip);
            return byte;
        case 3: /*8-bit*/
            addr += (y * width);
            addr += x;
            if (!skip) {
                READ(addr, byte);
            } else
                byte = mem_readb_phys(addr);

            return byte;
        case 4: /*16-bit*/
            addr += (y * (width << 1));
            addr += (x << 1);

            if (!skip) {
                READW(addr, byte);
            } else  {
                byte = mem_readw_phys(addr);
                if ((xga->access_mode & 0x07) == 0x04)
                    byte = ((byte & 0xff00) >> 8) | ((byte & 0x00ff) << 8);
                else if (xga->access_mode & 0x08)
                    byte = ((byte & 0xff00) >> 8) | ((byte & 0x00ff) << 8);
            }
            return byte;

        default:
            break;
    }
    return 0;
}

static void
xga_accel_write_map_pixel(svga_t *svga, int x, int y, int map, uint32_t base, uint32_t pixel, int width)
{
    xga_t   *xga  = (xga_t *) svga->xga;
    uint32_t addr = base;
    uint8_t  byte;
    uint8_t  mask;
    int      skip = 0;

    if ((addr < xga->linear_base) || (addr > (xga->linear_base + 0xfffff)))
        skip = 1;

    switch (xga->accel.px_map_format[map] & 0x07) {
        case 0: /*1-bit*/
            addr += (y * (width >> 3));
            addr += (x >> 3);
            if (!skip) {
                READ(addr, byte);
            } else
                byte = mem_readb_phys(addr);

            if (xga->access_mode & 0x08)
                mask = 1 << (7 - (x & 7));
            else {
                if ((xga->accel.px_map_format[map] & 0x08) || (xga->accel.px_map_format[xga->accel.src_map] & 0x08)) {
                    xga_log("2. AccessMode=%02x, SRCMAP=%02x, DSTMAP=%02x, PAT=%02x.\n", xga->access_mode & 0x0f, (xga->accel.px_map_format[xga->accel.src_map] & 0x0f), (xga->accel.px_map_format[map] & 0x0f), xga->accel.pat_src);
                    mask = 1 << (x & 7);
                } else
                    mask = 1 << (7 - (x & 7));
            }

            byte = (byte & ~mask) | ((pixel ? 0xff : 0) & mask);
            if (pixel & 1) {
                if (!skip) {
                    xga->vram[addr & (xga->vram_mask)] |= mask;
                    xga->changedvram[(addr & (xga->vram_mask)) >> 12] = svga->monitor->mon_changeframecount;
                }
            } else {
                if (!skip) {
                    xga->vram[addr & (xga->vram_mask)] &= ~mask;
                    xga->changedvram[(addr & (xga->vram_mask)) >> 12] = svga->monitor->mon_changeframecount;
                }
            }
            mem_writeb_phys(addr, byte);
            break;
        case 2: /*4-bit*/
            addr += (y * (width >> 1));
            addr += (x >> 1);
            if (!skip) {
                READ(addr, byte);
            } else
                byte = mem_readb_phys(addr);

            if (xga->accel.px_map_format[map] & 0x08)
                mask = 0x0f << ((x & 1) << 2);
            else
                mask = 0x0f << ((1 - (x & 1)) << 2);

            byte = (byte & ~mask) | (pixel & mask);
            if (!skip) {
                WRITE(addr, byte);
            }
            mem_writeb_phys(addr, byte);
            break;
        case 3: /*8-bit*/
            addr += (y * width);
            addr += x;
            if (!skip) {
                WRITE(addr, pixel & 0xff);
            }
            mem_writeb_phys(addr, pixel & 0xff);
            break;
        case 4: /*16-bit*/
            addr += (y * width << 1);
            addr += (x << 1);

            if (!skip) {
                WRITEW(addr, pixel);
            } else {
                if ((xga->access_mode & 0x07) == 0x04)
                    pixel = ((pixel & 0xff00) >> 8) | ((pixel & 0x00ff) << 8);
                else if (xga->access_mode & 0x08)
                    pixel = ((pixel & 0xff00) >> 8) | ((pixel & 0x00ff) << 8);
            }
            mem_writew_phys(addr, pixel);
            break;

        default:
            break;
    }
}

static void
xga_short_stroke(svga_t *svga, uint8_t ssv)
{
    xga_t   *xga = (xga_t *) svga->xga;
    uint32_t src_dat;
    uint32_t dest_dat;
    uint32_t old_dest_dat;
    uint32_t color_cmp  = xga->accel.color_cmp;
    uint32_t plane_mask = xga->accel.plane_mask;
    uint32_t dstbase    = xga->accel.px_map_base[xga->accel.dst_map];
    uint32_t srcbase    = xga->accel.px_map_base[xga->accel.src_map];
    int      y          = ssv & 0x0f;
    int      x          = 0;
    int16_t  dx;
    int16_t  dy;
    int      dirx = 0;
    int      diry = 0;

    dx = xga->accel.dst_map_x;
    if (xga->accel.dst_map_x >= 0x1800)
        dx |= ~0x17ff;

    dy = xga->accel.dst_map_y;
    if (xga->accel.dst_map_y >= 0x1800)
        dy |= ~0x17ff;

    switch ((ssv >> 5) & 7) {
        case 0:
            dirx = 1;
            diry = 0;
            break;
        case 1:
            dirx = 1;
            diry = -1;
            break;
        case 2:
            dirx = 0;
            diry = -1;
            break;
        case 3:
            dirx = -1;
            diry = -1;
            break;
        case 4:
            dirx = -1;
            diry = 0;
            break;
        case 5:
            dirx = -1;
            diry = 1;
            break;
        case 6:
            dirx = 0;
            diry = 1;
            break;
        case 7:
            dirx = 1;
            diry = 1;
            break;

        default:
            break;
    }

    if (xga->accel.pat_src == 8) {
        while (y >= 0) {
            if (xga->accel.command & 0xc0) {
                if ((dx >= xga->accel.mask_map_origin_x_off) && (dx <= ((xga->accel.px_map_width[0] & 0xfff) + xga->accel.mask_map_origin_x_off)) && (dy >= xga->accel.mask_map_origin_y_off) && (dy <= ((xga->accel.px_map_height[0] & 0xfff) + xga->accel.mask_map_origin_y_off))) {
                    src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.src_map_x & 0xfff, xga->accel.src_map_y & 0xfff, xga->accel.src_map, srcbase, xga->accel.px_map_width[xga->accel.src_map] + 1) : xga->accel.frgd_color;
                    dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, xga->accel.px_map_width[xga->accel.dst_map] + 1);

                    if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                        old_dest_dat = dest_dat;
                        ROP(1, dest_dat, src_dat);
                        dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                        if ((xga->accel.command & 0x30) == 0) {
                            if (ssv & 0x10)
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        } else if (((xga->accel.command & 0x30) == 0x10) && x) {
                            if (ssv & 0x10)
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        } else if (((xga->accel.command & 0x30) == 0x20) && y) {
                            if (ssv & 0x10)
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        }
                    }
                }
            } else {
                src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.src_map_x & 0xfff, xga->accel.src_map_y & 0xfff, xga->accel.src_map, srcbase, xga->accel.px_map_width[xga->accel.src_map] + 1) : xga->accel.frgd_color;
                dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, xga->accel.px_map_width[xga->accel.dst_map] + 1);

                if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                    old_dest_dat = dest_dat;
                    ROP(1, dest_dat, src_dat);
                    dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                    if ((xga->accel.command & 0x30) == 0) {
                        if (ssv & 0x10)
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                    } else if (((xga->accel.command & 0x30) == 0x10) && x) {
                        if (ssv & 0x10)
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                    } else if (((xga->accel.command & 0x30) == 0x20) && y) {
                        if (ssv & 0x10)
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                    }
                }
            }

            if (!y)
                break;

            dx += dirx;
            dy += diry;

            x++;
            y--;
        }
    }

    xga->accel.dst_map_x = dx;
    xga->accel.dst_map_y = dy;
}

static void
xga_line_draw_write(svga_t *svga)
{
    xga_t   *xga = (xga_t *) svga->xga;
    uint32_t src_dat;
    uint32_t dest_dat;
    uint32_t old_dest_dat = 0x00000000;
    uint32_t color_cmp  = xga->accel.color_cmp;
    uint32_t plane_mask = xga->accel.plane_mask;
    uint32_t dstbase    = xga->accel.px_map_base[xga->accel.dst_map];
    uint32_t srcbase    = xga->accel.px_map_base[xga->accel.src_map];
    int      y = xga->accel.blt_width;
    int      x = 0;
    int      draw_pixel = 0;
    int16_t  dx;
    int16_t  dy;
    int16_t  cx;
    int16_t  cy;

    cx = xga->accel.src_map_x & 0xfff;
    cy = xga->accel.src_map_y & 0xfff;

    dx = xga->accel.dst_map_x;
    if (xga->accel.dst_map_x >= 0x1800)
        dx |= ~0x17ff;

    dy = xga->accel.dst_map_y;
    if (xga->accel.dst_map_y >= 0x1800)
        dy |= ~0x17ff;

    if ((xga->accel.command & 0x30) == 0x30)
        xga_log("Line Draw Write Fill: DX=%d, DY=%d, BLTWIDTH=%d, BLTHEIGHT=%d, FRGDCOLOR=%04x, negative XDIR=%i, negative YDIR=%i, YMAJOR=%d, ERR=%d, BRESK2=%d, BRESK1=%d, mask=%02x.\n", dx, dy, xga->accel.blt_width, xga->accel.blt_height, xga->accel.frgd_color & 0xffff, (xga->accel.octant & 0x04), (xga->accel.octant & 0x02), (xga->accel.octant & 0x01), xga->accel.bres_err_term, xga->accel.bres_k2, xga->accel.bres_k1, xga->accel.command & 0xc0);

    if (xga->accel.pat_src == 8) {
        if ((xga->accel.command & 0x30) == 0x30) {
            while (y >= 0) {
                draw_pixel = 0;

                if (xga->accel.octant & 0x01) { /*Y Major*/
                    if (xga->accel.octant & 0x02) { /*Bottom to Top*/
                        if (x)
                            draw_pixel = 1;
                    } else { /*Top to Bottom*/
                        if (y)
                            draw_pixel = 1;
                    }
                } else { /*X Major*/
                    if (xga->accel.octant & 0x04) { /*Right to Left*/
                         if (xga->accel.bres_err_term >= 0) {
                            if (xga->accel.octant & 0x02) { /*Bottom to Top*/
                                if (x)
                                    draw_pixel = 1;
                            } else { /*Top to Bottom*/
                                if (y)
                                    draw_pixel = 1;
                            }
                        }
                    } else { /*Left to Right*/
                         if (xga->accel.bres_err_term < (xga->accel.bres_k1 + xga->accel.bres_k2)) {
                            if (xga->accel.octant & 0x02) { /*Bottom to Top*/
                                if (x)
                                    draw_pixel = 1;
                            } else { /*Top to Bottom*/
                                if (y)
                                    draw_pixel = 1;
                            }
                        }
                    }
                }

                xga_log("Draw Boundary: DX=%d, DY=%d, wrt_pix=%d, ymajor=%d, bottomtotop=%x, len=%d, err=%d, frgdmix=%02x.\n", dx, dy, draw_pixel, xga->accel.octant & 0x01, xga->accel.octant & 0x02, y, xga->accel.bres_err_term, xga->accel.frgd_mix & 0x1f);
                if (xga->accel.command & 0xc0) {
                    if ((dx >= xga->accel.mask_map_origin_x_off) && (dx <= ((xga->accel.px_map_width[0] & 0xfff) + xga->accel.mask_map_origin_x_off)) && (dy >= xga->accel.mask_map_origin_y_off) && (dy <= ((xga->accel.px_map_height[0] & 0xfff) + xga->accel.mask_map_origin_y_off)) && draw_pixel) {
                        src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, cx, cy, xga->accel.src_map, srcbase, xga->accel.px_map_width[xga->accel.src_map] + 1) : xga->accel.frgd_color;
                        dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, xga->accel.px_map_width[xga->accel.dst_map] + 1);

                        if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                            old_dest_dat = dest_dat;
                            ROP(1, dest_dat, src_dat);
                            dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        }
                    }
                } else {
                    if (draw_pixel) {
                        src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, cx, cy, xga->accel.src_map, srcbase, xga->accel.px_map_width[xga->accel.src_map] + 1) : xga->accel.frgd_color;
                        dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, xga->accel.px_map_width[xga->accel.dst_map] + 1);

                        if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                            old_dest_dat = dest_dat;
                            ROP(1, dest_dat, src_dat);
                            dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        }
                    }
                }

                if (x == xga->accel.blt_width)
                    break;

                if (xga->accel.octant & 0x01) {
                    if (xga->accel.octant & 0x02)
                        dy--;
                    else
                        dy++;

                    if (xga->accel.bres_err_term >= 0) {
                        xga->accel.bres_err_term += xga->accel.bres_k2;
                        if (xga->accel.octant & 0x04)
                            dx--;
                        else
                            dx++;
                    } else
                        xga->accel.bres_err_term += xga->accel.bres_k1;
                } else {
                    if (xga->accel.octant & 0x04)
                        dx--;
                    else
                        dx++;

                    if (xga->accel.bres_err_term >= 0) {
                        xga->accel.bres_err_term += xga->accel.bres_k2;
                        if (xga->accel.octant & 0x02)
                            dy--;
                        else
                            dy++;
                    } else
                        xga->accel.bres_err_term += xga->accel.bres_k1;
                }
                x++;
                y--;
            }
        } else {
            while (y >= 0) {
                if (xga->accel.command & 0xc0) {
                    if ((dx >= xga->accel.mask_map_origin_x_off) && (dx <= ((xga->accel.px_map_width[0] & 0xfff) + xga->accel.mask_map_origin_x_off)) && (dy >= xga->accel.mask_map_origin_y_off) && (dy <= ((xga->accel.px_map_height[0] & 0xfff) + xga->accel.mask_map_origin_y_off))) {
                        src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.src_map_x & 0xfff, xga->accel.src_map_y & 0xfff, xga->accel.src_map, srcbase, xga->accel.px_map_width[xga->accel.src_map] + 1) : xga->accel.frgd_color;
                        dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, xga->accel.px_map_width[xga->accel.dst_map] + 1);

                        if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                            old_dest_dat = dest_dat;
                            ROP(1, dest_dat, src_dat);
                            dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                            if ((xga->accel.command & 0x30) == 0)
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                            else if (((xga->accel.command & 0x30) == 0x10) && x)
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                            else if (((xga->accel.command & 0x30) == 0x20) && y)
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        }
                    }
                } else {
                    src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.src_map_x & 0xfff, xga->accel.src_map_y & 0xfff, xga->accel.src_map, srcbase, xga->accel.px_map_width[xga->accel.src_map] + 1) : xga->accel.frgd_color;
                    dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, xga->accel.px_map_width[xga->accel.dst_map] + 1);

                    if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                        old_dest_dat = dest_dat;
                        ROP(1, dest_dat, src_dat);
                        dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                        if ((xga->accel.command & 0x30) == 0)
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        else if (((xga->accel.command & 0x30) == 0x10) && x)
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        else if (((xga->accel.command & 0x30) == 0x20) && y)
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                    }
                }

                if (!y) {
                    xga->accel.dst_map_x = dx;
                    xga->accel.dst_map_y = dy;
                    break;
                }

                if (xga->accel.octant & 0x01) {
                    if (xga->accel.octant & 0x02)
                        dy--;
                    else
                        dy++;

                    if (xga->accel.bres_err_term >= 0) {
                        xga->accel.bres_err_term += xga->accel.bres_k2;
                        if (xga->accel.octant & 0x04)
                            dx--;
                        else
                            dx++;
                    } else
                        xga->accel.bres_err_term += xga->accel.bres_k1;
                } else {
                    if (xga->accel.octant & 0x04)
                        dx--;
                    else
                        dx++;

                    if (xga->accel.bres_err_term >= 0) {
                        xga->accel.bres_err_term += xga->accel.bres_k2;
                        if (xga->accel.octant & 0x02)
                            dy--;
                        else
                            dy++;
                    } else
                        xga->accel.bres_err_term += xga->accel.bres_k1;
                }
                y--;
                x++;
            }
        }
    }
}

static void
xga_bitblt(svga_t *svga)
{
    xga_t   *xga = (xga_t *) svga->xga;
    uint32_t src_dat;
    uint32_t dest_dat;
    uint32_t old_dest_dat;
    uint32_t color_cmp  = xga->accel.color_cmp;
    uint32_t plane_mask = xga->accel.plane_mask;
    uint32_t patbase;
    uint32_t dstbase    = xga->accel.px_map_base[xga->accel.dst_map];
    uint32_t srcbase    = xga->accel.px_map_base[xga->accel.src_map];
    uint32_t patwidth   = xga->accel.px_map_width[xga->accel.pat_src];
    uint32_t dstwidth   = xga->accel.px_map_width[xga->accel.dst_map];
    uint32_t srcwidth   = xga->accel.px_map_width[xga->accel.src_map];
    uint32_t patheight  = xga->accel.px_map_height[xga->accel.pat_src];
    uint32_t srcheight  = xga->accel.px_map_height[xga->accel.src_map];
    uint32_t dstheight  = xga->accel.px_map_height[xga->accel.dst_map];
    uint32_t frgdcol = xga->accel.frgd_color;
    uint32_t bkgdcol = xga->accel.bkgd_color;
    int16_t  dx;
    int16_t  dy;
    int      mix  = 0;
    int      xdir = (xga->accel.octant & 0x04) ? -1 : 1;
    int      ydir = (xga->accel.octant & 0x02) ? -1 : 1;

    xga->accel.x = xga->accel.blt_width & 0xfff;
    xga->accel.y = xga->accel.blt_height & 0xfff;

    xga->accel.sx = xga->accel.src_map_x & 0xfff;
    xga->accel.sy = xga->accel.src_map_y & 0xfff;
    xga->accel.px = xga->accel.pat_map_x & 0xfff;
    xga->accel.py = xga->accel.pat_map_y & 0xfff;
    dx = xga->accel.dst_map_x;
    dy = xga->accel.dst_map_y;
    if (xga->accel.dst_map_x >= 0x1800)
        dx |= ~0x17ff;
    if (xga->accel.dst_map_y >= 0x1800)
        dy |= ~0x17ff;

    xga_log("D(%d,%d), SWH(%d,%d), BLT(%d,%d), dstwidth=%d, frgdcol=%04x, bkgdcol=%04x.\n", dx, dy, xga->accel.x, xga->accel.y, srcwidth, srcheight, dstwidth, frgdcol, bkgdcol);

    xga->accel.pattern = 0;
    xga->accel.filling = 0;

    xga_log("XGA bitblt access_mode=%x, octanty=%d, src command=%08x, "
            "pxsrcmap=%x, pxpatmap=%x, pxdstmap=%x, srcmap=%d, patmap=%d, dstmap=%d, "
            "usesrcvramfr=%d, usevrambk=%d, frgdcol=%04x, bkgdcol=%04x, bgmix=%02x, fgmix=%02x.\n",
            xga->access_mode & 0x0f, ydir, xga->accel.command,
            xga->accel.px_map_format[xga->accel.src_map] & 0x0f,
            xga->accel.px_map_format[xga->accel.pat_src] & 0x0f,
            xga->accel.px_map_format[xga->accel.dst_map] & 0x0f,
            xga->accel.src_map, xga->accel.pat_src,
            xga->accel.dst_map, ((xga->accel.command >> 28) & 3), ((xga->accel.command >> 30) & 3),
            frgdcol, bkgdcol, xga->accel.bkgd_mix & 0x1f, xga->accel.frgd_mix & 0x1f);

    if (xga->accel.pat_src == 8) {
        if (srcheight == 7)
            xga->accel.pattern = 1;
        else {
            if ((dstwidth == (xga->h_disp - 1)) && (srcwidth == 1)) {
                if ((xga->accel.dst_map == 1) && (xga->accel.src_map == 2)) {
                    if ((xga->accel.px_map_format[xga->accel.dst_map] >= 0x0a) && (xga->accel.px_map_format[xga->accel.src_map] >= 0x0a))
                        xga->accel.pattern = 1;
                }
            }
        }

        xga_log("PAT8: PatFormat=%x, SrcFormat=%x, DstFormat=%x.\n", xga->accel.px_map_format[xga->accel.pat_src] & 8, (xga->accel.px_map_format[xga->accel.src_map]), (xga->accel.px_map_format[xga->accel.dst_map]));
        xga_log("Pattern Map = 8: CMD = %08x: SRCBase = %08x, DSTBase = %08x, from/to vram dir = %d, "
                "cmd dir = %06x\n", xga->accel.command, srcbase, dstbase, xga->from_to_vram,
                xga->accel.dir_cmd);
        xga_log("CMD = %08x: Y = %d, X = %d, patsrc = %02x, srcmap = %d, dstmap = %d, py = %d, "
                "sy = %d, dy = %d, width0 = %d, width1 = %d, width2 = %d, width3 = %d\n",
                xga->accel.command, xga->accel.y, xga->accel.x, xga->accel.pat_src, xga->accel.src_map,
                xga->accel.dst_map, xga->accel.py, xga->accel.sy, dy,
                xga->accel.px_map_width[0], xga->accel.px_map_width[1],
                xga->accel.px_map_width[2], xga->accel.px_map_width[3]);
        xga_log("PAT8: Pattern Enabled?=%d, xdir=%d, ydir=%d.\n", xga->accel.pattern, xdir, ydir);

        while (xga->accel.y >= 0) {
            if (xga->accel.command & 0xc0) {
                if ((dx >= xga->accel.mask_map_origin_x_off) && (dx <= ((xga->accel.px_map_width[0] & 0xfff) + xga->accel.mask_map_origin_x_off)) && (dy >= xga->accel.mask_map_origin_y_off) && (dy <= ((xga->accel.px_map_height[0] & 0xfff) + xga->accel.mask_map_origin_y_off))) {
                    src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1) : frgdcol;
                    dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dstwidth + 1);
                    if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                        old_dest_dat = dest_dat;
                        ROP(1, dest_dat, src_dat);
                        dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                        xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, dstwidth + 1);
                    }
                }
            } else {
                if ((dx >= 0) && (dx <= dstwidth) && (dy >= 0) && (dy <= dstheight)) {
                    src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1) : frgdcol;
                    dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dstwidth + 1);
                    if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                        old_dest_dat = dest_dat;
                        ROP(1, dest_dat, src_dat);
                        dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                        xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, dstwidth + 1);
                    }
                }
            }

            if (xga->accel.pattern)
                xga->accel.sx = ((xga->accel.sx + xdir) & srcwidth) | (xga->accel.sx & ~srcwidth);
            else
                xga->accel.sx += xdir;

            dx += xdir;
            xga->accel.x--;
            if (xga->accel.x < 0) {
                xga->accel.x = xga->accel.blt_width & 0xfff;

                dx = xga->accel.dst_map_x;
                if (xga->accel.dst_map_x >= 0x1800)
                    dx |= ~0x17ff;
                xga->accel.sx = xga->accel.src_map_x & 0xfff;

                dy += ydir;

                if (xga->accel.pattern)
                    xga->accel.sy = ((xga->accel.sy + ydir) & srcheight) | (xga->accel.sy & ~srcheight);
                else
                    xga->accel.sy += ydir;

                xga->accel.y--;

                if (xga->accel.y < 0) {
                    xga->accel.dst_map_x = dx;
                    xga->accel.dst_map_y = dy;
                    return;
                }
            }
        }
    } else if (xga->accel.pat_src >= 1) {
        patbase    = xga->accel.px_map_base[xga->accel.pat_src];

        if (patheight == 7) {
            if (xga->accel.src_map != 1)
                xga->accel.pattern = 1;
            else if ((xga->accel.src_map == 1) && (patwidth == 7))
                xga->accel.pattern = 1;
        } else {
            if (dstwidth == (xga->h_disp - 1)) {
                if (srcwidth == (xga->h_disp - 1)) {
                    if ((xga->accel.src_map == 1) && (xga->accel.dst_map == 1) && (xga->accel.pat_src == 2)) {
                        if ((xga->accel.px_map_format[xga->accel.dst_map] >= 0x0a) && (xga->accel.px <= 7) && (xga->accel.py <= 3))
                            xga->accel.pattern = 1;
                    }
                } else {
                    if (!xga->accel.src_map && (xga->accel.dst_map == 1) && (xga->accel.pat_src == 2)) {
                        if ((xga->accel.px_map_format[xga->accel.dst_map] >= 0x0a) && (xga->accel.px <= 7) && (xga->accel.py <= 3)) {
                            if ((patwidth >= 7) && ((xga->accel.command & 0xc0) == 0x40))
                                xga->accel.pattern = 0;
                            else
                                xga->accel.pattern = 1;
                        }
                    }
                }
            }
        }

        xga_log("PAT%d: PatFormat=%x, SrcFormat=%x, DstFormat=%x.\n", xga->accel.pat_src, xga->accel.px_map_format[xga->accel.pat_src] & 8, (xga->accel.px_map_format[xga->accel.src_map]), (xga->accel.px_map_format[xga->accel.dst_map]));
        xga_log("XGA bitblt linear endian reverse=%d, octanty=%d, src command = %08x, pxsrcmap=%x, "
                "pxdstmap=%x, srcmap=%d, patmap=%d, dstmap=%d, dstwidth=%d, dstheight=%d, srcwidth=%d, "
                "srcheight=%d, dstbase=%08x, srcbase=%08x.\n", xga->linear_endian_reverse, ydir,
                xga->accel.command, xga->accel.px_map_format[xga->accel.src_map] & 0x0f,
                xga->accel.px_map_format[xga->accel.dst_map] & 0x0f, xga->accel.src_map,
                xga->accel.pat_src, xga->accel.dst_map, dstwidth, dstheight, srcwidth, srcheight,
                dstbase, srcbase);
        xga_log("Pattern Map = %d: CMD = %08x: PATBase = %08x, SRCBase = %08x, DSTBase = %08x\n",
                xga->accel.pat_src, xga->accel.command, patbase, srcbase, dstbase);
        xga_log("CMD = %08x: Y = %d, X = %d, patsrc = %02x, srcmap = %d, dstmap = %d, py = %d, "
                "sy = %d, dy = %d, width0 = %d, width1 = %d, width2 = %d, width3 = %d, bkgdcol = %02x\n",
                xga->accel.command, xga->accel.y, xga->accel.x, xga->accel.pat_src,
                xga->accel.src_map, xga->accel.dst_map, xga->accel.py, xga->accel.sy, xga->accel.dy,
                xga->accel.px_map_width[0], xga->accel.px_map_width[1],
                xga->accel.px_map_width[2], xga->accel.px_map_width[3], bkgdcol);
        xga_log("Pattern Enabled?=%d, patwidth=%d, patheight=%d, P(%d,%d).\n", xga->accel.pattern, patwidth, patheight, xga->accel.px, xga->accel.py);

        if (((xga->accel.command >> 24) & 0x0f) == 0x0a) {
            if ((xga->accel.bkgd_mix & 0x1f) == 0x05) {
                while (xga->accel.y >= 0) {
                    mix = xga_accel_read_area_map_pixel(svga, xga->accel.px, xga->accel.py, patbase, patwidth + 1);
                    if (mix)
                        xga->accel.filling ^= 1;

                    xga_log("Area Fill Command: dx=%d, dy=%d, mix=%x, filling=%x.\n", dx, dy, mix, xga->accel.filling);

                    if (xga->accel.command & 0xc0) {
                        if ((dx >= xga->accel.mask_map_origin_x_off) && (dx <= ((xga->accel.px_map_width[0] & 0xfff) + xga->accel.mask_map_origin_x_off)) && (dy >= xga->accel.mask_map_origin_y_off) && (dy <= ((xga->accel.px_map_height[0] & 0xfff) + xga->accel.mask_map_origin_y_off)) && xga->accel.filling) {
                            src_dat = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1) : frgdcol;
                            dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dstwidth + 1);
                            if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                                old_dest_dat = dest_dat;
                                ROP(1, dest_dat, src_dat);
                                dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                                xga_log("1SRCDat=%02x, DSTDat=%02x, Old=%02x, MIX=%d.\n", src_dat, dest_dat, old_dest_dat, area_state);
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, dstwidth + 1);
                            }
                        }
                    } else {
                        if ((dx >= 0) && (dx <= dstwidth) && (dy >= 0) && (dy <= dstheight) && xga->accel.filling) {
                            src_dat = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1) : frgdcol;
                            dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dstwidth + 1);
                            if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                                old_dest_dat = dest_dat;
                                ROP(1, dest_dat, src_dat);
                                dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                                xga_log("2Fill: NumXY(%d,%d): DXY(%d,%d): SRCDat=%02x, DSTDat=%02x, Old=%02x, frgdcol=%02x, bkgdcol=%02x, MIX=%d, frgdmix=%02x, bkgdmix=%02x, dstmapfmt=%02x, srcmapfmt=%02x, srcmapnum=%d.\n", x, y, dx, dy, src_dat, dest_dat, old_dest_dat, frgdcol, bkgdcol, area_state, xga->accel.frgd_mix & 0x1f, xga->accel.bkgd_mix & 0x1f, xga->accel.px_map_format[xga->accel.dst_map] & 0x0f, xga->accel.px_map_format[xga->accel.src_map] & 0x0f, xga->accel.src_map);
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, dstwidth + 1);
                            }
                        }
                    }

                    xga->accel.sx = ((xga->accel.sx + xdir) & srcwidth) | (xga->accel.sx & ~srcwidth);
                    xga->accel.px++;

                    dx++;
                    xga->accel.x--;
                    if (xga->accel.x < 0) {
                        xga->accel.y--;
                        xga->accel.x = xga->accel.blt_width & 0xfff;

                        dx = xga->accel.dst_map_x;
                        if (xga->accel.dst_map_x >= 0x1800)
                            dx |= ~0x17ff;

                        xga->accel.sx = xga->accel.src_map_x & 0xfff;
                        xga->accel.px = xga->accel.pat_map_x & 0xfff;

                        xga->accel.sy = ((xga->accel.sy + ydir) & srcheight) | (xga->accel.sy & ~srcheight);
                        xga->accel.py++;

                        dy++;
                        xga->accel.filling = 0;

                        if (xga->accel.y < 0) {
                            xga->accel.dst_map_x = dx;
                            xga->accel.dst_map_y = dy;
                            return;
                        }
                    }
                }
            }
        } else {
            patbase    = xga->accel.px_map_base[xga->accel.pat_src];

            while (xga->accel.y >= 0) {
                mix = xga_accel_read_pattern_map_pixel(svga, xga->accel.px, xga->accel.py, patbase, patwidth + 1);

                if (xga->accel.command & 0xc0) {
                    if ((dx >= xga->accel.mask_map_origin_x_off) && (dx <= ((xga->accel.px_map_width[0] & 0xfff) + xga->accel.mask_map_origin_x_off)) && (dy >= xga->accel.mask_map_origin_y_off) && (dy <= ((xga->accel.px_map_height[0] & 0xfff) + xga->accel.mask_map_origin_y_off))) {
                        if (mix)
                            src_dat = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1) : frgdcol;
                        else
                            src_dat = (((xga->accel.command >> 30) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1) : bkgdcol;

                        dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dstwidth + 1);
                        if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                            old_dest_dat = dest_dat;
                            ROP(mix, dest_dat, src_dat);
                            dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, dstwidth + 1);
                        }
                    }
                } else {
                    if ((dx >= 0) && (dx <= dstwidth) && (dy >= 0) && (dy <= dstheight)) {
                        if (mix)
                            src_dat = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1) : frgdcol;
                        else
                            src_dat = (((xga->accel.command >> 30) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1) : bkgdcol;

                        dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dstwidth + 1);
                        if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                            old_dest_dat = dest_dat;
                            ROP(mix, dest_dat, src_dat);
                            dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, dstwidth + 1);
                        }
                    }
                }

                xga->accel.sx += xdir;
                if (xga->accel.pattern)
                    xga->accel.px = ((xga->accel.px + xdir) & patwidth) | (xga->accel.px & ~patwidth);
                else
                    xga->accel.px += xdir;

                dx += xdir;
                xga->accel.x--;
                if (xga->accel.x < 0) {
                    xga->accel.y--;
                    xga->accel.x = xga->accel.blt_width & 0xfff;

                    dx = xga->accel.dst_map_x;
                    if (xga->accel.dst_map_x >= 0x1800)
                        dx |= ~0x17ff;

                    xga->accel.sx = xga->accel.src_map_x & 0xfff;
                    xga->accel.px = xga->accel.pat_map_x & 0xfff;

                    xga->accel.sy += ydir;
                    if (xga->accel.pattern)
                        xga->accel.py = ((xga->accel.py + ydir) & patheight) | (xga->accel.py & ~patheight);
                    else
                        xga->accel.py += ydir;

                    dy += ydir;

                    if (xga->accel.y < 0) {
                        xga->accel.dst_map_x = dx;
                        xga->accel.dst_map_y = dy;
                        return;
                    }
                }
            }
        }
    }
}

static void
xga_mem_write(uint32_t addr, uint32_t val, xga_t *xga, svga_t *svga, int len)
{
    uint32_t min_addr;
    uint32_t max_addr;
    int mmio_addr_enable = 0;

    if (xga_standalone_enabled) {
        addr &= 0x1fff;
        min_addr = (0x1c00 + (xga->instance << 7));
        max_addr = (0x1c00 + (xga->instance << 7)) + 0x7f;
    } else {
        addr &= 0x7fff;
        min_addr = (0x7c00 + (xga->instance << 7));
        max_addr = (0x7c00 + (xga->instance << 7)) + 0x7f;
    }

    if ((addr >= min_addr) && (addr <= max_addr))
        mmio_addr_enable = 1;

    if (mmio_addr_enable) {
        switch (addr & 0x7f) {
            case 0x11:
                xga->accel.control = val;
                xga_log("Control=%02x.\n", val);
                break;

            case 0x12:
                xga->accel.px_map_idx = val & 3;
                break;

            case 0x14:
                if (len == 4)
                    xga->accel.px_map_base[xga->accel.px_map_idx] = val;
                else if (len == 2)
                    xga->accel.px_map_base[xga->accel.px_map_idx] = (xga->accel.px_map_base[xga->accel.px_map_idx] & 0xffff0000) | val;
                else
                    xga->accel.px_map_base[xga->accel.px_map_idx] = (xga->accel.px_map_base[xga->accel.px_map_idx] & 0xffffff00) | val;
                break;
            case 0x15:
                if (len == 1)
                    xga->accel.px_map_base[xga->accel.px_map_idx] = (xga->accel.px_map_base[xga->accel.px_map_idx] & 0xffff00ff) | (val << 8);
                break;
            case 0x16:
                if (len == 2)
                    xga->accel.px_map_base[xga->accel.px_map_idx] = (xga->accel.px_map_base[xga->accel.px_map_idx] & 0x0000ffff) | (val << 16);
                else
                    xga->accel.px_map_base[xga->accel.px_map_idx] = (xga->accel.px_map_base[xga->accel.px_map_idx] & 0xff00ffff) | (val << 16);
                break;
            case 0x17:
                if (len == 1)
                    xga->accel.px_map_base[xga->accel.px_map_idx] = (xga->accel.px_map_base[xga->accel.px_map_idx] & 0x00ffffff) | (val << 24);
                break;

            case 0x18:
                if (len == 4) {
                    xga->accel.px_map_width[xga->accel.px_map_idx]  = val & 0xffff;
                    xga->accel.px_map_height[xga->accel.px_map_idx] = (val >> 16) & 0xffff;
                } else if (len == 2) {
                    xga->accel.px_map_width[xga->accel.px_map_idx] = val & 0xffff;
                } else
                    xga->accel.px_map_width[xga->accel.px_map_idx] = (xga->accel.px_map_width[xga->accel.px_map_idx] & 0xff00) | val;
                break;
            case 0x19:
                if (len == 1)
                    xga->accel.px_map_width[xga->accel.px_map_idx] = (xga->accel.px_map_width[xga->accel.px_map_idx] & 0xff) | (val << 8);
                break;

            case 0x1a:
                if (len == 2)
                    xga->accel.px_map_height[xga->accel.px_map_idx] = val & 0xffff;
                else
                    xga->accel.px_map_height[xga->accel.px_map_idx] = (xga->accel.px_map_height[xga->accel.px_map_idx] & 0xff00) | val;
                break;
            case 0x1b:
                if (len == 1)
                    xga->accel.px_map_height[xga->accel.px_map_idx] = (xga->accel.px_map_height[xga->accel.px_map_idx] & 0xff) | (val << 8);
                break;

            case 0x1c:
                xga->accel.px_map_format[xga->accel.px_map_idx] = val;
                if (val & 0x08)
                    xga_log("Big Endian Pixel Format=%d, AccessMode=%x.\n", xga->accel.px_map_idx, xga->access_mode & 0x08);
                else
                    xga_log("Little Endian Pixel Format=%d, AccessMode=%x.\n", xga->accel.px_map_idx, xga->access_mode & 0x08);
                break;

            case 0x20:
                if (len >= 2) {
                    xga->accel.bres_err_term = val & 0x3fff;
                    if (val & 0x2000)
                        xga->accel.bres_err_term |= ~0x1fff;
                } else
                    xga->accel.bres_err_term = (xga->accel.bres_err_term & 0x3f00) | val;
                break;
            case 0x21:
                if (len == 1) {
                    xga->accel.bres_err_term = (xga->accel.bres_err_term & 0xff) | ((val & 0x3f) << 8);
                    if (val & 0x20)
                        xga->accel.bres_err_term |= ~0x1fff;
                }
                break;

            case 0x24:
                if (len >= 2) {
                    xga->accel.bres_k1 = val & 0x3fff;
                    if (val & 0x2000)
                        xga->accel.bres_k1 |= ~0x1fff;
                } else
                    xga->accel.bres_k1 = (xga->accel.bres_k1 & 0x3f00) | val;
                break;
            case 0x25:
                if (len == 1) {
                    xga->accel.bres_k1 = (xga->accel.bres_k1 & 0xff) | ((val & 0x3f) << 8);
                    if (val & 0x20)
                        xga->accel.bres_k1 |= ~0x1fff;
                }
                break;

            case 0x28:
                if (len >= 2) {
                    xga->accel.bres_k2 = val & 0x3fff;
                    if (val & 0x2000)
                        xga->accel.bres_k2 |= ~0x1fff;
                } else
                    xga->accel.bres_k2 = (xga->accel.bres_k2 & 0x3f00) | val;
                break;
            case 0x29:
                if (len == 1) {
                    xga->accel.bres_k2 = (xga->accel.bres_k2 & 0xff) | ((val & 0x3f) << 8);
                    if (val & 0x20)
                        xga->accel.bres_k2 |= ~0x1fff;
                }
                break;

            case 0x2c:
                if (len == 4) {
                    xga->accel.short_stroke         = val;
                    xga->accel.short_stroke_vector1 = xga->accel.short_stroke & 0xff;
                    xga->accel.short_stroke_vector2 = (xga->accel.short_stroke >> 8) & 0xff;
                    xga->accel.short_stroke_vector3 = (xga->accel.short_stroke >> 16) & 0xff;
                    xga->accel.short_stroke_vector4 = (xga->accel.short_stroke >> 24) & 0xff;

                    xga_log("1Vector = %02x, 2Vector = %02x, 3Vector = %02x, 4Vector = %02x\n",
                            xga->accel.short_stroke_vector1, xga->accel.short_stroke_vector2,
                            xga->accel.short_stroke_vector3, xga->accel.short_stroke_vector4);

                    xga_short_stroke(svga, xga->accel.short_stroke_vector1);
                    xga_short_stroke(svga, xga->accel.short_stroke_vector2);
                    xga_short_stroke(svga, xga->accel.short_stroke_vector3);
                    xga_short_stroke(svga, xga->accel.short_stroke_vector4);
                } else if (len == 2)
                    xga->accel.short_stroke = (xga->accel.short_stroke & 0xffff0000) | val;
                else
                    xga->accel.short_stroke = (xga->accel.short_stroke & 0xffffff00) | val;
                break;
            case 0x2d:
                if (len == 1)
                    xga->accel.short_stroke = (xga->accel.short_stroke & 0xffff00ff) | (val << 8);
                break;
            case 0x2e:
                if (len == 2) {
                    xga->accel.short_stroke = (xga->accel.short_stroke & 0x0000ffff) | (val << 16);
                } else
                    xga->accel.short_stroke = (xga->accel.short_stroke & 0xff00ffff) | (val << 16);
                break;
            case 0x2f:
                if (len == 1) {
                    xga->accel.short_stroke = (xga->accel.short_stroke & 0x00ffffff) | (val << 24);
                }
                break;

            case 0x48:
                xga->accel.frgd_mix = val & 0xff;
                if (len == 4) {
                    xga->accel.bkgd_mix = (val >> 8) & 0xff;
                    xga->accel.cc_cond  = (val >> 16) & 0x07;
                } else if (len == 2) {
                    xga->accel.bkgd_mix = (val >> 8) & 0xff;
                }
                break;

            case 0x49:
                xga->accel.bkgd_mix = val & 0xff;
                break;

            case 0x4a:
                xga->accel.cc_cond = val & 0x07;
                break;

            case 0x4c:
                if (len == 4)
                    xga->accel.color_cmp = val;
                else if (len == 2)
                    xga->accel.color_cmp = (xga->accel.color_cmp & 0xffff0000) | val;
                else
                    xga->accel.color_cmp = (xga->accel.color_cmp & 0xffffff00) | val;
                break;
            case 0x4d:
                if (len == 1)
                    xga->accel.color_cmp = (xga->accel.color_cmp & 0xffff00ff) | (val << 8);
                break;
            case 0x4e:
                if (len == 2)
                    xga->accel.color_cmp = (xga->accel.color_cmp & 0x0000ffff) | (val << 16);
                else
                    xga->accel.color_cmp = (xga->accel.color_cmp & 0xff00ffff) | (val << 16);
                break;
            case 0x4f:
                if (len == 1)
                    xga->accel.color_cmp = (xga->accel.color_cmp & 0x00ffffff) | (val << 24);
                break;

            case 0x50:
                if (len == 4)
                    xga->accel.plane_mask = val;
                else if (len == 2)
                    xga->accel.plane_mask = (xga->accel.plane_mask & 0xffff0000) | val;
                else
                    xga->accel.plane_mask = (xga->accel.plane_mask & 0xffffff00) | val;
                break;
            case 0x51:
                if (len == 1)
                    xga->accel.plane_mask = (xga->accel.plane_mask & 0xffff00ff) | (val << 8);
                break;
            case 0x52:
                if (len == 2)
                    xga->accel.plane_mask = (xga->accel.plane_mask & 0x0000ffff) | (val << 16);
                else
                    xga->accel.plane_mask = (xga->accel.plane_mask & 0xff00ffff) | (val << 16);
                break;
            case 0x53:
                if (len == 1)
                    xga->accel.plane_mask = (xga->accel.plane_mask & 0x00ffffff) | (val << 24);
                break;

            case 0x54:
                if (len == 4)
                    xga->accel.carry_chain = val;
                else if (len == 2)
                    xga->accel.carry_chain = (xga->accel.carry_chain & 0xffff0000) | val;
                else
                    xga->accel.carry_chain = (xga->accel.carry_chain & 0xffffff00) | val;
                break;
            case 0x55:
                if (len == 1)
                    xga->accel.carry_chain = (xga->accel.carry_chain & 0xffff00ff) | (val << 8);
                break;
            case 0x56:
                if (len == 2)
                    xga->accel.carry_chain = (xga->accel.carry_chain & 0x0000ffff) | (val << 16);
                else
                    xga->accel.carry_chain = (xga->accel.carry_chain & 0xff00ffff) | (val << 16);
                break;
            case 0x57:
                if (len == 1)
                    xga->accel.carry_chain = (xga->accel.carry_chain & 0x00ffffff) | (val << 24);
                break;


            case 0x58:
                if (len == 4)
                    xga->accel.frgd_color = val;
                else if (len == 2)
                    xga->accel.frgd_color = (xga->accel.frgd_color & 0xffff0000) | val;
                else
                    xga->accel.frgd_color = (xga->accel.frgd_color & 0xffffff00) | val;
                break;
            case 0x59:
                if (len == 1)
                    xga->accel.frgd_color = (xga->accel.frgd_color & 0xffff00ff) | (val << 8);
                break;
            case 0x5a:
                if (len == 2)
                    xga->accel.frgd_color = (xga->accel.frgd_color & 0x0000ffff) | (val << 16);
                else
                    xga->accel.frgd_color = (xga->accel.frgd_color & 0xff00ffff) | (val << 16);
                break;
            case 0x5b:
                if (len == 1)
                    xga->accel.frgd_color = (xga->accel.frgd_color & 0x00ffffff) | (val << 24);
                break;

            case 0x5c:
                if (len == 4)
                    xga->accel.bkgd_color = val;
                else if (len == 2)
                    xga->accel.bkgd_color = (xga->accel.bkgd_color & 0xffff0000) | val;
                else
                    xga->accel.bkgd_color = (xga->accel.bkgd_color & 0xffffff00) | val;
                break;
            case 0x5d:
                if (len == 1)
                    xga->accel.bkgd_color = (xga->accel.bkgd_color & 0xffff00ff) | (val << 8);
                break;
            case 0x5e:
                if (len == 2)
                    xga->accel.bkgd_color = (xga->accel.bkgd_color & 0x0000ffff) | (val << 16);
                else
                    xga->accel.bkgd_color = (xga->accel.bkgd_color & 0xff00ffff) | (val << 16);
                break;
            case 0x5f:
                if (len == 1)
                    xga->accel.bkgd_color = (xga->accel.bkgd_color & 0x00ffffff) | (val << 24);
                break;

            case 0x60:
                if (len == 4) {
                    xga->accel.blt_width  = val & 0xffff;
                    xga->accel.blt_height = (val >> 16) & 0xffff;
                } else if (len == 2) {
                    xga->accel.blt_width = val;
                } else
                    xga->accel.blt_width = (xga->accel.blt_width & 0xff00) | val;
                break;
            case 0x61:
                if (len == 1)
                    xga->accel.blt_width = (xga->accel.blt_width & 0xff) | (val << 8);
                break;

            case 0x62:
                if (len == 2)
                    xga->accel.blt_height = val;
                else
                    xga->accel.blt_height = (xga->accel.blt_height & 0xff00) | val;
                break;
            case 0x63:
                if (len == 1)
                    xga->accel.blt_height = (xga->accel.blt_height & 0xff) | (val << 8);
                break;

            case 0x6c:
                if (len == 4) {
                    xga->accel.mask_map_origin_x_off = val & 0xffff;
                    xga->accel.mask_map_origin_y_off = (val >> 16) & 0xffff;
                } else if (len == 2) {
                    xga->accel.mask_map_origin_x_off = val;
                } else
                    xga->accel.mask_map_origin_x_off = (xga->accel.mask_map_origin_x_off & 0xff00) | val;
                break;
            case 0x6d:
                if (len == 1)
                    xga->accel.mask_map_origin_x_off = (xga->accel.mask_map_origin_x_off & 0xff) | (val << 8);
                break;

            case 0x6e:
                if (len == 2)
                    xga->accel.mask_map_origin_y_off = val;
                else
                    xga->accel.mask_map_origin_y_off = (xga->accel.mask_map_origin_y_off & 0xff00) | val;
                break;
            case 0x6f:
                if (len == 1)
                    xga->accel.mask_map_origin_y_off = (xga->accel.mask_map_origin_y_off & 0xff) | (val << 8);
                break;

            case 0x70:
                if (len == 4) {
                    xga->accel.src_map_x = val & 0xffff;
                    xga->accel.src_map_y = (val >> 16) & 0xffff;
                } else if (len == 2)
                    xga->accel.src_map_x = val;
                else
                    xga->accel.src_map_x = (xga->accel.src_map_x & 0xff00) | val;
                break;
            case 0x71:
                if (len == 1)
                    xga->accel.src_map_x = (xga->accel.src_map_x & 0xff) | (val << 8);
                break;

            case 0x72:
                if (len == 2)
                    xga->accel.src_map_y = val;
                else
                    xga->accel.src_map_y = (xga->accel.src_map_y & 0xff00) | val;
                break;
            case 0x73:
                if (len == 1)
                    xga->accel.src_map_y = (xga->accel.src_map_y & 0xff) | (val << 8);
                break;

            case 0x74:
                if (len == 4) {
                    xga->accel.pat_map_x = val & 0xffff;
                    xga->accel.pat_map_y = (val >> 16) & 0xffff;
                } else if (len == 2)
                    xga->accel.pat_map_x = val;
                else
                    xga->accel.pat_map_x = (xga->accel.pat_map_x & 0xff00) | val;
                break;
            case 0x75:
                if (len == 1)
                    xga->accel.pat_map_x = (xga->accel.pat_map_x & 0xff) | (val << 8);
                break;

            case 0x76:
                if (len == 2)
                    xga->accel.pat_map_y = val;
                else
                    xga->accel.pat_map_y = (xga->accel.pat_map_y & 0xff00) | val;
                break;
            case 0x77:
                if (len == 1)
                    xga->accel.pat_map_y = (xga->accel.pat_map_y & 0xff) | (val << 8);
                break;

            case 0x78:
                if (len == 4) {
                    xga->accel.dst_map_x = val & 0xffff;
                    xga->accel.dst_map_y = (val >> 16) & 0xffff;
                } else if (len == 2)
                    xga->accel.dst_map_x = val;
                else
                    xga->accel.dst_map_x = (xga->accel.dst_map_x & 0xff00) | val;
                break;
            case 0x79:
                if (len == 1)
                    xga->accel.dst_map_x = (xga->accel.dst_map_x & 0xff) | (val << 8);
                break;

            case 0x7a:
                if (len == 2)
                    xga->accel.dst_map_y = val;
                else
                    xga->accel.dst_map_y = (xga->accel.dst_map_y & 0xff00) | val;
                break;
            case 0x7b:
                if (len == 1)
                    xga->accel.dst_map_y = (xga->accel.dst_map_y & 0xff) | (val << 8);
                break;

            case 0x7c:
                if (len == 4) {
                    xga->accel.command = val;
exec_command:
                    xga->accel.octant    = xga->accel.command & 0x07;
                    xga->accel.draw_mode = xga->accel.command & 0x30;
                    xga->accel.pat_src   = ((xga->accel.command >> 12) & 0x0f);
                    xga->accel.dst_map   = ((xga->accel.command >> 16) & 0x0f);
                    xga->accel.src_map   = ((xga->accel.command >> 20) & 0x0f);
                    xga_log("PATMAP=%x, DSTMAP=%x, SRCMAP=%x.\n", xga->accel.px_map_format[xga->accel.pat_src], xga->accel.px_map_format[xga->accel.dst_map], xga->accel.px_map_format[xga->accel.src_map]);

                    if (xga->accel.pat_src)
                        xga_log("[%04X:%08X]: Accel Command = %02x, full = %08x, patwidth = %d, "
                                "dstwidth = %d, srcwidth = %d, patheight = %d, dstheight = %d, "
                                "srcheight = %d, px = %d, py = %d, dx = %d, dy = %d, sx = %d, "
                                "sy = %d, patsrc = %d, dstmap = %d, srcmap = %d, dstbase = %08x, "
                                "srcbase = %08x, patbase = %08x, dstformat = %x, srcformat = %x, "
                                "planemask = %08x\n\n",
                                CS, cpu_state.pc, ((xga->accel.command >> 24) & 0x0f),
                                xga->accel.command, xga->accel.px_map_width[xga->accel.pat_src],
                                xga->accel.px_map_width[xga->accel.dst_map],
                                xga->accel.px_map_width[xga->accel.src_map],
                                xga->accel.px_map_height[xga->accel.pat_src],
                                xga->accel.px_map_height[xga->accel.dst_map],
                                xga->accel.px_map_height[xga->accel.src_map],
                                xga->accel.pat_map_x, xga->accel.pat_map_y,
                                xga->accel.dst_map_x, xga->accel.dst_map_y,
                                xga->accel.src_map_x, xga->accel.src_map_y,
                                xga->accel.pat_src, xga->accel.dst_map,
                                xga->accel.src_map, xga->accel.px_map_base[xga->accel.dst_map],
                                xga->accel.px_map_base[xga->accel.src_map],
                                xga->accel.px_map_base[xga->accel.pat_src],
                                xga->accel.px_map_format[xga->accel.dst_map] & 0x0f,
                                xga->accel.px_map_format[xga->accel.src_map] & 0x0f,
                                xga->accel.plane_mask);

                    switch ((xga->accel.command >> 24) & 0x0f) {
                        case 2: /*Short Stroke Vectors Read */
                            xga_log("Short Stroke Vectors Read.\n");
                            break;
                        case 3: /*Bresenham Line Draw Read*/
                            xga_log("Line Draw Read\n");
                            break;
                        case 4: /*Short Stroke Vectors Write*/
                            xga_log("Short Stroke Vectors Write.\n");
                            break;
                        case 5: /*Bresenham Line Draw Write*/
                            xga_log("Line Draw Write.\n");
                            xga_line_draw_write(svga);
                            break;
                        case 8: /*BitBLT*/
                            xga_log("BitBLT.\n");
                            xga_bitblt(svga);
                            break;
                        case 9: /*Inverting BitBLT*/
                            xga_log("Inverting BitBLT\n");
                            break;
                        case 0x0a: /*Area Fill*/
                            xga_log("Area Fill BitBLT.\n");
                            xga_bitblt(svga);
                            break;

                        default:
                            break;
                    }
                } else if (len == 2) {
                    xga->accel.command = (xga->accel.command & 0xffff0000) | val;
                } else
                    xga->accel.command = (xga->accel.command & 0xffffff00) | val;
                break;
            case 0x7d:
                if (len == 1)
                    xga->accel.command = (xga->accel.command & 0xffff00ff) | (val << 8);
                break;
            case 0x7e:
                if (len == 2) {
                    xga->accel.command = (xga->accel.command & 0x0000ffff) | (val << 16);
                    goto exec_command;
                } else
                    xga->accel.command = (xga->accel.command & 0xff00ffff) | (val << 16);
                break;
            case 0x7f:
                if (len == 1) {
                    xga->accel.command = (xga->accel.command & 0x00ffffff) | (val << 24);
                    goto exec_command;
                }
                break;

            default:
                break;
        }
    }
}

static void
xga_memio_writeb(uint32_t addr, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;

    xga_mem_write(addr, val, xga, svga, 1);

    xga_log("[%04X:%08X]: Write MEMIOB = %04x, val = %02x\n", CS, cpu_state.pc, addr, val);
}

static void
xga_memio_writew(uint32_t addr, uint16_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;

    xga_mem_write(addr, val, xga, svga, 2);

    xga_log("[%04X:%08X]: Write MEMIOW = %04x, val = %04x\n", CS, cpu_state.pc, addr, val);
}

static void
xga_memio_writel(uint32_t addr, uint32_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;

    xga_mem_write(addr, val, xga, svga, 4);

    xga_log("[%04X:%08X]: Write MEMIOL = %04x, val = %08x\n", CS, cpu_state.pc, addr, val);
}

static uint8_t
xga_mem_read(uint32_t addr, xga_t *xga, UNUSED(svga_t *svga))
{
    uint32_t min_addr;
    uint32_t max_addr;
    uint8_t temp = 0;
    int mmio_addr_enable = 0;

    if (xga_standalone_enabled) {
        addr &= 0x1fff;
        min_addr = (0x1c00 + (xga->instance << 7));
        max_addr = (0x1c00 + (xga->instance << 7)) + 0x7f;
        if (addr < 0x1c00)
            temp = xga->bios_rom.rom[addr];
        else if ((addr >= 0x1c00) && (addr <= 0x1c7f) && xga->instance)
            temp = 0xff;
        else if ((addr >= min_addr) && (addr <= max_addr))
            mmio_addr_enable = 1;
    } else {
        addr &= 0x7fff;
        min_addr = (0x7c00 + (xga->instance << 7));
        max_addr = (0x7c00 + (xga->instance << 7)) + 0x7f;
        if (addr < 0x7c00)
            temp = xga->bios_rom.rom[addr];
        else if ((addr >= 0x7c00) && (addr <= 0x7c7f) && xga->instance)
            temp = 0xff;
        else if ((addr >= min_addr) && (addr <= max_addr))
            mmio_addr_enable = 1;
    }

    if (mmio_addr_enable) {
        switch (addr & 0x7f) {
            case 0x11:
                temp = xga->accel.control;
                if (xga->accel.control & 0x08)
                    temp |= 0x10;
                else
                    temp &= ~0x10;
                break;

            case 0x20:
                temp = xga->accel.bres_err_term & 0xff;
                break;
            case 0x21:
                temp = xga->accel.bres_err_term >> 8;
                break;
            case 0x22:
                temp = xga->accel.bres_err_term >> 16;
                break;
            case 0x23:
                temp = xga->accel.bres_err_term >> 24;
                break;

            case 0x70:
                temp = xga->accel.src_map_x & 0xff;
                break;
            case 0x71:
                temp = xga->accel.src_map_x >> 8;
                break;

            case 0x72:
                temp = xga->accel.src_map_y & 0xff;
                break;
            case 0x73:
                temp = xga->accel.src_map_y >> 8;
                break;

            case 0x74:
                temp = xga->accel.pat_map_x & 0xff;
                break;
            case 0x75:
                temp = xga->accel.pat_map_x >> 8;
                break;

            case 0x76:
                temp = xga->accel.pat_map_y & 0xff;
                break;
            case 0x77:
                temp = xga->accel.pat_map_y >> 8;
                break;

            case 0x78:
                temp = xga->accel.dst_map_x & 0xff;
                break;
            case 0x79:
                temp = xga->accel.dst_map_x >> 8;
                break;

            case 0x7a:
                temp = xga->accel.dst_map_y & 0xff;
                break;
            case 0x7b:
                temp = xga->accel.dst_map_y >> 8;
                break;

            default:
                break;
        }
        xga_log("MMIO Addr=%02x, ret=%02x.\n", addr & 0x7f, temp);
    }
    return temp;
}

static uint8_t
xga_memio_readb(uint32_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;
    uint8_t temp;

    temp = xga_mem_read(addr, xga, svga);

    xga_log("[%04X:%08X]: Read MEMIOB = %04x, temp = %02x\n", CS, cpu_state.pc, addr, temp);

    return temp;
}

static uint16_t
xga_memio_readw(uint32_t addr, void *priv)
{
    svga_t  *svga = (svga_t *) priv;
    xga_t   *xga  = (xga_t *) svga->xga;
    uint16_t temp;

    temp = xga_mem_read(addr, xga, svga);
    temp |= (xga_mem_read(addr + 1, xga, svga) << 8);

    xga_log("[%04X:%08X]: Read MEMIOW = %04x, temp = %04x\n", CS, cpu_state.pc, addr, temp);

    return temp;
}

static uint32_t
xga_memio_readl(uint32_t addr, void *priv)
{
    svga_t  *svga = (svga_t *) priv;
    xga_t   *xga  = (xga_t *) svga->xga;
    uint32_t temp;

    temp = xga_mem_read(addr, xga, svga);
    temp |= (xga_mem_read(addr + 1, xga, svga) << 8);
    temp |= (xga_mem_read(addr + 2, xga, svga) << 16);
    temp |= (xga_mem_read(addr + 3, xga, svga) << 24);

    xga_log("[%04X:%08X]: Read MEMIOL = %04x, temp = %08x\n", CS, cpu_state.pc, addr, temp);

    return temp;
}

static void
xga_hwcursor_draw(svga_t *svga, int displine)
{
    xga_t          *xga    = (xga_t *) svga->xga;
    int             comb;
    uint8_t         dat    = 0;
    int             offset = xga->hwcursor_latch.x - xga->hwcursor_latch.xoff;
    int             idx    = 0;
    int             x_pos;
    int             y_pos;
    uint32_t       *p;
    const uint8_t  *cd;

    if (xga->hwcursor_latch.xoff & 0x20)
        idx = 32;

    cd = (uint8_t *) xga->sprite_data;

    if (xga->interlace && xga->hwcursor_oddeven)
        xga->hwcursor_latch.addr += 16;

    for (int x = 0; x < xga->hwcursor_latch.cur_xsize; x++) {
        dat = cd[xga->hwcursor_latch.addr & 0x3ff];

        comb = (dat >> ((x & 0x03) << 1)) & 0x03;

        y_pos = displine;
        x_pos = offset + svga->x_add;
        p     = buffer32->line[y_pos];

        if (x >= idx) {
            switch (comb) {
                case 0x00:
					/* Cursor Color 1 */
                    p[x_pos] = xga->hwc_color0;
                    break;
                case 0x01:
					/* Cursor Color 2 */
                    p[x_pos] = xga->hwc_color1;
                    break;
                case 0x03:
					/* Complement */
                    p[x_pos] ^= 0xffffff;
                    break;

                default:
                    break;
            }
        }
        offset++;
        xga_log("P=%08x, xpos=%d, comb=%x, ypos=%d, offset=%d, latchx=%d, latchxoff=%d.\n", p[x_pos], x_pos, comb, y_pos, offset, xga->hwcursor_latch.x, xga->hwcursor_latch.xoff);

        if ((x & 0x03) == 0x03)
            xga->hwcursor_latch.addr++;
    }

    if (xga->interlace && !xga->hwcursor_oddeven)
        xga->hwcursor_latch.addr += 16;
}

static void
xga_render_overscan_left(xga_t *xga, svga_t *svga)
{
    if ((xga->displine + svga->y_add) < 0)
        return;

    if (svga->scrblank || (xga->h_disp == 0))
        return;

    uint32_t *line_ptr = buffer32->line[xga->displine + svga->y_add];
    for (int i = 0; i < svga->x_add; i++)
        *line_ptr++ = svga->overscan_color;
}

static void
xga_render_overscan_right(xga_t *xga, svga_t *svga)
{
    int right;

    if ((xga->displine + svga->y_add) < 0)
        return;

    if (svga->scrblank || (xga->h_disp == 0))
        return;

    uint32_t *line_ptr = &buffer32->line[xga->displine + svga->y_add][svga->x_add + xga->h_disp];
    right              = (overscan_x >> 1);
    for (int i = 0; i < right; i++)
        *line_ptr++ = svga->overscan_color;
}

static void
xga_render_4bpp(svga_t *svga)
{
    xga_t *xga = (xga_t *) svga->xga;
    uint32_t *p;
    uint32_t  dat;

    if ((xga->displine + svga->y_add) < 0)
        return;

    if (xga->changedvram[xga->ma >> 12] || xga->changedvram[(xga->ma >> 12) + 1] || svga->fullchange) {
        p = &buffer32->line[xga->displine + svga->y_add][svga->x_add];

        if (xga->firstline_draw == 2000)
            xga->firstline_draw = xga->displine;

        xga->lastline_draw = xga->displine;

        for (int x = 0; x <= xga->h_disp; x += 8) {
            dat  = *(uint32_t *) (&xga->vram[xga->ma & xga->vram_mask]);
            p[0] = xga->pallook[dat & 0x0f];
            p[1] = xga->pallook[(dat >> 8) & 0x0f];
            p[2] = xga->pallook[(dat >> 16) & 0x0f];
            p[3] = xga->pallook[(dat >> 24) & 0x0f];

            dat  = *(uint32_t *) (&xga->vram[(xga->ma + 2) & xga->vram_mask]);
            p[4] = xga->pallook[dat & 0x0f];
            p[5] = xga->pallook[(dat >> 8) & 0x0f];
            p[6] = xga->pallook[(dat >> 16) & 0x0f];
            p[7] = xga->pallook[(dat >> 24) & 0x0f];

            xga->ma += 8;
            p += 8;
        }
        xga->ma &= xga->vram_mask;
    }
}

static void
xga_render_8bpp(svga_t *svga)
{
    xga_t *xga = (xga_t *) svga->xga;
    uint32_t *p;
    uint32_t  dat;

    if ((xga->displine + svga->y_add) < 0)
        return;

    if (xga->changedvram[xga->ma >> 12] || xga->changedvram[(xga->ma >> 12) + 1] || svga->fullchange) {
        p = &buffer32->line[xga->displine + svga->y_add][svga->x_add];

        if (xga->firstline_draw == 2000)
            xga->firstline_draw = xga->displine;
        xga->lastline_draw = xga->displine;

        for (int x = 0; x <= xga->h_disp; x += 8) {
            dat  = *(uint32_t *) (&xga->vram[xga->ma & xga->vram_mask]);
            p[0] = xga->pallook[dat & 0xff];
            p[1] = xga->pallook[(dat >> 8) & 0xff];
            p[2] = xga->pallook[(dat >> 16) & 0xff];
            p[3] = xga->pallook[(dat >> 24) & 0xff];

            dat  = *(uint32_t *) (&xga->vram[(xga->ma + 4) & xga->vram_mask]);
            p[4] = xga->pallook[dat & 0xff];
            p[5] = xga->pallook[(dat >> 8) & 0xff];
            p[6] = xga->pallook[(dat >> 16) & 0xff];
            p[7] = xga->pallook[(dat >> 24) & 0xff];

            xga->ma += 8;
            p += 8;
        }
        xga->ma &= xga->vram_mask;
    }
}

static void
xga_render_16bpp(svga_t *svga)
{
    xga_t *xga = (xga_t *) svga->xga;
    int       x;
    uint32_t *p;
    uint32_t  dat;

    if ((xga->displine + svga->y_add) < 0)
        return;

    if (xga->changedvram[xga->ma >> 12] || xga->changedvram[(xga->ma >> 12) + 1] || svga->fullchange) {
        p = &buffer32->line[xga->displine + svga->y_add][svga->x_add];

        if (xga->firstline_draw == 2000)
            xga->firstline_draw = xga->displine;
        xga->lastline_draw = xga->displine;

        for (x = 0; x <= xga->h_disp; x += 8) {
            dat      = *(uint32_t *) (&xga->vram[(xga->ma + (x << 1)) & xga->vram_mask]);
            p[x]     = video_16to32[dat & 0xffff];
            p[x + 1] = video_16to32[dat >> 16];

            dat      = *(uint32_t *) (&xga->vram[(xga->ma + (x << 1) + 4) & xga->vram_mask]);
            p[x + 2] = video_16to32[dat & 0xffff];
            p[x + 3] = video_16to32[dat >> 16];

            dat      = *(uint32_t *) (&xga->vram[(xga->ma + (x << 1) + 8) & xga->vram_mask]);
            p[x + 4] = video_16to32[dat & 0xffff];
            p[x + 5] = video_16to32[dat >> 16];

            dat      = *(uint32_t *) (&xga->vram[(xga->ma + (x << 1) + 12) & xga->vram_mask]);
            p[x + 6] = video_16to32[dat & 0xffff];
            p[x + 7] = video_16to32[dat >> 16];
        }
        xga->ma += x << 1;
        xga->ma &= xga->vram_mask;
    }
}

void
xga_write_test(uint32_t addr, uint8_t val, void *priv)
{
    svga_t *svga       = (svga_t *) priv;
    xga_t  *xga        = (xga_t *) svga->xga;

    if (xga_active && xga) {
        if (((xga->op_mode & 7) >= 1) && xga->aperture_cntl) {
            xga_log("WriteAddr=%05x.\n", addr);
            if (val == 0xa5) { /*Memory size test of XGA*/
                xga->test    = val;
                if (addr == 0xa0001)
                    xga->a5_test = 1;
                else if (addr == 0xafffe)
                    xga->a5_test = 2;

                xga->on = 0;
                xga_log("XGA test1 addr=%05x, test=%02x.\n", addr, xga->a5_test);
            } else if (val == 0x5a) {
                xga->test = val;
                xga->on = 0;
                xga_log("XGA test2 addr = %05x.\n", addr);
            } else if ((addr == 0xa0000) || (addr == 0xa0010)) {
                addr += xga->write_bank;
                xga->vram[addr & xga->vram_mask] = val;
                xga_log("XGA Linear endian reverse write, val = %02x, addr = %05x, banked mask = %04x, a5test=%d.\n", val, addr, svga->banked_mask, xga->a5_test);
            }
        } else if (xga->aperture_cntl || (!xga->aperture_cntl && (svga->mapping.base == 0xa0000))) {
            xga->on = 0;
            xga_log("OFF XGA write.\n");
        }
    }
}

static void
xga_write_banked(uint32_t addr, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;

    if (xga->access_mode & 0x08) {
        if ((xga->access_mode & 0x07) == 0x04)
            addr ^= 1;
    }

    xga->changedvram[(addr & xga->vram_mask) >> 12] = svga->monitor->mon_changeframecount;
    xga->vram[addr & xga->vram_mask]                = val;
}

static void
xga_write(uint32_t addr, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;

    addr &= xga->banked_mask;
    addr += xga->write_bank;

    if (addr >= xga->vram_size)
        return;

    cycles -= svga->monitor->mon_video_timing_write_b;

    xga_write_banked(addr, val, svga);
}

static void
xga_writew(uint32_t addr, uint16_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;

    addr &= xga->banked_mask;
    addr += xga->write_bank;

    if (addr >= xga->vram_size)
        return;

    cycles -= svga->monitor->mon_video_timing_write_w;

    xga_write_banked(addr, val & 0xff, svga);
    xga_write_banked(addr + 1, val >> 8, svga);
}

static void
xga_writel(uint32_t addr, uint32_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;

    addr &= xga->banked_mask;
    addr += xga->write_bank;

    if (addr >= xga->vram_size)
        return;

    cycles -= svga->monitor->mon_video_timing_write_l;

    xga_write_banked(addr, val & 0xff, svga);
    xga_write_banked(addr + 1, val >> 8, svga);
    xga_write_banked(addr + 2, val >> 16, svga);
    xga_write_banked(addr + 3, val >> 24, svga);
}

uint8_t
xga_read_test(uint32_t addr, void *priv)
{
    svga_t  *svga = (svga_t *) priv;
    xga_t   *xga = (xga_t *) svga->xga;
    uint8_t ret = 0x00;

    if (xga_active && xga) {
        if (((xga->op_mode & 7) >= 1) && xga->aperture_cntl) {
            if (xga->test == 0xa5) { /*Memory size test of XGA*/
                if (addr == 0xa0001) {
                    ret = xga->test;
                    xga->on = 1;
                } else if ((addr == 0xa0000) && (xga->a5_test == 1)) { /*This is required by XGAKIT to pass the memory test*/
                    xga_log("A5 test bank = %x.\n", addr);
                    addr += xga->read_bank;
                    ret = xga->vram[addr & xga->vram_mask];
                } else {
                    ret = xga->test;
                    xga->on = 1;
                }
                xga_log("A5 read: XGA ON = %d, addr = %05x, ret = %02x, test1 = %x.\n", xga->on, addr, ret, xga->a5_test);
                return ret;
            } else if (xga->test == 0x5a) {
                ret = xga->test;
                xga->on = 1;
                xga_log("5A read: XGA ON = %d.\n", xga->on);
                return ret;
            } else if ((addr == 0xa0000) || (addr == 0xa0010)) {
                addr += xga->read_bank;
                return xga->vram[addr & xga->vram_mask];
            }
        } else if (xga->aperture_cntl || (!xga->aperture_cntl && (svga->mapping.base == 0xa0000))) {
            xga->on = 0;
            xga_log("OFF XGA read.\n");
        }
    }
    return ret;
}

static uint8_t
xga_read_banked(uint32_t addr, void *priv)
{
    svga_t      *svga = (svga_t *) priv;
    xga_t *xga  = (xga_t *) svga->xga;
    uint8_t      ret  = 0xff;

    if (xga->access_mode & 0x08) {
        if ((xga->access_mode & 0x07) == 0x04)
            addr ^= 1;
    }

    ret = xga->vram[addr & xga->vram_mask];

    return ret;
}

static uint8_t
xga_read(uint32_t addr, void *priv)
{
    svga_t      *svga = (svga_t *) priv;
    xga_t *xga  = (xga_t *) svga->xga;
    uint8_t     ret  = 0xff;

    addr &= xga->banked_mask;
    addr += xga->read_bank;

    if (addr >= xga->vram_size) {
        xga_log("Over Read ADDR=%x.\n", addr);
        return ret;
    }

    cycles -= svga->monitor->mon_video_timing_read_b;

    ret = xga_read_banked(addr, svga);
    return ret;
}

static uint16_t
xga_readw(uint32_t addr, void *priv)
{
    svga_t      *svga = (svga_t *) priv;
    xga_t *xga  = (xga_t *) svga->xga;
    uint16_t     ret  = 0xffff;

    addr &= xga->banked_mask;
    addr += xga->read_bank;

    if (addr >= xga->vram_size) {
        xga_log("Over Read ADDR=%x.\n", addr);
        return ret;
    }

    cycles -= svga->monitor->mon_video_timing_read_w;

    ret = xga_read_banked(addr, svga);
    ret |= (xga_read_banked(addr + 1, svga) << 8);
    return ret;
}

static uint32_t
xga_readl(uint32_t addr, void *priv)
{
    svga_t      *svga = (svga_t *) priv;
    xga_t *xga  = (xga_t *) svga->xga;
    uint32_t     ret  = 0xffffffff;

    addr &= xga->banked_mask;
    addr += xga->read_bank;

    if (addr >= xga->vram_size) {
        xga_log("Over Read ADDR=%x.\n", addr);
        return ret;
    }

    cycles -= svga->monitor->mon_video_timing_read_l;

    ret = xga_read_banked(addr, svga);
    ret |= (xga_read_banked(addr + 1, svga) << 8);
    ret |= (xga_read_banked(addr + 2, svga) << 16);
    ret |= (xga_read_banked(addr + 3, svga) << 24);
    return ret;
}

static void
xga_write_linear(uint32_t addr, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;

    xga_log("WriteLL XGA=%d.\n", xga->on);
    if (!xga->on) {
        svga_write_linear(addr, val, svga);
        return;
    }

    addr &= (xga->vram_size - 1);

    if (addr >= xga->vram_size) {
        xga_log("Write Linear Over!.\n");
        return;
    }

    cycles -= svga->monitor->mon_video_timing_write_b;

    xga->changedvram[(addr & xga->vram_mask) >> 12] = svga->monitor->mon_changeframecount;
    xga->vram[addr & xga->vram_mask]                = val;
}

static void
xga_writew_linear(uint32_t addr, uint16_t val, void *priv)
{
    svga_t      *svga = (svga_t *) priv;
    const xga_t *xga  = (xga_t *) svga->xga;

    if (!xga->on) {
        svga_writew_linear(addr, val, svga);
        return;
    }

    xga_write_linear(addr, val, priv);
    xga_write_linear(addr + 1, val >> 8, priv);
}

static void
xga_writel_linear(uint32_t addr, uint32_t val, void *priv)
{
    svga_t      *svga = (svga_t *) priv;
    const xga_t *xga  = (xga_t *) svga->xga;

    if (!xga->on) {
        svga_writel_linear(addr, val, svga);
        return;
    }

    xga_write_linear(addr, val, priv);
    xga_write_linear(addr + 1, val >> 8, priv);
    xga_write_linear(addr + 2, val >> 16, priv);
    xga_write_linear(addr + 3, val >> 24, priv);
}

static uint8_t
xga_read_linear(uint32_t addr, void *priv)
{
    svga_t      *svga = (svga_t *) priv;
    const xga_t *xga  = (xga_t *) svga->xga;
    uint8_t      ret  = 0xff;

    if (!xga->on)
        return svga_read_linear(addr, svga);

    addr &= (xga->vram_size - 1);

    if (addr >= xga->vram_size) {
        xga_log("Read Linear Over ADDR=%x!.\n", addr);
        return ret;
    }

    cycles -= svga->monitor->mon_video_timing_read_b;

    ret = xga->vram[addr & xga->vram_mask];

    return ret;
}

static uint16_t
xga_readw_linear(uint32_t addr, void *priv)
{
    svga_t      *svga = (svga_t *) priv;
    const xga_t *xga  = (xga_t *) svga->xga;
    uint16_t     ret;

    if (!xga->on)
        return svga_readw_linear(addr, svga);

    ret = xga_read_linear(addr, svga);
    ret |= (xga_read_linear(addr + 1, svga) << 8);

    return ret;
}

static uint32_t
xga_readl_linear(uint32_t addr, void *priv)
{
    svga_t      *svga = (svga_t *) priv;
    const xga_t *xga  = (xga_t *) svga->xga;
    uint32_t     ret;

    if (!xga->on)
        return svga_readl_linear(addr, svga);

    ret = xga_read_linear(addr, svga);
    ret |= (xga_read_linear(addr + 1, svga) << 8);
    ret |= (xga_read_linear(addr + 2, svga) << 16);
    ret |= (xga_read_linear(addr + 3, svga) << 24);

    return ret;
}

void
xga_set_poll(svga_t *svga)
{
    timer_set_callback(&svga->timer, xga_poll);
}

void
xga_poll(void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t *xga   = (xga_t *) svga->xga;
    uint32_t x;
    int      wx;
    int      wy;

    xga_log("XGA Poll=%d.\n", xga->on);
    if (xga->on) {
        if (!xga->linepos) {
            if (xga->displine == xga->hwcursor_latch.y && xga->hwcursor_latch.ena) {
                xga->hwcursor_on      = xga->hwcursor_latch.cur_ysize - ((xga->hwcursor_latch.yoff & 0x20) ? 32 : 0);
                xga->hwcursor_oddeven = 0;
            }

            if (xga->displine == (xga->hwcursor_latch.y + 1) && xga->hwcursor_latch.ena && xga->interlace) {
                xga->hwcursor_on      = xga->hwcursor_latch.cur_ysize - ((xga->hwcursor_latch.yoff & 0x20) ? 33 : 1);
                xga->hwcursor_oddeven = 1;
            }

            timer_advance_u64(&svga->timer, xga->dispofftime);
            svga->cgastat |= 1;
            xga->linepos = 1;

            if (xga->dispon) {
                xga->h_disp_on = 1;

                xga->ma &= xga->vram_mask;

                if (xga->firstline == 2000) {
                    xga->firstline = xga->displine;
                    video_wait_for_buffer_monitor(svga->monitor_index);
                }

                if (xga->hwcursor_on)
                    xga->changedvram[xga->ma >> 12] = xga->changedvram[(xga->ma >> 12) + 1] = xga->interlace ? 3 : 2;

                svga->render_xga(svga);

                svga->x_add = (overscan_x >> 1);
                xga_render_overscan_left(xga, svga);
                xga_render_overscan_right(xga, svga);
                svga->x_add = (overscan_x >> 1);

                if (xga->hwcursor_on) {
                    xga_hwcursor_draw(svga, xga->displine + svga->y_add);
                    xga->hwcursor_on--;
                    if (xga->hwcursor_on && xga->interlace)
                        xga->hwcursor_on--;
                }

                if (xga->lastline < xga->displine)
                    xga->lastline = xga->displine;
            }

            xga->displine++;
            if (xga->interlace)
                xga->displine++;
            if ((svga->cgastat & 8) && ((xga->displine & 0x0f) == (svga->crtc[0x11] & 0x0f)) && svga->vslines)
                svga->cgastat &= ~8;
            if (xga->displine > 1500)
                xga->displine = 0;
        } else {
            timer_advance_u64(&svga->timer, xga->dispontime);
            if (xga->dispon)
                svga->cgastat &= ~1;

            xga->h_disp_on = 0;

            xga->linepos = 0;
            if (xga->dispon) {
                if (xga->sc == xga->rowcount) {
                    xga->sc = 0;

                    xga_log("MA=%08x, MALATCH=%x.\n", xga->ma, xga->ma_latch);
                    xga->maback += (xga->rowoffset << 3);
                    if (xga->interlace)
                        xga->maback += (xga->rowoffset << 3);

                    xga->maback &= xga->vram_mask;
                    xga->ma = xga->maback;
                } else {
                    xga->sc++;
                    xga->sc &= 0x1f;
                    xga->ma = xga->maback;
                }
            }

            xga->vc++;
            xga->vc &= 0x7ff;

            if (xga->vc == xga->split) {
                if (xga->interlace && xga->oddeven)
                    xga->ma = xga->maback = (xga->rowoffset << 1);
                else
                    xga->ma = xga->maback = 0;

                xga->ma     = (xga->ma << 2);
                xga->maback = (xga->maback << 2);

                xga->sc = 0;
            }
            if (xga->vc == xga->dispend) {
                xga->dispon = 0;

                for (x = 0; x < ((xga->vram_mask + 1) >> 12); x++) {
                    if (xga->changedvram[x])
                        xga->changedvram[x]--;
                }
                if (svga->fullchange)
                    svga->fullchange--;
            }
            if (xga->vc == xga->v_syncstart) {
                xga->dispon = 0;
                svga->cgastat |= 8;
                x           = xga->h_disp;

                if (xga->interlace && !xga->oddeven)
                    xga->lastline++;
                if (xga->interlace && xga->oddeven)
                    xga->firstline--;

                wx = x;

                wy = xga->lastline - xga->firstline;
                svga_doblit(wx, wy, svga);

                xga->firstline = 2000;
                xga->lastline  = 0;

                xga->firstline_draw = 2000;
                xga->lastline_draw  = 0;

                xga->oddeven ^= 1;

                svga->monitor->mon_changeframecount = xga->interlace ? 3 : 2;

                if (xga->interlace && xga->oddeven)
                    xga->ma = xga->maback = xga->ma_latch + (xga->rowoffset << 1);
                else
                    xga->ma = xga->maback = xga->ma_latch;

                xga->ma     = (xga->ma << 2);
                xga->maback = (xga->maback << 2);
            }
            if (xga->vc == xga->v_total) {
                xga->vc       = 0;
                xga->sc       = 0;
                xga->dispon   = 1;
                xga->displine = (xga->interlace && xga->oddeven) ? 1 : 0;

                svga->x_add = (overscan_x >> 1);

                xga->hwcursor_on    = 0;
                xga->hwcursor_latch = xga->hwcursor;
            }
        }
    } else
        svga_recalctimings(svga);
}

static uint8_t
xga_mca_read(int port, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;
    uint8_t       ret  = xga->pos_regs[port & 7];

    xga_log("[%04X:%08X]: POS Read Port = %x, val = %02x\n", CS, cpu_state.pc,
            port & 7, xga->pos_regs[port & 7]);

    return ret;
}

static void
xga_mca_write(int port, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102)
        return;

    io_removehandler(0x2100 + (xga->instance << 4), 0x0010, xga_ext_inb, NULL, NULL, xga_ext_outb, NULL, NULL, svga);
    mem_mapping_disable(&xga->memio_mapping);
    xga->on                    = 0;
    xga->a5_test               = 0;

    /* Save the MCA register value. */
    xga->pos_regs[port & 7] = val;
    if (!(xga->pos_regs[4] & 1)) /*MCA 4MB addressing on systems with more than 16MB of memory*/
        xga->pos_regs[4] |= 1;

    if (xga->pos_regs[2] & 1) {
        xga->instance      = (xga->pos_regs[2] & 0x0e) >> 1;
        xga->base_addr_1mb = (xga->pos_regs[5] & 0x0f) << 20;
        xga->linear_base   = ((xga->pos_regs[4] & 0xfe) * 0x1000000) + (xga->instance << 22);
        xga->rom_addr      = 0xc0000 + (((xga->pos_regs[2] & 0xf0) >> 4) * 0x2000);

        io_sethandler(0x2100 + (xga->instance << 4), 0x0010, xga_ext_inb, NULL, NULL, xga_ext_outb, NULL, NULL, svga);

        if (xga->rom_addr) {
            mem_mapping_set_addr(&xga->memio_mapping, xga->rom_addr, 0x2000);
            xga_log("ROM address=%05x.\n", xga->rom_addr);
        }
    }

    xga_log("[%04X:%08X]: POS Write Port = %x, val = %02x, linear base = %08x, instance = %d, "
            "rom addr = %05x\n", CS, cpu_state.pc, port & 7, val, xga->linear_base,
            xga->instance, xga->rom_addr);
}

static uint8_t
xga_mca_feedb(void *priv)
{
    const svga_t *svga = (svga_t *) priv;
    const xga_t  *xga  = (xga_t *) svga->xga;

    xga_log("FeedB.\n");
    return xga->pos_regs[2] & 1;
}

static void
xga_mca_reset(void *priv)
{
    svga_t *svga = (svga_t *) priv;

    xga_log("MCA Reset.\n");
    mem_mapping_set_handler(&svga->mapping, svga->read, svga->readw, svga->readl, svga->write, svga->writew, svga->writel);
    xga_mca_write(0x102, 0, svga);
}

static void
xga_reset(void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;

    xga_log("Normal Reset.\n");
    if (xga_standalone_enabled)
        mem_mapping_disable(&xga->memio_mapping);

    xga->on                    = 0;
    xga->a5_test               = 0;
    mem_mapping_set_handler(&svga->mapping, svga->read, svga->readw, svga->readl, svga->write, svga->writew, svga->writel);
    svga_set_poll(svga);
}

static uint8_t
xga_pos_in(uint16_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;
    uint8_t ret  = 0x00;

    if (!xga_standalone_enabled) {
        switch (addr) {
            case 0x0100:
            case 0x0101:
                if (xga->instance == xga->instance_num)
                    ret = xga->pos_regs[addr & 7];
                else
                    ret = 0xff;

                xga_log("%03xRead=%02x.\n", addr, ret);
                break;
            case 0x0102:
                ret = xga->pos_regs[2] | 0x30;
                break;
            case 0x0105:
                ret = xga->pos_regs[5];
                xga_log("POS IDX Read 010%x ret = %02x.\n", addr & 7, ret);
                break;
            case 0x0103:
                if ((xga->pos_idx & 3) == 0) {
                    ret = xga->pos_regs[3];
                    ret |= (xga->dma_channel << 3);
                }

                xga_log("POS IDX for 0103 = %d, ret = %02x.\n", xga->pos_idx & 3, ret);
                break;
            case 0x0104:
                switch (xga->pos_idx & 3) {
                    case 0:
                        ret = xga->pos_regs[4];
                        break;
                    case 1:
                        ret = xga->pos_regs[0];
                        break;
                    case 2:
                        ret = xga->pos_regs[1];
                        break;

                    default:
                        break;
                }
                xga_log("POS IDX for 0104 = %d, ret = %02x.\n", xga->pos_idx & 3, ret);
                break;
            case 0x0106:
                ret = xga->pos_idx >> 8;
                break;
            case 0x0107:
                ret = xga->pos_idx & 0xff;
                break;
            case 0x0108 ... 0x010f:
                xga->instance_num = addr & 0x07;
                if (xga->instance == xga->instance_num)
                    ret = xga->instance;

                ret |= xga->isa_pos_enable;
                xga_log("%03xRead=%02x.\n", addr, ret);
                break;

            default:
                break;
        }
    } else {
        switch (addr) {
            case 0x0096:
                ret = xga->vga_post;
                break;
            case 0x0100:
            case 0x0101:
                ret = xga->pos_regs[addr & 7];
                break;
            case 0x0103:
                ret = xga->pos_regs[3] | 0x06;
                ret |= (xga->dma_channel << 3);
                break;
            case 0x0102:
            case 0x0104:
            case 0x0105:
            case 0x0106:
            case 0x0107:
                ret = (xga_mca_read(addr, svga));
                break;

            default:
                break;
        }
    }
    xga_log("[%04X:%08X]: XGA POS IN addr=%04x, ret=%02x.\n", CS, cpu_state.pc, addr, ret);
    return ret;
}

static void
xga_pos_out(uint16_t addr, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;

    xga_log("[%04X:%08X]: XGA POS OUT addr=%04x, val=%02x.\n", CS, cpu_state.pc, addr, val);
    if (!xga_standalone_enabled) {
        switch (addr) {
            case 0x0096:
                xga->instance_num = val & 0x07;
                xga->isa_pos_enable = val & 0x08;
                xga_log("096Write=%02x.\n", val);
                break;
            case 0x0102:
                xga_log("[%04X:%08X]: 102Write=%02x.\n", CS, cpu_state.pc, val);
                xga->pos_regs[2] = val | 0x02; /*Instance 0 is not recommended on AT bus/ISA bus systems, so force it to use instance 1.*/
                io_removehandler(0x2100 + (xga->instance << 4), 0x0010, xga_ext_inb, NULL, NULL, xga_ext_outb, NULL, NULL, svga);
                mem_mapping_disable(&xga->memio_mapping);
                if (xga->pos_regs[2] & 0x01) {
                    xga->rom_addr    = 0xc0000 + (((xga->pos_regs[2] & 0xc0) >> 6) * 0x8000);
                    xga->instance    = (xga->pos_regs[2] & 0x0e) >> 1;
                    xga->linear_base = ((xga->pos_regs[4] & 0xfe) * 0x1000000) + (xga->instance << 22);
                    xga->base_addr_1mb = (xga->pos_regs[5] & 0x0f) << 20;
                    io_sethandler(0x2100 + (xga->instance << 4), 0x0010, xga_ext_inb, NULL, NULL, xga_ext_outb, NULL, NULL, svga);
                    xga_log("XGA ISA ROM address=%05x, instance=%d.\n", xga->rom_addr, xga->instance);
                    mem_mapping_set_addr(&xga->memio_mapping, xga->rom_addr, 0x8000);
                }
                break;
            case 0x0103:
                if ((xga->pos_idx & 3) == 0)
                    xga->pos_regs[3] = val;

                xga_log("[%04X:%08X]: 103Write=%02x.\n", CS, cpu_state.pc, val);
                break;
            case 0x0104:
                xga_log("104Write=%02x.\n", val);
                if ((xga->pos_idx & 3) == 0) {
                    xga->pos_regs[4] = val;
                    if (!(xga->pos_regs[4] & 0x01)) /*4MB addressing on systems with more than 15MB of memory*/
                        xga->pos_regs[4] |= 0x01;
                }
                break;
            case 0x0105:
                xga_log("105Write=%02x.\n", val);
                xga->pos_regs[5] = val;
                break;
            case 0x0106:
                xga->pos_idx = (xga->pos_idx & 0x00ff) | (val << 8);
                break;
            case 0x0107:
                xga->pos_idx = (xga->pos_idx & 0xff00) | val;
                xga_log("POS IDX Write = %04x.\n", xga->pos_idx);
                break;
            case 0x0108 ... 0x010f:
                xga_log("%03xWrite=%02x.\n", addr, val);
                xga->instance_num = addr & 0x07;
                xga->isa_pos_enable = val & 0x08;
                break;

            default:
                break;
        }
    } else {
        xga_log("XGA Standalone ISA Write Port=%04x, Val=%02x.\n", addr, val);
        switch (addr) {
            case 0x0096:
                xga->vga_post = val;
                break;

            default:
                break;
        }
    }
}

static void *
xga_init(const device_t *info)
{
    if (svga_get_pri() == NULL)
        return NULL;

    svga_t  *svga     = svga_get_pri();
    xga_t   *xga      = (xga_t *) calloc(1, sizeof(xga_t));

    svga->xga         = xga;

    xga->ext_mem_addr = device_get_config_hex16("ext_mem_addr");
    xga->instance_isa = device_get_config_int("instance");
    xga->type         = device_get_config_int("type");
    xga->dma_channel  = device_get_config_int("dma");
    xga->bus          = info->flags;

    xga->vram_size             = (1024 << 10);
    xga->vram_mask             = xga->vram_size - 1;
    xga->vram                  = calloc(xga->vram_size, 1);
    xga->changedvram           = calloc((xga->vram_size >> 12) + 1, 1);
    xga->on                    = 0;
    xga->hwcursor.cur_xsize    = 64;
    xga->hwcursor.cur_ysize    = 64;
    xga->a5_test               = 0;

    if (info->flags & DEVICE_MCA) {
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_xga_mca);
        xga->base_addr_1mb = 0;
        xga->linear_base = 0;
        xga->instance    = 0;
        xga->rom_addr    = 0;
        rom_init(&xga->bios_rom, xga->type ? XGA2_BIOS_PATH : XGA_BIOS_PATH, 0xc0000, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);
        mem_mapping_disable(&xga->bios_rom.mapping);
        mem_mapping_add(&xga->memio_mapping, 0, 0, xga_memio_readb, xga_memio_readw, xga_memio_readl,
                        xga_memio_writeb, xga_memio_writew, xga_memio_writel,
                        xga->bios_rom.rom, MEM_MAPPING_EXTERNAL, svga);
    } else {
        xga->pos_regs[4] = 0x02;
        if (!xga_standalone_enabled) {
            rom_init(&xga->bios_rom, INMOS_XGA_BIOS_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL); /*VGA BIOS only*/
            mem_mapping_add(&xga->memio_mapping, 0, 0, xga_memio_readb, xga_memio_readw, xga_memio_readl,
                            xga_memio_writeb, xga_memio_writew, xga_memio_writel,
                            xga->bios_rom.rom, MEM_MAPPING_EXTERNAL, svga);
        } else {
            xga->pos_regs[2] = (xga->instance_isa << 1) | xga->ext_mem_addr;
            xga->rom_addr    = 0xc0000 + (((xga->pos_regs[2] & 0xf0) >> 4) * 0x2000);
            xga->instance    = (xga->pos_regs[2] & 0x0e) >> 1;
            xga->pos_regs[2] |= 0x01;
            xga->pos_regs[4] |= 0x01;
            if (mem_size >= 15360)
                xga->pos_regs[5] = 0;
            else {
                xga->pos_regs[5] = ((mem_size * 64) >> 0x10) + 1;
                if (xga->pos_regs[5] == 0x10)
                    xga->pos_regs[5] = 0x00;
            }
            xga->base_addr_1mb = (xga->pos_regs[5] & 0x0f) << 20;
            xga->linear_base = ((xga->pos_regs[4] & 0xfe) * 0x1000000) + (xga->instance << 22);
            rom_init(&xga->bios_rom, xga->type ? XGA2_BIOS_PATH : XGA_BIOS_PATH, xga->rom_addr, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);
            mem_mapping_add(&xga->memio_mapping, 0, 0, xga_memio_readb, xga_memio_readw, xga_memio_readl,
                            xga_memio_writeb, xga_memio_writew, xga_memio_writel,
                            xga->bios_rom.rom, MEM_MAPPING_EXTERNAL, svga);
        }
    }

    mem_mapping_add(&xga->linear_mapping, 0, 0, xga_read_linear, xga_readw_linear, xga_readl_linear,
                    xga_write_linear, xga_writew_linear, xga_writel_linear,
                    NULL, MEM_MAPPING_EXTERNAL, svga);

    mem_mapping_disable(&xga->linear_mapping);
    mem_mapping_disable(&xga->memio_mapping);

    xga->pos_regs[0] = xga->type ? 0xda : 0xdb;
    xga->pos_regs[1] = 0x8f;

    if (xga->bus & DEVICE_MCA) {
        mca_add(xga_mca_read, xga_mca_write, xga_mca_feedb, xga_mca_reset, svga);
    } else {
        io_sethandler(0x0096, 0x0001, xga_pos_in, NULL, NULL, xga_pos_out, NULL, NULL, svga);
        io_sethandler(0x0100, 0x0010, xga_pos_in, NULL, NULL, xga_pos_out, NULL, NULL, svga);
        if (xga_standalone_enabled) {
            io_sethandler(0x2100 + (xga->instance << 4), 0x0010, xga_ext_inb, NULL, NULL, xga_ext_outb, NULL, NULL, svga);
            mem_mapping_set_addr(&xga->memio_mapping, xga->rom_addr, 0x2000);
        }
    }
    return svga;
}

static void *
svga_xga_init(const device_t *info)
{
    svga_t *svga = (svga_t *) calloc(1, sizeof(svga_t));

    video_inform(VIDEO_FLAG_TYPE_XGA, &timing_xga_isa);

    svga_init(info, svga, svga, 1 << 18, /*256kB*/
              NULL,
              svga_xga_in, svga_xga_out,
              NULL,
              NULL);

    io_sethandler(0x03c0, 0x0020, svga_xga_in, NULL, NULL, svga_xga_out, NULL, NULL, svga);

    svga->bpp     = 8;
    svga->miscout = 1;
    xga_active    = 1;

    return xga_init(info);
}

static void
xga_close(void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = (xga_t *) svga->xga;

    if (svga) {
        free(xga->vram);
        free(xga->changedvram);

        free(xga);
    }
}

static int
xga_available(void)
{
    return rom_present(XGA_BIOS_PATH) && rom_present(XGA2_BIOS_PATH);
}

static int
inmos_xga_available(void)
{
    return rom_present(INMOS_XGA_BIOS_PATH);
}

static void
xga_speed_changed(void *priv)
{
    svga_t *svga = (svga_t *) priv;

    svga_recalctimings(svga);
}

static void
xga_force_redraw(void *priv)
{
    svga_t *svga = (svga_t *) priv;

    svga->fullchange = svga->monitor->mon_changeframecount;
}

static const device_config_t xga_mca_configuration[] = {
  // clang-format off
    {
        .name           = "type",
        .description    = "XGA type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "XGA-1", .value = 0 },
            { .description = "XGA-2", .value = 1 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_config_t xga_isa_configuration[] = {
  // clang-format off
    {
        .name           = "type",
        .description    = "XGA type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "XGA-1", .value = 0 },
            { .description = "XGA-2", .value = 1 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "instance",
        .description    = "Instance",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 6,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0 (2100h-210Fh)", .value = 0 },
            { .description = "1 (2110h-211Fh)", .value = 1 },
            { .description = "2 (2120h-212Fh)", .value = 2 },
            { .description = "3 (2130h-213Fh)", .value = 3 },
            { .description = "4 (2140h-214Fh)", .value = 4 },
            { .description = "5 (2150h-215Fh)", .value = 5 },
            { .description = "6 (2160h-216Fh)", .value = 6 },
            { .description = "7 (2170h-217Fh)", .value = 7 },
            { .description = ""                            }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "ext_mem_addr",
        .description    = "MMIO Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x00f0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "C800h", .value = 0x0040 },
            { .description = "CA00h", .value = 0x0050 },
            { .description = "CC00h", .value = 0x0060 },
            { .description = "CE00h", .value = 0x0070 },
            { .description = "D000h", .value = 0x0080 },
            { .description = "D200h", .value = 0x0090 },
            { .description = "D400h", .value = 0x00a0 },
            { .description = "D600h", .value = 0x00b0 },
            { .description = "D800h", .value = 0x00c0 },
            { .description = "DA00h", .value = 0x00d0 },
            { .description = "DC00h", .value = 0x00e0 },
            { .description = "DE00h", .value = 0x00f0 },
            { .description = ""                       }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "dma",
        .description    = "DMA",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 7,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0 },
            { .description = "DMA 6",    .value = 6 },
            { .description = "DMA 7",    .value = 7 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_config_t xga_inmos_isa_configuration[] = {
  // clang-format off
    {
        .name           = "type",
        .description    = "XGA type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "XGA-1", .value = 0 },
            { .description = "XGA-2", .value = 1 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "dma",
        .description    = "DMA",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 7,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0 },
            { .description = "DMA 6",    .value = 6 },
            { .description = "DMA 7",    .value = 7 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t xga_device = {
    .name          = "XGA (MCA)",
    .internal_name = "xga_mca",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = xga_init,
    .close         = xga_close,
    .reset         = xga_reset,
    .available     = xga_available,
    .speed_changed = xga_speed_changed,
    .force_redraw  = xga_force_redraw,
    .config        = xga_mca_configuration
};

const device_t xga_isa_device = {
    .name          = "XGA (ISA)",
    .internal_name = "xga_isa",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = xga_init,
    .close         = xga_close,
    .reset         = xga_reset,
    .available     = xga_available,
    .speed_changed = xga_speed_changed,
    .force_redraw  = xga_force_redraw,
    .config        = xga_isa_configuration
};

const device_t inmos_isa_device = {
    .name          = "INMOS XGA (ISA)",
    .internal_name = "inmos_xga_isa",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = svga_xga_init,
    .close         = xga_close,
    .reset         = xga_reset,
    .available     = inmos_xga_available,
    .speed_changed = xga_speed_changed,
    .force_redraw  = xga_force_redraw,
    .config        = xga_inmos_isa_configuration
};

void
xga_device_add(void)
{
    if (!xga_standalone_enabled)
        return;

    if (machine_has_bus(machine, MACHINE_BUS_MCA))
        device_add(&xga_device);
    else
        device_add(&xga_isa_device);
}
