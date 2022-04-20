/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		3DFX Voodoo emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2008-2020 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_voodoo_common.h>
#include <86box/vid_voodoo_blitter.h>
#include <86box/vid_voodoo_dither.h>
#include <86box/vid_voodoo_regs.h>
#include <86box/vid_voodoo_render.h>


enum
{
        BLIT_COMMAND_SCREEN_TO_SCREEN = 0,
        BLIT_COMMAND_CPU_TO_SCREEN = 1,
        BLIT_COMMAND_RECT_FILL = 2,
        BLIT_COMMAND_SGRAM_FILL = 3
};

enum
{
        BLIT_SRC_1BPP             = (0 << 3),
        BLIT_SRC_1BPP_BYTE_PACKED = (1 << 3),
        BLIT_SRC_16BPP            = (2 << 3),
        BLIT_SRC_24BPP            = (3 << 3),
        BLIT_SRC_24BPP_DITHER_2X2 = (4 << 3),
        BLIT_SRC_24BPP_DITHER_4X4 = (5 << 3)
};

enum
{
        BLIT_SRC_RGB_ARGB = (0 << 6),
        BLIT_SRC_RGB_ABGR = (1 << 6),
        BLIT_SRC_RGB_RGBA = (2 << 6),
        BLIT_SRC_RGB_BGRA = (3 << 6)
};

enum
{
        BLIT_COMMAND_MASK = 7,
        BLIT_SRC_FORMAT = (7 << 3),
        BLIT_SRC_RGB_FORMAT = (3 << 6),
        BLIT_SRC_CHROMA = (1 << 10),
        BLIT_DST_CHROMA = (1 << 12),
        BLIT_CLIPPING_ENABLED = (1 << 16)
};

enum
{
        BLIT_ROP_DST_PASS = (1 << 0),
        BLIT_ROP_SRC_PASS = (1 << 1)
};


#ifdef ENABLE_VOODOOBLT_LOG
int voodooblt_do_log = ENABLE_VOODOOBLT_LOG;


static void
voodooblt_log(const char *fmt, ...)
{
    va_list ap;

    if (voodooblt_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define voodooblt_log(fmt, ...)
#endif


#define MIX(src_dat, dst_dat, rop) \
        switch (rop)                                                    \
        {                                                               \
                case 0x0: dst_dat = 0; break;                           \
                case 0x1: dst_dat = ~(src_dat | dst_dat); break;        \
                case 0x2: dst_dat = ~src_dat & dst_dat; break;          \
                case 0x3: dst_dat = ~src_dat; break;                    \
                case 0x4: dst_dat = src_dat & ~dst_dat; break;          \
                case 0x5: dst_dat = ~dst_dat; break;                    \
                case 0x6: dst_dat = src_dat ^ dst_dat; break;           \
                case 0x7: dst_dat = ~(src_dat & dst_dat); break;        \
                case 0x8: dst_dat = src_dat & dst_dat; break;           \
                case 0x9: dst_dat = ~(src_dat ^ dst_dat); break;        \
                case 0xa: dst_dat = dst_dat; break;                     \
                case 0xb: dst_dat = ~src_dat | dst_dat; break;          \
                case 0xc: dst_dat = src_dat; break;                     \
                case 0xd: dst_dat = src_dat | ~dst_dat; break;          \
                case 0xe: dst_dat = src_dat | dst_dat; break;           \
                case 0xf: dst_dat = 0xffff; break;                      \
        }

void voodoo_v2_blit_start(voodoo_t *voodoo)
{
        uint64_t dat64;
        int size_x = ABS(voodoo->bltSizeX), size_y = ABS(voodoo->bltSizeY);
        int x_dir = (voodoo->bltSizeX > 0) ? 1 : -1;
        int y_dir = (voodoo->bltSizeY > 0) ? 1 : -1;
        int dst_x;
        int src_y = voodoo->bltSrcY & 0x7ff, dst_y = voodoo->bltDstY & 0x7ff;
        int src_stride = (voodoo->bltCommand & BLTCMD_SRC_TILED) ? ((voodoo->bltSrcXYStride & 0x3f) * 32*2) : (voodoo->bltSrcXYStride & 0xff8);
        int dst_stride = (voodoo->bltCommand & BLTCMD_DST_TILED) ? ((voodoo->bltDstXYStride & 0x3f) * 32*2) : (voodoo->bltDstXYStride & 0xff8);
        uint32_t src_base_addr = (voodoo->bltCommand & BLTCMD_SRC_TILED) ? ((voodoo->bltSrcBaseAddr & 0x3ff) << 12) : (voodoo->bltSrcBaseAddr & 0x3ffff8);
        uint32_t dst_base_addr = (voodoo->bltCommand & BLTCMD_DST_TILED) ? ((voodoo->bltDstBaseAddr & 0x3ff) << 12) : (voodoo->bltDstBaseAddr & 0x3ffff8);
        int x, y;

/*        voodooblt_log("blit_start: command=%08x srcX=%i srcY=%i dstX=%i dstY=%i sizeX=%i sizeY=%i color=%04x,%04x\n",
                voodoo->bltCommand, voodoo->bltSrcX, voodoo->bltSrcY, voodoo->bltDstX, voodoo->bltDstY, voodoo->bltSizeX, voodoo->bltSizeY, voodoo->bltColorFg, voodoo->bltColorBg);*/

        voodoo_wait_for_render_thread_idle(voodoo);

        switch (voodoo->bltCommand & BLIT_COMMAND_MASK)
        {
                case BLIT_COMMAND_SCREEN_TO_SCREEN:
                for (y = 0; y <= size_y; y++)
                {
                        uint16_t *src = (uint16_t *)&voodoo->fb_mem[src_base_addr + src_y*src_stride];
                        uint16_t *dst = (uint16_t *)&voodoo->fb_mem[dst_base_addr + dst_y*dst_stride];
                        int src_x = voodoo->bltSrcX, dst_x = voodoo->bltDstX;

                        for (x = 0; x <= size_x; x++)
                        {
                                uint16_t src_dat = src[src_x];
                                uint16_t dst_dat = dst[dst_x];
                                int rop = 0;

                                if (voodoo->bltCommand & BLIT_CLIPPING_ENABLED)
                                {
                                        if (dst_x < voodoo->bltClipLeft || dst_x >= voodoo->bltClipRight ||
                                            dst_y < voodoo->bltClipLowY || dst_y >= voodoo->bltClipHighY)
                                                goto skip_pixel_blit;
                                }

                                if (voodoo->bltCommand & BLIT_SRC_CHROMA)
                                {
                                        int r = (src_dat >> 11);
                                        int g = (src_dat >> 5) & 0x3f;
                                        int b = src_dat & 0x1f;

                                        if (r >= voodoo->bltSrcChromaMinR && r <= voodoo->bltSrcChromaMaxR &&
                                            g >= voodoo->bltSrcChromaMinG && g <= voodoo->bltSrcChromaMaxG &&
                                            b >= voodoo->bltSrcChromaMinB && b <= voodoo->bltSrcChromaMaxB)
                                                rop |= BLIT_ROP_SRC_PASS;
                                }
                                if (voodoo->bltCommand & BLIT_DST_CHROMA)
                                {
                                        int r = (dst_dat >> 11);
                                        int g = (dst_dat >> 5) & 0x3f;
                                        int b = dst_dat & 0x1f;

                                        if (r >= voodoo->bltDstChromaMinR && r <= voodoo->bltDstChromaMaxR &&
                                            g >= voodoo->bltDstChromaMinG && g <= voodoo->bltDstChromaMaxG &&
                                            b >= voodoo->bltDstChromaMinB && b <= voodoo->bltDstChromaMaxB)
                                                rop |= BLIT_ROP_DST_PASS;
                                }

                                MIX(src_dat, dst_dat, voodoo->bltRop[rop]);

                                dst[dst_x] = dst_dat;
skip_pixel_blit:
                                src_x += x_dir;
                                dst_x += x_dir;
                        }

                        src_y += y_dir;
                        dst_y += y_dir;
                }
                break;

                case BLIT_COMMAND_CPU_TO_SCREEN:
                voodoo->blt.dst_x = voodoo->bltDstX;
                voodoo->blt.dst_y = voodoo->bltDstY;
                voodoo->blt.cur_x = 0;
                voodoo->blt.size_x = size_x;
                voodoo->blt.size_y = size_y;
                voodoo->blt.x_dir = x_dir;
                voodoo->blt.y_dir = y_dir;
                voodoo->blt.dst_stride = (voodoo->bltCommand & BLTCMD_DST_TILED) ? ((voodoo->bltDstXYStride & 0x3f) * 32*2) : (voodoo->bltDstXYStride & 0xff8);
                break;

                case BLIT_COMMAND_RECT_FILL:
                for (y = 0; y <= size_y; y++)
                {
                        uint16_t *dst;
                        int dst_x = voodoo->bltDstX;

                        if (SLI_ENABLED)
                        {
                                if ((!(voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && (voodoo->blt.dst_y & 1)) ||
                                    ((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && !(voodoo->blt.dst_y & 1)))
                                        goto skip_line_fill;
                                dst = (uint16_t *)&voodoo->fb_mem[dst_base_addr + (dst_y >> 1) * dst_stride];
                        }
                        else
                                dst = (uint16_t *)&voodoo->fb_mem[dst_base_addr + dst_y*dst_stride];

                        for (x = 0; x <= size_x; x++)
                        {
                                if (voodoo->bltCommand & BLIT_CLIPPING_ENABLED)
                                {
                                        if (dst_x < voodoo->bltClipLeft || dst_x >= voodoo->bltClipRight ||
                                            dst_y < voodoo->bltClipLowY || dst_y >= voodoo->bltClipHighY)
                                                goto skip_pixel_fill;
                                }

                                dst[dst_x] = voodoo->bltColorFg;
skip_pixel_fill:
                                dst_x += x_dir;
                        }
skip_line_fill:
                        dst_y += y_dir;
                }
                break;

                case BLIT_COMMAND_SGRAM_FILL:
                /*32x32 tiles - 2kb*/
                dst_y = voodoo->bltDstY & 0x3ff;
                size_x = voodoo->bltSizeX & 0x1ff; //512*8 = 4kb
                size_y = voodoo->bltSizeY & 0x3ff;

                dat64 = voodoo->bltColorFg | ((uint64_t)voodoo->bltColorFg << 16) |
                        ((uint64_t)voodoo->bltColorFg << 32) | ((uint64_t)voodoo->bltColorFg << 48);

                for (y = 0; y <= size_y; y++)
                {
                        uint64_t *dst;

                        /*This may be wrong*/
                        if (!y)
                        {
                                dst_x = voodoo->bltDstX & 0x1ff;
                                size_x = 511 - dst_x;
                        }
                        else if (y < size_y)
                        {
                                dst_x = 0;
                                size_x = 511;
                        }
                        else
                        {
                                dst_x = 0;
                                size_x = voodoo->bltSizeX & 0x1ff;
                        }

                        dst = (uint64_t *)&voodoo->fb_mem[(dst_y*512*8 + dst_x*8) & voodoo->fb_mask];

                        for (x = 0; x <= size_x; x++)
                                dst[x] = dat64;

                        dst_y++;
                }
                break;

                default:
                fatal("bad blit command %08x\n", voodoo->bltCommand);
        }
}

void voodoo_v2_blit_data(voodoo_t *voodoo, uint32_t data)
{
        int src_bits = 32;
        uint32_t base_addr = (voodoo->bltCommand & BLTCMD_DST_TILED) ? ((voodoo->bltDstBaseAddr & 0x3ff) << 12) : (voodoo->bltDstBaseAddr & 0x3ffff8);
        uint32_t addr;
        uint16_t *dst;

        if ((voodoo->bltCommand & BLIT_COMMAND_MASK) != BLIT_COMMAND_CPU_TO_SCREEN)
                return;

        if (SLI_ENABLED)
        {
                addr = base_addr + (voodoo->blt.dst_y >> 1) * voodoo->blt.dst_stride;
                dst = (uint16_t *)&voodoo->fb_mem[addr];
        }
        else
        {
                addr = base_addr + voodoo->blt.dst_y*voodoo->blt.dst_stride;
                dst = (uint16_t *)&voodoo->fb_mem[addr];
        }

        if (addr >= voodoo->front_offset && voodoo->row_width)
        {
                int y = (addr - voodoo->front_offset) / voodoo->row_width;
                if (y < voodoo->v_disp)
                        voodoo->dirty_line[y] = 2;
        }

        while (src_bits && voodoo->blt.cur_x <= voodoo->blt.size_x)
        {
                int r = 0, g = 0, b = 0;
                uint16_t src_dat = 0, dst_dat;
                int x = (voodoo->blt.x_dir > 0) ? (voodoo->blt.dst_x + voodoo->blt.cur_x) : (voodoo->blt.dst_x - voodoo->blt.cur_x);
                int rop = 0;

                switch (voodoo->bltCommand & BLIT_SRC_FORMAT)
                {
                        case BLIT_SRC_1BPP: case BLIT_SRC_1BPP_BYTE_PACKED:
                        src_dat = (data & 1) ? voodoo->bltColorFg : voodoo->bltColorBg;
                        data >>= 1;
                        src_bits--;
                        break;
                        case BLIT_SRC_16BPP:
                        switch (voodoo->bltCommand & BLIT_SRC_RGB_FORMAT)
                        {
                                case BLIT_SRC_RGB_ARGB: case BLIT_SRC_RGB_RGBA:
                                src_dat = data & 0xffff;
                                break;
                                case BLIT_SRC_RGB_ABGR: case BLIT_SRC_RGB_BGRA:
                                src_dat = ((data & 0xf800) >> 11) | (data & 0x07c0) | ((data & 0x0038) << 11);
                                break;
                        }
                        data >>= 16;
                        src_bits -= 16;
                        break;
                        case BLIT_SRC_24BPP: case BLIT_SRC_24BPP_DITHER_2X2: case BLIT_SRC_24BPP_DITHER_4X4:
                        switch (voodoo->bltCommand & BLIT_SRC_RGB_FORMAT)
                        {
                                case BLIT_SRC_RGB_ARGB:
                                r = (data >> 16) & 0xff;
                                g = (data >> 8) & 0xff;
                                b = data & 0xff;
                                break;
                                case BLIT_SRC_RGB_ABGR:
                                r = data & 0xff;
                                g = (data >> 8) & 0xff;
                                b = (data >> 16) & 0xff;
                                break;
                                case BLIT_SRC_RGB_RGBA:
                                r = (data >> 24) & 0xff;
                                g = (data >> 16) & 0xff;
                                b = (data >> 8) & 0xff;
                                break;
                                case BLIT_SRC_RGB_BGRA:
                                r = (data >> 8) & 0xff;
                                g = (data >> 16) & 0xff;
                                b = (data >> 24) & 0xff;
                                break;
                        }
                        switch (voodoo->bltCommand & BLIT_SRC_FORMAT)
                        {
                                case BLIT_SRC_24BPP:
                                src_dat = (b >> 3) | ((g & 0xfc) << 3) | ((r & 0xf8) << 8);
                                break;
                                case BLIT_SRC_24BPP_DITHER_2X2:
                                r = dither_rb2x2[r][voodoo->blt.dst_y & 1][x & 1];
                                g =  dither_g2x2[g][voodoo->blt.dst_y & 1][x & 1];
                                b = dither_rb2x2[b][voodoo->blt.dst_y & 1][x & 1];
                                src_dat = (b >> 3) | ((g & 0xfc) << 3) | ((r & 0xf8) << 8);
                                break;
                                case BLIT_SRC_24BPP_DITHER_4X4:
                                r = dither_rb[r][voodoo->blt.dst_y & 3][x & 3];
                                g =  dither_g[g][voodoo->blt.dst_y & 3][x & 3];
                                b = dither_rb[b][voodoo->blt.dst_y & 3][x & 3];
                                src_dat = (b >> 3) | ((g & 0xfc) << 3) | ((r & 0xf8) << 8);
                                break;
                        }
                        src_bits = 0;
                        break;
                }

                if (SLI_ENABLED)
                {
                        if ((!(voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && (voodoo->blt.dst_y & 1)) ||
                            ((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && !(voodoo->blt.dst_y & 1)))
                                goto skip_pixel;
                }

                if (voodoo->bltCommand & BLIT_CLIPPING_ENABLED)
                {
                        if (x < voodoo->bltClipLeft || x >= voodoo->bltClipRight ||
                            voodoo->blt.dst_y < voodoo->bltClipLowY || voodoo->blt.dst_y >= voodoo->bltClipHighY)
                                goto skip_pixel;
                }

                dst_dat = dst[x];

                if (voodoo->bltCommand & BLIT_SRC_CHROMA)
                {
                        r = (src_dat >> 11);
                        g = (src_dat >> 5) & 0x3f;
                        b = src_dat & 0x1f;

                        if (r >= voodoo->bltSrcChromaMinR && r <= voodoo->bltSrcChromaMaxR &&
                            g >= voodoo->bltSrcChromaMinG && g <= voodoo->bltSrcChromaMaxG &&
                            b >= voodoo->bltSrcChromaMinB && b <= voodoo->bltSrcChromaMaxB)
                                rop |= BLIT_ROP_SRC_PASS;
                }
                if (voodoo->bltCommand & BLIT_DST_CHROMA)
                {
                        r = (dst_dat >> 11);
                        g = (dst_dat >> 5) & 0x3f;
                        b = dst_dat & 0x1f;

                        if (r >= voodoo->bltDstChromaMinR && r <= voodoo->bltDstChromaMaxR &&
                            g >= voodoo->bltDstChromaMinG && g <= voodoo->bltDstChromaMaxG &&
                            b >= voodoo->bltDstChromaMinB && b <= voodoo->bltDstChromaMaxB)
                                rop |= BLIT_ROP_DST_PASS;
                }

                MIX(src_dat, dst_dat, voodoo->bltRop[rop]);

                dst[x] = dst_dat;

skip_pixel:
                voodoo->blt.cur_x++;
        }

        if (voodoo->blt.cur_x > voodoo->blt.size_x)
        {
                voodoo->blt.size_y--;
                if (voodoo->blt.size_y >= 0)
                {
                        voodoo->blt.cur_x = 0;
                        voodoo->blt.dst_y += voodoo->blt.y_dir;
                }
        }
}


void voodoo_fastfill(voodoo_t *voodoo, voodoo_params_t *params)
{
        int y;
        int low_y, high_y;

        if (params->fbzMode & (1 << 17))
        {
                int y_origin = (voodoo->type >= VOODOO_BANSHEE) ? (voodoo->y_origin_swap+1) : voodoo->v_disp;

                high_y = y_origin - params->clipLowY;
                low_y = y_origin - params->clipHighY;
        }
        else
        {
                low_y = params->clipLowY;
                high_y = params->clipHighY;
        }

        if (params->fbzMode & FBZ_RGB_WMASK)
        {
                int r, g, b;
                uint16_t col;

                r = ((params->color1 >> 16) >> 3) & 0x1f;
                g = ((params->color1 >> 8) >> 2) & 0x3f;
                b = (params->color1 >> 3) & 0x1f;
                col = b | (g << 5) | (r << 11);

                if (SLI_ENABLED)
                {
                        for (y = low_y; y < high_y; y += 2)
                        {
                                uint16_t *cbuf = (uint16_t *)&voodoo->fb_mem[(params->draw_offset + (y >> 1) * voodoo->row_width) & voodoo->fb_mask];
                                int x;

                                for (x = params->clipLeft; x < params->clipRight; x++)
                                        cbuf[x] = col;
                        }
                }
                else
                {
                        for (y = low_y; y < high_y; y++)
                        {
                                if (voodoo->col_tiled)
                                {
                                        uint16_t *cbuf = (uint16_t *)&voodoo->fb_mem[(params->draw_offset + (y >> 5) * voodoo->row_width + (y & 31) * 128) & voodoo->fb_mask];
                                        int x;

                                        for (x = params->clipLeft; x < params->clipRight; x++)
                                        {
                                                int x2 = (x & 63) | ((x >> 6) * 128*32/2);
                                                cbuf[x2] = col;
                                        }
                                }
                                else
                                {
                                        uint16_t *cbuf = (uint16_t *)&voodoo->fb_mem[(params->draw_offset + y * voodoo->row_width) & voodoo->fb_mask];
                                        int x;

                                        for (x = params->clipLeft; x < params->clipRight; x++)
                                                cbuf[x] = col;
                                }
                        }
                }
        }
        if (params->fbzMode & FBZ_DEPTH_WMASK)
        {
                if (SLI_ENABLED)
                {
                        for (y = low_y; y < high_y; y += 2)
                        {
                                uint16_t *abuf = (uint16_t *)&voodoo->fb_mem[(params->aux_offset + (y >> 1) * voodoo->row_width) & voodoo->fb_mask];
                                int x;

                                for (x = params->clipLeft; x < params->clipRight; x++)
                                        abuf[x] = params->zaColor & 0xffff;
                        }
                }
                else
                {
                        for (y = low_y; y < high_y; y++)
                        {
                                if (voodoo->aux_tiled)
                                {
                                        uint16_t *abuf = (uint16_t *)&voodoo->fb_mem[(params->aux_offset + (y >> 5) * voodoo->aux_row_width + (y & 31) * 128) & voodoo->fb_mask];
                                        int x;

                                        for (x = params->clipLeft; x < params->clipRight; x++)
                                        {
                                                int x2 = (x & 63) | ((x >> 6) * 128*32/2);
                                                abuf[x2] = params->zaColor & 0xffff;
                                        }
                                }
                                else
                                {
                                        uint16_t *abuf = (uint16_t *)&voodoo->fb_mem[(params->aux_offset + y * voodoo->aux_row_width) & voodoo->fb_mask];
                                        int x;

                                        for (x = params->clipLeft; x < params->clipRight; x++)
                                                abuf[x] = params->zaColor & 0xffff;
                                }
                        }
                }
        }
}
