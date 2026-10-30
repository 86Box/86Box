/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          3DFX Voodoo emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2008-2020 Sarah Walker.
 */
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
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/bswap.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_voodoo_common.h>
#include <86box/vid_voodoo_dither.h>
#include <86box/vid_voodoo_regs.h>
#include <86box/vid_voodoo_render.h>
#include <86box/vid_voodoo_fb.h>

#ifdef ENABLE_VOODOO_FB_LOG
int voodoo_fb_do_log = ENABLE_VOODOO_FB_LOG;

static void
voodoo_fb_log(const char *fmt, ...)
{
    va_list ap;

    if (voodoo_fb_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define voodoo_fb_log(fmt, ...)
#endif

/* The row an LFB read reaches: reads take the Y origin from lfbMode alone,
   whatever the pixel pipeline setting (SST-1 and Voodoo2 specs, lfbMode
   bit 13); writes flip in lfb_y_origin_flip. Negative when the row is
   above the origin. */
int
voodoo_fb_read_y(const voodoo_t *voodoo, int y)
{
    if (voodoo->lfbMode & LFB_Y_ORIGIN)
        return ((voodoo->type >= VOODOO_BANSHEE) ? voodoo->y_origin_swap : (voodoo->v_disp - 1)) - y;
    return y;
}

/* lfbMode's read swaps, on the 32 bits read: words, then bytes. */
static inline uint32_t
lfb_read_swap(const voodoo_t *voodoo, uint32_t val)
{
    if (voodoo->lfbMode & LFB_READ_WORD_SWAP)
        val = (val << 16) | (val >> 16);
    if (voodoo->lfbMode & LFB_READ_BYTE_SWIZZLE)
        val = bswap32(val);
    return val;
}

uint16_t
voodoo_fb_readw(uint32_t addr, void *priv)
{
    voodoo_t *voodoo = (voodoo_t *) priv;
    int       x;
    int       y;
    uint32_t  read_addr;
    uint16_t  temp;

    if (voodoo->type >= VOODOO_BANSHEE) {
        x = addr & 0xffe;
        y = (addr >> 12) & 0x3ff;
    } else {
        x = addr & 0x7fe;
        y = (addr >> 11) & 0x3ff;
    }

    y = voodoo_fb_read_y(voodoo, y);
    if (y < 0)
        return 0xffff;

    if (SLI_ENABLED) {
        const voodoo_set_t *set = voodoo->set;

        if (y & 1)
            voodoo = set->voodoos[1];
        else
            voodoo = set->voodoos[0];

        y >>= 1;
    }

    if (voodoo->col_tiled)
        read_addr = voodoo->fb_read_offset + (x & 127) + (x >> 7) * 128 * 32 + (y & 31) * 128 + (y >> 5) * voodoo->row_width;
    else
        read_addr = voodoo->fb_read_offset + x + (y * voodoo->row_width);

    if (read_addr > voodoo->fb_mask)
        return 0xffff;

    if (voodoo->lfbMode & (LFB_READ_WORD_SWAP | LFB_READ_BYTE_SWIZZLE)) {
        /* The swaps work on the 32 bits holding the pixel; this is half */
        uint32_t val = lfb_read_swap(voodoo, *(uint32_t *) (&voodoo->fb_mem[read_addr & ~3 & voodoo->fb_mask]));

        return (read_addr & 2) ? (val >> 16) : (val & 0xffff);
    }

    temp = *(uint16_t *) (&voodoo->fb_mem[read_addr & voodoo->fb_mask]);

    //        voodoo_fb_log("voodoo_fb_readw : %08X %08X  %i %i  %08X %08X  %08x:%08x %i\n", addr, temp, x, y, read_addr, *(uint32_t *)(&voodoo->fb_mem[4]), cs, pc, fb_reads++);
    return temp;
}
uint32_t
voodoo_fb_readl(uint32_t addr, void *priv)
{
    voodoo_t *voodoo = (voodoo_t *) priv;
    int       x;
    int       y;
    uint32_t  read_addr;
    uint32_t  temp;

    if (voodoo->type >= VOODOO_BANSHEE) {
        x = addr & 0xffe;
        y = (addr >> 12) & 0x3ff;
    } else {
        x = addr & 0x7fe;
        y = (addr >> 11) & 0x3ff;
    }

    y = voodoo_fb_read_y(voodoo, y);
    if (y < 0)
        return 0xffffffff;

    if (SLI_ENABLED) {
        const voodoo_set_t *set = voodoo->set;

        if (y & 1)
            voodoo = set->voodoos[1];
        else
            voodoo = set->voodoos[0];

        y >>= 1;
    }

    if (voodoo->col_tiled)
        read_addr = voodoo->fb_read_offset + (x & 127) + (x >> 7) * 128 * 32 + (y & 31) * 128 + (y >> 5) * voodoo->row_width;
    else
        read_addr = voodoo->fb_read_offset + x + (y * voodoo->row_width);

    if (read_addr > voodoo->fb_mask)
        return 0xffffffff;

    temp = lfb_read_swap(voodoo, *(uint32_t *) (&voodoo->fb_mem[read_addr & voodoo->fb_mask]));

    //        voodoo_fb_log("voodoo_fb_readl : %08X %08x %08X  x=%i y=%i  %08X %08X  %08x:%08x %i ro=%08x rw=%i\n", addr, read_addr, temp, x, y, read_addr, *(uint32_t *)(&voodoo->fb_mem[4]), cs, pc, fb_reads++, voodoo->fb_read_offset, voodoo->row_width);
    return temp;
}

static inline uint16_t
do_dither(voodoo_params_t *params, rgba8_t col, int x, int y)
{
    int r;
    int g;
    int b;

    if (dither) {
        if (dither2x2) {
            r = dither_rb2x2[col.r][y & 1][x & 1];
            g = dither_g2x2[col.g][y & 1][x & 1];
            b = dither_rb2x2[col.b][y & 1][x & 1];
        } else {
            r = dither_rb[col.r][y & 3][x & 3];
            g = dither_g[col.g][y & 3][x & 3];
            b = dither_rb[col.b][y & 3][x & 3];
        }
    } else {
        r = col.r >> 3;
        g = col.g >> 2;
        b = col.b >> 3;
    }

    return b | (g << 5) | (r << 11);
}

static inline rgba8_t
lfb_decode_565(const voodoo_t *voodoo, uint16_t val)
{
    /*RGB565 has no alpha, so ABGR/BGRA and ARGB/RGBA only differ in R/B order*/
    if (voodoo->lfbMode & LFB_RGBCHANNEL_ABGR)
        return bgr565[val];
    return rgb565[val];
}

static inline rgba8_t
lfb_decode_1555(const voodoo_t *voodoo, uint16_t val)
{
    switch (voodoo->lfbMode & LFB_RGBCHANNEL_MASK) {
        default:
        case LFB_RGBCHANNEL_ARGB:
            return argb1555[val];
        case LFB_RGBCHANNEL_ABGR:
            return abgr1555[val];
        case LFB_RGBCHANNEL_RGBA:
            return rgba1555[val];
        case LFB_RGBCHANNEL_BGRA:
            return bgra1555[val];
    }
}

static inline void
lfb_decode_8888(const voodoo_t *voodoo, uint32_t val, rgba8_t *col, uint8_t *alpha)
{
    switch (voodoo->lfbMode & LFB_RGBCHANNEL_MASK) {
        default:
        case LFB_RGBCHANNEL_ARGB:
            col->b = val & 0xff;
            col->g = (val >> 8) & 0xff;
            col->r = (val >> 16) & 0xff;
            *alpha = (val >> 24) & 0xff;
            break;
        case LFB_RGBCHANNEL_ABGR:
            col->r = val & 0xff;
            col->g = (val >> 8) & 0xff;
            col->b = (val >> 16) & 0xff;
            *alpha = (val >> 24) & 0xff;
            break;
        case LFB_RGBCHANNEL_RGBA:
            *alpha = val & 0xff;
            col->b = (val >> 8) & 0xff;
            col->g = (val >> 16) & 0xff;
            col->r = (val >> 24) & 0xff;
            break;
        case LFB_RGBCHANNEL_BGRA:
            *alpha = val & 0xff;
            col->r = (val >> 8) & 0xff;
            col->g = (val >> 16) & 0xff;
            col->b = (val >> 24) & 0xff;
            break;
    }
}

static inline int
lfb_y_origin_flip(const voodoo_t *voodoo, int y)
{
    int y_origin = (voodoo->type >= VOODOO_BANSHEE) ? voodoo->y_origin_swap : (voodoo->v_disp - 1);

    if ((!(voodoo->lfbMode & 0x100) && (voodoo->lfbMode & (1 << 13))) || ((voodoo->lfbMode & 0x100) && (voodoo->params.fbzMode & (1 << 17))))
        return y_origin - y;
    return y;
}

/*Colour combine for LFB writes with the pixel pipeline enabled. This isn't
  obviously documented in any 3dfx/Glide documentation, but has been verified
  on hardware. It functions as in normal rendering, except the written colour
  data is unconditionally used as Cother/Aother.*/
static inline void
voodoo_fb_cca_process(const voodoo_params_t *params, rgba8_t *write_data, uint16_t new_depth)
{
    int     src_r;
    int     src_g;
    int     src_b;
    int     src_a;
    int     msel_r;
    int     msel_g;
    int     msel_b;
    int     msel_a;
    uint8_t clocal_r;
    uint8_t clocal_g;
    uint8_t clocal_b;
    uint8_t alocal;
    uint8_t cother_r = write_data->r;
    uint8_t cother_g = write_data->g;
    uint8_t cother_b = write_data->b;
    uint8_t aother   = write_data->a;
    int     sel;

    if (cc_localselect_override)
        sel = (aother & 0x80) ? 1 : 0;
    else
        sel = cc_localselect;

    if (sel) {
        clocal_r = (params->color0 >> 16) & 0xff;
        clocal_g = (params->color0 >> 8) & 0xff;
        clocal_b = params->color0 & 0xff;
    } else {
        clocal_r = 0; /*FIXME*/
        clocal_g = 0;
        clocal_b = 0;
    }

    switch (cca_localselect) {
        case CCA_LOCALSELECT_ITER_A:
            alocal = 0; /*FIXME*/
            break;
        case CCA_LOCALSELECT_COLOR0:
            alocal = (params->color0 >> 24) & 0xff;
            break;
        case CCA_LOCALSELECT_ITER_Z:
            alocal = new_depth >> 8; /*FIXME*/
            break;
        default:
            fatal("Bad cca_localselect %i\n", cca_localselect);
            alocal = 0xff;
            break;
    }

    if (cc_zero_other) {
        src_r = 0;
        src_g = 0;
        src_b = 0;
    } else {
        src_r = cother_r;
        src_g = cother_g;
        src_b = cother_b;
    }

    if (cca_zero_other)
        src_a = 0;
    else
        src_a = aother;

    if (cc_sub_clocal) {
        src_r -= clocal_r;
        src_g -= clocal_g;
        src_b -= clocal_b;
    }

    if (cca_sub_clocal)
        src_a -= alocal;

    switch (cc_mselect) {
        case CC_MSELECT_ZERO:
            msel_r = 0;
            msel_g = 0;
            msel_b = 0;
            break;
        case CC_MSELECT_CLOCAL:
            msel_r = clocal_r;
            msel_g = clocal_g;
            msel_b = clocal_b;
            break;
        case CC_MSELECT_AOTHER:
        case CC_MSELECT_TEX:
            msel_r = aother;
            msel_g = aother;
            msel_b = aother;
            break;
        case CC_MSELECT_ALOCAL:
            msel_r = alocal;
            msel_g = alocal;
            msel_b = alocal;
            break;
        case CC_MSELECT_TEXRGB:
            msel_r = cother_r;
            msel_g = cother_g;
            msel_b = cother_b;
            break;
        default:
            fatal("Bad cc_mselect %i\n", cc_mselect);
            msel_r = 0;
            msel_g = 0;
            msel_b = 0;
            break;
    }

    switch (cca_mselect) {
        case CCA_MSELECT_ZERO:
            msel_a = 0;
            break;
        case CCA_MSELECT_ALOCAL:
        case CCA_MSELECT_ALOCAL2:
            msel_a = alocal;
            break;
        case CCA_MSELECT_AOTHER:
        case CCA_MSELECT_TEX:
            msel_a = aother;
            break;
        default:
            fatal("Bad cca_mselect %i\n", cca_mselect);
            msel_a = 0;
            break;
    }

    if (!cc_reverse_blend) {
        msel_r ^= 0xff;
        msel_g ^= 0xff;
        msel_b ^= 0xff;
    }
    msel_r++;
    msel_g++;
    msel_b++;

    if (!cca_reverse_blend)
        msel_a ^= 0xff;
    msel_a++;

    src_r = (src_r * msel_r) >> 8;
    src_g = (src_g * msel_g) >> 8;
    src_b = (src_b * msel_b) >> 8;
    src_a = (src_a * msel_a) >> 8;

    switch (cc_add) {
        case CC_ADD_CLOCAL:
            src_r += clocal_r;
            src_g += clocal_g;
            src_b += clocal_b;
            break;
        case CC_ADD_ALOCAL:
            src_r += alocal;
            src_g += alocal;
            src_b += alocal;
            break;
        case 0:
            break;
        default:
            fatal("Bad cc_add %i\n", cc_add);
    }

    if (cca_add)
        src_a += alocal;

    write_data->r = CLAMP(src_r);
    write_data->g = CLAMP(src_g);
    write_data->b = CLAMP(src_b);
    write_data->a = CLAMP(src_a);

    if (cc_invert_output) {
        write_data->r ^= 0xff;
        write_data->g ^= 0xff;
        write_data->b ^= 0xff;
    }
    if (cca_invert_output)
        write_data->a ^= 0xff;
}

void
voodoo_fb_writew(uint32_t addr, uint16_t val, void *priv)
{
    voodoo_t              *voodoo = (voodoo_t *) priv;
    const voodoo_params_t *params = &voodoo->params;
    int                    x;
    int                    y;
    uint32_t               write_addr;
    uint32_t               write_addr_aux;
    uint32_t               lfb_addr = addr;
    rgba8_t                colour_data;
    uint16_t               depth_data;
    uint8_t                alpha_data;
    int                    write_mask = 0;

    colour_data.r = colour_data.g = colour_data.b = colour_data.a = 0;

    depth_data = voodoo->params.zaColor & 0xffff;
    alpha_data = voodoo->params.zaColor >> 24;

#if 0
    while (!RB_EMPTY)
        thread_reset_event(voodoo->not_full_event);
#endif

#if 0
    voodoo_fb_log("voodoo_fb_writew : %08X %04X\n", addr, val);
#endif

    switch (voodoo->lfbMode & LFB_FORMAT_MASK) {
        case LFB_FORMAT_RGB565:
            colour_data = lfb_decode_565(voodoo, val);
            alpha_data  = 0xff;
            write_mask  = LFB_WRITE_COLOUR;
            break;
        case LFB_FORMAT_RGB555:
            colour_data = lfb_decode_1555(voodoo, val);
            alpha_data  = 0xff;
            write_mask  = LFB_WRITE_COLOUR;
            break;
        case LFB_FORMAT_ARGB1555:
            colour_data = lfb_decode_1555(voodoo, val);
            alpha_data  = colour_data.a;
            write_mask  = LFB_WRITE_COLOUR;
            break;
        case LFB_FORMAT_DEPTH:
            depth_data = val;
            write_mask = LFB_WRITE_DEPTH;
            break;

        case LFB_FORMAT_DEPTH_RGB565:
            lfb_addr >>= 1;
            if (lfb_addr & 1) {
                depth_data = val;
                write_mask = LFB_WRITE_DEPTH;
            } else {
                colour_data = lfb_decode_565(voodoo, val);
                write_mask  = LFB_WRITE_COLOUR;
            }
            break;
        case LFB_FORMAT_DEPTH_RGB555:
            lfb_addr >>= 1;
            if (lfb_addr & 1) {
                depth_data = val;
                write_mask = LFB_WRITE_DEPTH;
            } else {
                colour_data = lfb_decode_1555(voodoo, val);
                alpha_data  = 0xff;
                write_mask  = LFB_WRITE_COLOUR;
            }
            break;
        case LFB_FORMAT_DEPTH_ARGB1555:
            lfb_addr >>= 1;
            if (lfb_addr & 1) {
                depth_data = val;
                write_mask = LFB_WRITE_DEPTH;
            } else {
                colour_data = lfb_decode_1555(voodoo, val);
                alpha_data  = colour_data.a;
                write_mask  = LFB_WRITE_COLOUR;
            }
            break;

        default:
            fatal("voodoo_fb_writew : bad LFB format %08X\n", voodoo->lfbMode);
    }

    if (voodoo->type >= VOODOO_BANSHEE) {
        x = lfb_addr & 0xffe;
        y = (lfb_addr >> 12) & 0x3ff;
    } else {
        x = lfb_addr & 0x7fe;
        y = (lfb_addr >> 11) & 0x3ff;
    }

    y = lfb_y_origin_flip(voodoo, y);
    if (y < 0)
        return;

    if (SLI_ENABLED) {
        if ((!(voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && (y & 1)) || ((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && !(y & 1)))
            return;
        y >>= 1;
    }

    if (voodoo->fb_write_offset == voodoo->params.front_offset && y < 2048)
        voodoo->dirty_line[y] = 1;

    if (voodoo->col_tiled)
        write_addr = voodoo->fb_write_offset + (x & 127) + (x >> 7) * 128 * 32 + (y & 31) * 128 + (y >> 5) * voodoo->row_width;
    else
        write_addr = voodoo->fb_write_offset + x + (y * voodoo->row_width);
    if (voodoo->aux_tiled)
        write_addr_aux = voodoo->params.aux_offset + (x & 127) + (x >> 7) * 128 * 32 + (y & 31) * 128 + (y >> 5) * voodoo->row_width;
    else
        write_addr_aux = voodoo->params.aux_offset + x + (y * voodoo->row_width);

    //        voodoo_fb_log("fb_writew %08x %i %i %i %08x\n", addr, x, y, voodoo->row_width, write_addr);

    if (voodoo->lfbMode & 0x100) {
        {
            rgba8_t  write_data = colour_data;
            uint16_t new_depth  = depth_data;
            int      colbfog_r  = 0;
            int      colbfog_g  = 0;
            int      colbfog_b  = 0;

            write_data.a = alpha_data;

            if (params->fbzMode & FBZ_STIPPLE) {
                if (params->fbzMode & FBZ_STIPPLE_PATT) {
                    int index = ((y & 3) << 3) | (~x & 7);
                    if (!(params->stipple & (1 << index)))
                        goto skip_pixel;
                } else {
                    voodoo->params.stipple = (voodoo->params.stipple << 1) | (voodoo->params.stipple >> 31);
                    if (!(voodoo->params.stipple & 0x80000000)) {
                        goto skip_pixel;
                    }
                }
            }

            if (params->fbzMode & FBZ_DEPTH_ENABLE) {
                uint16_t old_depth = *(uint16_t *) (&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]);

                DEPTH_TEST(new_depth);
            }

            if ((params->fbzMode & FBZ_CHROMAKEY) && write_data.r == params->chromaKey_r && write_data.g == params->chromaKey_g && write_data.b == params->chromaKey_b)
                goto skip_pixel;

            voodoo_fb_cca_process(params, &write_data, new_depth);

            colbfog_r = write_data.r;
            colbfog_g = write_data.g;
            colbfog_b = write_data.b;

            if (params->fogMode & FOG_ENABLE) {
                int32_t z       = new_depth << 12;
                int64_t w_depth = (int64_t) (int32_t) new_depth;
                int32_t ia      = write_data.a << 12;

                APPLY_FOG(write_data.r, write_data.g, write_data.b, z, ia, w_depth);
            }

            if (params->alphaMode & 1)
                ALPHA_TEST(write_data.a);

            if (params->alphaMode & (1 << 4)) {
                uint16_t dat = *(uint16_t *) (&voodoo->fb_mem[write_addr & voodoo->fb_mask]);
                int      dest_r;
                int      dest_g;
                int      dest_b;
                int      dest_a;
                int      src_r = write_data.r;
                int      src_g = write_data.g;
                int      src_b = write_data.b;

                dest_r = (dat >> 8) & 0xf8;
                dest_g = (dat >> 3) & 0xfc;
                dest_b = (dat << 3) & 0xf8;
                dest_r |= (dest_r >> 5);
                dest_g |= (dest_g >> 6);
                dest_b |= (dest_b >> 5);
                dest_a = 0xff;

                ALPHA_BLEND(src_r, src_g, src_b, write_data.a);
                write_data.r = src_r;
                write_data.g = src_g;
                write_data.b = src_b;
            }

            if (params->fbzMode & FBZ_RGB_WMASK)
                *(uint16_t *) (&voodoo->fb_mem[write_addr & voodoo->fb_mask]) = do_dither(&voodoo->params, write_data, x >> 1, y);
            if (params->fbzMode & FBZ_DEPTH_WMASK)
                *(uint16_t *) (&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]) = new_depth;

skip_pixel:
            return;
        }
    } else {
        if (write_mask & LFB_WRITE_COLOUR)
            *(uint16_t *) (&voodoo->fb_mem[write_addr & voodoo->fb_mask]) = do_dither(&voodoo->params, colour_data, x >> 1, y);
        if (write_mask & LFB_WRITE_DEPTH)
            *(uint16_t *) (&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]) = depth_data;
    }
}

void
voodoo_fb_writel(uint32_t addr, uint32_t val, void *priv)
{
    voodoo_t              *voodoo = (voodoo_t *) priv;
    const voodoo_params_t *params = &voodoo->params;
    int                    x;
    int                    y;
    uint32_t               write_addr;
    uint32_t               write_addr_aux;
    rgba8_t                colour_data[2];
    uint16_t               depth_data[2];
    uint8_t                alpha_data[2];
    int                    write_mask = 0;
    int                    count = 1;

    depth_data[0] = depth_data[1] = voodoo->params.zaColor & 0xffff;
    alpha_data[0] = alpha_data[1] = voodoo->params.zaColor >> 24;
#if 0
    while (!RB_EMPTY)
        thread_reset_event(voodoo->not_full_event);
#endif

#if 0
    voodoo_fb_log("voodoo_fb_writel : %08X %08X\n", addr, val);
#endif

    switch (voodoo->lfbMode & LFB_FORMAT_MASK) {
        case LFB_FORMAT_RGB565:
            colour_data[0] = lfb_decode_565(voodoo, val & 0xffff);
            colour_data[1] = lfb_decode_565(voodoo, val >> 16);
            write_mask     = LFB_WRITE_COLOUR;
            count          = 2;
            break;
        case LFB_FORMAT_RGB555:
            colour_data[0] = lfb_decode_1555(voodoo, val & 0xffff);
            colour_data[1] = lfb_decode_1555(voodoo, val >> 16);
            write_mask     = LFB_WRITE_COLOUR;
            count          = 2;
            break;
        case LFB_FORMAT_ARGB1555:
            colour_data[0] = lfb_decode_1555(voodoo, val & 0xffff);
            alpha_data[0]  = colour_data[0].a;
            colour_data[1] = lfb_decode_1555(voodoo, val >> 16);
            alpha_data[1]  = colour_data[1].a;
            write_mask     = LFB_WRITE_COLOUR;
            count          = 2;
            break;

        case LFB_FORMAT_ARGB8888:
            lfb_decode_8888(voodoo, val, &colour_data[0], &alpha_data[0]);
            write_mask = LFB_WRITE_COLOUR;
            addr >>= 1;
            break;

        case LFB_FORMAT_XRGB8888: {
            uint8_t unused_alpha;

            lfb_decode_8888(voodoo, val, &colour_data[0], &unused_alpha);
            write_mask = LFB_WRITE_COLOUR;
            addr >>= 1;
            break;
        }

        case LFB_FORMAT_DEPTH_RGB565:
            colour_data[0] = lfb_decode_565(voodoo, val & 0xffff);
            depth_data[0]  = val >> 16;
            write_mask     = LFB_WRITE_BOTH;
            count          = 1;
            addr >>= 1;
            break;
        case LFB_FORMAT_DEPTH_RGB555:
            colour_data[0] = lfb_decode_1555(voodoo, val & 0xffff);
            depth_data[0]  = val >> 16;
            write_mask     = LFB_WRITE_BOTH;
            count          = 1;
            addr >>= 1;
            break;
        case LFB_FORMAT_DEPTH_ARGB1555:
            colour_data[0] = lfb_decode_1555(voodoo, val & 0xffff);
            alpha_data[0]  = colour_data[0].a;
            depth_data[0]  = val >> 16;
            write_mask     = LFB_WRITE_BOTH;
            count          = 1;
            addr >>= 1;
            break;

        case LFB_FORMAT_DEPTH:
            depth_data[0] = val;
            depth_data[1] = val >> 16;
            write_mask    = LFB_WRITE_DEPTH;
            count         = 2;
            break;

        default:
            fatal("voodoo_fb_writel : bad LFB format %08X\n", voodoo->lfbMode);
    }

    if (voodoo->type >= VOODOO_BANSHEE) {
        x = addr & 0xffe;
        y = (addr >> 12) & 0x3ff;
    } else {
        x = addr & 0x7fe;
        y = (addr >> 11) & 0x3ff;
    }

    y = lfb_y_origin_flip(voodoo, y);
    if (y < 0)
        return;

    if (SLI_ENABLED) {
        if ((!(voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && (y & 1)) || ((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && !(y & 1)))
            return;
        y >>= 1;
    }

    if (voodoo->fb_write_offset == voodoo->params.front_offset && y < 2048)
        voodoo->dirty_line[y] = 1;

    if (voodoo->col_tiled)
        write_addr = voodoo->fb_write_offset + (x & 127) + (x >> 7) * 128 * 32 + (y & 31) * 128 + (y >> 5) * voodoo->row_width;
    else
        write_addr = voodoo->fb_write_offset + x + (y * voodoo->row_width);
    if (voodoo->aux_tiled)
        write_addr_aux = voodoo->params.aux_offset + (x & 127) + (x >> 7) * 128 * 32 + (y & 31) * 128 + (y >> 5) * voodoo->row_width;
    else
        write_addr_aux = voodoo->params.aux_offset + x + (y * voodoo->row_width);

#if 0
    voodoo_fb_log("fb_writel %08x x=%i y=%i rw=%i %08x wo=%08x\n", addr, x, y, voodoo->row_width, write_addr, voodoo->fb_write_offset);
#endif

    if (voodoo->lfbMode & 0x100) {
        for (int c = 0; c < count; c++) {
            rgba8_t  write_data = colour_data[c];
            uint16_t new_depth  = depth_data[c];
            int      colbfog_r  = 0;
            int      colbfog_g  = 0;
            int      colbfog_b  = 0;

            write_data.a = alpha_data[c];

            if (params->fbzMode & FBZ_STIPPLE) {
                if (params->fbzMode & FBZ_STIPPLE_PATT) {
                    int index = ((y & 3) << 3) | (~(x + c) & 7);
                    if (!(params->stipple & (1 << index)))
                        goto skip_pixel;
                } else {
                    voodoo->params.stipple = (voodoo->params.stipple << 1) | (voodoo->params.stipple >> 31);
                    if (!(voodoo->params.stipple & 0x80000000)) {
                        goto skip_pixel;
                    }
                }
            }

            if (params->fbzMode & FBZ_DEPTH_ENABLE) {
                uint16_t old_depth = *(uint16_t *) (&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]);

                DEPTH_TEST(new_depth);
            }

            if ((params->fbzMode & FBZ_CHROMAKEY) && write_data.r == params->chromaKey_r && write_data.g == params->chromaKey_g && write_data.b == params->chromaKey_b)
                goto skip_pixel;

            voodoo_fb_cca_process(params, &write_data, new_depth);

            colbfog_r = write_data.r;
            colbfog_g = write_data.g;
            colbfog_b = write_data.b;

            if (params->fogMode & FOG_ENABLE) {
                int32_t z       = new_depth << 12;
                int64_t w_depth = new_depth;
                int32_t ia      = write_data.a << 12;

                APPLY_FOG(write_data.r, write_data.g, write_data.b, z, ia, w_depth);
            }

            if (params->alphaMode & 1)
                ALPHA_TEST(write_data.a);

            if (params->alphaMode & (1 << 4)) {
                uint16_t dat = *(uint16_t *) (&voodoo->fb_mem[write_addr & voodoo->fb_mask]);
                int      dest_r;
                int      dest_g;
                int      dest_b;
                int      dest_a;
                int      src_r = write_data.r;
                int      src_g = write_data.g;
                int      src_b = write_data.b;

                dest_r = (dat >> 8) & 0xf8;
                dest_g = (dat >> 3) & 0xfc;
                dest_b = (dat << 3) & 0xf8;
                dest_r |= (dest_r >> 5);
                dest_g |= (dest_g >> 6);
                dest_b |= (dest_b >> 5);
                dest_a = 0xff;

                ALPHA_BLEND(src_r, src_g, src_b, write_data.a);
                write_data.r = src_r;
                write_data.g = src_g;
                write_data.b = src_b;
            }

            if (params->fbzMode & FBZ_RGB_WMASK)
                *(uint16_t *) (&voodoo->fb_mem[write_addr & voodoo->fb_mask]) = do_dither(&voodoo->params, write_data, (x >> 1) + c, y);
            if (params->fbzMode & FBZ_DEPTH_WMASK)
                *(uint16_t *) (&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]) = new_depth;

skip_pixel:
            write_addr += 2;
            write_addr_aux += 2;
        }
    } else {
        for (int c = 0; c < count; c++) {
            if (write_mask & LFB_WRITE_COLOUR)
                *(uint16_t *) (&voodoo->fb_mem[write_addr & voodoo->fb_mask]) =
                              do_dither(&voodoo->params, colour_data[c], (x >> 1) + c, y);
            if (write_mask & LFB_WRITE_DEPTH)
                *(uint16_t *) (&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]) = depth_data[c];
            if (write_mask & LFB_WRITE_BOTH) {
                *(uint16_t *) (&voodoo->fb_mem[write_addr & voodoo->fb_mask]) =
                              do_dither(&voodoo->params, colour_data[c], (x >> 1) + c, y);
                *(uint16_t *) (&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]) = depth_data[c];
            }

            write_addr += 2;
            write_addr_aux += 2;
        }
    }
}
