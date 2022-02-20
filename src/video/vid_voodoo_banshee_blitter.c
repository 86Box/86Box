/*Current issues :
  - missing screen->screen scaled blits with format conversion
  - missing YUV blits
  - missing linestyle
  - missing wait for vsync
  - missing reversible lines

  Notes :
  - 16 bpp runs with tiled framebuffer - to aid 3D?
    8 and 32 bpp use linear
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
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_voodoo_common.h>
#include <86box/vid_voodoo_banshee_blitter.h>
#include <86box/vid_voodoo_render.h>

#define COMMAND_CMD_MASK                         (0xf)
#define COMMAND_CMD_NOP                          (0 << 0)
#define COMMAND_CMD_SCREEN_TO_SCREEN_BLT         (1 << 0)
#define COMMAND_CMD_SCREEN_TO_SCREEN_STRETCH_BLT (2 << 0)
#define COMMAND_CMD_HOST_TO_SCREEN_BLT           (3 << 0)
#define COMMAND_CMD_HOST_TO_SCREEN_STRETCH_BLT   (4 << 0)
#define COMMAND_CMD_RECTFILL                     (5 << 0)
#define COMMAND_CMD_LINE                         (6 << 0)
#define COMMAND_CMD_POLYLINE                     (7 << 0)
#define COMMAND_CMD_POLYFILL                     (8 << 0)
#define COMMAND_INITIATE        (1 << 8)
#define COMMAND_INC_X_START     (1 << 10)
#define COMMAND_INC_Y_START     (1 << 11)
#define COMMAND_STIPPLE_LINE    (1 << 12)
#define COMMAND_PATTERN_MONO    (1 << 13)
#define COMMAND_DX              (1 << 14)
#define COMMAND_DY              (1 << 15)
#define COMMAND_TRANS_MONO      (1 << 16)
#define COMMAND_PATOFF_X_MASK   (7 << 17)
#define COMMAND_PATOFF_X_SHIFT  (17)
#define COMMAND_PATOFF_Y_MASK   (7 << 20)
#define COMMAND_PATOFF_Y_SHIFT  (20)
#define COMMAND_CLIP_SEL        (1 << 23)

#define CMDEXTRA_SRC_COLORKEY   (1 << 0)
#define CMDEXTRA_DST_COLORKEY   (1 << 1)
#define CMDEXTRA_FORCE_PAT_ROW0 (1 << 3)

#define SRC_FORMAT_STRIDE_MASK (0x1fff)
#define SRC_FORMAT_COL_MASK    (0xf << 16)
#define SRC_FORMAT_COL_1_BPP   (0 << 16)
#define SRC_FORMAT_COL_8_BPP   (1 << 16)
#define SRC_FORMAT_COL_16_BPP  (3 << 16)
#define SRC_FORMAT_COL_24_BPP  (4 << 16)
#define SRC_FORMAT_COL_32_BPP  (5 << 16)
#define SRC_FORMAT_COL_YUYV    (8 << 16)
#define SRC_FORMAT_COL_UYVY    (9 << 16)
#define SRC_FORMAT_BYTE_SWIZZLE   (1 << 20)
#define SRC_FORMAT_WORD_SWIZZLE   (1 << 21)
#define SRC_FORMAT_PACKING_MASK   (3 << 22)
#define SRC_FORMAT_PACKING_STRIDE (0 << 22)
#define SRC_FORMAT_PACKING_BYTE   (1 << 22)
#define SRC_FORMAT_PACKING_WORD   (2 << 22)
#define SRC_FORMAT_PACKING_DWORD  (3 << 22)

#define DST_FORMAT_STRIDE_MASK (0x1fff)
#define DST_FORMAT_COL_MASK    (0xf << 16)
#define DST_FORMAT_COL_8_BPP   (1 << 16)
#define DST_FORMAT_COL_16_BPP  (3 << 16)
#define DST_FORMAT_COL_24_BPP  (4 << 16)
#define DST_FORMAT_COL_32_BPP  (5 << 16)

#define BRES_ERROR_MASK        (0xffff)
#define BRES_ERROR_USE         (1 << 31)

enum
{
        COLORKEY_8,
        COLORKEY_16,
        COLORKEY_32
};

#ifdef ENABLE_BANSHEEBLT_LOG
int bansheeblt_do_log = ENABLE_BANSHEEBLT_LOG;

static void
bansheeblt_log(const char *fmt, ...)
{
    va_list ap;

    if (bansheeblt_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define banshee_log(fmt, ...)
#endif

static int colorkey(voodoo_t *voodoo, uint32_t src, int src_notdst, int color_format)
{
        uint32_t min = src_notdst ? voodoo->banshee_blt.srcColorkeyMin : voodoo->banshee_blt.dstColorkeyMin;
        uint32_t max = src_notdst ? voodoo->banshee_blt.srcColorkeyMax : voodoo->banshee_blt.dstColorkeyMax;

        if (!(voodoo->banshee_blt.commandExtra & (src_notdst ? CMDEXTRA_SRC_COLORKEY : CMDEXTRA_DST_COLORKEY)))
                return 0;

        switch (color_format)
        {
                case COLORKEY_8:
                return ((src & 0xff) >= (min & 0xff)) && ((src & 0xff) <= (max & 0xff));

                case COLORKEY_16:
                {
                        int r = (src >> 11) & 0x1f, r_min = (min >> 11) & 0x1f, r_max = (max >> 11) & 0x1f;
                        int g = (src >>  5) & 0x3f, g_min = (min >>  5) & 0x3f, g_max = (max >>  5) & 0x3f;
                        int b = src & 0x1f, b_min = min & 0x1f, b_max = max & 0x1f;

                        return (r >= r_min) && (r <= r_max) && (g >= g_min) && (g <= g_max) &&
                                (b >= b_min) && (b <= b_max);
                }

                case COLORKEY_32:
                {
                        int r = (src >> 16) & 0xff, r_min = (min >> 16) & 0xff, r_max = (max >> 16) & 0xff;
                        int g = (src >>  8) & 0xff, g_min = (min >>  8) & 0xff, g_max = (max >>  8) & 0xff;
                        int b = src & 0xff, b_min = min & 0xff, b_max = max & 0xff;

                        return (r >= r_min) && (r <= r_max) && (g >= g_min) && (g <= g_max) &&
                                (b >= b_min) && (b <= b_max);
                }

                default:
                return 0;
        }
}

static uint32_t MIX(voodoo_t *voodoo, uint32_t dest, uint32_t src, uint32_t pattern, int colour_format_src, int colour_format_dest)
{
        int rop_nr = 0;
        uint32_t result = 0;
        uint32_t rop;

        if (colorkey(voodoo, src, 1, colour_format_src))
                rop_nr |= 2;
        if (colorkey(voodoo, dest, 0, colour_format_dest))
                rop_nr |= 1;

        rop = voodoo->banshee_blt.rops[rop_nr];

        if (rop & 0x01)
                result |= (~pattern & ~src & ~dest);
        if (rop & 0x02)
                result |= (~pattern & ~src &  dest);
        if (rop & 0x04)
                result |= (~pattern &  src & ~dest);
        if (rop & 0x08)
                result |= (~pattern &  src &  dest);
        if (rop & 0x10)
                result |= ( pattern & ~src & ~dest);
        if (rop & 0x20)
                result |= ( pattern & ~src &  dest);
        if (rop & 0x40)
                result |= ( pattern &  src & ~dest);
        if (rop & 0x80)
                result |= ( pattern &  src &  dest);

        return result;
}

static uint32_t get_addr(voodoo_t *voodoo, int x, int y, int src_notdst, uint32_t src_stride)
{
        uint32_t stride = src_notdst ? src_stride : voodoo->banshee_blt.dst_stride;
        uint32_t base_addr = src_notdst ? voodoo->banshee_blt.srcBaseAddr : voodoo->banshee_blt.dstBaseAddr;

        if (src_notdst ? voodoo->banshee_blt.srcBaseAddr_tiled : voodoo->banshee_blt.dstBaseAddr_tiled)
                return (base_addr + (x & 127) + ((x >> 7) * 128*32) + ((y & 31) * 128) + (y >> 5)*stride) & voodoo->fb_mask;
        else
                return (base_addr + x + y*stride) & voodoo->fb_mask;
}

static void PLOT(voodoo_t *voodoo, int x, int y, int pat_x, int pat_y, uint8_t pattern_mask, uint8_t rop, uint32_t src, int src_colorkey)
{
        switch (voodoo->banshee_blt.dstFormat & DST_FORMAT_COL_MASK)
        {
                case DST_FORMAT_COL_8_BPP:
                {
                        uint32_t addr = get_addr(voodoo, x, y, 0, 0);//(voodoo->banshee_blt.dstBaseAddr + x + y*voodoo->banshee_blt.dst_stride) & voodoo->fb_mask;
                        uint32_t dest = voodoo->vram[addr];
                        uint32_t pattern = (voodoo->banshee_blt.command & COMMAND_PATTERN_MONO) ?
                                        ((pattern_mask & (1 << (7-(pat_x & 7)))) ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack) :
                                        voodoo->banshee_blt.colorPattern8[(pat_x & 7) + (pat_y & 7)*8];

                        voodoo->vram[addr] = MIX(voodoo, dest, src, pattern, src_colorkey, COLORKEY_8);
                        voodoo->changedvram[addr >> 12] = changeframecount;
                        break;
                }
                case DST_FORMAT_COL_16_BPP:
                {
                        uint32_t addr = get_addr(voodoo, x*2, y, 0, 0);//(voodoo->banshee_blt.dstBaseAddr + x*2 + y*voodoo->banshee_blt.dst_stride)  & voodoo->fb_mask;
                        uint32_t dest = *(uint16_t *)&voodoo->vram[addr];
                        uint32_t pattern = (voodoo->banshee_blt.command & COMMAND_PATTERN_MONO) ?
                                        ((pattern_mask & (1 << (7-(pat_x & 7)))) ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack) :
                                        voodoo->banshee_blt.colorPattern16[(pat_x & 7) + (pat_y & 7)*8];

                        *(uint16_t *)&voodoo->vram[addr] = MIX(voodoo, dest, src, pattern, src_colorkey, COLORKEY_16);
                        voodoo->changedvram[addr >> 12] = changeframecount;
                        break;
                }
                case DST_FORMAT_COL_24_BPP:
                {
                        uint32_t addr = get_addr(voodoo, x*3, y, 0, 0);//(voodoo->banshee_blt.dstBaseAddr + x*3 + y*voodoo->banshee_blt.dst_stride)  & voodoo->fb_mask;
                        uint32_t dest = *(uint32_t *)&voodoo->vram[addr];
                        uint32_t pattern = (voodoo->banshee_blt.command & COMMAND_PATTERN_MONO) ?
                                        ((pattern_mask & (1 << (7-(pat_x & 7)))) ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack) :
                                        voodoo->banshee_blt.colorPattern24[(pat_x & 7) + (pat_y & 7)*8];

                        *(uint32_t *)&voodoo->vram[addr] = (MIX(voodoo, dest, src, pattern, src_colorkey, COLORKEY_32) & 0xffffff) | (dest & 0xff000000);
                        voodoo->changedvram[addr >> 12] = changeframecount;
                        break;
                }
                case DST_FORMAT_COL_32_BPP:
                {
                        uint32_t addr = get_addr(voodoo, x*4, y, 0, 0);//(voodoo->banshee_blt.dstBaseAddr + x*4 + y*voodoo->banshee_blt.dst_stride) & voodoo->fb_mask;
                        uint32_t dest = *(uint32_t *)&voodoo->vram[addr];
                        uint32_t pattern = (voodoo->banshee_blt.command & COMMAND_PATTERN_MONO) ?
                                        ((pattern_mask & (1 << (7-(pat_x & 7)))) ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack) :
                                        voodoo->banshee_blt.colorPattern[(pat_x & 7) + (pat_y & 7)*8];

                        *(uint32_t *)&voodoo->vram[addr] = MIX(voodoo, dest, src, pattern, src_colorkey, COLORKEY_32);
                        voodoo->changedvram[addr >> 12] = changeframecount;
                        break;
                }
        }
}

static void PLOT_LINE(voodoo_t *voodoo, int x, int y, uint8_t rop, uint32_t pattern, int src_colorkey)
{
        switch (voodoo->banshee_blt.dstFormat & DST_FORMAT_COL_MASK)
        {
                case DST_FORMAT_COL_8_BPP:
                {
                        uint32_t addr = get_addr(voodoo, x, y, 0, 0);//(voodoo->banshee_blt.dstBaseAddr + x + y*voodoo->banshee_blt.dst_stride) & voodoo->fb_mask;
                        uint32_t dest = voodoo->vram[addr];

                        voodoo->vram[addr] = MIX(voodoo, dest, voodoo->banshee_blt.colorFore, pattern, src_colorkey, COLORKEY_8);
                        voodoo->changedvram[addr >> 12] = changeframecount;
                        break;
                }
                case DST_FORMAT_COL_16_BPP:
                {
                        uint32_t addr = get_addr(voodoo, x*2, y, 0, 0);//(voodoo->banshee_blt.dstBaseAddr + x*2 + y*voodoo->banshee_blt.dst_stride)  & voodoo->fb_mask;
                        uint32_t dest = *(uint16_t *)&voodoo->vram[addr];

                        *(uint16_t *)&voodoo->vram[addr] = MIX(voodoo, dest, voodoo->banshee_blt.colorFore, pattern, src_colorkey, COLORKEY_16);
                        voodoo->changedvram[addr >> 12] = changeframecount;
                        break;
                }
                case DST_FORMAT_COL_24_BPP:
                {
                        uint32_t addr = get_addr(voodoo, x*3, y, 0, 0);//(voodoo->banshee_blt.dstBaseAddr + x*3 + y*voodoo->banshee_blt.dst_stride)  & voodoo->fb_mask;
                        uint32_t dest = *(uint32_t *)&voodoo->vram[addr];

                        *(uint32_t *)&voodoo->vram[addr] = (MIX(voodoo, dest, voodoo->banshee_blt.colorFore, pattern, src_colorkey, COLORKEY_32) & 0xffffff) | (dest & 0xff000000);
                        voodoo->changedvram[addr >> 12] = changeframecount;
                        break;
                }
                case DST_FORMAT_COL_32_BPP:
                {
                        uint32_t addr = get_addr(voodoo, x*4, y, 0, 0);//(voodoo->banshee_blt.dstBaseAddr + x*4 + y*voodoo->banshee_blt.dst_stride) & voodoo->fb_mask;
                        uint32_t dest = *(uint32_t *)&voodoo->vram[addr];

                        *(uint32_t *)&voodoo->vram[addr] = MIX(voodoo, dest, voodoo->banshee_blt.colorFore, pattern, src_colorkey, COLORKEY_32);
                        voodoo->changedvram[addr >> 12] = changeframecount;
                        break;
                }
        }
}


static void update_src_stride(voodoo_t *voodoo)
{
        int bpp;

        switch (voodoo->banshee_blt.srcFormat & SRC_FORMAT_COL_MASK)
        {
                case SRC_FORMAT_COL_1_BPP:
                bpp = 1;
                break;
                case SRC_FORMAT_COL_8_BPP:
                bpp = 8;
                break;
                case SRC_FORMAT_COL_16_BPP:
                bpp = 16;
                break;
                case SRC_FORMAT_COL_24_BPP:
                bpp = 24;
                break;
                case SRC_FORMAT_COL_32_BPP:
                bpp = 32;
                break;

                default:
                bpp = 16;
                break;
        }

        switch (voodoo->banshee_blt.srcFormat & SRC_FORMAT_PACKING_MASK)
        {
                case SRC_FORMAT_PACKING_STRIDE:
                voodoo->banshee_blt.src_stride_src = voodoo->banshee_blt.src_stride; //voodoo->banshee_blt.srcFormat & SRC_FORMAT_STRIDE_MASK;
                voodoo->banshee_blt.src_stride_dest = voodoo->banshee_blt.src_stride; //voodoo->banshee_blt.srcFormat & SRC_FORMAT_STRIDE_MASK;
                voodoo->banshee_blt.host_data_size_src = (voodoo->banshee_blt.srcSizeX * bpp + 7) >> 3;
                voodoo->banshee_blt.host_data_size_dest = (voodoo->banshee_blt.dstSizeX * bpp + 7) >> 3;
//                bansheeblt_log("Stride packing %08x %08x   bpp=%i dstSizeX=%i\n", voodoo->banshee_blt.src_stride_dest, voodoo->banshee_blt.host_data_size_dest, bpp, voodoo->banshee_blt.dstSizeX);
                break;

                case SRC_FORMAT_PACKING_BYTE:
                voodoo->banshee_blt.src_stride_src = (voodoo->banshee_blt.srcSizeX * bpp + 7) >> 3;
                voodoo->banshee_blt.src_stride_dest = (voodoo->banshee_blt.dstSizeX * bpp + 7) >> 3;
                voodoo->banshee_blt.host_data_size_src = voodoo->banshee_blt.src_stride_src;
                voodoo->banshee_blt.host_data_size_dest = voodoo->banshee_blt.src_stride_dest;
//                bansheeblt_log("Byte packing %08x %08x\n", voodoo->banshee_blt.src_stride_dest, voodoo->banshee_blt.host_data_size_dest);
                break;

                case SRC_FORMAT_PACKING_WORD:
                voodoo->banshee_blt.src_stride_src = ((voodoo->banshee_blt.srcSizeX * bpp + 15) >> 4) * 2;
                voodoo->banshee_blt.src_stride_dest = ((voodoo->banshee_blt.dstSizeX * bpp + 15) >> 4) * 2;
                voodoo->banshee_blt.host_data_size_src = voodoo->banshee_blt.src_stride_src;
                voodoo->banshee_blt.host_data_size_dest = voodoo->banshee_blt.src_stride_dest;
//                bansheeblt_log("Word packing %08x %08x\n", voodoo->banshee_blt.src_stride_dest, voodoo->banshee_blt.host_data_size_dest);
                break;

                case SRC_FORMAT_PACKING_DWORD:
                voodoo->banshee_blt.src_stride_src = ((voodoo->banshee_blt.srcSizeX * bpp + 31) >> 5) * 4;
                voodoo->banshee_blt.src_stride_dest = ((voodoo->banshee_blt.dstSizeX * bpp + 31) >> 5) * 4;
                voodoo->banshee_blt.host_data_size_src = voodoo->banshee_blt.src_stride_src;
                voodoo->banshee_blt.host_data_size_dest = voodoo->banshee_blt.src_stride_dest;
//                bansheeblt_log("Dword packing %08x %08x\n", voodoo->banshee_blt.src_stride_dest, voodoo->banshee_blt.host_data_size_dest);
                break;
        }
}

static void end_command(voodoo_t *voodoo)
{
        /*Update dest coordinates if required*/
        if (voodoo->banshee_blt.command & COMMAND_INC_X_START)
        {
                voodoo->banshee_blt.dstXY &= ~0x0000ffff;
                voodoo->banshee_blt.dstXY |= (voodoo->banshee_blt.dstX & 0xffff);
        }

        if (voodoo->banshee_blt.command & COMMAND_INC_Y_START)
        {
                voodoo->banshee_blt.dstXY &= ~0xffff0000;
                voodoo->banshee_blt.dstXY |= (voodoo->banshee_blt.dstY << 16);
        }
}

static void banshee_do_rectfill(voodoo_t *voodoo)
{
        clip_t *clip = &voodoo->banshee_blt.clip[(voodoo->banshee_blt.command & COMMAND_CLIP_SEL) ? 1 : 0];
        int dst_y = voodoo->banshee_blt.dstY;
        uint8_t *pattern_mono = (uint8_t *)voodoo->banshee_blt.colorPattern;
        int pat_y = (voodoo->banshee_blt.commandExtra & CMDEXTRA_FORCE_PAT_ROW0) ? 0 : (voodoo->banshee_blt.patoff_y + voodoo->banshee_blt.dstY);
        int use_pattern_trans = (voodoo->banshee_blt.command & (COMMAND_PATTERN_MONO | COMMAND_TRANS_MONO)) ==
                                             (COMMAND_PATTERN_MONO | COMMAND_TRANS_MONO);
        uint8_t rop = voodoo->banshee_blt.command >> 24;

//        bansheeblt_log("banshee_do_rectfill: size=%i,%i  dst=%i,%i\n", voodoo->banshee_blt.dstSizeX, voodoo->banshee_blt.dstSizeY, voodoo->banshee_blt.dstX, voodoo->banshee_blt.dstY);
//        bansheeblt_log("clipping: %i,%i -> %i,%i\n", clip->x_min, clip->y_min, clip->x_max, clip->y_max);
//        bansheeblt_log("colorFore=%08x\n", voodoo->banshee_blt.colorFore);
        for (voodoo->banshee_blt.cur_y = 0; voodoo->banshee_blt.cur_y < voodoo->banshee_blt.dstSizeY; voodoo->banshee_blt.cur_y++)
        {
                int dst_x = voodoo->banshee_blt.dstX;

                if (dst_y >= clip->y_min && dst_y < clip->y_max)
                {
                        int pat_x = voodoo->banshee_blt.patoff_x + voodoo->banshee_blt.dstX;
                        uint8_t pattern_mask = pattern_mono[pat_y & 7];

                        for (voodoo->banshee_blt.cur_x = 0; voodoo->banshee_blt.cur_x < voodoo->banshee_blt.dstSizeX; voodoo->banshee_blt.cur_x++)
                        {
                                int pattern_trans = use_pattern_trans ? (pattern_mask & (1 << (7-(pat_x & 7)))) : 1;

                                if (dst_x >= clip->x_min && dst_x < clip->x_max && pattern_trans)
                                        PLOT(voodoo, dst_x, dst_y, pat_x, pat_y, pattern_mask, rop, voodoo->banshee_blt.colorFore, COLORKEY_32);

                                dst_x += (voodoo->banshee_blt.command & COMMAND_DX) ? -1 : 1;
                                pat_x += (voodoo->banshee_blt.command & COMMAND_DX) ? -1 : 1;
                        }
                }
                dst_y += (voodoo->banshee_blt.command & COMMAND_DY) ? -1 : 1;
                if (!(voodoo->banshee_blt.commandExtra & CMDEXTRA_FORCE_PAT_ROW0))
                        pat_y += (voodoo->banshee_blt.command & COMMAND_DY) ? -1 : 1;
        }

        end_command(voodoo);
}

static void do_screen_to_screen_line(voodoo_t *voodoo, uint8_t *src_p, int use_x_dir, int src_x, int src_tiled)
{
        clip_t *clip = &voodoo->banshee_blt.clip[(voodoo->banshee_blt.command & COMMAND_CLIP_SEL) ? 1 : 0];
        int dst_y = voodoo->banshee_blt.dstY;
        int pat_y = (voodoo->banshee_blt.commandExtra & CMDEXTRA_FORCE_PAT_ROW0) ? 0 : (voodoo->banshee_blt.patoff_y + voodoo->banshee_blt.dstY);
        uint8_t *pattern_mono = (uint8_t *)voodoo->banshee_blt.colorPattern;
        int use_pattern_trans = (voodoo->banshee_blt.command & (COMMAND_PATTERN_MONO | COMMAND_TRANS_MONO)) ==
                                             (COMMAND_PATTERN_MONO | COMMAND_TRANS_MONO);
        uint8_t rop = voodoo->banshee_blt.command >> 24;
        int src_colorkey;

        switch (voodoo->banshee_blt.srcFormat & SRC_FORMAT_COL_MASK)
        {
                case SRC_FORMAT_COL_8_BPP:
                src_colorkey = COLORKEY_8;
                break;
                case SRC_FORMAT_COL_16_BPP:
                src_colorkey = COLORKEY_16;
                break;
                default:
                src_colorkey = COLORKEY_32;
                break;
        }
//        bansheeblt_log("do_screen_to_screen_line: srcFormat=%08x dst=%08x\n", voodoo->banshee_blt.srcFormat, voodoo->banshee_blt.dstFormat);
        if ((voodoo->banshee_blt.srcFormat & SRC_FORMAT_COL_MASK) ==
                (voodoo->banshee_blt.dstFormat & DST_FORMAT_COL_MASK))
        {
                /*No conversion required*/
                if (dst_y >= clip->y_min && dst_y < clip->y_max)
                {
                        int dst_x = voodoo->banshee_blt.dstX;
                        int pat_x = voodoo->banshee_blt.patoff_x + voodoo->banshee_blt.dstX;
                        uint8_t pattern_mask = pattern_mono[pat_y & 7];

                        for (voodoo->banshee_blt.cur_x = 0; voodoo->banshee_blt.cur_x < voodoo->banshee_blt.dstSizeX; voodoo->banshee_blt.cur_x++)
                        {
                                int pattern_trans = use_pattern_trans ? (pattern_mask & (1 << (7-(pat_x & 7)))) : 1;
                                int src_x_real = (src_x * voodoo->banshee_blt.src_bpp) >> 3;

                                if (src_tiled)
                                        src_x_real = (src_x_real & 127) + ((src_x_real >> 7) * 128*32);

                                if (dst_x >= clip->x_min && dst_x < clip->x_max && pattern_trans)
                                {
                                        switch (voodoo->banshee_blt.dstFormat & DST_FORMAT_COL_MASK)
                                        {
                                                case DST_FORMAT_COL_8_BPP:
                                                {
                                                        uint32_t dst_addr = get_addr(voodoo, dst_x, dst_y, 0, 0);//(voodoo->banshee_blt.dstBaseAddr + dst_x + dst_y*voodoo->banshee_blt.dst_stride) & voodoo->fb_mask;
                                                        uint32_t src = src_p[src_x_real];
                                                        uint32_t dest = voodoo->vram[dst_addr];
                                                        uint32_t pattern = (voodoo->banshee_blt.command & COMMAND_PATTERN_MONO) ?
                                                                        ((pattern_mask & (1 << (7-(pat_x & 7)))) ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack) :
                                                                        voodoo->banshee_blt.colorPattern8[(pat_x & 7) + (pat_y & 7)*8];

                                                        voodoo->vram[dst_addr] = MIX(voodoo, dest, src, pattern, COLORKEY_8, COLORKEY_8);
                                                        voodoo->changedvram[dst_addr >> 12] = changeframecount;
                                                        break;
                                                }
                                                case DST_FORMAT_COL_16_BPP:
                                                {
                                                        uint32_t dst_addr = get_addr(voodoo, dst_x*2, dst_y, 0, 0);//dst_addr = (voodoo->banshee_blt.dstBaseAddr + dst_x*2 + dst_y*voodoo->banshee_blt.dst_stride) & voodoo->fb_mask;
                                                        uint32_t src = *(uint16_t *)&src_p[src_x_real];
                                                        uint32_t dest = *(uint16_t *)&voodoo->vram[dst_addr];
                                                        uint32_t pattern = (voodoo->banshee_blt.command & COMMAND_PATTERN_MONO) ?
                                                                        ((pattern_mask & (1 << (7-(pat_x & 7)))) ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack) :
                                                                        voodoo->banshee_blt.colorPattern16[(pat_x & 7) + (pat_y & 7)*8];

                                                        *(uint16_t *)&voodoo->vram[dst_addr] = MIX(voodoo, dest, src, pattern, COLORKEY_16, COLORKEY_16);
                                                        voodoo->changedvram[dst_addr >> 12] = changeframecount;
                                                        break;
                                                }
                                                case DST_FORMAT_COL_24_BPP:
                                                {
                                                        uint32_t dst_addr = get_addr(voodoo, dst_x*3, dst_y, 0, 0);//dst_addr = (voodoo->banshee_blt.dstBaseAddr + dst_x*3 + dst_y*voodoo->banshee_blt.dst_stride) & voodoo->fb_mask;
                                                        uint32_t src = *(uint32_t *)&src_p[src_x_real];
                                                        uint32_t dest = *(uint32_t *)&voodoo->vram[dst_addr];
                                                        uint32_t pattern = (voodoo->banshee_blt.command & COMMAND_PATTERN_MONO) ?
                                                                        ((pattern_mask & (1 << (7-(pat_x & 7)))) ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack) :
                                                                        voodoo->banshee_blt.colorPattern24[(pat_x & 7) + (pat_y & 7)*8];

                                                        *(uint32_t *)&voodoo->vram[dst_addr] = (MIX(voodoo, dest, src, pattern, COLORKEY_32, COLORKEY_32) & 0xffffff) | (dest & 0xff000000);
                                                        voodoo->changedvram[dst_addr >> 12] = changeframecount;
                                                        break;
                                                }
                                                case DST_FORMAT_COL_32_BPP:
                                                {
                                                        uint32_t dst_addr = get_addr(voodoo, dst_x*4, dst_y, 0, 0);//dst_addr = (voodoo->banshee_blt.dstBaseAddr + dst_x*4 + dst_y*voodoo->banshee_blt.dst_stride) & voodoo->fb_mask;
                                                        uint32_t src = *(uint32_t *)&src_p[src_x_real];
                                                        uint32_t dest = *(uint32_t *)&voodoo->vram[dst_addr];
                                                        uint32_t pattern = (voodoo->banshee_blt.command & COMMAND_PATTERN_MONO) ?
                                                                        ((pattern_mask & (1 << (7-(pat_x & 7)))) ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack) :
                                                                        voodoo->banshee_blt.colorPattern[(pat_x & 7) + (pat_y & 7)*8];

                                                        *(uint32_t *)&voodoo->vram[dst_addr] = MIX(voodoo, dest, src, pattern, COLORKEY_32, COLORKEY_32);
                                                        voodoo->changedvram[dst_addr >> 12] = changeframecount;
                                                        break;
                                                }
                                        }
                                }
                                if (use_x_dir)
                                {
                                        src_x += (voodoo->banshee_blt.command & COMMAND_DX) ? -1 : 1;
                                        dst_x += (voodoo->banshee_blt.command & COMMAND_DX) ? -1 : 1;
                                        pat_x += (voodoo->banshee_blt.command & COMMAND_DX) ? -1 : 1;
                                }
                                else
                                {
                                        src_x++;
                                        dst_x++;
                                        pat_x++;
                                }
                        }
                }
                voodoo->banshee_blt.srcY += (voodoo->banshee_blt.command & COMMAND_DY) ? -1 : 1;
                voodoo->banshee_blt.dstY += (voodoo->banshee_blt.command & COMMAND_DY) ? -1 : 1;
        }
        else
        {
                /*Conversion required*/
                if (dst_y >= clip->y_min && dst_y < clip->y_max)
                {
//                        int src_x = voodoo->banshee_blt.srcX;
                        int dst_x = voodoo->banshee_blt.dstX;
                        int pat_x = voodoo->banshee_blt.patoff_x + voodoo->banshee_blt.dstX;
                        uint8_t pattern_mask = pattern_mono[pat_y & 7];

                        for (voodoo->banshee_blt.cur_x = 0; voodoo->banshee_blt.cur_x < voodoo->banshee_blt.dstSizeX; voodoo->banshee_blt.cur_x++)
                        {
                                int pattern_trans = use_pattern_trans ? (pattern_mask & (1 << (7-(pat_x & 7)))) : 1;
                                int src_x_real = (src_x * voodoo->banshee_blt.src_bpp) >> 3;

                                if (src_tiled)
                                        src_x_real = (src_x_real & 127) + ((src_x_real >> 7) * 128*32);

                                if (dst_x >= clip->x_min && dst_x < clip->x_max && pattern_trans)
                                {
                                        uint32_t src_data = 0;
                                        int transparent = 0;

                                        switch (voodoo->banshee_blt.srcFormat & SRC_FORMAT_COL_MASK)
                                        {
                                                case SRC_FORMAT_COL_1_BPP:
                                                {
                                                        uint8_t src_byte = src_p[src_x_real];
                                                        src_data = (src_byte & (0x80 >> (src_x & 7))) ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack;
                                                        if (voodoo->banshee_blt.command & COMMAND_TRANS_MONO)
                                                                transparent = !(src_byte & (0x80 >> (src_x & 7)));
//                                                        bansheeblt_log(" 1bpp src_byte=%02x src_x=%i src_data=%x transparent=%i\n", src_byte, src_x, src_data, transparent);
                                                        break;
                                                }
                                                case SRC_FORMAT_COL_8_BPP:
                                                {
                                                        src_data = src_p[src_x_real];
                                                        break;
                                                }
                                                case SRC_FORMAT_COL_16_BPP:
                                                {
                                                        uint16_t src_16 = *(uint16_t *)&src_p[src_x_real];
                                                        int r = (src_16 >> 11);
                                                        int g = (src_16 >> 5) & 0x3f;
                                                        int b = src_16 & 0x1f;

                                                        r = (r << 3) | (r >> 2);
                                                        g = (g << 2) | (g >> 4);
                                                        b = (b << 3) | (b >> 2);
                                                        src_data = (r << 16) | (g << 8) | b;
                                                        break;
                                                }
                                                case SRC_FORMAT_COL_24_BPP:
                                                {
                                                        src_data = *(uint32_t *)&src_p[src_x_real];
                                                        break;
                                                }
                                                case SRC_FORMAT_COL_32_BPP:
                                                {
                                                        src_data = *(uint32_t *)&src_p[src_x_real];
                                                        break;
                                                }

                                                default:
                                                fatal("banshee_do_screen_to_screen_blt: unknown srcFormat %08x\n", voodoo->banshee_blt.srcFormat);
                                        }

                                        if ((voodoo->banshee_blt.dstFormat & DST_FORMAT_COL_MASK) == DST_FORMAT_COL_16_BPP &&
                                            (voodoo->banshee_blt.srcFormat & SRC_FORMAT_COL_MASK) != SRC_FORMAT_COL_1_BPP)
                                        {
                                                int r = src_data >> 16;
                                                int g = (src_data >> 8) & 0xff;
                                                int b = src_data & 0xff;

                                                src_data = (b >> 3) | ((g >> 2) << 5) | ((r >> 3) << 11);
                                        }

                                        if (!transparent)
                                                PLOT(voodoo, dst_x, dst_y, pat_x, pat_y, pattern_mask, rop, src_data, src_colorkey);
                                }
                                if (use_x_dir)
                                {
                                        src_x += (voodoo->banshee_blt.command & COMMAND_DX) ? -1 : 1;
                                        dst_x += (voodoo->banshee_blt.command & COMMAND_DX) ? -1 : 1;
                                        pat_x += (voodoo->banshee_blt.command & COMMAND_DX) ? -1 : 1;
                                }
                                else
                                {
                                        src_x++;
                                        dst_x++;
                                        pat_x++;
                                }
                        }
                }
                voodoo->banshee_blt.srcY += (voodoo->banshee_blt.command & COMMAND_DY) ? -1 : 1;
                voodoo->banshee_blt.dstY += (voodoo->banshee_blt.command & COMMAND_DY) ? -1 : 1;
        }
}

static void banshee_do_screen_to_screen_blt(voodoo_t *voodoo)
{
//        bansheeblt_log("screen_to_screen: %08x %08x %08x\n", voodoo->banshee_blt.srcFormat, voodoo->banshee_blt.src_stride, voodoo->banshee_blt.src_stride_dest);
//                return;
        for (voodoo->banshee_blt.cur_y = 0; voodoo->banshee_blt.cur_y < voodoo->banshee_blt.dstSizeY; voodoo->banshee_blt.cur_y++)
        {
                uint32_t src_addr = get_addr(voodoo, 0, voodoo->banshee_blt.srcY, 1, voodoo->banshee_blt.src_stride_dest);
//                if ((voodoo->banshee_blt.srcFormat & SRC_FORMAT_COL_MASK) == SRC_FORMAT_COL_1_BPP)
//                        bansheeblt_log(" srcY=%i src_addr=%08x\n", voodoo->banshee_blt.srcY, src_addr);
                do_screen_to_screen_line(voodoo, &voodoo->vram[src_addr], 1, voodoo->banshee_blt.srcX, voodoo->banshee_blt.srcBaseAddr_tiled);
        }
        end_command(voodoo);
}

static void banshee_do_host_to_screen_blt(voodoo_t *voodoo, int count, uint32_t data)
{
//        if (voodoo->banshee_blt.dstBaseAddr == 0xee5194)
//                bansheeblt_log("banshee_do_host_to_screen_blt: data=%08x host_data_count=%i src_stride_dest=%i host_data_size_dest=%i\n", data, voodoo->banshee_blt.host_data_count, voodoo->banshee_blt.src_stride_dest, voodoo->banshee_blt.host_data_size_dest);

        if (voodoo->banshee_blt.srcFormat & SRC_FORMAT_BYTE_SWIZZLE)
                data = (data >> 24) | ((data >> 8) & 0xff00) | ((data << 8) & 0xff0000) | (data << 24);
        if (voodoo->banshee_blt.srcFormat & SRC_FORMAT_WORD_SWIZZLE)
                data = (data >> 16) | (data << 16);

        if ((voodoo->banshee_blt.srcFormat & SRC_FORMAT_PACKING_MASK) == SRC_FORMAT_PACKING_STRIDE)
        {
                int last_byte;

                if ((voodoo->banshee_blt.srcFormat & SRC_FORMAT_COL_MASK) == SRC_FORMAT_COL_1_BPP)
                        last_byte = ((voodoo->banshee_blt.srcX & 31) + voodoo->banshee_blt.dstSizeX + 7) >> 3;
                else
                        last_byte = (voodoo->banshee_blt.srcX & 3) + voodoo->banshee_blt.host_data_size_dest;

                *(uint32_t *)&voodoo->banshee_blt.host_data[voodoo->banshee_blt.host_data_count] = data;
                voodoo->banshee_blt.host_data_count += 4;
                if (voodoo->banshee_blt.host_data_count >= last_byte)
                {
//                        bansheeblt_log("  %i %i srcX=%i srcFormat=%08x\n", voodoo->banshee_blt.cur_y, voodoo->banshee_blt.dstSizeY, voodoo->banshee_blt.srcX);
                        if (voodoo->banshee_blt.cur_y < voodoo->banshee_blt.dstSizeY)
                        {
                                if ((voodoo->banshee_blt.srcFormat & SRC_FORMAT_COL_MASK) == SRC_FORMAT_COL_1_BPP)
                                        do_screen_to_screen_line(voodoo, &voodoo->banshee_blt.host_data[(voodoo->banshee_blt.srcX >> 3) & 3], 0, voodoo->banshee_blt.srcX & 7, 0);
                                else
                                        do_screen_to_screen_line(voodoo, &voodoo->banshee_blt.host_data[voodoo->banshee_blt.srcX & 3], 0, 0, 0);
                                voodoo->banshee_blt.cur_y++;
                                if (voodoo->banshee_blt.cur_y == voodoo->banshee_blt.dstSizeY)
                                        end_command(voodoo);
                        }

                        if ((voodoo->banshee_blt.srcFormat & SRC_FORMAT_COL_MASK) == SRC_FORMAT_COL_1_BPP)
                                voodoo->banshee_blt.srcX += (voodoo->banshee_blt.srcFormat & SRC_FORMAT_STRIDE_MASK) << 3;
                        else
                                voodoo->banshee_blt.srcX += (voodoo->banshee_blt.srcFormat & SRC_FORMAT_STRIDE_MASK);

                        voodoo->banshee_blt.host_data_count = 0;
                }
        }
        else
        {
                *(uint32_t *)&voodoo->banshee_blt.host_data[voodoo->banshee_blt.host_data_count] = data;
                voodoo->banshee_blt.host_data_count += 4;
                while (voodoo->banshee_blt.host_data_count >= voodoo->banshee_blt.src_stride_dest)
                {
                        voodoo->banshee_blt.host_data_count -= voodoo->banshee_blt.src_stride_dest;

//                        bansheeblt_log("  %i %i\n", voodoo->banshee_blt.cur_y, voodoo->banshee_blt.dstSizeY);
                        if (voodoo->banshee_blt.cur_y < voodoo->banshee_blt.dstSizeY)
                        {
                                do_screen_to_screen_line(voodoo, voodoo->banshee_blt.host_data, 0, 0, 0);
                                voodoo->banshee_blt.cur_y++;
                                if (voodoo->banshee_blt.cur_y == voodoo->banshee_blt.dstSizeY)
                                        end_command(voodoo);
                        }

                        if (voodoo->banshee_blt.host_data_count)
                        {
//                                bansheeblt_log("  remaining=%i\n", voodoo->banshee_blt.host_data_count);
                                *(uint32_t *)&voodoo->banshee_blt.host_data[0] = data >> (4-voodoo->banshee_blt.host_data_count)*8;
                        }
                }
        }
}

static void do_screen_to_screen_stretch_line(voodoo_t *voodoo,uint8_t *src_p, int src_x, int *src_y)
{
        clip_t *clip = &voodoo->banshee_blt.clip[(voodoo->banshee_blt.command & COMMAND_CLIP_SEL) ? 1 : 0];
//        int src_y = voodoo->banshee_blt.srcY;
        int dst_y = voodoo->banshee_blt.dstY;
        int pat_y = (voodoo->banshee_blt.commandExtra & CMDEXTRA_FORCE_PAT_ROW0) ? 0 : (voodoo->banshee_blt.patoff_y + voodoo->banshee_blt.dstY);
        uint8_t *pattern_mono = (uint8_t *)voodoo->banshee_blt.colorPattern;
        int use_pattern_trans = (voodoo->banshee_blt.command & (COMMAND_PATTERN_MONO | COMMAND_TRANS_MONO)) ==
                                             (COMMAND_PATTERN_MONO | COMMAND_TRANS_MONO);
        uint32_t *colorPattern = voodoo->banshee_blt.colorPattern;

        //int error_y = voodoo->banshee_blt.dstSizeY / 2;

/*        bansheeblt_log("banshee_do_screen_to_screen_stretch_blt:\n");
        bansheeblt_log("  srcXY=%i,%i srcsizeXY=%i,%i\n", voodoo->banshee_blt.srcX, voodoo->banshee_blt.srcY, voodoo->banshee_blt.srcSizeX, voodoo->banshee_blt.srcSizeY);
        bansheeblt_log("  dstXY=%i,%i dstsizeXY=%i,%i\n", voodoo->banshee_blt.dstX, voodoo->banshee_blt.dstY, voodoo->banshee_blt.dstSizeX, voodoo->banshee_blt.dstSizeY);*/
        if (dst_y >= clip->y_min && dst_y < clip->y_max)
        {
//                int src_x = voodoo->banshee_blt.srcX;
                int dst_x = voodoo->banshee_blt.dstX;
                int pat_x = voodoo->banshee_blt.patoff_x + voodoo->banshee_blt.dstX;
                uint8_t pattern_mask = pattern_mono[pat_y & 7];
                int error_x = voodoo->banshee_blt.dstSizeX / 2;

//                bansheeblt_log(" Plot dest line %03i : src line %03i\n", dst_y, src_y);
                for (voodoo->banshee_blt.cur_x = 0; voodoo->banshee_blt.cur_x < voodoo->banshee_blt.dstSizeX; voodoo->banshee_blt.cur_x++)
                {
                        int pattern_trans = use_pattern_trans ? (pattern_mask & (1 << (7-(pat_x & 7)))) : 1;

                        if (dst_x >= clip->x_min && dst_x < clip->x_max && pattern_trans)
                        {
                                switch (voodoo->banshee_blt.dstFormat & DST_FORMAT_COL_MASK)
                                {
                                        case DST_FORMAT_COL_8_BPP:
                                        {
                                                uint32_t dst_addr = get_addr(voodoo, dst_x, dst_y, 0, 0);//(voodoo->banshee_blt.dstBaseAddr + dst_x + dst_y*voodoo->banshee_blt.dst_stride) & voodoo->fb_mask;
                                                uint32_t src = src_p[src_x];
                                                uint32_t dest = voodoo->vram[dst_addr];
                                                uint32_t pattern = (voodoo->banshee_blt.command & COMMAND_PATTERN_MONO) ?
                                                                ((pattern_mask & (1 << (7-(pat_x & 7)))) ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack) :
                                                                colorPattern[(pat_x & 7) + (pat_y & 7)*8];

                                                voodoo->vram[dst_addr] = MIX(voodoo, dest, src, pattern, COLORKEY_8, COLORKEY_8);
//                                                bansheeblt_log("%i,%i : sdp=%02x,%02x,%02x res=%02x\n", voodoo->banshee_blt.cur_x, voodoo->banshee_blt.cur_y, src, dest, pattern, voodoo->vram[dst_addr]);
                                                voodoo->changedvram[dst_addr >> 12] = changeframecount;
                                                break;
                                        }
                                        case DST_FORMAT_COL_16_BPP:
                                        {
                                                uint32_t dst_addr = get_addr(voodoo, dst_x*2, dst_y, 0, 0);//(voodoo->banshee_blt.dstBaseAddr + dst_x*2 + dst_y*voodoo->banshee_blt.dst_stride) & voodoo->fb_mask;
                                                uint32_t src = *(uint16_t *)&src_p[src_x*2];
                                                uint32_t dest = *(uint16_t *)&voodoo->vram[dst_addr];
                                                uint32_t pattern = (voodoo->banshee_blt.command & COMMAND_PATTERN_MONO) ?
                                                                ((pattern_mask & (1 << (7-(pat_x & 7)))) ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack) :
                                                                colorPattern[(pat_x & 7) + (pat_y & 7)*8];

                                                *(uint16_t *)&voodoo->vram[dst_addr] = MIX(voodoo, dest, src, pattern, COLORKEY_16, COLORKEY_16);
//                                                bansheeblt_log("%i,%i : sdp=%02x,%02x,%02x res=%02x\n", voodoo->banshee_blt.cur_x, voodoo->banshee_blt.cur_y, src, dest, pattern, *(uint16_t *)&voodoo->vram[dst_addr]);
                                                voodoo->changedvram[dst_addr >> 12] = changeframecount;
                                                break;
                                        }
                                        case DST_FORMAT_COL_24_BPP:
                                        {
                                                uint32_t dst_addr = get_addr(voodoo, dst_x*3, dst_y, 0, 0);//(voodoo->banshee_blt.dstBaseAddr + dst_x*3 + dst_y*voodoo->banshee_blt.dst_stride) & voodoo->fb_mask;
                                                uint32_t src = *(uint32_t *)&src_p[src_x*3];
                                                uint32_t dest = *(uint32_t *)&voodoo->vram[dst_addr];
                                                uint32_t pattern = (voodoo->banshee_blt.command & COMMAND_PATTERN_MONO) ?
                                                                ((pattern_mask & (1 << (7-(pat_x & 7)))) ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack) :
                                                                colorPattern[(pat_x & 7) + (pat_y & 7)*8];

                                                *(uint32_t *)&voodoo->vram[dst_addr] = (MIX(voodoo, dest, src, pattern, COLORKEY_32, COLORKEY_32) & 0xffffff) | (*(uint32_t *)&voodoo->vram[dst_addr] & 0xff000000);
//                                                bansheeblt_log("%i,%i : sdp=%02x,%02x,%02x res=%02x\n", voodoo->banshee_blt.cur_x, voodoo->banshee_blt.cur_y, src, dest, pattern, voodoo->vram[dst_addr]);
                                                voodoo->changedvram[dst_addr >> 12] = changeframecount;
                                                break;
                                        }
                                        case DST_FORMAT_COL_32_BPP:
                                        {
                                                uint32_t dst_addr = get_addr(voodoo, dst_x*4, dst_y, 0, 0);//(voodoo->banshee_blt.dstBaseAddr + dst_x*4 + dst_y*voodoo->banshee_blt.dst_stride) & voodoo->fb_mask;
                                                uint32_t src = *(uint32_t *)&src_p[src_x*4];
                                                uint32_t dest = *(uint32_t *)&voodoo->vram[dst_addr];
                                                uint32_t pattern = (voodoo->banshee_blt.command & COMMAND_PATTERN_MONO) ?
                                                                ((pattern_mask & (1 << (7-(pat_x & 7)))) ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack) :
                                                                colorPattern[(pat_x & 7) + (pat_y & 7)*8];

                                                *(uint32_t *)&voodoo->vram[dst_addr] = MIX(voodoo, dest, src, pattern, COLORKEY_32, COLORKEY_32);
//                                                bansheeblt_log("%i,%i : sdp=%02x,%02x,%02x res=%02x\n", voodoo->banshee_blt.cur_x, voodoo->banshee_blt.cur_y, src, dest, pattern, voodoo->vram[dst_addr]);
                                                voodoo->changedvram[dst_addr >> 12] = changeframecount;
                                                break;
                                        }
                                }
                        }

                        error_x -= voodoo->banshee_blt.srcSizeX;
                        while (error_x < 0)
                        {
                                error_x += voodoo->banshee_blt.dstSizeX;
                                src_x++;
                        }
                        dst_x++;
                        pat_x++;
                }
        }

        voodoo->banshee_blt.bres_error_0 -= voodoo->banshee_blt.srcSizeY;
        while (voodoo->banshee_blt.bres_error_0 < 0)
        {
                voodoo->banshee_blt.bres_error_0 += voodoo->banshee_blt.dstSizeY;
                if (src_y)
                        (*src_y) += (voodoo->banshee_blt.command & COMMAND_DY) ? -1 : 1;
        }
        voodoo->banshee_blt.dstY += (voodoo->banshee_blt.command & COMMAND_DY) ? -1 : 1;
//        pat_y += (voodoo->banshee_blt.command & COMMAND_DY) ? -1 : 1;
}

static void banshee_do_screen_to_screen_stretch_blt(voodoo_t *voodoo)
{
//        bansheeblt_log("screen_to_screen: %08x %08x %08x\n", voodoo->banshee_blt.srcFormat, voodoo->banshee_blt.src_stride, voodoo->banshee_blt.src_stride_dest);
//                return;
        for (voodoo->banshee_blt.cur_y = 0; voodoo->banshee_blt.cur_y < voodoo->banshee_blt.dstSizeY; voodoo->banshee_blt.cur_y++)
        {
                uint32_t src_addr = get_addr(voodoo, 0, voodoo->banshee_blt.srcY, 1, voodoo->banshee_blt.src_stride_src);//(voodoo->banshee_blt.srcBaseAddr + voodoo->banshee_blt.srcY*voodoo->banshee_blt.src_stride_src) & voodoo->fb_mask;
//                bansheeblt_log("scale_blit %i %08x  %08x\n", voodoo->banshee_blt.cur_y, src_addr, voodoo->banshee_blt.command);
//                if ((voodoo->banshee_blt.srcFormat & SRC_FORMAT_COL_MASK) == SRC_FORMAT_COL_1_BPP)
//                        bansheeblt_log(" srcY=%i src_addr=%08x\n", voodoo->banshee_blt.srcY, src_addr);
                do_screen_to_screen_stretch_line(voodoo, &voodoo->vram[src_addr], voodoo->banshee_blt.srcX, &voodoo->banshee_blt.srcY);
        }
        end_command(voodoo);
}

static void banshee_do_host_to_screen_stretch_blt(voodoo_t *voodoo, int count, uint32_t data)
{
//        if (voodoo->banshee_blt.dstBaseAddr == 0xee5194)
//                bansheeblt_log("banshee_do_host_to_screen_blt: data=%08x host_data_count=%i src_stride_dest=%i host_data_size_dest=%i\n", data, voodoo->banshee_blt.host_data_count, voodoo->banshee_blt.src_stride_dest, voodoo->banshee_blt.host_data_size_dest);

        if (voodoo->banshee_blt.srcFormat & SRC_FORMAT_BYTE_SWIZZLE)
                data = (data >> 24) | ((data >> 8) & 0xff00) | ((data << 8) & 0xff0000) | (data << 24);
        if (voodoo->banshee_blt.srcFormat & SRC_FORMAT_WORD_SWIZZLE)
                data = (data >> 16) | (data << 16);

        if ((voodoo->banshee_blt.srcFormat & SRC_FORMAT_PACKING_MASK) == SRC_FORMAT_PACKING_STRIDE)
        {
                int last_byte = (voodoo->banshee_blt.srcX & 3) + voodoo->banshee_blt.host_data_size_src;

                *(uint32_t *)&voodoo->banshee_blt.host_data[voodoo->banshee_blt.host_data_count] = data;
                voodoo->banshee_blt.host_data_count += 4;
                if (voodoo->banshee_blt.host_data_count >= last_byte)
                {
//                        bansheeblt_log("  %i %i srcX=%i srcFormat=%08x\n", voodoo->banshee_blt.cur_y, voodoo->banshee_blt.dstSizeY, voodoo->banshee_blt.srcX);
                        if (voodoo->banshee_blt.cur_y < voodoo->banshee_blt.dstSizeY)
                        {
                                if ((voodoo->banshee_blt.srcFormat & SRC_FORMAT_COL_MASK) == SRC_FORMAT_COL_1_BPP)
                                        do_screen_to_screen_stretch_line(voodoo, &voodoo->banshee_blt.host_data[(voodoo->banshee_blt.srcX >> 3) & 3], voodoo->banshee_blt.srcX & 7, NULL);
                                else
                                        do_screen_to_screen_stretch_line(voodoo, &voodoo->banshee_blt.host_data[voodoo->banshee_blt.srcX & 3], 0, NULL);
                                voodoo->banshee_blt.cur_y++;
                                if (voodoo->banshee_blt.cur_y == voodoo->banshee_blt.dstSizeY)
                                        end_command(voodoo);
                        }

                        if ((voodoo->banshee_blt.srcFormat & SRC_FORMAT_COL_MASK) == SRC_FORMAT_COL_1_BPP)
                                voodoo->banshee_blt.srcX += (voodoo->banshee_blt.srcFormat & SRC_FORMAT_STRIDE_MASK) << 3;
                        else
                                voodoo->banshee_blt.srcX += (voodoo->banshee_blt.srcFormat & SRC_FORMAT_STRIDE_MASK);

                        voodoo->banshee_blt.host_data_count = 0;
                }
        }
        else
        {
                *(uint32_t *)&voodoo->banshee_blt.host_data[voodoo->banshee_blt.host_data_count] = data;
                voodoo->banshee_blt.host_data_count += 4;
                while (voodoo->banshee_blt.host_data_count >= voodoo->banshee_blt.src_stride_src)
                {
                        voodoo->banshee_blt.host_data_count -= voodoo->banshee_blt.src_stride_src;

//                        bansheeblt_log("  %i %i\n", voodoo->banshee_blt.cur_y, voodoo->banshee_blt.dstSizeY);
                        if (voodoo->banshee_blt.cur_y < voodoo->banshee_blt.dstSizeY)
                        {
                                do_screen_to_screen_stretch_line(voodoo, voodoo->banshee_blt.host_data, 0, NULL);
                                voodoo->banshee_blt.cur_y++;
                                if (voodoo->banshee_blt.cur_y == voodoo->banshee_blt.dstSizeY)
                                        end_command(voodoo);
                        }

                        if (voodoo->banshee_blt.host_data_count)
                        {
//                                bansheeblt_log("  remaining=%i\n", voodoo->banshee_blt.host_data_count);
                                *(uint32_t *)&voodoo->banshee_blt.host_data[0] = data >> (4-voodoo->banshee_blt.host_data_count)*8;
                        }
                }
        }
}

static void step_line(voodoo_t *voodoo)
{
        if (voodoo->banshee_blt.line_pix_pos == voodoo->banshee_blt.line_rep_cnt)
        {
                voodoo->banshee_blt.line_pix_pos = 0;
                if (voodoo->banshee_blt.line_bit_pos == voodoo->banshee_blt.line_bit_mask_size)
                        voodoo->banshee_blt.line_bit_pos = 0;
                else
                        voodoo->banshee_blt.line_bit_pos++;
        }
        else
                voodoo->banshee_blt.line_pix_pos++;
}


static void banshee_do_line(voodoo_t *voodoo, int draw_last_pixel)
{
        clip_t *clip = &voodoo->banshee_blt.clip[(voodoo->banshee_blt.command & COMMAND_CLIP_SEL) ? 1 : 0];
        uint8_t rop = voodoo->banshee_blt.command >> 24;
        int dx = ABS(voodoo->banshee_blt.dstX - voodoo->banshee_blt.srcX);
        int dy = ABS(voodoo->banshee_blt.dstY - voodoo->banshee_blt.srcY);
        int x_inc = (voodoo->banshee_blt.dstX > voodoo->banshee_blt.srcX) ? 1 : -1;
        int y_inc = (voodoo->banshee_blt.dstY > voodoo->banshee_blt.srcY) ? 1 : -1;
        int x = voodoo->banshee_blt.srcX;
        int y = voodoo->banshee_blt.srcY;
        int error;
        uint32_t stipple = (voodoo->banshee_blt.command & COMMAND_STIPPLE_LINE) ?
                        voodoo->banshee_blt.lineStipple : ~0;

        if (dx > dy) /*X major*/
        {
                error = dx/2;
                while (x != voodoo->banshee_blt.dstX)
                {
                        int mask = stipple & (1 << voodoo->banshee_blt.line_bit_pos);
                        int pattern_trans = (voodoo->banshee_blt.command & COMMAND_TRANS_MONO) ? mask : 1;

                        if (y >= clip->y_min && y < clip->y_max && x >= clip->x_min && x < clip->x_max && pattern_trans)
                                PLOT_LINE(voodoo, x, y, rop, mask ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack, COLORKEY_32);

                        error -= dy;
                        if (error < 0)
                        {
                                error += dx;
                                y += y_inc;
                        }
                        x += x_inc;
                        step_line(voodoo);
                }
        }
        else         /*Y major*/
        {
                error = dy/2;
                while (y != voodoo->banshee_blt.dstY)
                {
                        int mask = stipple & (1 << voodoo->banshee_blt.line_bit_pos);
                        int pattern_trans = (voodoo->banshee_blt.command & COMMAND_TRANS_MONO) ? mask : 1;

                        if (y >= clip->y_min && y < clip->y_max && x >= clip->x_min && x < clip->x_max && pattern_trans)
                                PLOT_LINE(voodoo, x, y, rop, mask ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack, COLORKEY_32);

                        error -= dx;
                        if (error < 0)
                        {
                                error += dy;
                                x += x_inc;
                        }
                        y += y_inc;
                        step_line(voodoo);
                }
        }

        if (draw_last_pixel)
        {
                int mask = stipple & (1 << voodoo->banshee_blt.line_bit_pos);
                int pattern_trans = (voodoo->banshee_blt.command & COMMAND_TRANS_MONO) ? mask : 1;

                if (y >= clip->y_min && y < clip->y_max && x >= clip->x_min && x < clip->x_max && pattern_trans)
                        PLOT_LINE(voodoo, x, y, rop, mask ? voodoo->banshee_blt.colorFore : voodoo->banshee_blt.colorBack, COLORKEY_32);
        }

        voodoo->banshee_blt.srcXY = (x & 0xffff) | (y << 16);
        voodoo->banshee_blt.srcX = x;
        voodoo->banshee_blt.srcY = y;
}

static void banshee_polyfill_start(voodoo_t *voodoo)
{
        voodoo->banshee_blt.lx[0] = voodoo->banshee_blt.srcX;
        voodoo->banshee_blt.ly[0] = voodoo->banshee_blt.srcY;
        voodoo->banshee_blt.rx[0] = voodoo->banshee_blt.dstX;
        voodoo->banshee_blt.ry[0] = voodoo->banshee_blt.dstY;
        voodoo->banshee_blt.lx[1] = voodoo->banshee_blt.srcX;
        voodoo->banshee_blt.ly[1] = voodoo->banshee_blt.srcY;
        voodoo->banshee_blt.rx[1] = voodoo->banshee_blt.dstX;
        voodoo->banshee_blt.ry[1] = voodoo->banshee_blt.dstY;
        voodoo->banshee_blt.lx_cur = voodoo->banshee_blt.srcX;
        voodoo->banshee_blt.rx_cur = voodoo->banshee_blt.dstX;
}

static void banshee_polyfill_continue(voodoo_t *voodoo, uint32_t data)
{
        clip_t *clip = &voodoo->banshee_blt.clip[(voodoo->banshee_blt.command & COMMAND_CLIP_SEL) ? 1 : 0];
        uint8_t *pattern_mono = (uint8_t *)voodoo->banshee_blt.colorPattern;
        int use_pattern_trans = (voodoo->banshee_blt.command & (COMMAND_PATTERN_MONO | COMMAND_TRANS_MONO)) ==
                                             (COMMAND_PATTERN_MONO | COMMAND_TRANS_MONO);
        uint8_t rop = voodoo->banshee_blt.command >> 24;
        int y = MAX(voodoo->banshee_blt.ly[0], voodoo->banshee_blt.ry[0]);
        int y_end;

//        bansheeblt_log("Polyfill : data %08x\n", data);

        /*if r1.y>=l1.y, next vertex is left*/
        if (voodoo->banshee_blt.ry[1] >= voodoo->banshee_blt.ly[1])
        {
                voodoo->banshee_blt.lx[1] = ((int32_t)(data << 19)) >> 19;
                voodoo->banshee_blt.ly[1] = ((int32_t)(data << 3)) >> 19;
                voodoo->banshee_blt.dx[0] = ABS(voodoo->banshee_blt.lx[1] - voodoo->banshee_blt.lx[0]);
                voodoo->banshee_blt.dy[0] = ABS(voodoo->banshee_blt.ly[1] - voodoo->banshee_blt.ly[0]);
                voodoo->banshee_blt.x_inc[0] = (voodoo->banshee_blt.lx[1] > voodoo->banshee_blt.lx[0]) ? 1 : -1;
                voodoo->banshee_blt.error[0] = voodoo->banshee_blt.dy[0] / 2;
        }
        else
        {
                voodoo->banshee_blt.rx[1] = ((int32_t)(data << 19)) >> 19;
                voodoo->banshee_blt.ry[1] = ((int32_t)(data << 3)) >> 19;
                voodoo->banshee_blt.dx[1] = ABS(voodoo->banshee_blt.rx[1] - voodoo->banshee_blt.rx[0]);
                voodoo->banshee_blt.dy[1] = ABS(voodoo->banshee_blt.ry[1] - voodoo->banshee_blt.ry[0]);
                voodoo->banshee_blt.x_inc[1] = (voodoo->banshee_blt.rx[1] > voodoo->banshee_blt.rx[0]) ? 1 : -1;
                voodoo->banshee_blt.error[1] = voodoo->banshee_blt.dy[1] / 2;
        }

/*        bansheeblt_log("   verts now : %03i,%03i    %03i,%03i\n", voodoo->banshee_blt.lx[0], voodoo->banshee_blt.ly[0], voodoo->banshee_blt.rx[0], voodoo->banshee_blt.ry[0]);
        bansheeblt_log("               %03i,%03i    %03i,%03i\n", voodoo->banshee_blt.lx[1], voodoo->banshee_blt.ly[1], voodoo->banshee_blt.rx[1], voodoo->banshee_blt.ry[1]);
        bansheeblt_log("        left  dx=%i dy=%i x_inc=%i error=%i\n", voodoo->banshee_blt.dx[0],voodoo->banshee_blt.dy[0],voodoo->banshee_blt.x_inc[0],voodoo->banshee_blt.error[0]);
        bansheeblt_log("        right dx=%i dy=%i x_inc=%i error=%i\n", voodoo->banshee_blt.dx[1],voodoo->banshee_blt.dy[1],voodoo->banshee_blt.x_inc[1],voodoo->banshee_blt.error[1]);*/
        y_end = MIN(voodoo->banshee_blt.ly[1], voodoo->banshee_blt.ry[1]);
//        bansheeblt_log("Polyfill : draw spans from %i-%i\n", y, y_end);
        for (; y < y_end; y++)
        {
//                bansheeblt_log("   %i:  %i %i\n", y, voodoo->banshee_blt.lx_cur, voodoo->banshee_blt.rx_cur);
                /*Draw span from lx_cur to rx_cur*/
                if (y >= clip->y_min && y < clip->y_max)
                {
                        int pat_y = (voodoo->banshee_blt.commandExtra & CMDEXTRA_FORCE_PAT_ROW0) ? 0 : (voodoo->banshee_blt.patoff_y + y);
                        uint8_t pattern_mask = pattern_mono[pat_y & 7];
                        int x;

                        for (x = voodoo->banshee_blt.lx_cur; x < voodoo->banshee_blt.rx_cur; x++)
                        {
                                int pat_x = voodoo->banshee_blt.patoff_x + x;
                                int pattern_trans = use_pattern_trans ? (pattern_mask & (1 << (7-(pat_x & 7)))) : 1;

                                if (x >= clip->x_min && x < clip->x_max && pattern_trans)
                                        PLOT(voodoo, x, y, pat_x, pat_y, pattern_mask, rop, voodoo->banshee_blt.colorFore, COLORKEY_32);
                        }
                }

                voodoo->banshee_blt.error[0] -= voodoo->banshee_blt.dx[0];
                while (voodoo->banshee_blt.error[0] < 0)
                {
                        voodoo->banshee_blt.error[0] += voodoo->banshee_blt.dy[0];
                        voodoo->banshee_blt.lx_cur += voodoo->banshee_blt.x_inc[0];
                }
                voodoo->banshee_blt.error[1] -= voodoo->banshee_blt.dx[1];
                while (voodoo->banshee_blt.error[1] < 0)
                {
                        voodoo->banshee_blt.error[1] += voodoo->banshee_blt.dy[1];
                        voodoo->banshee_blt.rx_cur += voodoo->banshee_blt.x_inc[1];
                }
        }

        if (voodoo->banshee_blt.ry[1] == voodoo->banshee_blt.ly[1])
        {
                voodoo->banshee_blt.lx[0] = voodoo->banshee_blt.lx[1];
                voodoo->banshee_blt.ly[0] = voodoo->banshee_blt.ly[1];
                voodoo->banshee_blt.rx[0] = voodoo->banshee_blt.rx[1];
                voodoo->banshee_blt.ry[0] = voodoo->banshee_blt.ry[1];
        }
        else if (voodoo->banshee_blt.ry[1] >= voodoo->banshee_blt.ly[1])
        {
                voodoo->banshee_blt.lx[0] = voodoo->banshee_blt.lx[1];
                voodoo->banshee_blt.ly[0] = voodoo->banshee_blt.ly[1];
        }
        else
        {
                voodoo->banshee_blt.rx[0] = voodoo->banshee_blt.rx[1];
                voodoo->banshee_blt.ry[0] = voodoo->banshee_blt.ry[1];
        }
}

static void banshee_do_2d_blit(voodoo_t *voodoo, int count, uint32_t data)
{
        switch (voodoo->banshee_blt.command & COMMAND_CMD_MASK)
        {
                case COMMAND_CMD_NOP:
                break;

                case COMMAND_CMD_SCREEN_TO_SCREEN_BLT:
                banshee_do_screen_to_screen_blt(voodoo);
                break;

                case COMMAND_CMD_SCREEN_TO_SCREEN_STRETCH_BLT:
                banshee_do_screen_to_screen_stretch_blt(voodoo);
                break;

                case COMMAND_CMD_HOST_TO_SCREEN_BLT:
                banshee_do_host_to_screen_blt(voodoo, count, data);
                break;

                case COMMAND_CMD_HOST_TO_SCREEN_STRETCH_BLT:
                banshee_do_host_to_screen_stretch_blt(voodoo, count, data);
                break;

                case COMMAND_CMD_RECTFILL:
                banshee_do_rectfill(voodoo);
                break;

                case COMMAND_CMD_LINE:
                banshee_do_line(voodoo, 1);
                break;

                case COMMAND_CMD_POLYLINE:
                banshee_do_line(voodoo, 0);
                break;

                default:
                fatal("banshee_do_2d_blit: unknown command=%08x\n", voodoo->banshee_blt.command);
        }
}

void voodoo_2d_reg_writel(voodoo_t *voodoo, uint32_t addr, uint32_t val)
{
//        /*if ((addr & 0x1fc) != 0x80) */bansheeblt_log("2D reg write %03x %08x\n", addr & 0x1fc, val);
        switch (addr & 0x1fc)
        {
                case 0x08:
                voodoo->banshee_blt.clip0Min = val;
                voodoo->banshee_blt.clip[0].x_min = val & 0xfff;
                voodoo->banshee_blt.clip[0].y_min = (val >> 16) & 0xfff;
                break;
                case 0x0c:
                voodoo->banshee_blt.clip0Max = val;
                voodoo->banshee_blt.clip[0].x_max = val & 0xfff;
                voodoo->banshee_blt.clip[0].y_max = (val >> 16) & 0xfff;
                break;
                case 0x10:
                voodoo->banshee_blt.dstBaseAddr = val & 0xffffff;
                voodoo->banshee_blt.dstBaseAddr_tiled = val & 0x80000000;
                if (voodoo->banshee_blt.dstBaseAddr_tiled)
                        voodoo->banshee_blt.dst_stride = (voodoo->banshee_blt.dstFormat & DST_FORMAT_STRIDE_MASK) * 128*32;
                else
                        voodoo->banshee_blt.dst_stride = voodoo->banshee_blt.dstFormat & DST_FORMAT_STRIDE_MASK;
//                bansheeblt_log("dstBaseAddr=%08x\n", val);
                break;
                case 0x14:
                voodoo->banshee_blt.dstFormat = val;
                if (voodoo->banshee_blt.dstBaseAddr_tiled)
                        voodoo->banshee_blt.dst_stride = (voodoo->banshee_blt.dstFormat & DST_FORMAT_STRIDE_MASK) * 128*32;
                else
                        voodoo->banshee_blt.dst_stride = voodoo->banshee_blt.dstFormat & DST_FORMAT_STRIDE_MASK;
//                bansheeblt_log("dstFormat=%08x\n", val);
                break;

                case 0x18:
                voodoo->banshee_blt.srcColorkeyMin = val & 0xffffff;
                break;
                case 0x1c:
                voodoo->banshee_blt.srcColorkeyMax = val & 0xffffff;
                break;
                case 0x20:
                voodoo->banshee_blt.dstColorkeyMin = val & 0xffffff;
                break;
                case 0x24:
                voodoo->banshee_blt.dstColorkeyMax = val & 0xffffff;
                break;

                case 0x28:
                voodoo->banshee_blt.bresError0 = val;
                voodoo->banshee_blt.bres_error_0 = val & 0xffff;
                break;
                case 0x2c:
                voodoo->banshee_blt.bresError1 = val;
                voodoo->banshee_blt.bres_error_1 = val & 0xffff;
                break;

                case 0x30:
                voodoo->banshee_blt.rop = val;
                voodoo->banshee_blt.rops[1] = val & 0xff;
                voodoo->banshee_blt.rops[2] = (val >> 8) & 0xff;
                voodoo->banshee_blt.rops[3] = (val >> 16) & 0xff;
//                bansheeblt_log("rop=%08x\n", val);
                break;
                case 0x34:
                voodoo->banshee_blt.srcBaseAddr = val & 0xffffff;
                voodoo->banshee_blt.srcBaseAddr_tiled = val & 0x80000000;
                if (voodoo->banshee_blt.srcBaseAddr_tiled)
                        voodoo->banshee_blt.src_stride = (voodoo->banshee_blt.srcFormat & SRC_FORMAT_STRIDE_MASK) * 128*32;
                else
                        voodoo->banshee_blt.src_stride = voodoo->banshee_blt.srcFormat & SRC_FORMAT_STRIDE_MASK;
                update_src_stride(voodoo);
//                bansheeblt_log("srcBaseAddr=%08x\n", val);
                break;
                case 0x38:
                voodoo->banshee_blt.commandExtra = val;
//                bansheeblt_log("commandExtra=%08x\n", val);
                break;
                case 0x3c:
                voodoo->banshee_blt.lineStipple = val;
                break;
                case 0x40:
                voodoo->banshee_blt.lineStyle = val;
                voodoo->banshee_blt.line_rep_cnt = val & 0xff;
                voodoo->banshee_blt.line_bit_mask_size = (val >> 8) & 0x1f;
                voodoo->banshee_blt.line_pix_pos = (val >> 16) & 0xff;
                voodoo->banshee_blt.line_bit_pos = (val >> 24) & 0x1f;
                break;
                case 0x44:
                voodoo->banshee_blt.colorPattern[0] = val;
//                bansheeblt_log("colorPattern0=%08x\n", val);
                voodoo->banshee_blt.colorPattern24[0] = val & 0xffffff;
                voodoo->banshee_blt.colorPattern24[1] = (voodoo->banshee_blt.colorPattern24[1] & 0xffff00) | (val >> 24);
                voodoo->banshee_blt.colorPattern16[0] = val & 0xffff;
                voodoo->banshee_blt.colorPattern16[1] = (val >> 16) & 0xffff;
                voodoo->banshee_blt.colorPattern8[0] = val & 0xff;
                voodoo->banshee_blt.colorPattern8[1] = (val >> 8) & 0xff;
                voodoo->banshee_blt.colorPattern8[2] = (val >> 16) & 0xff;
                voodoo->banshee_blt.colorPattern8[3] = (val >> 24) & 0xff;
                break;
                case 0x48:
                voodoo->banshee_blt.colorPattern[1] = val;
//                bansheeblt_log("colorPattern1=%08x\n", val);
                voodoo->banshee_blt.colorPattern24[1] = (voodoo->banshee_blt.colorPattern24[1] & 0xff) | ((val & 0xffff) << 8);
                voodoo->banshee_blt.colorPattern24[2] = (voodoo->banshee_blt.colorPattern24[2] & 0xff0000) | (val >> 16);
                voodoo->banshee_blt.colorPattern16[2] = val & 0xffff;
                voodoo->banshee_blt.colorPattern16[3] = (val >> 16) & 0xffff;
                voodoo->banshee_blt.colorPattern8[4] = val & 0xff;
                voodoo->banshee_blt.colorPattern8[5] = (val >> 8) & 0xff;
                voodoo->banshee_blt.colorPattern8[6] = (val >> 16) & 0xff;
                voodoo->banshee_blt.colorPattern8[7] = (val >> 24) & 0xff;
                break;
                case 0x4c:
                voodoo->banshee_blt.clip1Min = val;
                voodoo->banshee_blt.clip[1].x_min = val & 0xfff;
                voodoo->banshee_blt.clip[1].y_min = (val >> 16) & 0xfff;
                break;
                case 0x50:
                voodoo->banshee_blt.clip1Max = val;
                voodoo->banshee_blt.clip[1].x_max = val & 0xfff;
                voodoo->banshee_blt.clip[1].y_max = (val >> 16) & 0xfff;
                break;
                case 0x54:
                voodoo->banshee_blt.srcFormat = val;
                if (voodoo->banshee_blt.srcBaseAddr_tiled)
                        voodoo->banshee_blt.src_stride = (voodoo->banshee_blt.srcFormat & SRC_FORMAT_STRIDE_MASK) * 128*32;
                else
                        voodoo->banshee_blt.src_stride = voodoo->banshee_blt.srcFormat & SRC_FORMAT_STRIDE_MASK;
                update_src_stride(voodoo);
                switch (voodoo->banshee_blt.srcFormat & SRC_FORMAT_COL_MASK)
                {
                        case SRC_FORMAT_COL_1_BPP:
                        voodoo->banshee_blt.src_bpp = 1;
                        break;
                        case SRC_FORMAT_COL_8_BPP:
                        voodoo->banshee_blt.src_bpp = 8;
                        break;
                        case SRC_FORMAT_COL_24_BPP:
                        voodoo->banshee_blt.src_bpp = 24;
                        break;
                        case SRC_FORMAT_COL_32_BPP:
                        voodoo->banshee_blt.src_bpp = 32;
                        break;
                        case SRC_FORMAT_COL_16_BPP: default:
                        voodoo->banshee_blt.src_bpp = 16;
                        break;
                }
//                bansheeblt_log("srcFormat=%08x\n", val);
                break;
                case 0x58:
                voodoo->banshee_blt.srcSize = val;
                voodoo->banshee_blt.srcSizeX = voodoo->banshee_blt.srcSize & 0x1fff;
                voodoo->banshee_blt.srcSizeY = (voodoo->banshee_blt.srcSize >> 16) & 0x1fff;
                update_src_stride(voodoo);
//                bansheeblt_log("srcSize=%08x\n", val);
                break;
                case 0x5c:
                voodoo->banshee_blt.srcXY = val;
                voodoo->banshee_blt.srcX = ((int32_t)(val << 19)) >> 19;
                voodoo->banshee_blt.srcY = ((int32_t)(val << 3)) >> 19;
                update_src_stride(voodoo);
//                bansheeblt_log("srcXY=%08x\n", val);
                break;
                case 0x60:
                voodoo->banshee_blt.colorBack = val;
                break;
                case 0x64:
                voodoo->banshee_blt.colorFore = val;
                break;
                case 0x68:
                voodoo->banshee_blt.dstSize = val;
                voodoo->banshee_blt.dstSizeX = voodoo->banshee_blt.dstSize & 0x1fff;
                voodoo->banshee_blt.dstSizeY = (voodoo->banshee_blt.dstSize >> 16) & 0x1fff;
                update_src_stride(voodoo);
//                bansheeblt_log("dstSize=%08x\n", val);
                break;
                case 0x6c:
                voodoo->banshee_blt.dstXY = val;
                voodoo->banshee_blt.dstX = ((int32_t)(val << 19)) >> 19;
                voodoo->banshee_blt.dstY = ((int32_t)(val << 3)) >> 19;
//                bansheeblt_log("dstXY=%08x\n", val);
                break;
                case 0x70:
                voodoo_wait_for_render_thread_idle(voodoo);
                voodoo->banshee_blt.command = val;
                voodoo->banshee_blt.rops[0] = val >> 24;
//                bansheeblt_log("command=%x %08x\n", voodoo->banshee_blt.command & COMMAND_CMD_MASK, val);
                voodoo->banshee_blt.patoff_x = (val & COMMAND_PATOFF_X_MASK) >> COMMAND_PATOFF_X_SHIFT;
                voodoo->banshee_blt.patoff_y = (val & COMMAND_PATOFF_Y_MASK) >> COMMAND_PATOFF_Y_SHIFT;
                voodoo->banshee_blt.cur_x = 0;
                voodoo->banshee_blt.cur_y = 0;
                voodoo->banshee_blt.dstX = ((int32_t)(voodoo->banshee_blt.dstXY << 19)) >> 19;
                voodoo->banshee_blt.dstY = ((int32_t)(voodoo->banshee_blt.dstXY << 3)) >> 19;
                voodoo->banshee_blt.srcX = ((int32_t)(voodoo->banshee_blt.srcXY << 19)) >> 19;
                voodoo->banshee_blt.srcY = ((int32_t)(voodoo->banshee_blt.srcXY << 3)) >> 19;
                voodoo->banshee_blt.old_srcX = voodoo->banshee_blt.srcX;
                voodoo->banshee_blt.host_data_remainder = 0;
                voodoo->banshee_blt.host_data_count = 0;
                switch (voodoo->banshee_blt.command & COMMAND_CMD_MASK)
                {
/*                        case COMMAND_CMD_SCREEN_TO_SCREEN_STRETCH_BLT:
                        if (voodoo->banshee_blt.bresError0 & BRES_ERROR_USE)
                                voodoo->banshee_blt.bres_error_0 = (int32_t)(int16_t)(voodoo->banshee_blt.bresError0 & BRES_ERROR_MASK);
                        else
                                voodoo->banshee_blt.bres_error_0 = voodoo->banshee_blt.dstSizeY / 2;
                        if (voodoo->banshee_blt.bresError1 & BRES_ERROR_USE)
                                voodoo->banshee_blt.bres_error_1 = (int32_t)(int16_t)(voodoo->banshee_blt.bresError1 & BRES_ERROR_MASK);
                        else
                                voodoo->banshee_blt.bres_error_1 = voodoo->banshee_blt.dstSizeX / 2;

                        if (val & COMMAND_INITIATE)
                                banshee_do_2d_blit(voodoo, -1, 0);
                        break;*/

                        case COMMAND_CMD_POLYFILL:
                        if (val & COMMAND_INITIATE)
                        {
                                voodoo->banshee_blt.dstXY = voodoo->banshee_blt.srcXY;
                                voodoo->banshee_blt.dstX = voodoo->banshee_blt.srcX;
                                voodoo->banshee_blt.dstY = voodoo->banshee_blt.srcY;
                        }
                        banshee_polyfill_start(voodoo);
                        break;

                        default:
                        if (val & COMMAND_INITIATE)
                        {
                                banshee_do_2d_blit(voodoo, -1, 0);
                        //       fatal("Initiate command!\n");
                        }
                        break;
                }
                break;

                case 0x80: case 0x84: case 0x88: case 0x8c:
                case 0x90: case 0x94: case 0x98: case 0x9c:
                case 0xa0: case 0xa4: case 0xa8: case 0xac:
                case 0xb0: case 0xb4: case 0xb8: case 0xbc:
                case 0xc0: case 0xc4: case 0xc8: case 0xcc:
                case 0xd0: case 0xd4: case 0xd8: case 0xdc:
                case 0xe0: case 0xe4: case 0xe8: case 0xec:
                case 0xf0: case 0xf4: case 0xf8: case 0xfc:
//                bansheeblt_log("launch %08x  %08x %08x %08x\n", voodoo->banshee_blt.command,  voodoo->banshee_blt.commandExtra, voodoo->banshee_blt.srcColorkeyMin, voodoo->banshee_blt.srcColorkeyMax);
                switch (voodoo->banshee_blt.command & COMMAND_CMD_MASK)
                {
                        case COMMAND_CMD_SCREEN_TO_SCREEN_BLT:
                        voodoo->banshee_blt.srcXY = val;
                        voodoo->banshee_blt.srcX = ((int32_t)(val << 19)) >> 19;
                        voodoo->banshee_blt.srcY = ((int32_t)(val << 3)) >> 19;
                        banshee_do_screen_to_screen_blt(voodoo);
                        break;

                        case COMMAND_CMD_HOST_TO_SCREEN_BLT:
                        banshee_do_2d_blit(voodoo, 32, val);
                        break;

                        case COMMAND_CMD_HOST_TO_SCREEN_STRETCH_BLT:
                        banshee_do_2d_blit(voodoo, 32, val);
                        break;

                        case COMMAND_CMD_RECTFILL:
                        voodoo->banshee_blt.dstXY = val;
                        voodoo->banshee_blt.dstX = ((int32_t)(val << 19)) >> 19;
                        voodoo->banshee_blt.dstY = ((int32_t)(val << 3)) >> 19;
                        banshee_do_rectfill(voodoo);
                        break;

                        case COMMAND_CMD_LINE:
                        voodoo->banshee_blt.dstXY = val;
                        voodoo->banshee_blt.dstX = ((int32_t)(val << 19)) >> 19;
                        voodoo->banshee_blt.dstY = ((int32_t)(val << 3)) >> 19;
                        banshee_do_line(voodoo, 1);
                        break;

                        case COMMAND_CMD_POLYLINE:
                        voodoo->banshee_blt.dstXY = val;
                        voodoo->banshee_blt.dstX = ((int32_t)(val << 19)) >> 19;
                        voodoo->banshee_blt.dstY = ((int32_t)(val << 3)) >> 19;
                        banshee_do_line(voodoo, 0);
                        break;

                        case COMMAND_CMD_POLYFILL:
                        banshee_polyfill_continue(voodoo, val);
                        break;

                        default:
                        fatal("launch area write, command=%08x\n", voodoo->banshee_blt.command);
                }
                break;

                case 0x100: case 0x104: case 0x108: case 0x10c:
                case 0x110: case 0x114: case 0x118: case 0x11c:
                case 0x120: case 0x124: case 0x128: case 0x12c:
                case 0x130: case 0x134: case 0x138: case 0x13c:
                case 0x140: case 0x144: case 0x148: case 0x14c:
                case 0x150: case 0x154: case 0x158: case 0x15c:
                case 0x160: case 0x164: case 0x168: case 0x16c:
                case 0x170: case 0x174: case 0x178: case 0x17c:
                case 0x180: case 0x184: case 0x188: case 0x18c:
                case 0x190: case 0x194: case 0x198: case 0x19c:
                case 0x1a0: case 0x1a4: case 0x1a8: case 0x1ac:
                case 0x1b0: case 0x1b4: case 0x1b8: case 0x1bc:
                case 0x1c0: case 0x1c4: case 0x1c8: case 0x1cc:
                case 0x1d0: case 0x1d4: case 0x1d8: case 0x1dc:
                case 0x1e0: case 0x1e4: case 0x1e8: case 0x1ec:
                case 0x1f0: case 0x1f4: case 0x1f8: case 0x1fc:
                voodoo->banshee_blt.colorPattern[(addr >> 2) & 63] = val;
                if ((addr & 0x1fc) < 0x1c0)
                {
                        int base_addr = (addr & 0xfc) / 0xc;
                        uintptr_t src_p = (uintptr_t)&voodoo->banshee_blt.colorPattern[base_addr * 3];
                        int col24 = base_addr * 4;

                        voodoo->banshee_blt.colorPattern24[col24]     = *(uint32_t *)src_p & 0xffffff;
                        voodoo->banshee_blt.colorPattern24[col24 + 1] = *(uint32_t *)(src_p + 3) & 0xffffff;
                        voodoo->banshee_blt.colorPattern24[col24 + 2] = *(uint32_t *)(src_p + 6) & 0xffffff;
                        voodoo->banshee_blt.colorPattern24[col24 + 3] = *(uint32_t *)(src_p + 9) & 0xffffff;
                }
                if ((addr & 0x1fc) < 0x180)
                {
                        voodoo->banshee_blt.colorPattern16[(addr >> 1) & 62]       = val & 0xffff;
                        voodoo->banshee_blt.colorPattern16[((addr >> 1) & 62) + 1] = (val >> 16) & 0xffff;
                }
                if ((addr & 0x1fc) < 0x140)
                {
                        voodoo->banshee_blt.colorPattern8[addr & 60]       = val & 0xff;
                        voodoo->banshee_blt.colorPattern8[(addr & 60) + 1] = (val >> 8) & 0xff;
                        voodoo->banshee_blt.colorPattern8[(addr & 60) + 2] = (val >> 16) & 0xff;
                        voodoo->banshee_blt.colorPattern8[(addr & 60) + 3] = (val >> 24) & 0xff;
                }
//                bansheeblt_log("colorPattern%02x=%08x\n", (addr >> 2) & 63, val);
                break;

                default:
                fatal("Unknown 2D reg write %03x %08x\n", addr & 0x1fc, val);
        }
}
