/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the 8514/A-compatible Mach8 and Mach32 graphics
 *          chips from ATI for the ISA/VLB/MCA/PCI buses.
 *
 * Authors: TheCollector1995.
 *
 *          Copyright 2022-2024 TheCollector1995.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdatomic.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/mca.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/vid_8514a.h>
#include <86box/vid_xga.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_ati_eeprom.h>
#include <86box/vid_ati_mach8.h>

#define BIOS_MACH8_VGA_ROM_PATH  "roms/video/mach8/BIOS.BIN"
#define BIOS_MACH32_ISA_ROM_PATH "roms/video/mach32/ATi Mach32 Graphics Pro ISA.BIN"
#define BIOS_MACH32_VLB_ROM_PATH "roms/video/mach32/MACH32VLB.VBI"
#define BIOS_MACH32_MCA_ROM_PATH "roms/video/mach32/MACH32MCA_Olivetti.BIN"
#define BIOS_MACH32_PCI_ROM_PATH "roms/video/mach32/intelopt_00000.rom"

static video_timings_t timing_gfxultra_isa = { .type = VIDEO_ISA, .write_b = 3, .write_w = 3, .write_l =  6, .read_b =  5, .read_w =  5, .read_l = 10 };
static video_timings_t timing_mach32_vlb   = { .type = VIDEO_BUS, .write_b = 2, .write_w = 2, .write_l =  1, .read_b = 20, .read_w = 20, .read_l = 21 };
static video_timings_t timing_mach32_mca   = { .type = VIDEO_MCA, .write_b = 4, .write_w = 5, .write_l = 10, .read_b =  5, .read_w =  5, .read_l = 10 };
static video_timings_t timing_mach32_pci   = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l =  1, .read_b = 20, .read_w = 20, .read_l = 21 };

static void     mach_accel_outb(uint16_t port, uint8_t val, void *priv);
static void     mach_accel_outw(uint16_t port, uint16_t val, void *priv);
static void     mach_accel_outl(uint16_t port, uint32_t val, void *priv);
static uint8_t  mach_accel_inb(uint16_t port, void *priv);
static uint16_t mach_accel_inw(uint16_t port, void *priv);
static uint32_t mach_accel_inl(uint16_t port, void *priv);

static void     ati8514_accel_outb(uint16_t port, uint8_t val, void *priv);
static void     ati8514_accel_outw(uint16_t port, uint16_t val, void *priv);
static void     ati8514_accel_outl(uint16_t port, uint32_t val, void *priv);
static uint8_t  ati8514_accel_inb(uint16_t port, void *priv);
static uint16_t ati8514_accel_inw(uint16_t port, void *priv);
static uint32_t ati8514_accel_inl(uint16_t port, void *priv);

static void mach_set_resolution(mach_t *mach, svga_t *svga);
static void mach32_updatemapping(mach_t *mach, svga_t *svga);
static __inline void mach32_writew_linear(uint32_t addr, uint16_t val, mach_t *mach);
static __inline void mach32_write_common(uint32_t addr, uint8_t val, int linear, mach_t *mach, svga_t *svga);

static mach_t *reset_state = NULL;

#ifdef ENABLE_MACH_LOG
int mach_do_log = ENABLE_MACH_LOG;

static void
mach_log(const char *fmt, ...)
{
    va_list ap;

    if (mach_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define mach_log(fmt, ...)
#endif

#define WRITE8(addr, var, val)                   \
    switch ((addr) & 1) {                        \
        case 0:                                  \
            var = (var & 0xff00) | (val);        \
            break;                               \
        case 1:                                  \
            var = (var & 0x00ff) | ((val) << 8); \
            break;                               \
    }

#define READ8(addr, var)                \
    switch ((addr) & 1) {               \
        case 0:                         \
            temp = (var) & 0xff;        \
            break;                      \
        case 1:                         \
            temp = ((var) >> 8) & 0xff; \
            break;                      \
    }

#define READ_PIXTRANS_BYTE_IO(cx, n) \
    if ((mach->accel.cmd_type == 2) || (mach->accel.cmd_type == 5)) { \
        if (dev->bpp) {                                                                                             \
            if (n == 0)                                                                                             \
                mach->accel.pix_trans[(n)] = vram_w[(dev->accel.dest + (cx) + (n)) & (dev->vram_mask >> 1)] & 0xff; \
            else                                                                                                    \
                mach->accel.pix_trans[(n)] = vram_w[(dev->accel.dest + (cx) + (n)) & (dev->vram_mask >> 1)] >> 8;   \
        } else                                                                                                      \
            mach->accel.pix_trans[(n)] = dev->vram[(dev->accel.dest + (cx) + (n)) & dev->vram_mask];                \
    }

#define READ_PIXTRANS_WORD(cx, n)                                                                                                          \
    if ((cmd == 0) || (cmd == 1) || (cmd == 5) || ((mach->accel.cmd_type == -1) && (cmd != 2))) {                                                          \
        if (dev->bpp)                                                                                                                      \
            temp = vram_w[((dev->accel.cy * dev->pitch) + (cx) + (n)) & (dev->vram_mask >> 1)];                                            \
        else {                                                                                                                             \
            temp = dev->vram[((dev->accel.cy * dev->pitch) + (cx) + (n)) & dev->vram_mask];                                                \
            temp |= (dev->vram[((dev->accel.cy * dev->pitch) + (cx) + (n + 1)) & dev->vram_mask] << 8);                                    \
        }                                                                                                                                  \
    } else if (((cmd == 2) && (mach->accel.cmd_type == -1)) || (mach->accel.cmd_type == 2) || (mach->accel.cmd_type == 5)) {                                                               \
        if (dev->bpp)                                                                                                                      \
            temp = vram_w[((dev->accel.dest) + (cx) + (n)) & (dev->vram_mask >> 1)];                                                       \
        else {                                                                                                                             \
            temp = dev->vram[((dev->accel.dest) + (cx) + (n)) & dev->vram_mask];                                                           \
            temp |= (dev->vram[((dev->accel.dest) + (cx) + (n + 1)) & dev->vram_mask] << 8);                                               \
        }                                                                                                                                  \
    } else if ((mach->accel.cmd_type == 3) || (mach->accel.cmd_type == 4)) {                                                               \
        if (dev->bpp)                                                                                                                      \
            temp = vram_w[(mach->accel.dst_ge_offset + ((dev->accel.cy) * (mach->accel.dst_pitch)) + (cx) + (n)) & (dev->vram_mask >> 1)];         \
        else {                                                                                                                             \
            temp = dev->vram[(mach->accel.dst_ge_offset + ((dev->accel.cy) * (mach->accel.dst_pitch)) + (cx) + (n)) & dev->vram_mask];             \
            temp |= (dev->vram[(mach->accel.dst_ge_offset + ((dev->accel.cy) * (mach->accel.dst_pitch)) + (cx) + (n + 1)) & dev->vram_mask] << 8); \
        }                                                                                                                                  \
    }

#define READ(addr, dat)                               \
    if (dev->bpp)                                     \
        dat = vram_w[(addr) & (dev->vram_mask >> 1)]; \
    else                                              \
        dat = (dev->vram[(addr) & (dev->vram_mask)]);

#define READ_HIGH(addr, dat)                            \
    dat |= (dev->vram[(addr) & (dev->vram_mask)] << 8);

#define MIX(mixmode, dest_dat, src_dat)                                                               \
    {                                                                                                 \
        switch ((mixmode) ? dev->accel.frgd_mix : dev->accel.bkgd_mix) {                              \
            case 0x00:                                                                                \
                dest_dat = ~dest_dat;                                                                 \
                break;                                                                                \
            case 0x01:                                                                                \
                dest_dat = 0;                                                                         \
                break;                                                                                \
            case 0x02:                                                                                \
                dest_dat = ~0;                                                                        \
                break;                                                                                \
            case 0x03:                                                                                \
                dest_dat = dest_dat;                                                                  \
                break;                                                                                \
            case 0x04:                                                                                \
                dest_dat = ~src_dat;                                                                  \
                break;                                                                                \
            case 0x05:                                                                                \
                dest_dat = src_dat ^ dest_dat;                                                        \
                break;                                                                                \
            case 0x06:                                                                                \
                dest_dat = ~(src_dat ^ dest_dat);                                                     \
                break;                                                                                \
            case 0x07:                                                                                \
                dest_dat = src_dat;                                                                   \
                break;                                                                                \
            case 0x08:                                                                                \
                dest_dat = ~(src_dat & dest_dat);                                                     \
                break;                                                                                \
            case 0x09:                                                                                \
            case 0x11:                                                                                \
                dest_dat = ~src_dat | dest_dat;                                                       \
                break;                                                                                \
            case 0x0a:                                                                                \
            case 0x12:                                                                                \
                dest_dat = src_dat | ~dest_dat;                                                       \
                break;                                                                                \
            case 0x0b:                                                                                \
            case 0x13:                                                                                \
                dest_dat = src_dat | dest_dat;                                                        \
                break;                                                                                \
            case 0x0c:                                                                                \
                dest_dat = src_dat & dest_dat;                                                        \
                break;                                                                                \
            case 0x0d:                                                                                \
                dest_dat = src_dat & ~dest_dat;                                                       \
                break;                                                                                \
            case 0x0e:                                                                                \
                dest_dat = ~src_dat & dest_dat;                                                       \
                break;                                                                                \
            case 0x0f:                                                                                \
                dest_dat = ~(src_dat | dest_dat);                                                     \
                break;                                                                                \
            case 0x10:                                                                                \
                dest_dat = MIN(src_dat, dest_dat);                                                    \
                break;                                                                                \
            case 0x14:                                                                                \
                dest_dat = MAX(src_dat, dest_dat);                                                    \
                break;                                                                                \
            case 0x15:                                                                                \
                dest_dat = (src_dat | ~dest_dat) >> 1;                                                \
                break;                                                                                \
            case 0x16:                                                                                \
                dest_dat = (~src_dat | dest_dat) >> 1;                                                \
                break;                                                                                \
            case 0x17:                                                                                \
                dest_dat = (src_dat | dest_dat) >> 1;                                                 \
                break;                                                                                \
            case 0x18:                                                                                \
            case 0x19:                                                                                \
                dest_dat = MAX(0, ~src_dat | dest_dat);                                               \
                break;                                                                                \
            case 0x1a:                                                                                \
                dest_dat = MAX(0, src_dat | ~dest_dat);                                               \
                break;                                                                                \
            case 0x1b:                                                                                \
                if (dev->bpp)                                                                         \
                    dest_dat = MIN(0xffff, src_dat | dest_dat);                                       \
                else                                                                                  \
                    dest_dat = MIN(0xff, src_dat | dest_dat);                                         \
                break;                                                                                \
            case 0x1c:                                                                                \
            case 0x1d:                                                                                \
                dest_dat = MAX(0, ~src_dat | dest_dat) >> 1;                                          \
                break;                                                                                \
            case 0x1e:                                                                                \
                dest_dat = MAX(0, src_dat | ~dest_dat) >> 1;                                          \
                break;                                                                                \
            case 0x1f:                                                                                \
                if (dev->bpp)                                                                         \
                    dest_dat = (0xffff < (src_dat | dest_dat)) ? 0xffff : ((src_dat | dest_dat) >> 1); \
                else                                                                                  \
                    dest_dat = (0xff < (src_dat | dest_dat)) ? 0xff : ((src_dat | dest_dat) >> 1);    \
                break;                                                                                \
        }                                                                                             \
    }


#define WRITE(addr, dat)                                                               \
    if (dev->bpp) {                                                                    \
        vram_w[((addr)) & (dev->vram_mask >> 1)]                    = dat;             \
        dev->changedvram[(((addr)) & (dev->vram_mask >> 1)) >> 11] = svga->monitor->mon_changeframecount; \
    } else {                                                                           \
        dev->vram[((addr)) & (dev->vram_mask)]                = dat;                   \
        dev->changedvram[(((addr)) & (dev->vram_mask)) >> 12] = svga->monitor->mon_changeframecount;      \
    }

static int
mach_pixel_write(mach_t *mach)
{
    if (mach->accel.dp_config & 0x01)
        return 1;

    return 0;
}

static int
mach_pixel_read(mach_t *mach)
{
    if (mach->accel.dp_config & 0x01)
        return 0;

    return 1;
}

static void
mach_accel_start(int cmd_type, int cpu_input, int count, uint32_t mix_dat, uint32_t cpu_dat, UNUSED(svga_t *svga), mach_t *mach, ibm8514_t *dev)
{
    int           compare_mode;
    uint16_t      poly_src     = 0;
    uint16_t      rd_mask      = dev->accel.rd_mask;
    uint16_t      wrt_mask     = dev->accel.wrt_mask;
    uint16_t      dest_cmp_clr = dev->accel.color_cmp;
    uint16_t      frgd_color   = dev->accel.frgd_color;
    uint16_t      bkgd_color   = dev->accel.bkgd_color;
    int           frgd_sel;
    int           bkgd_sel;
    int           mono_src;
    int           compare  = 0;
    uint16_t      src_dat  = 0;
    uint16_t      dest_dat = 0;
    uint16_t      old_dest_dat;
    uint16_t     *vram_w    = (uint16_t *) dev->vram;
    uint16_t      mix       = 0;
    uint32_t      mono_dat0 = 0;
    uint32_t      mono_dat1 = 0;
    int16_t       clip_t    = dev->accel.clip_top;
    int16_t       clip_l    = dev->accel.clip_left;
    int16_t       clip_b    = dev->accel.clip_bottom;
    int16_t       clip_r    = dev->accel.clip_right;

    if (clip_l < 0)
        clip_l = 0;
    if (clip_t < 0)
        clip_t = 0;

    if (!dev->bpp) {
        rd_mask &= 0xff;
        dest_cmp_clr &= 0xff;
        frgd_color &= 0xff;
        bkgd_color &= 0xff;
    }

    compare_mode = (mach->accel.dest_cmp_fn >> 3) & 7;
    frgd_sel     = (mach->accel.dp_config >> 13) & 7;
    bkgd_sel     = (mach->accel.dp_config >> 7) & 3;
    mono_src     = (mach->accel.dp_config >> 5) & 3;

    if (cpu_input) {
        if (dev->bpp) {
            if ((mach->accel.dp_config & 0x200) && (count == 2))
                count >>= 1;
        }
    }

    if (cmd_type == 1 || cmd_type == 3 || cmd_type == 4) {
        if (mach->accel.linedraw_opt & 0x04)
            mach_log("cmd_type = %i, frgd_sel = %i, bkgd_sel = %i, mono_src = %i, dpconfig = %04x, cur_x = %d, cur_y = %d, cl = %d, cr = %d, ct = %d, cb = %d, accel_bpp = %d, pitch = %d, hicolbpp = %d, pattlen = %d.\n", cmd_type, frgd_sel, bkgd_sel, mono_src, mach->accel.dp_config, dev->accel.cur_x, dev->accel.cur_y, clip_l, clip_r, clip_t, clip_b, dev->accel_bpp, dev->pitch, dev->bpp, mach->accel.patt_len);
    }

    switch (cmd_type) {
        case 1: /*Extended Raw Linedraw from bres_count register (0x96ee)*/
            if (!cpu_input) {
                dev->accel.dx = dev->accel.cur_x;
                if (dev->accel.cur_x >= 0x600)
                    dev->accel.dx |= ~0x5ff;

                dev->accel.dy = dev->accel.cur_y;
                if (dev->accel.cur_y >= 0x600)
                    dev->accel.dy |= ~0x5ff;

                dev->accel.cx = dev->accel.destx_distp;
                if (dev->accel.destx_distp >= 0x600)
                    dev->accel.cx |= ~0x5ff;

                dev->accel.cy = dev->accel.desty_axstp;
                if (dev->accel.desty_axstp >= 0x600)
                    dev->accel.cy |= ~0x5ff;

                mach->accel.width     = mach->accel.bres_count;
                dev->accel.sx         = 0;
                mach->accel.poly_fill = 0;

                mach->accel.stepx = (mach->accel.linedraw_opt & 0x20) ? 1 : -1;
                mach->accel.stepy = (mach->accel.linedraw_opt & 0x80) ? 1 : -1;

                mach_log("Extended bresenham, CUR(%d,%d), DEST(%d,%d), width = %d, options = %04x, dpconfig = %04x, opt_ena = %03x.\n",
                         dev->accel.dx, dev->accel.dy, dev->accel.cx, dev->accel.cy, mach->accel.width, mach->accel.linedraw_opt,
                         mach->accel.dp_config, mach->accel.max_waitstates & 0x100);

                if (!dev->accel.cmd_back) {
                    if (mach_pixel_write(mach)) {
                        mach_log("Extended Bresenham Write pixtrans.\n");
                        dev->force_busy = 1;
                        dev->force_busy2 = 1;
                        mach->force_busy = 1;
                        dev->data_available  = 0;
                        dev->data_available2 = 0;
                        return;
                    } else if (mach_pixel_read(mach)) {
                        mach_log("Extended Bresenham Read pixtrans.\n");
                        dev->force_busy = 1;
                        dev->force_busy2 = 1;
                        mach->force_busy = 1;
                        dev->data_available  = 1;
                        dev->data_available2 = 1;
                        return;
                    }
                }
            }

            if (mono_src == 1) {
                count = mach->accel.width;
                mix_dat = mach->accel.mono_pattern_normal[0];
                dev->accel.temp_cnt = 8;
            }

            if (mach->accel.linedraw_opt & 0x08) { /*Vector Line*/
                while (count--) {
                    switch (mono_src) {
                        case 0:
                            mix = 1;
                            break;
                        case 1:
                            if (!dev->accel.temp_cnt) {
                                dev->accel.temp_cnt = 8;
                                mix_dat >>= 8;
                            }
                            mix = !!(mix_dat & 0x80);
                            dev->accel.temp_cnt--;
                            mix_dat <<= 1;
                            mix_dat |= 1;
                            break;
                        case 2:
                            if (mach->accel.dp_config & 0x1000) {
                                mix = mix_dat >> 0x1f;
                                mix_dat <<= 1;
                            } else {
                                if (mach->accel.dp_config & 0x200) {
                                    mix = mix_dat & 0x01;
                                    mix_dat >>= 1;
                                } else {
                                    mix = !!(mix_dat & 0x80);
                                    mix_dat <<= 1;
                                    mix_dat |= 1;
                                }
                            }
                            break;
                        case 3:
                            READ(mach->accel.src_ge_offset + (dev->accel.cy * mach->accel.src_pitch) + dev->accel.cx, mix);
                            mix = (mix & rd_mask) == rd_mask;
                            break;

                        default:
                            break;
                    }

                    if ((dev->accel.dx >= clip_l) &&
                        (dev->accel.dx <= clip_r) &&
                        (dev->accel.dy >= clip_t) &&
                        (dev->accel.dy <= clip_b)) {
                        dev->subsys_stat |= INT_GE_BSY;
                        if (mach_pixel_write(mach) || !cpu_input) {
                            switch (mix ? frgd_sel : bkgd_sel) {
                                case 0:
                                    src_dat = bkgd_color;
                                    break;
                                case 1:
                                    src_dat = frgd_color;
                                    break;
                                case 2:
                                    src_dat = cpu_dat;
                                    break;
                                case 3:
                                    READ(mach->accel.src_ge_offset + (dev->accel.cy * mach->accel.src_pitch) + dev->accel.cx, src_dat);
                                    if (mono_src == 3)
                                        src_dat = (src_dat & rd_mask) == rd_mask;
                                    break;
                                case 5:
                                    if (dev->bpp)
                                        src_dat = mach->accel.color_pattern_hicol[mach->accel.color_pattern_idx];
                                    else
                                        src_dat = mach->accel.color_pattern[mach->accel.color_pattern_idx];
                                    break;

                                default:
                                    break;
                            }

                            if (mach->accel.linedraw_opt & 0x02) {
                                READ(mach->accel.src_ge_offset + (dev->accel.cy * mach->accel.src_pitch) + dev->accel.cx, poly_src);
                                poly_src = ((poly_src & rd_mask) == rd_mask);
                                if (poly_src)
                                    mach->accel.poly_fill = !mach->accel.poly_fill;
                            }

                            if (mach->accel.poly_fill || !(mach->accel.linedraw_opt & 0x02)) {
                                READ(mach->accel.dst_ge_offset + (dev->accel.dy * mach->accel.dst_pitch) + dev->accel.dx, dest_dat);

                                switch (compare_mode) {
                                    case 1:
                                        compare = 1;
                                        break;
                                    case 2:
                                        compare = (dest_dat >= dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 3:
                                        compare = (dest_dat < dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 4:
                                        compare = (dest_dat != dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 5:
                                        compare = (dest_dat == dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 6:
                                        compare = (dest_dat <= dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 7:
                                        compare = (dest_dat > dest_cmp_clr) ? 0 : 1;
                                        break;

                                    default:
                                        break;
                                }

                                if (!compare) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                }

                                if (mach->accel.dp_config & 0x10) {
                                    if (mach->accel.linedraw_opt & 0x04) {
                                        if (((mono_src != 1) && (dev->accel.sx < mach->accel.width)) || ((mono_src == 1) && count)) {
                                            WRITE(mach->accel.dst_ge_offset + (dev->accel.dy * mach->accel.dst_pitch) + dev->accel.dx, dest_dat);
                                        }
                                    } else {
                                        WRITE(mach->accel.dst_ge_offset + (dev->accel.dy * mach->accel.dst_pitch) + dev->accel.dx, dest_dat);
                                    }
                                }
                            }
                        }
                    }

                    if ((mono_src == 1) && !count) {
                        if (cpu_input) {
                            mach->force_busy  = 0;
                            dev->force_busy   = 0;
                            dev->force_busy2  = 0;
                        }
                        dev->fifo_idx = 0;
                        dev->accel.cmd_back = 1;
                        break;
                    } else if ((mono_src != 1) && (dev->accel.sx >= mach->accel.width)) {
                        if (cpu_input) {
                            mach->force_busy  = 0;
                            dev->force_busy   = 0;
                            dev->force_busy2  = 0;
                        }
                        dev->fifo_idx = 0;
                        dev->accel.cmd_back = 1;
                        break;
                    }

                    if (dev->bpp)
                        cpu_dat >>= 16;
                    else
                        cpu_dat >>= 8;

                    mach->accel.color_pattern_idx++;

                    if (mach->accel.color_pattern_idx > mach->accel.patt_len)
                        mach->accel.color_pattern_idx = 0;

                    switch (mach->accel.linedraw_opt & 0xe0) {
                        case 0x00:
                            dev->accel.cx++;
                            dev->accel.dx++;
                            break;
                        case 0x20:
                            dev->accel.cx++;
                            dev->accel.dx++;
                            dev->accel.cy--;
                            dev->accel.dy--;
                            break;
                        case 0x40:
                            dev->accel.cy--;
                            dev->accel.dy--;
                            break;
                        case 0x60:
                            dev->accel.cx--;
                            dev->accel.dx--;
                            dev->accel.cy--;
                            dev->accel.dy--;
                            break;
                        case 0x80:
                            dev->accel.cx--;
                            dev->accel.dx--;
                            break;
                        case 0xa0:
                            dev->accel.cx--;
                            dev->accel.dx--;
                            dev->accel.cy++;
                            dev->accel.dy++;
                            break;
                        case 0xc0:
                            dev->accel.cy++;
                            dev->accel.dy++;
                            break;
                        case 0xe0:
                            dev->accel.cx++;
                            dev->accel.dx++;
                            dev->accel.cy++;
                            dev->accel.dy++;
                            break;

                        default:
                            break;
                    }

                    dev->accel.sx++;
                }
            } else { /*Bresenham*/
                while (count--) {
                    switch (mono_src) {
                        case 0:
                            mix = 1;
                            break;
                        case 1:
                            if (!dev->accel.temp_cnt) {
                                dev->accel.temp_cnt = 8;
                                mix_dat >>= 8;
                            }
                            mix = (mix_dat & 0x80);
                            dev->accel.temp_cnt--;
                            mix_dat <<= 1;
                            mix_dat |= 1;
                            break;
                        case 2:
                            if (mach->accel.dp_config & 0x1000) {
                                mix = mix_dat >> 0x1f;
                                mix_dat <<= 1;
                            } else {
                                if (mach->accel.dp_config & 0x200) {
                                    mix = mix_dat & 1;
                                    mix_dat >>= 1;
                                } else {
                                    mix = mix_dat & 0x80;
                                    mix_dat <<= 1;
                                    mix_dat |= 1;
                                }
                            }
                            break;
                        case 3:
                            READ(mach->accel.src_ge_offset + (dev->accel.cy * mach->accel.src_pitch) + dev->accel.cx, mix);
                            mix = (mix & rd_mask) == rd_mask;
                            break;

                        default:
                            break;
                    }

                    if ((dev->accel.dx >= clip_l) &&
                        (dev->accel.dx <= clip_r) &&
                        (dev->accel.dy >= clip_t) &&
                        (dev->accel.dy <= clip_b)) {
                        dev->subsys_stat |= INT_GE_BSY;
                        if (mach_pixel_write(mach) || !cpu_input) {
                            switch (mix ? frgd_sel : bkgd_sel) {
                                case 0:
                                    src_dat = bkgd_color;
                                    break;
                                case 1:
                                    src_dat = frgd_color;
                                    break;
                                case 2:
                                    src_dat = cpu_dat;
                                    break;
                                case 3:
                                    READ(mach->accel.src_ge_offset + (dev->accel.cy * mach->accel.src_pitch) + dev->accel.cx, src_dat);
                                    if (mono_src == 3)
                                        src_dat = (src_dat & rd_mask) == rd_mask;
                                    break;
                                case 5:
                                    if (dev->bpp)
                                        src_dat = mach->accel.color_pattern_hicol[mach->accel.color_pattern_idx];
                                    else
                                        src_dat = mach->accel.color_pattern[mach->accel.color_pattern_idx];
                                    break;

                                default:
                                    break;
                            }

                            if (mach->accel.linedraw_opt & 0x02) {
                                READ(mach->accel.src_ge_offset + (dev->accel.cy * mach->accel.src_pitch) + dev->accel.cx, poly_src);
                                poly_src = ((poly_src & rd_mask) == rd_mask);
                                if (poly_src)
                                    mach->accel.poly_fill = !mach->accel.poly_fill;
                            }

                            if (mach->accel.poly_fill || !(mach->accel.linedraw_opt & 0x02)) {
                                READ(mach->accel.dst_ge_offset + (dev->accel.dy * mach->accel.dst_pitch) + dev->accel.dx, dest_dat);

                                switch (compare_mode) {
                                    case 1:
                                        compare = 1;
                                        break;
                                    case 2:
                                        compare = (dest_dat >= dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 3:
                                        compare = (dest_dat < dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 4:
                                        compare = (dest_dat != dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 5:
                                        compare = (dest_dat == dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 6:
                                        compare = (dest_dat <= dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 7:
                                        compare = (dest_dat > dest_cmp_clr) ? 0 : 1;
                                        break;

                                    default:
                                        break;
                                }

                                if (!compare) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                }

                                if (mach->accel.dp_config & 0x10) {
                                    if (mach->accel.linedraw_opt & 0x04) {
                                        if (((mono_src != 1) && (dev->accel.sx < mach->accel.width)) || ((mono_src == 1) && count)) {
                                            WRITE(mach->accel.dst_ge_offset + (dev->accel.dy * mach->accel.dst_pitch) + dev->accel.dx, dest_dat);
                                        }
                                    } else {
                                        WRITE(mach->accel.dst_ge_offset + (dev->accel.dy * mach->accel.dst_pitch) + dev->accel.dx, dest_dat);
                                    }
                                }
                            }
                        }
                    }

                    if ((mono_src == 1) && !count) {
                        if (cpu_input) {
                            dev->force_busy = 0;
                            dev->force_busy2 = 0;
                            mach->force_busy = 0;
                        }
                        dev->fifo_idx = 0;
                        dev->accel.cmd_back = 1;
                        break;
                    } else if ((mono_src != 1) && (dev->accel.sx >= mach->accel.width)) {
                        if (cpu_input) {
                            dev->force_busy = 0;
                            dev->force_busy2 = 0;
                            mach->force_busy = 0;
                        }
                        dev->fifo_idx = 0;
                        dev->accel.cmd_back = 1;
                        break;
                    }

                    if (dev->bpp)
                        cpu_dat >>= 16;
                    else
                        cpu_dat >>= 8;

                    mach->accel.color_pattern_idx++;

                    if (mach->accel.color_pattern_idx > mach->accel.patt_len)
                        mach->accel.color_pattern_idx = 0;

                    if (mach->accel.linedraw_opt & 0x40) {
                        dev->accel.dy += mach->accel.stepy;
                        if ((frgd_sel == 3) || (bkgd_sel == 3))
                            dev->accel.cy += mach->accel.stepy;

                        if (dev->accel.err_term >= 0) {
                            dev->accel.err_term += dev->accel.destx_distp;
                            dev->accel.dx += mach->accel.stepx;
                            if ((frgd_sel == 3) || (bkgd_sel == 3))
                                dev->accel.cx += mach->accel.stepx;
                        } else {
                            dev->accel.err_term += dev->accel.desty_axstp;
                        }
                    } else {
                        dev->accel.dx += mach->accel.stepx;
                        if ((frgd_sel == 3) || (bkgd_sel == 3))
                            dev->accel.cx += mach->accel.stepx;

                        if (dev->accel.err_term >= 0) {
                            dev->accel.err_term += dev->accel.destx_distp;
                            dev->accel.dy += mach->accel.stepy;
                            if ((frgd_sel == 3) || (bkgd_sel == 3))
                                dev->accel.cy += mach->accel.stepy;
                        } else {
                            dev->accel.err_term += dev->accel.desty_axstp;
                        }
                    }

                    dev->accel.sx++;
                }
            }
            mach->accel.poly_fill = 0;
            dev->accel.cur_x = dev->accel.dx;
            dev->accel.cur_y = dev->accel.dy;
            break;

        case 2: /*Non-conforming BitBLT from dest_y_end register (0xaeee)*/
            if (!cpu_input) {
                mach->accel.stepx = 0;
                mach->accel.stepy = 0;

                dev->accel.dx = dev->accel.cur_x;
                if (dev->accel.cur_x >= 0x600)
                    dev->accel.dx |= ~0x5ff;

                dev->accel.dy = dev->accel.cur_y;
                if (dev->accel.cur_y >= 0x600)
                    dev->accel.dy |= ~0x5ff;

                /*Destination Width*/
                mach->accel.dx_first_row_start = dev->accel.cur_x;
                if (dev->accel.cur_x >= 0x600)
                    mach->accel.dx_first_row_start |= ~0x5ff;

                mach->accel.dx_start = mach->accel.dest_x_start;
                if (mach->accel.dest_x_start >= 0x600)
                    mach->accel.dx_start |= ~0x5ff;

                mach->accel.dx_end = mach->accel.dest_x_end;
                if (mach->accel.dest_x_end >= 0x600)
                    mach->accel.dx_end |= ~0x5ff;

                if (mach->accel.dx_end > mach->accel.dx_start) {
                    mach->accel.width = (mach->accel.dx_end - mach->accel.dx_start);
                    mach->accel.stepx = 1;
                } else if (mach->accel.dx_end < mach->accel.dx_start) {
                    mach->accel.width = (mach->accel.dx_start - mach->accel.dx_end);
                    mach->accel.stepx = -1;
                } else {
                    mach->accel.stepx = 1;
                    mach->accel.width = 0;
                }

                dev->accel.sx = 0;
                mach->accel.poly_fill = 0;

                /*Height*/
                mach->accel.dy_start = dev->accel.cur_y;
                if (dev->accel.cur_y >= 0x600)
                    mach->accel.dy_start |= ~0x5ff;

                mach->accel.dy_end = mach->accel.dest_y_end;
                if (mach->accel.dest_y_end >= 0x600)
                    mach->accel.dy_end |= ~0x5ff;

                if (mach->accel.dy_end > mach->accel.dy_start) {
                    mach->accel.height = (mach->accel.dy_end - mach->accel.dy_start);
                    mach->accel.stepy  = 1;
                } else if (mach->accel.dy_end < mach->accel.dy_start) {
                    mach->accel.height = (mach->accel.dy_start - mach->accel.dy_end);
                    mach->accel.stepy  = -1;
                } else {
                    mach->accel.height = 0;
                    mach->accel.stepy  = 1;
                }

                if (mach->accel.dp_config == 0x4011)
                    mach->accel.height++;

                if (mach->accel.height == 1) {
                    if (mach->accel.dx_end > mach->accel.dx_first_row_start) {
                        mach->accel.width = (mach->accel.dx_end - mach->accel.dx_first_row_start);
                        mach->accel.stepx = 1;
                    } else if (mach->accel.dx_end < mach->accel.dx_first_row_start) {
                        mach->accel.width = (mach->accel.dx_first_row_start - mach->accel.dx_end);
                        mach->accel.stepx = -1;
                    } else {
                        mach->accel.stepx = 1;
                        mach->accel.width = 0;
                    }
                }

                if (mach->accel.stepx == -1) {
                    if (dev->accel.dx > 0)
                        dev->accel.dx--;
                }

                dev->accel.sy = 0;
                dev->accel.dest = mach->accel.dst_ge_offset + (dev->accel.dy * mach->accel.dst_pitch);

                mach->accel.src_stepx = 0;
                /*Source Width*/
                dev->accel.cx = mach->accel.src_x;
                if (mach->accel.src_x >= 0x600)
                    dev->accel.cx |= ~0x5ff;

                dev->accel.cy = mach->accel.src_y;
                if (mach->accel.src_y >= 0x600)
                    dev->accel.cy |= ~0x5ff;

                mach->accel.sx_start = mach->accel.src_x_start;
                if (mach->accel.src_x_start >= 0x600)
                    mach->accel.sx_start |= ~0x5ff;

                mach->accel.sx_end = mach->accel.src_x_end;
                if (mach->accel.src_x_end >= 0x600)
                    mach->accel.sx_end |= ~0x5ff;

                if (mach->accel.sx_end > mach->accel.sx_start) {
                    mach->accel.src_width = (mach->accel.sx_end - mach->accel.sx_start);
                    mach->accel.src_stepx = 1;
                    if (mach->accel.dp_config == 0x6011)
                        mach_log("BitBLT: Src Positive X: wh(%d,%d), srcwidth = %d, coordinates: %d,%d px, start: %d, end: %d px, stepx = %d, dpconfig = %04x, oddwidth = %d, srcpitch = %d, dstpitch = %d, dststepx = %d, dststepy = %d, dx = %d, dy = %d.\n",
                             mach->accel.width, mach->accel.height, mach->accel.src_width, dev->accel.cx, dev->accel.cy, mach->accel.src_x_start, mach->accel.src_x_end,
                             mach->accel.src_stepx, mach->accel.dp_config, mach->accel.src_width & 1, mach->accel.src_pitch, mach->accel.dst_pitch, mach->accel.stepx, mach->accel.stepy, dev->accel.dx, dev->accel.dy);
                } else if (mach->accel.sx_end < mach->accel.sx_start) {
                    mach->accel.src_width = (mach->accel.sx_start - mach->accel.sx_end);
                    mach->accel.src_stepx = -1;
                    if (dev->accel.cx > 0)
                        dev->accel.cx--;

                    if (mach->accel.dp_config == 0x6011)
                        mach_log("BitBLT: Src Negative X: width = %d, coordinates: %d,%d px, end: %d px, stepx = %d, dpconfig = %04x, oddwidth = %d.\n",
                        mach->accel.src_width, dev->accel.cx, dev->accel.cy, mach->accel.src_x_end, mach->accel.src_stepx, mach->accel.dp_config,
                        mach->accel.src_width & 1);
                } else {
                    mach->accel.src_stepx = 1;
                    mach->accel.src_width = 0;
                    if (mach->accel.dp_config == 0x6011)
                        mach_log("BitBLT: Src Indeterminate X: width = %d, coordinates: %d,%d px, end: %d px, stepx = %d, dpconfig = %04x, oddwidth = %d.\n",
                        mach->accel.src_width, dev->accel.cx, dev->accel.cy, mach->accel.src_x_end, mach->accel.src_stepx,
                        mach->accel.dp_config, mach->accel.src_width & 1);
                }
                mach->accel.sx = 0;
                if (mach->accel.patt_data_idx < 0x10)
                    mach->accel.color_pattern_idx = mach->accel.patt_idx;
                else
                    mach->accel.color_pattern_idx = 0;

                dev->accel.src = mach->accel.src_ge_offset + (dev->accel.cy * mach->accel.src_pitch);

                if (mono_src == 1) {
                    if (mach->accel.mono_pattern_enable || mach->accel.block_write_mono_pattern_enable) {
                        mono_dat0 = mach->accel.mono_pattern_normal[0];
                        mono_dat0 |= (mach->accel.mono_pattern_normal[1] << 8);
                        mono_dat0 |= (mach->accel.mono_pattern_normal[2] << 16);
                        mono_dat0 |= (mach->accel.mono_pattern_normal[3] << 24);
                        mono_dat1 = mach->accel.mono_pattern_normal[4];
                        mono_dat1 |= (mach->accel.mono_pattern_normal[5] << 8);
                        mono_dat1 |= (mach->accel.mono_pattern_normal[6] << 16);
                        mono_dat1 |= (mach->accel.mono_pattern_normal[7] << 24);

                        mach_log("MonoData0=%x, MonoData1=%x, enable mono pattern=%x, dpconfig=%04x.\n", mono_dat0, mono_dat1, mach->accel.mono_pattern_enable, mach->accel.dp_config);
                        for (uint8_t y = 0; y < 8; y++) {
                            for (uint8_t x = 0; x < 8; x++) {
                                uint32_t temp                      = (y & 4) ? mono_dat1 : mono_dat0;
                                mach->accel.mono_pattern[y][7 - x] = (temp >> (x + ((y & 3) << 3))) & 1;
                            }
                        }
                    }
                }

                if (!dev->accel.cmd_back) {
                    if (mach_pixel_write(mach)) {
                        mach_log("Non-Conforming BitBLT Write pixtrans.\n");
                        dev->force_busy = 1;
                        dev->force_busy2 = 1;
                        mach->force_busy = 1;
                        dev->data_available  = 0;
                        dev->data_available2 = 0;
                        return;
                    } else if (mach_pixel_read(mach)) {
                        mach_log("Non-Conforming BitBLT Read pixtrans.\n");
                        dev->force_busy = 1;
                        dev->force_busy2 = 1;
                        mach->force_busy = 1;
                        dev->data_available  = 1;
                        dev->data_available2 = 1;
                        return;
                    }
                }
            }

            if (mono_src == 1) {
                if (!mach->accel.mono_pattern_enable && !mach->accel.block_write_mono_pattern_enable) {
                    if (((dev->accel_bpp == 24) && (frgd_sel != 5)) || (dev->accel_bpp != 24)) {
                        mix_dat = mach->accel.mono_pattern_normal[0] ^ ((mach->accel.patt_idx & 0x01) ? 0xff : 0);
                        dev->accel.temp_cnt = 8;
                    }
                }
            }

            if (mach->accel.dy_end == mach->accel.dy_start) {
                mach_log("No DEST.\n");
                if (cpu_input) {
                    dev->force_busy = 0;
                    dev->force_busy2 = 0;
                    mach->force_busy = 0;
                }
                dev->fifo_idx = 0;
                dev->accel.cmd_back = 1;
                return;
            }

            if ((mono_src == 3) || (bkgd_sel == 3) || (frgd_sel == 3)) {
                if (mach->accel.sx_end == mach->accel.sx_start) {
                    if (cpu_input) {
                        dev->force_busy = 0;
                        dev->force_busy2 = 0;
                        mach->force_busy = 0;
                    }
                    mach_log("No SRC.\n");
                    dev->fifo_idx = 0;
                    dev->accel.cmd_back = 1;
                    return;
                }
            }

            if (cpu_input) {
                if (mach->accel.dp_config == 0x3251) {
                    mach_log("DPCONFIG 3251: monosrc=%d, frgdsel=%d, bkgdsel=%d, pitch=%d.\n",
                             mono_src, frgd_sel, bkgd_sel, dev->pitch);
                    if (dev->accel.sy == mach->accel.height) {
                        mach_log("No Blit on DPCONFIG=3251.\n");
                        dev->force_busy = 0;
                        dev->force_busy2 = 0;
                        mach->force_busy = 0;
                        dev->fifo_idx = 0;
                        dev->accel.cmd_back = 1;
                        return;
                    }
                }
            }

            while (count--) {
                switch (mono_src) {
                    case 0:
                        mix = 0x01;
                        break;
                    case 1:
                        if (mach->accel.mono_pattern_enable || mach->accel.block_write_mono_pattern_enable)
                            mix = mach->accel.mono_pattern[dev->accel.dy & 7][dev->accel.dx & 7];
                        else if ((dev->accel_bpp == 24) && (frgd_sel == 5))
                            mix = 0x01;
                        else {
                            if (!dev->accel.temp_cnt) {
                                dev->accel.temp_cnt = 8;
                                mix_dat >>= 8;
                            }
                            mix = !!(mix_dat & 0x80);
                            dev->accel.temp_cnt--;
                            mix_dat <<= 1;
                            mix_dat |= 1;
                        }
                        break;
                    case 2:
                        if ((mach->accel.dp_config & 0x1000) || (mach->accel.dp_config & 0x04)) {
                            mix = mix_dat >> 0x1f;
                            mix_dat <<= 1;
                        } else {
                            if (mach->accel.dp_config & 0x200) {
                                mix = mix_dat & 0x01;
                                mix_dat >>= 1;
                            } else {
                                mix = !!(mix_dat & 0x80);
                                mix_dat <<= 1;
                                mix_dat |= 1;
                            }
                        }
                        break;
                    case 3:
                        READ(dev->accel.src + dev->accel.cx, mix);
                        mix = (mix & rd_mask) == rd_mask;
                        break;

                    default:
                        break;
                }

                if ((dev->accel.dx >= clip_l) &&
                    (dev->accel.dx <= clip_r) &&
                    (dev->accel.dy >= clip_t) &&
                    (dev->accel.dy <= clip_b)) {
                    dev->subsys_stat |= INT_GE_BSY;
                    if (mach_pixel_write(mach) || !cpu_input) {
                        if (mach->accel.dp_config & 0x02) {
                            READ(dev->accel.src + dev->accel.cx, poly_src);
                            poly_src = ((poly_src & rd_mask) == rd_mask);
                            if (poly_src)
                                mach->accel.poly_fill ^= 1;
                        }

                        if (mach->accel.poly_fill || !(mach->accel.dp_config & 0x02)) {
                            switch (mix ? frgd_sel : bkgd_sel) {
                                case 0:
                                    src_dat = bkgd_color;
                                    break;
                                case 1:
                                    src_dat = frgd_color;
                                    break;
                                case 2:
                                    src_dat = cpu_dat;
                                    break;
                                case 3:
                                    READ(dev->accel.src + dev->accel.cx, src_dat);
                                    if (mono_src == 3)
                                        src_dat = (src_dat & rd_mask) == rd_mask;
                                    break;
                                case 5:
                                    if (dev->bpp)
                                        src_dat = mach->accel.color_pattern_hicol[mach->accel.color_pattern_idx];
                                    else
                                        src_dat = mach->accel.color_pattern[mach->accel.color_pattern_idx];
                                    break;

                                default:
                                    break;
                            }

                            if ((dev->accel_bpp == 24) && (mono_src == 1) && (frgd_sel == 5) && !mach->accel.mono_pattern_enable) {
                                if (dev->accel.sy & 1) {
                                    READ(dev->accel.dest + dev->accel.dx - mach->accel.dst_pitch, dest_dat);
                                } else {
                                    READ(dev->accel.dest + dev->accel.dx, dest_dat);
                                }
                            } else {
                                READ(dev->accel.dest + dev->accel.dx, dest_dat);
                            }

                            switch (compare_mode) {
                                case 1:
                                    compare = 1;
                                    break;
                                case 2:
                                    compare = (dest_dat >= dest_cmp_clr) ? 0 : 1;
                                    break;
                                case 3:
                                    compare = (dest_dat < dest_cmp_clr) ? 0 : 1;
                                    break;
                                case 4:
                                    compare = (dest_dat != dest_cmp_clr) ? 0 : 1;
                                    break;
                                case 5:
                                    compare = (dest_dat == dest_cmp_clr) ? 0 : 1;
                                    break;
                                case 6:
                                    compare = (dest_dat <= dest_cmp_clr) ? 0 : 1;
                                    break;
                                case 7:
                                    compare = (dest_dat > dest_cmp_clr) ? 0 : 1;
                                    break;

                                default:
                                    break;
                            }

                            if (!compare) {
                                old_dest_dat = dest_dat;
                                MIX(mix, dest_dat, src_dat);
                                dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                            }

                            if (mach->accel.dp_config & 0x10) {
                                if ((dev->accel_bpp == 24) && (mono_src == 1) && (frgd_sel == 5) && !mach->accel.mono_pattern_enable) {
                                    if (dev->accel.sy & 1) {
                                        WRITE(dev->accel.dest + dev->accel.dx - mach->accel.dst_pitch, dest_dat);
                                    } else {
                                        WRITE(dev->accel.dest + dev->accel.dx, dest_dat);
                                    }
                                } else {
                                    WRITE(dev->accel.dest + dev->accel.dx, dest_dat);
                                }
                            }
                        }
                    }
                }

                if (dev->bpp)
                    cpu_dat >>= 16;
                else
                    cpu_dat >>= 8;

                if (mach->accel.dp_config == 0x2071)
                    mach_log("FontBlit: SX=%d, C(%d,%d), SRCWidth=%d, frgdmix=%d, bkgdmix=%d, rdmask=%04x, D(%d,%d), geoffset=%x, addr=%08x, 8bppdata=%02x, 16bppdata=%04x, vgabase=%06x.\n",
                             mach->accel.sx, dev->accel.cx, dev->accel.cy, mach->accel.src_width, dev->accel.frgd_mix & 0x1f,
                             dev->accel.bkgd_mix & 0x1f, rd_mask, dev->accel.dx, dev->accel.dy, dev->accel.ge_offset,
                             (dev->accel.src + dev->accel.cx) & dev->vram_mask, dev->vram[(dev->accel.src + dev->accel.cx) & dev->vram_mask], vram_w[(dev->accel.src + dev->accel.cx) & (dev->vram_mask >> 1)], svga->mapping.base);

                if ((mono_src == 3) || (frgd_sel == 3) || (bkgd_sel == 3) || (mach->accel.dp_config & 0x02)) {
                    dev->accel.cx += mach->accel.src_stepx;
                    mach->accel.sx++;
                    if (mach->accel.dp_config == 0x6011) {
                        mach_log("DX=%d, DY=%d, SX=%d, SY=%d, SRCSX=%d, SRCWIDTH=%d, CX=%d, CY=%d, srcydir=%d, srcoffset=%08x.\n", dev->accel.dx, dev->accel.dy, dev->accel.sx, dev->accel.sy, mach->accel.sx - 1, mach->accel.src_width, dev->accel.cx - mach->accel.src_stepx, dev->accel.cy, mach->accel.src_y_dir, mach->accel.src_ge_offset);
                        if (mach->accel.sx >= mach->accel.src_width) {
                            mach->accel.sx = 0;
                            if (mach->accel.src_stepx == -1)
                                dev->accel.cx += mach->accel.src_width;
                            else
                                dev->accel.cx -= mach->accel.src_width;

                            dev->accel.cy += mach->accel.stepy;
                            dev->accel.src = mach->accel.src_ge_offset + (dev->accel.cy * mach->accel.src_pitch);
                        }
                    } else {
                        if (mach->accel.sx >= mach->accel.src_width) {
                            mach->accel.sx = 0;
                            if (mach->accel.src_stepx == -1)
                                dev->accel.cx += mach->accel.src_width;
                            else
                                dev->accel.cx -= mach->accel.src_width;

                            dev->accel.cy += mach->accel.src_y_dir;
                            dev->accel.src = mach->accel.src_ge_offset + (dev->accel.cy * mach->accel.src_pitch);
                        }
                    }
                }

                mach->accel.color_pattern_idx++;

                if ((mono_src == 1) && !mach->accel.mono_pattern_enable && !mach->accel.block_write_mono_pattern_enable && (frgd_sel == 5) && (dev->accel_bpp == 24)) {
                    if (mach->accel.color_pattern_idx > 2)
                        mach->accel.color_pattern_idx = 0;
                } else {
                    if (mach->accel.color_pattern_idx > mach->accel.patt_len)
                        mach->accel.color_pattern_idx = 0;
                }

                dev->accel.dx += mach->accel.stepx;
                dev->accel.sx++;
                if ((dev->accel.sx >= mach->accel.width) || (dev->accel.dx >= 0x600)) {
                    dev->accel.sx = 0;
                    if (mach->accel.stepx == -1)
                        dev->accel.dx += mach->accel.width;
                    else
                        dev->accel.dx -= mach->accel.width;

                    dev->accel.dy += mach->accel.stepy;
                    dev->accel.sy++;

                    mach->accel.poly_fill = 0;
                    dev->accel.dest = mach->accel.dst_ge_offset + (dev->accel.dy * mach->accel.dst_pitch);

                    if (dev->accel.sy >= mach->accel.height) {
                        if (cpu_input) {
                            dev->force_busy = 0;
                            dev->force_busy2 = 0;
                            mach->force_busy = 0;
                        }
                        dev->fifo_idx = 0;
                        dev->accel.cmd_back = 1;
                        if ((mono_src == 2) || (mono_src == 3) || (frgd_sel == 3) || (bkgd_sel == 3) || (mach->accel.dp_config & 0x02))
                            return;
                        if ((mono_src == 1) && (frgd_sel == 5) && (dev->accel_bpp == 24))
                            return;
                        dev->accel.cur_x = dev->accel.dx;
                        dev->accel.cur_y = dev->accel.dy;
                        return;
                    }
                }
            }
            break;

        case 3: /*Direct Linedraw (Polyline) from linedraw indexes (0xfeee)*/
        case 4:
            if (!cpu_input) {
                dev->accel.cx = dev->accel.cur_x;
                dev->accel.cy = dev->accel.cur_y;

                if (dev->accel.cur_x >= 0x600) {
                    mach_log("Linedraw XOver = %d.\n", dev->accel.cur_x);
                    dev->accel.cx |= ~0x5ff;
                }
                if (dev->accel.cur_y >= 0x600) {
                    mach_log("Linedraw YOver = %d.\n", dev->accel.cur_y);
                    dev->accel.cy |= ~0x5ff;
                }

                dev->accel.dx = ABS(mach->accel.cx_end_line - dev->accel.cx) << 1;
                dev->accel.dy = ABS(mach->accel.cy_end_line - dev->accel.cy) << 1;

                mach->accel.stepx = (mach->accel.cx_end_line < dev->accel.cx) ? -1 : 1;
                mach->accel.stepy = (mach->accel.cy_end_line < dev->accel.cy) ? -1 : 1;

                dev->accel.sx = 0;

                mach_log("Linedraw: c(%d,%d), d(%d,%d), cend(%d,%d), bounds: l=%d, r=%d, t=%d, b=%d.\n",
                         dev->accel.cur_x, dev->accel.cur_y, dev->accel.dx, dev->accel.dy, mach->accel.cx_end_line,
                         mach->accel.cy_end_line, mach->accel.bleft, mach->accel.bright, mach->accel.btop, mach->accel.bbottom);

                if (!dev->accel.cmd_back) {
                    if (mach_pixel_write(mach)) {
                        mach_log("Direct Linedraw Write pixtrans.\n");
                        dev->force_busy = 1;
                        dev->force_busy2 = 1;
                        mach->force_busy = 1;
                        dev->data_available  = 0;
                        dev->data_available2 = 0;
                        return;
                    } else if (mach_pixel_read(mach)) {
                        mach_log("Direct Linedraw Read pixtrans.\n");
                        dev->force_busy = 1;
                        dev->force_busy2 = 1;
                        mach->force_busy = 1;
                        dev->data_available  = 1;
                        dev->data_available2 = 1;
                        return;
                    }
                }
            }

            if (mono_src == 1) {
                mix_dat = mach->accel.mono_pattern_normal[0];
                dev->accel.temp_cnt = 8;
            }

            count = (dev->accel.dx > dev->accel.dy) ? (dev->accel.dx >> 1) : (dev->accel.dy >> 1);
            mach->accel.width = count;

            if (dev->accel.dx > dev->accel.dy) {
                mach->accel.err = (dev->accel.dy - dev->accel.dx) >> 1;
                if (mono_src == 1) {
                    while (count--) {
                        if (!dev->accel.temp_cnt) {
                            dev->accel.temp_cnt = 8;
                            mix_dat >>= 8;
                        }
                        mix = (mix_dat & 0x80);
                        dev->accel.temp_cnt--;
                        mix_dat <<= 1;
                        mix_dat |= 1;

                        if ((dev->accel.cx >= clip_l) &&
                            (dev->accel.cx <= clip_r) &&
                            (dev->accel.cy >= clip_t) &&
                            (dev->accel.cy <= clip_b)) {
                            dev->subsys_stat |= INT_GE_BSY;
                            mach->accel.clip_overrun = 0;
                            if (mach_pixel_write(mach) || !cpu_input) {
                                switch (mix ? frgd_sel : bkgd_sel) {
                                    case 0:
                                        src_dat = bkgd_color;
                                        break;
                                    case 1:
                                        src_dat = frgd_color;
                                        break;
                                    case 2:
                                        src_dat = cpu_dat;
                                        break;
                                    case 3:
                                        src_dat = 0;
                                        break;
                                    case 5:
                                        if (dev->bpp)
                                            src_dat = mach->accel.color_pattern_hicol[mach->accel.color_pattern_idx];
                                        else
                                            src_dat = mach->accel.color_pattern[mach->accel.color_pattern_idx];
                                        break;

                                    default:
                                        break;
                                }

                                READ(mach->accel.dst_ge_offset + (dev->accel.cy * mach->accel.dst_pitch) + dev->accel.cx, dest_dat);

                                switch (compare_mode) {
                                    case 1:
                                        compare = 1;
                                        break;
                                    case 2:
                                        compare = (dest_dat >= dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 3:
                                        compare = (dest_dat < dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 4:
                                        compare = (dest_dat != dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 5:
                                        compare = (dest_dat == dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 6:
                                        compare = (dest_dat <= dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 7:
                                        compare = (dest_dat > dest_cmp_clr) ? 0 : 1;
                                        break;

                                    default:
                                        break;
                                }

                                if (!compare) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                }
                                if ((mach->accel.dp_config & 0x10) && (cmd_type == 3)) {
                                    WRITE(mach->accel.dst_ge_offset + (dev->accel.cy * mach->accel.dst_pitch) + dev->accel.cx, dest_dat);
                                }
                            }
                        } else
                            mach->accel.clip_overrun = ((mach->accel.clip_overrun + 1) & 0x0f);

                        if (!count) {
                            if (cpu_input) {
                                dev->force_busy = 0;
                                dev->force_busy2 = 0;
                                mach->force_busy = 0;
                            }
                            dev->fifo_idx = 0;
                            dev->accel.cmd_back = 1;
                            break;
                        }

                        if (dev->bpp)
                            cpu_dat >>= 16;
                        else
                            cpu_dat >>= 8;

                        mach->accel.color_pattern_idx++;

                        if (mach->accel.color_pattern_idx > mach->accel.patt_len)
                            mach->accel.color_pattern_idx = 0;

                        if (mach->accel.err >= 0) {
                            dev->accel.cy += mach->accel.stepy;
                            mach->accel.err -= dev->accel.dx;
                        }
                        dev->accel.cx += mach->accel.stepx;
                        mach->accel.err += dev->accel.dy;
                    }
                } else {
                    while (count--) {
                        switch (mono_src) {
                            case 0:
                            case 3:
                                mix = 1;
                                break;
                            case 2:
                                if (mach->accel.dp_config & 0x1000) {
                                    mix = mix_dat >> 0x1f;
                                    mix_dat <<= 1;
                                } else {
                                    if (mach->accel.dp_config & 0x200) {
                                        mix = mix_dat & 1;
                                        mix_dat >>= 1;
                                    } else {
                                        mix = mix_dat & 0x80;
                                        mix_dat <<= 1;
                                        mix_dat |= 1;
                                    }
                                }
                                break;

                            default:
                                break;
                        }

                        if ((dev->accel.cx >= clip_l) &&
                            (dev->accel.cx <= clip_r) &&
                            (dev->accel.cy >= clip_t) &&
                            (dev->accel.cy <= clip_b)) {
                            dev->subsys_stat |= INT_GE_BSY;
                            mach->accel.clip_overrun = 0;
                            if (mach_pixel_write(mach) || !cpu_input) {
                                if (mach->accel.linedraw_opt & 0x02) {
                                    READ(mach->accel.src_ge_offset + (dev->accel.cy * mach->accel.src_pitch) + dev->accel.cx, poly_src);
                                    if (poly_src)
                                        mach->accel.poly_fill ^= 1;
                                }

                                switch (mix ? frgd_sel : bkgd_sel) {
                                    case 0:
                                        src_dat = bkgd_color;
                                        break;
                                    case 1:
                                        src_dat = frgd_color;
                                        break;
                                    case 2:
                                        src_dat = cpu_dat;
                                        break;
                                    case 3:
                                        src_dat = 0;
                                        break;
                                    case 5:
                                        if (dev->bpp)
                                            src_dat = mach->accel.color_pattern_hicol[mach->accel.color_pattern_idx];
                                        else
                                            src_dat = mach->accel.color_pattern[mach->accel.color_pattern_idx];
                                        break;

                                    default:
                                        break;
                                }

                                READ(mach->accel.dst_ge_offset + (dev->accel.cy * mach->accel.dst_pitch) + dev->accel.cx, dest_dat);

                                switch (compare_mode) {
                                    case 1:
                                        compare = 1;
                                        break;
                                    case 2:
                                        compare = (dest_dat >= dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 3:
                                        compare = (dest_dat < dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 4:
                                        compare = (dest_dat != dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 5:
                                        compare = (dest_dat == dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 6:
                                        compare = (dest_dat <= dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 7:
                                        compare = (dest_dat > dest_cmp_clr) ? 0 : 1;
                                        break;

                                    default:
                                        break;
                                }

                                if (!compare) {
                                    old_dest_dat = dest_dat;
                                    if (mach->accel.poly_fill || !(mach->accel.linedraw_opt & 0x02)) {
                                        MIX(mix, dest_dat, src_dat);
                                    }
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                }

                                if ((mach->accel.dp_config & 0x10) && (cmd_type == 3)) {
                                    if (mach->accel.linedraw_opt & 0x04) {
                                        if (dev->accel.sx < mach->accel.width) {
                                            WRITE(mach->accel.dst_ge_offset + (dev->accel.cy * mach->accel.dst_pitch) + dev->accel.cx, dest_dat);
                                        }
                                    } else {
                                        WRITE(mach->accel.dst_ge_offset + (dev->accel.cy * mach->accel.dst_pitch) + dev->accel.cx, dest_dat);
                                    }
                                }
                            }
                        } else
                            mach->accel.clip_overrun = ((mach->accel.clip_overrun + 1) & 0x0f);

                        if (dev->accel.sx >= mach->accel.width) {
                            if (cpu_input) {
                                dev->force_busy = 0;
                                dev->force_busy2 = 0;
                                mach->force_busy = 0;
                            }
                            dev->fifo_idx = 0;
                            dev->accel.cmd_back = 1;
                            break;
                        }

                        if (dev->bpp)
                            cpu_dat >>= 16;
                        else
                            cpu_dat >>= 8;

                        mach->accel.color_pattern_idx++;

                        if (mach->accel.color_pattern_idx > mach->accel.patt_len)
                            mach->accel.color_pattern_idx = 0;

                        if (mach->accel.err >= 0) {
                            dev->accel.cy += mach->accel.stepy;
                            mach->accel.err -= dev->accel.dx;
                        }
                        dev->accel.cx += mach->accel.stepx;
                        mach->accel.err += dev->accel.dy;

                        dev->accel.sx++;
                    }
                }
            } else {
                mach->accel.err = (dev->accel.dx - dev->accel.dy) >> 1;
                if (mono_src == 1) {
                    while (count--) {
                        if (dev->accel.temp_cnt == 0) {
                            dev->accel.temp_cnt = 8;
                            mix_dat >>= 8;
                        }
                        mix = (mix_dat & 0x80);
                        dev->accel.temp_cnt--;
                        mix_dat <<= 1;
                        mix_dat |= 1;

                        if ((dev->accel.cx >= clip_l) &&
                            (dev->accel.cx <= clip_r) &&
                            (dev->accel.cy >= clip_t) &&
                            (dev->accel.cy <= clip_b)) {
                            dev->subsys_stat |= INT_GE_BSY;
                            mach->accel.clip_overrun = 0;
                            if (mach_pixel_write(mach) || !cpu_input) {
                                switch (mix ? frgd_sel : bkgd_sel) {
                                    case 0:
                                        src_dat = bkgd_color;
                                        break;
                                    case 1:
                                        src_dat = frgd_color;
                                        break;
                                    case 2:
                                        src_dat = cpu_dat;
                                        break;
                                    case 3:
                                        src_dat = 0;
                                        break;
                                    case 5:
                                        if (dev->bpp)
                                            src_dat = mach->accel.color_pattern_hicol[mach->accel.color_pattern_idx];
                                        else
                                            src_dat = mach->accel.color_pattern[mach->accel.color_pattern_idx];
                                        break;

                                    default:
                                        break;
                                }

                                READ(mach->accel.dst_ge_offset + (dev->accel.cy * mach->accel.dst_pitch) + dev->accel.cx, dest_dat);

                                switch (compare_mode) {
                                    case 1:
                                        compare = 1;
                                        break;
                                    case 2:
                                        compare = (dest_dat >= dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 3:
                                        compare = (dest_dat < dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 4:
                                        compare = (dest_dat != dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 5:
                                        compare = (dest_dat == dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 6:
                                        compare = (dest_dat <= dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 7:
                                        compare = (dest_dat > dest_cmp_clr) ? 0 : 1;
                                        break;

                                    default:
                                        break;
                                }

                                if (!compare) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                }

                                if ((mach->accel.dp_config & 0x10) && (cmd_type == 3)) {
                                    WRITE(mach->accel.dst_ge_offset + (dev->accel.cy * mach->accel.dst_pitch) + dev->accel.cx, dest_dat);
                                }
                            }
                        } else
                            mach->accel.clip_overrun = ((mach->accel.clip_overrun + 1) & 0x0f);

                        if (!count) {
                            if (cpu_input) {
                                dev->force_busy = 0;
                                dev->force_busy2 = 0;
                                mach->force_busy = 0;
                            }
                            dev->fifo_idx = 0;
                            dev->accel.cmd_back = 1;
                            break;
                        }

                        if (dev->bpp)
                            cpu_dat >>= 16;
                        else
                            cpu_dat >>= 8;

                        mach->accel.color_pattern_idx++;

                        if (mach->accel.color_pattern_idx > mach->accel.patt_len)
                            mach->accel.color_pattern_idx = 0;

                        if (mach->accel.err >= 0) {
                            dev->accel.cx += mach->accel.stepx;
                            mach->accel.err -= dev->accel.dy;
                        }
                        dev->accel.cy += mach->accel.stepy;
                        mach->accel.err += dev->accel.dx;
                    }
                } else {
                    while (count--) {
                        switch (mono_src) {
                            case 0:
                            case 3:
                                mix = 1;
                                break;
                            case 2:
                                if (mach->accel.dp_config & 0x1000) {
                                    mix = mix_dat >> 0x1f;
                                    mix_dat <<= 1;
                                } else {
                                    if (mach->accel.dp_config & 0x200) {
                                        mix = mix_dat & 1;
                                        mix_dat >>= 1;
                                    } else {
                                        mix = mix_dat & 0x80;
                                        mix_dat <<= 1;
                                        mix_dat |= 1;
                                    }
                                }
                                break;

                            default:
                                break;
                        }

                        if ((dev->accel.cx >= clip_l) &&
                            (dev->accel.cx <= clip_r) &&
                            (dev->accel.cy >= clip_t) &&
                            (dev->accel.cy <= clip_b)) {
                            dev->subsys_stat |= INT_GE_BSY;
                            mach->accel.clip_overrun = 0;
                            if (mach_pixel_write(mach) || !cpu_input) {
                                switch (mix ? frgd_sel : bkgd_sel) {
                                    case 0:
                                        src_dat = bkgd_color;
                                        break;
                                    case 1:
                                        src_dat = frgd_color;
                                        break;
                                    case 2:
                                        src_dat = cpu_dat;
                                        break;
                                    case 3:
                                        src_dat = 0;
                                        break;
                                    case 5:
                                        if (dev->bpp)
                                            src_dat = mach->accel.color_pattern_hicol[mach->accel.color_pattern_idx];
                                        else
                                            src_dat = mach->accel.color_pattern[mach->accel.color_pattern_idx];
                                        break;

                                    default:
                                        break;
                                }

                                READ(mach->accel.dst_ge_offset + (dev->accel.cy * mach->accel.dst_pitch) + dev->accel.cx, dest_dat);

                                switch (compare_mode) {
                                    case 1:
                                        compare = 1;
                                        break;
                                    case 2:
                                        compare = (dest_dat >= dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 3:
                                        compare = (dest_dat < dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 4:
                                        compare = (dest_dat != dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 5:
                                        compare = (dest_dat == dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 6:
                                        compare = (dest_dat <= dest_cmp_clr) ? 0 : 1;
                                        break;
                                    case 7:
                                        compare = (dest_dat > dest_cmp_clr) ? 0 : 1;
                                        break;

                                    default:
                                        break;
                                }

                                if (!compare) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                }

                                if ((mach->accel.dp_config & 0x10) && (cmd_type == 3)) {
                                    if (mach->accel.linedraw_opt & 0x04) {
                                        if (dev->accel.sx < mach->accel.width) {
                                            WRITE(mach->accel.dst_ge_offset + (dev->accel.cy * mach->accel.dst_pitch) + dev->accel.cx, dest_dat);
                                        }
                                    } else {
                                        WRITE(mach->accel.dst_ge_offset + (dev->accel.cy * mach->accel.dst_pitch) + dev->accel.cx, dest_dat);
                                    }
                                }
                            }
                        } else
                            mach->accel.clip_overrun = ((mach->accel.clip_overrun + 1) & 0x0f);

                        if (dev->accel.sx >= mach->accel.width) {
                            if (cpu_input) {
                                dev->force_busy = 0;
                                dev->force_busy2 = 0;
                                mach->force_busy = 0;
                            }
                            dev->fifo_idx = 0;
                            dev->accel.cmd_back = 1;
                            break;
                        }

                        if (dev->bpp)
                            cpu_dat >>= 16;
                        else
                            cpu_dat >>= 8;

                        mach->accel.color_pattern_idx++;

                        if (mach->accel.color_pattern_idx > mach->accel.patt_len)
                            mach->accel.color_pattern_idx = 0;

                        if (mach->accel.err >= 0) {
                            dev->accel.cx += mach->accel.stepx;
                            mach->accel.err -= dev->accel.dy;
                        }
                        dev->accel.cy += mach->accel.stepy;
                        mach->accel.err += dev->accel.dx;

                        dev->accel.sx++;
                    }
                }
            }
            mach->accel.poly_fill = 0;
            mach->accel.line_array[(cmd_type == 4) ? 4 : 0] = dev->accel.cx;
            mach->accel.line_array[(cmd_type == 4) ? 5 : 1] = dev->accel.cy;
            dev->accel.cur_x = dev->accel.cx;
            dev->accel.cur_y = dev->accel.cy;
            mach_log("Done: %i, %i\n", dev->accel.cur_x, dev->accel.cur_y);
            break;

        case 5: /*Horizontal Raster Draw from scan_to_x register (0xcaee)*/
            if (!cpu_input) {
                mach->accel.stepx = 0;
                mach->accel.stepy = 0;

                dev->accel.dx = dev->accel.cur_x;
                if (dev->accel.cur_x >= 0x600)
                    dev->accel.dx |= ~0x5ff;
                dev->accel.dy = dev->accel.cur_y;
                if (dev->accel.cur_y >= 0x600)
                    dev->accel.dy |= ~0x5ff;

                /*Destination Width*/
                mach->accel.dx_start = dev->accel.cur_x;
                if (dev->accel.cur_x >= 0x600)
                    mach->accel.dx_start |= ~0x5ff;
                mach->accel.dx_end = mach->accel.scan_to_x;
                if (mach->accel.scan_to_x >= 0x600)
                    mach->accel.dx_end |= ~0x5ff;

                if (mach->accel.dx_end > mach->accel.dx_start) {
                    mach->accel.width = (mach->accel.dx_end - mach->accel.dx_start);
                    mach->accel.stepx = 1;
                } else if (mach->accel.dx_end < mach->accel.dx_start) {
                    mach->accel.width = (mach->accel.dx_start - mach->accel.dx_end);
                    mach->accel.stepx = -1;
                    if (dev->accel.dx > 0)
                        dev->accel.dx--;
                } else {
                    mach->accel.stepx = 1;
                    mach->accel.width = 0;
                }

                dev->accel.sx = 0;

                /*Step Y*/
                mach->accel.dy_start = dev->accel.cur_y;
                if (dev->accel.cur_y >= 0x600)
                    mach->accel.dy_start |= ~0x5ff;
                mach->accel.dy_end = mach->accel.dest_y_end;
                if (mach->accel.dest_y_end >= 0x600)
                    mach->accel.dy_end |= ~0x5ff;

                if (mach->accel.dy_end > mach->accel.dy_start) {
                    dev->accel.sy = (mach->accel.dy_end - mach->accel.dy_start);
                    mach->accel.stepy = 1;
                } else if (mach->accel.dy_end < mach->accel.dy_start) {
                    dev->accel.sy = (mach->accel.dy_start - mach->accel.dy_end);
                    mach->accel.stepy = -1;
                } else {
                    mach->accel.stepy = 0;
                    dev->accel.sy = 0;
                }

                dev->accel.dest = mach->accel.dst_ge_offset + (dev->accel.dy * mach->accel.dst_pitch);
                mach->accel.src_stepx = 0;

                /*Source Width*/
                dev->accel.cx = mach->accel.src_x;
                if (mach->accel.src_x >= 0x600)
                    dev->accel.cx |= ~0x5ff;
                dev->accel.cy = mach->accel.src_y;
                if (mach->accel.src_y >= 0x600)
                    dev->accel.cy |= ~0x5ff;

                mach->accel.sx_start = mach->accel.src_x_start;
                if (mach->accel.src_x_start >= 0x600)
                    mach->accel.sx_start |= ~0x5ff;

                mach->accel.sx_end = mach->accel.src_x_end;
                if (mach->accel.src_x_end >= 0x600)
                    mach->accel.sx_end |= ~0x5ff;

                if (mach->accel.sx_end > mach->accel.sx_start) {
                    mach->accel.src_width = (mach->accel.sx_end - mach->accel.sx_start);
                    mach->accel.src_stepx = 1;
                } else if (mach->accel.sx_end < mach->accel.sx_start) {
                    mach->accel.src_width = (mach->accel.sx_start - mach->accel.sx_end);
                    mach->accel.src_stepx = -1;
                    if (dev->accel.cx > 0)
                        dev->accel.cx--;
                } else {
                    mach->accel.src_stepx = 1;
                    mach->accel.src_width = 0;
                }

                mach->accel.sx = 0;
                dev->accel.src = mach->accel.src_ge_offset + (dev->accel.cy * mach->accel.src_pitch);

                mach_log("ScanToX: Parameters=%04x: xbit=%d, ybit=%d, widthbit=%d, DX=%d, DY=%d, CX=%d, CY=%d, dstwidth=%d, srcwidth=%d, height=%d, frmix=%02x, colpatidx=%d, srcpitch=%d, dstpitch=%d, scantox=%d.\n",
                      mach->accel.dp_config, dev->accel.dx & 1, dev->accel.dy & 1, mach->accel.width & 1, dev->accel.dx, dev->accel.dy, dev->accel.cx, dev->accel.cy, mach->accel.width, mach->accel.src_width, dev->accel.sy, dev->accel.frgd_mix & 0x1f, mach->accel.color_pattern_idx, mach->accel.src_pitch, mach->accel.dst_pitch, mach->accel.scan_to_x);

                if (!dev->accel.cmd_back) {
                    if (mach_pixel_write(mach)) {
                        mach_log("Scan To X Write pixtrans.\n");
                        dev->force_busy = 1;
                        dev->force_busy2 = 1;
                        mach->force_busy = 1;
                        dev->data_available  = 0;
                        dev->data_available2 = 0;
                        return;
                    } else if (mach_pixel_read(mach)) {
                        mach_log("Scan To X Read pixtrans.\n");
                        dev->force_busy = 1;
                        dev->force_busy2 = 1;
                        mach->force_busy = 1;
                        dev->data_available  = 1;
                        dev->data_available2 = 1;
                        return;
                    }
                }
            }

            if ((dev->accel_bpp == 24) && (mach->accel.dp_config == 0x6211)) {
                int64_t cx;
                int64_t cy;

                cx = mach->accel.src_x_scan;
                cy = mach->accel.src_y_scan;

                if (mach->accel.src_stepx == -1) {
                    if (cx > 0)
                        cx--;
                }

                dev->accel.src = mach->accel.src_ge_offset + (cy * mach->accel.src_pitch);

                while (1) {
                    mix = 1;

                    if ((dev->accel.dx >= clip_l) &&
                        (dev->accel.dx <= clip_r) &&
                        (dev->accel.dy >= clip_t) &&
                        (dev->accel.dy <= clip_b)) {
                        dev->subsys_stat |= INT_GE_BSY;
                        READ(dev->accel.src + cx, src_dat);
                        READ(dev->accel.dest + dev->accel.dx, dest_dat);

                        switch (compare_mode) {
                            case 1:
                                compare = 1;
                                break;
                            case 2:
                                compare = (dest_dat >= dest_cmp_clr) ? 0 : 1;
                                break;
                            case 3:
                                compare = (dest_dat < dest_cmp_clr) ? 0 : 1;
                                break;
                            case 4:
                                compare = (dest_dat != dest_cmp_clr) ? 0 : 1;
                                break;
                            case 5:
                                compare = (dest_dat == dest_cmp_clr) ? 0 : 1;
                                break;
                            case 6:
                                compare = (dest_dat <= dest_cmp_clr) ? 0 : 1;
                                break;
                            case 7:
                                compare = (dest_dat > dest_cmp_clr) ? 0 : 1;
                                break;

                            default:
                                break;
                        }

                        if (!compare) {
                            old_dest_dat = dest_dat;
                            MIX(mix, dest_dat, src_dat);
                            dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                        }

                        WRITE(dev->accel.dest + dev->accel.dx, dest_dat);
                    }

                    cx += mach->accel.src_stepx;
                    mach->accel.sx++;
                    if (mach->accel.sx >= mach->accel.src_width) {
                        mach->accel.sx = 0;
                        cx = mach->accel.src_x_scan;
                        if (mach->accel.src_stepx == -1) {
                            if (cx > 0)
                                cx--;
                        }

                        cy += mach->accel.src_y_dir;
                        dev->accel.src = mach->accel.src_ge_offset + (cy * mach->accel.src_pitch);
                    }

                    dev->accel.dx += mach->accel.stepx;
                    dev->accel.sx++;
                    if (dev->accel.sx >= mach->accel.width) {
                        dev->accel.sx = 0;
                        dev->accel.dy += mach->accel.stepy;
                        dev->accel.dest = mach->accel.dst_ge_offset + (dev->accel.dy * mach->accel.dst_pitch);

                        if (mach->accel.line_idx == 2) {
                            mach->accel.line_array[0] = dev->accel.dx;
                            mach->accel.line_array[4] = dev->accel.dx;
                        }
                        if (dev->accel.sy >= 0)
                            dev->accel.sy--;

                        dev->fifo_idx = 0;
                        dev->force_busy = 0;
                        dev->force_busy2 = 0;
                        mach->force_busy = 0;
                        dev->accel.cmd_back = 1;
                        dev->accel.cur_x = dev->accel.dx;
                        dev->accel.cur_y = dev->accel.dy;
                        mach->accel.src_x_scan = cx;
                        mach->accel.src_y_scan = cy;
                        return;
                    }
                }
                return;
            }

            if (mono_src == 1) {
                count               = mach->accel.width;
                mix_dat             = mach->accel.mono_pattern_normal[0];
                dev->accel.temp_cnt = 8;
            }

            while (count--) {
                switch (mono_src) {
                    case 0:
                        mix = 1;
                        break;
                    case 1:
                        if (!dev->accel.temp_cnt) {
                            dev->accel.temp_cnt = 8;
                            mix_dat >>= 8;
                        }
                        mix = (mix_dat & 0x80);
                        dev->accel.temp_cnt--;
                        mix_dat <<= 1;
                        mix_dat |= 1;
                        break;
                    case 2:
                        if (mach->accel.dp_config & 0x1000) {
                            mix = mix_dat >> 0x1f;
                            mix_dat <<= 1;
                        } else {
                            if (mach->accel.dp_config & 0x200) {
                                mix = mix_dat & 1;
                                mix_dat >>= 1;
                            } else {
                                mix = mix_dat & 0x80;
                                mix_dat <<= 1;
                                mix_dat |= 1;
                            }
                        }
                        break;
                    case 3:
                        READ(dev->accel.src + dev->accel.cx, mix);
                        mix = (mix & rd_mask) == rd_mask;
                        break;

                    default:
                        break;
                }

                if ((dev->accel.dx >= clip_l) &&
                    (dev->accel.dx <= clip_r) &&
                    (dev->accel.dy >= clip_t) &&
                    (dev->accel.dy <= clip_b)) {
                    dev->subsys_stat |= INT_GE_BSY;
                    if (mach_pixel_write(mach) || !cpu_input) {
                        switch (mix ? frgd_sel : bkgd_sel) {
                            case 0:
                                src_dat = bkgd_color;
                                break;
                            case 1:
                                src_dat = frgd_color;
                                break;
                            case 2:
                                src_dat = cpu_dat;
                                break;
                            case 3:
                                READ(dev->accel.src + dev->accel.cx, src_dat);
                                if (mono_src == 3)
                                    src_dat = (src_dat & rd_mask) == rd_mask;
                                break;
                            case 5:
                                if (dev->bpp)
                                    src_dat = mach->accel.color_pattern_hicol[mach->accel.color_pattern_idx];
                                else
                                    src_dat = mach->accel.color_pattern[mach->accel.color_pattern_idx];
                                break;

                            default:
                                break;
                        }

                        READ(dev->accel.dest + dev->accel.dx, dest_dat);

                        switch (compare_mode) {
                            case 1:
                                compare = 1;
                                break;
                            case 2:
                                compare = (dest_dat >= dest_cmp_clr) ? 0 : 1;
                                break;
                            case 3:
                                compare = (dest_dat < dest_cmp_clr) ? 0 : 1;
                                break;
                            case 4:
                                compare = (dest_dat != dest_cmp_clr) ? 0 : 1;
                                break;
                            case 5:
                                compare = (dest_dat == dest_cmp_clr) ? 0 : 1;
                                break;
                            case 6:
                                compare = (dest_dat <= dest_cmp_clr) ? 0 : 1;
                                break;
                            case 7:
                                compare = (dest_dat > dest_cmp_clr) ? 0 : 1;
                                break;

                            default:
                                break;
                        }

                        if (!compare) {
                            old_dest_dat = dest_dat;
                            MIX(mix, dest_dat, src_dat);
                            dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                        }

                        if (mach->accel.dp_config & 0x10) {
                            WRITE(dev->accel.dest + dev->accel.dx, dest_dat);
                        }
                    }
                }

                if (dev->bpp)
                    cpu_dat >>= 16;
                else
                    cpu_dat >>= 8;

                dev->accel.cx += mach->accel.src_stepx;
                mach->accel.sx++;
                if (mach->accel.sx >= mach->accel.src_width) {
                    mach->accel.sx = 0;
                    if (mach->accel.src_stepx == -1)
                        dev->accel.cx += mach->accel.src_width;
                    else
                        dev->accel.cx -= mach->accel.src_width;

                    dev->accel.cy += mach->accel.src_y_dir;
                    dev->accel.src = mach->accel.src_ge_offset + (dev->accel.cy * mach->accel.src_pitch);
                }

                mach->accel.color_pattern_idx++;

                if (mach->accel.color_pattern_idx > mach->accel.patt_len)
                    mach->accel.color_pattern_idx = 0;

                dev->accel.dx += mach->accel.stepx;
                dev->accel.sx++;
                if (dev->accel.sx >= mach->accel.width) {
                    dev->accel.sx = 0;
                    dev->accel.dy += mach->accel.stepy;
                    dev->accel.dest = mach->accel.dst_ge_offset + (dev->accel.dy * mach->accel.dst_pitch);

                    if (mach->accel.line_idx == 2) {
                        mach->accel.line_array[0] = dev->accel.dx;
                        mach->accel.line_array[4] = dev->accel.dx;
                    }
                    if (dev->accel.sy >= 0)
                        dev->accel.sy--;

                    dev->fifo_idx = 0;
                    dev->force_busy = 0;
                    dev->force_busy2 = 0;
                    mach->force_busy = 0;
                    dev->accel.cmd_back = 1;
                    return;
                }
            }
            break;

        default:
            break;
    }
}

static void
mach_accel_out_pixtrans(svga_t *svga, mach_t *mach, ibm8514_t *dev, uint16_t val)
{
    int frgd_sel;
    int bkgd_sel;
    int mono_src;
    int swap = 0;

    frgd_sel = (mach->accel.dp_config >> 13) & 7;
    bkgd_sel = (mach->accel.dp_config >> 7) & 3;
    mono_src = (mach->accel.dp_config >> 5) & 3;

    if ((mach->accel.dp_config & 0x04) && (mach->accel.cmd_type != 5)) {
        mach_log("Read Host Monochrome Data.\n");
        val = (val >> 8) | (val << 8);
        swap = 1;
    }

    switch (mach->accel.dp_config & 0x200) {
        case 0x000: /*8-bit size*/
            if (mono_src == 2) {
                if ((frgd_sel != 2) && (bkgd_sel != 2)) {
                    if ((mach->accel.dp_config & 0x1000) && !swap) {
                        mach_log("8-bit bus size swap.\n");
                        val = (val >> 8) | (val << 8);
                    }
                    mach_accel_start(mach->accel.cmd_type, 1, 8, val | (val << 16), 0, svga, mach, dev);
                } else
                    mach_accel_start(mach->accel.cmd_type, 1, 1, -1, val | (val << 16), svga, mach, dev);
            } else
                mach_accel_start(mach->accel.cmd_type, 1, 1, -1, val | (val << 16), svga, mach, dev);
            break;
        case 0x200: /*16-bit size*/
            if (mono_src == 2) {
                if ((frgd_sel != 2) && (bkgd_sel != 2)) {
                    if (((mach->accel.dp_config & 0x1000) && !swap) || (!(mach->accel.dp_config & 0x1000) && swap)) {
                        mach_log("16-bit bus size swap.\n");
                        val = (val >> 8) | (val << 8);
                    }
                    mach_accel_start(mach->accel.cmd_type, 1, 16, val | (val << 16), 0, svga, mach, dev);
                } else
                    mach_accel_start(mach->accel.cmd_type, 1, 2, -1, val | (val << 16), svga, mach, dev);
            } else
                mach_accel_start(mach->accel.cmd_type, 1, 2, -1, val | (val << 16), svga, mach, dev);
            break;

        default:
            break;
    }
}

static void
mach_out(uint16_t addr, uint8_t val, void *priv)
{
    mach_t          *mach = (mach_t *) priv;
    svga_t          *svga = &mach->svga;
    ibm8514_t       *dev  = (ibm8514_t *) svga->dev8514;
    uint8_t          old;
    uint8_t          rs2;
    uint8_t          rs3;

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

    if ((addr >= 0x3c6) && (addr <= 0x3c9)) {
        mach_log("VGA DAC write regs=%03x, on=%d, display control=%02x, on1=%x, clocksel=%02x.\n",
                 addr, dev->on, dev->disp_cntl & 0x60, dev->accel.advfunc_cntl & 0x01, mach->accel.clock_sel & 0x01);
    } else if ((addr >= 0x2ea) && (addr <= 0x2ed)) {
        mach_log("8514/A DAC write regs=%03x, on=%d, display control=%02x, on1=%x, clocksel=%02x.\n",
                 addr, dev->on, dev->disp_cntl & 0x60, dev->accel.advfunc_cntl & 0x01, mach->accel.clock_sel & 0x01);
    }

    switch (addr) {
        case 0x1ce:
            mach->index = val;
            break;
        case 0x1cf:
            old                     = mach->regs[mach->index];
            mach->regs[mach->index] = val;
            mach_log("ATI VGA write reg=%02x, val=%02x, old=%02x.\n", mach->index, val, old);
            switch (mach->index) {
                case 0xa3:
                    if ((old ^ val) & 0x10) {
                        mach_log("ATI A3 bit 7.\n");
                        svga_recalctimings(svga);
                    }
                    break;
                case 0xa7:
                    if ((old ^ val) & 0x80) {
                        mach_log("ATI A7 bit 7.\n");
                        svga_recalctimings(svga);
                    }
                    break;
                case 0xad:
                    if (ATI_MACH32) {
                        if ((old ^ val) & 0x0c) {
                            mach_log("ATI AD bits 2-3.\n");
                            svga_recalctimings(svga);
                        }
                    }
                    break;
                case 0xb0:
                    if ((old ^ val) & 0x60) {
                        if (dev->_8514crt) {
                            if (!(mach->accel.clock_sel & 0x01)) {
                                if ((val & 0x20) && !(old & 0x20)) {
                                    dev->on = 1;
                                    dev->vendor_mode = !!(ATI_MACH32);
                                    mach_set_resolution(mach, svga);
                                    mach32_updatemapping(mach, svga);
                                } else if (!(val & 0x20) && (old & 0x20)) {
                                    dev->on = 0;
                                    dev->vendor_mode = 0;
                                    mach_set_resolution(mach, svga);
                                    mach32_updatemapping(mach, svga);
                                }
                            }
                        } else
                            svga_recalctimings(svga);

                        mach_log("ATI B0 bits 5-6: old=%02x, val=%02x, on=%d, bpp=%d, hires=%x, vgahires=%02x, base=%05x.\n",
                              old & 0x60, val & 0x60, dev->on, dev->accel_bpp, dev->accel.advfunc_cntl & 0x04, svga->gdcreg[5] & 0x60, svga->mapping.base);
                    }
                    break;
                case 0xae:
                case 0xb2:
                case 0xbe:
                    mach_log("ATI VGA write reg=0x%02X, val=0x%02X\n", mach->index, val);
                    if (mach->regs[0xbe] & 0x08) { /* Read/write bank mode */
                        mach->bank_r = (((mach->regs[0xb2] & 1) << 3) | ((mach->regs[0xb2] & 0xe0) >> 5));
                        mach->bank_w = ((mach->regs[0xb2] & 0x1e) >> 1);
                        if (ATI_MACH32) {
                            mach->bank_r |= ((mach->regs[0xae] & 0x0c) << 2);
                            mach->bank_w |= ((mach->regs[0xae] & 3) << 4);
                        }
                        mach_log("Separate B2Bank = %02x, AEbank = %02x.\n", mach->regs[0xb2], mach->regs[0xae]);
                    } else { /* Single bank mode */
                        mach->bank_w = ((mach->regs[0xb2] & 0x1e) >> 1);
                        if (ATI_MACH32)
                            mach->bank_w |= ((mach->regs[0xae] & 3) << 4);

                        mach->bank_r = mach->bank_w;
                        mach_log("Single B2Bank = %02x, AEbank = %02x.\n", mach->regs[0xb2], mach->regs[0xae]);
                    }
                    svga->read_bank  = mach->bank_r << 16;
                    svga->write_bank = mach->bank_w << 16;

                    if (mach->index == 0xbe) {
                        if ((old ^ val) & 0x13) {
                            mach_log("ATI BE bit 4.\n");
                            svga_recalctimings(svga);
                        }
                    }
                    break;
                case 0xbd:
                    if ((old ^ val) & 0x04)
                        mach32_updatemapping(mach, svga);
                    break;
                case 0xb3:
                    ati_eeprom_write(&mach->eeprom, val & 0x08, val & 0x02, val & 0x01);
                    break;
                case 0xb6:
                    if ((old ^ val) & 0x10) {
                        mach_log("ATI B6 bit 4.\n");
                        svga_recalctimings(svga);
                    }
                    break;
                case 0xb8:
                    if (ATI_MACH32) {
                        if ((old ^ val) & 0x40) {
                            mach_log("ATI B8 bit 6.\n");
                            svga_recalctimings(svga);
                        }
                    } else {
                        if ((old ^ val) & 0xc0)
                            svga_recalctimings(svga);
                    }
                    break;
                case 0xb9:
                    if ((old ^ val) & 0x02) {
                        mach_log("ATI B9 bit 1.\n");
                        svga_recalctimings(svga);
                    }
                    break;

                default:
                    break;
            }
            break;

        case 0x2ea:
        case 0x2eb:
        case 0x2ec:
        case 0x2ed:
            rs2 = !!(mach->accel.ext_ge_config & 0x1000);
            rs3 = !!(mach->accel.ext_ge_config & 0x2000);
            mach_log("8514/A Extended mode=%02x.\n", mach->regs[0xb0] & 0x20);
            if (ATI_MACH32) {
                if (mach->pci_bus && (mach->ramdac_type == ATI_68860))
                    ati68860_ramdac_out((addr & 0x03) | (rs2 << 2) | (rs3 << 3), val, 1, svga->ramdac, svga);
                else
                    ati68875_ramdac_out(addr, rs2, rs3, val, 1, svga->ramdac, svga);
            } else
                svga_out(addr, val, svga);
            return;

        case 0x3C2:
            if (mach->regs[0xb8] & 0x08)
                return;
            break;

        case 0x3C6:
        case 0x3C7:
        case 0x3C8:
        case 0x3C9:
            rs2 = !!(mach->regs[0xa0] & 0x20);
            rs3 = !!(mach->regs[0xa0] & 0x40);
            mach_log("VGA Extended mode=%02x.\n", mach->regs[0xb0] & 0x20);
            if (ATI_MACH32) {
                if (mach->pci_bus && (mach->ramdac_type == ATI_68860))
                    ati68860_ramdac_out((addr & 0x03) | (rs2 << 2) | (rs3 << 3), val, 0, svga->ramdac, svga);
                else
                    ati68875_ramdac_out(addr, rs2, rs3, val, 0, svga->ramdac, svga);
            } else
                svga_out(addr, val, svga);
            return;

        case 0x3CF:
            if (svga->gdcaddr == 6) {
                uint8_t old_val = svga->gdcreg[6];
                svga->gdcreg[6] = val;
                if ((svga->gdcreg[6] & 0xc) != (old_val & 0xc)) {
                    mach_log("GDCREG6=%02x.\n", svga->gdcreg[6] & 0xc);
                    mach32_updatemapping(mach, svga);
                }
                return;
            }
            break;

        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if (svga->crtcreg & 0x20)
                return;
            if ((svga->crtcreg < 7) && ((svga->crtc[0x11] & 0x80) || (mach->regs[0xb4] & 0x40)) && !(mach->regs[0xb4] & 0x80))
                return;
            if ((svga->crtcreg == 7) && ((svga->crtc[0x11] & 0x80) || (mach->regs[0xb4] & 0x40)) && !(mach->regs[0xb4] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            if (mach->regs[0xb8] & 0x04) {
                if ((svga->crtcreg < 0x0a) || (svga->crtcreg > 0x0d))
                    return;
            }
            if (mach->regs[0xb4] & 0x04) {
                if (svga->crtcreg == 9) {
                    if (val & 0x8f)
                        return;
                }
            }
            if (mach->regs[0xb4] & 0x08) {
                if (svga->crtcreg == 6)
                    return;
                if (svga->crtcreg == 7) {
                    if (val & 0xaf)
                        return;
                }
                if (svga->crtcreg == 9) {
                    if (val & 0x20)
                        return;
                }
                if (svga->crtcreg == 0x11) {
                    if (val & 0x0f)
                        return;
                }
                if ((svga->crtcreg == 0x10) ||
                    (svga->crtcreg == 0x12) ||
                    (svga->crtcreg == 0x15) ||
                    (svga->crtcreg == 0x16))
                    return;
            }
            if (mach->regs[0xb4] & 0x10) {
                if ((svga->crtcreg == 0x0a) ||
                    (svga->crtcreg == 0x0b))
                    return;
            }
            if (mach->regs[0xb4] & 0x20) {
                if (svga->crtcreg == 8) {
                    if (val & 0x7f)
                        return;
                }
                if (svga->crtcreg == 0x14) {
                    if (val & 0x1f)
                        return;
                }
            }

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

static uint8_t
mach_in(uint16_t addr, void *priv)
{
    mach_t          *mach = (mach_t *) priv;
    svga_t          *svga = &mach->svga;
    ibm8514_t       *dev  = (ibm8514_t *) svga->dev8514;
    uint8_t          temp = 0xff;
    uint8_t          rs2;
    uint8_t          rs3;

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

    if ((addr >= 0x3c6) && (addr <= 0x3c9) && dev->on) {
        addr -= 0xdc;
        mach_log("VGA DAC read regs=%03x.\n", addr);
    } else if ((addr >= 0x2ea) && (addr <= 0x2ed))
        mach_log("8514/A DAC read regs=%03x.\n", addr);

    switch (addr) {
        case 0x1ce:
            temp = mach->index;
            break;
        case 0x1cf:
            switch (mach->index) {
                case 0xa0:
                    temp = mach->regs[0xa0] | 0x10;
                    break;
                case 0xa8:
                    temp = (svga->vc >> 8) & 3;
                    break;
                case 0xa9:
                    temp = svga->vc & 0xff;
                    break;
                case 0xaa:
                    if (ATI_GRAPHICS_ULTRA)
                        temp = 0x06;
                    else
                        temp = 0x00;
                    break;
                case 0xb0:
                    temp = mach->regs[0xb0] | 0x80;
                    temp &= ~0x18;
                    if (ATI_MACH32) { /*Mach32 VGA 1MB memory*/
                        temp |= 0x08;
                    } else { /*ATI 28800 VGA 512kB memory*/
                        temp |= 0x10;
                    }
                    break;
                case 0xb7:
                    temp = mach->regs[0xb7] & ~0x08;
                    if (ati_eeprom_read(&mach->eeprom))
                        temp |= 0x08;
                    break;

                case 0xbd:
                    temp = mach->regs[0xbd] | 0x10;
                    break;

                default:
                    temp = mach->regs[mach->index];
                    break;
            }
            mach_log("ATI VGA read reg=%02x, val=%02x.\n", mach->index, temp);
            break;

        case 0x2ea:
        case 0x2eb:
        case 0x2ec:
        case 0x2ed:
            rs2 = !!(mach->accel.ext_ge_config & 0x1000);
            rs3 = !!(mach->accel.ext_ge_config & 0x2000);
            if (ATI_MACH32) {
                if (mach->pci_bus && (mach->ramdac_type == ATI_68860))
                    temp = ati68860_ramdac_in((addr & 3) | (rs2 << 2) | (rs3 << 3), 1, svga->ramdac, svga);
                else
                    temp = ati68875_ramdac_in(addr, rs2, rs3, 1, svga->ramdac, svga);
            } else
                temp = svga_in(addr, svga);
            break;

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
ati8514_out(uint16_t addr, uint8_t val, void *priv)
{
    mach_log("[%04X:%08X]: ADDON OUT addr=%03x, val=%02x.\n", CS, cpu_state.pc, addr, val);

    svga_out(addr, val, priv);
}

uint8_t
ati8514_in(uint16_t addr, void *priv)
{
    uint8_t temp = 0xff;

    temp = svga_in(addr, priv);

    mach_log("[%04X:%08X]: ADDON IN addr=%03x, temp=%02x.\n", CS, cpu_state.pc, addr, temp);
    return temp;
}

static void
ati_render_24bpp(svga_t *svga)
{
    mach_t    *mach = (mach_t *) svga->priv;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    uint32_t *p;
    uint32_t  dat;

    if ((dev->displine + svga->y_add) < 0)
        return;

    if (dev->changedvram[dev->memaddr >> 12] || dev->changedvram[(dev->memaddr >> 12) + 1] || svga->fullchange) {
        p = &buffer32->line[dev->displine + svga->y_add][svga->x_add];

        if (dev->firstline_draw == 2000)
            dev->firstline_draw = dev->displine;
        dev->lastline_draw = dev->displine;

        if (mach->accel.ext_ge_config & 0x400) { /*BGR, Blue-(23:16), Green-(15:8), Red-(7:0)*/
            for (int x = 0; x <= dev->h_disp; x += 4) {
                dat  = *(uint32_t *) (&dev->vram[dev->memaddr & dev->vram_mask]);
                p[x] = ((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16);

                dat      = *(uint32_t *) (&dev->vram[(dev->memaddr + 3) & dev->vram_mask]);
                p[x + 1] = ((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16);

                dat      = *(uint32_t *) (&dev->vram[(dev->memaddr + 6) & dev->vram_mask]);
                p[x + 2] = ((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16);

                dat      = *(uint32_t *) (&dev->vram[(dev->memaddr + 9) & dev->vram_mask]);
                p[x + 3] = ((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16);

                dev->memaddr += 12;
            }
        } else { /*RGB, Red-(23:16), Green-(15:8), Blue-(7:0)*/
            for (int x = 0; x <= dev->h_disp; x += 4) {
                dat  = *(uint32_t *) (&dev->vram[dev->memaddr & dev->vram_mask]);
                p[x] = dat & 0xffffff;

                dat      = *(uint32_t *) (&dev->vram[(dev->memaddr + 3) & dev->vram_mask]);
                p[x + 1] = dat & 0xffffff;

                dat      = *(uint32_t *) (&dev->vram[(dev->memaddr + 6) & dev->vram_mask]);
                p[x + 2] = dat & 0xffffff;

                dat      = *(uint32_t *) (&dev->vram[(dev->memaddr + 9) & dev->vram_mask]);
                p[x + 3] = dat & 0xffffff;

                dev->memaddr += 12;
            }
        }
        dev->memaddr &= dev->vram_mask;
    }
}

static void
ati_render_32bpp(svga_t *svga)
{
    mach_t    *mach = (mach_t *) svga->priv;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    int        x;
    uint32_t  *p;
    uint32_t   dat;

    if ((dev->displine + svga->y_add) < 0)
        return;

    if (dev->changedvram[dev->memaddr >> 12] || dev->changedvram[(dev->memaddr >> 12) + 1] || dev->changedvram[(dev->memaddr >> 12) + 2] || svga->fullchange) {
        p = &buffer32->line[dev->displine + svga->y_add][svga->x_add];

        if (dev->firstline_draw == 2000)
            dev->firstline_draw = dev->displine;
        dev->lastline_draw = dev->displine;

        if (mach->accel.ext_ge_config & 0x400) { /*BGR, Blue-(23:16), Green-(15:8), Red-(7:0)*/
            for (x = 0; x <= dev->h_disp; x++) {
                dat  = *(uint32_t *) (&dev->vram[(dev->memaddr + (x << 2)) & dev->vram_mask]);
                *p++ = ((dat & 0x00ff0000) >> 16) | (dat & 0x0000ff00) | ((dat & 0x000000ff) << 16);
            }
        } else { /*RGB, Red-(31:24), Green-(23:16), Blue-(15:8)*/
            for (x = 0; x <= dev->h_disp; x++) {
                dat  = *(uint32_t *) (&dev->vram[(dev->memaddr + (x << 2)) & dev->vram_mask]);
                *p++ = ((dat & 0xffffff00) >> 8);
            }
        }
        dev->memaddr += (x * 4);
        dev->memaddr &= dev->vram_mask;
    }
}

/*The situation is the following:
  When ATI (0x4aee) mode is selected, allow complete auto-detection.
  When 8514/A (0x4ae8) mode is selected, allow detection based on the shadow register sets.
*/
static void
mach_set_resolution(mach_t *mach, svga_t *svga)
{
    ibm8514_t    *dev = (ibm8514_t *) svga->dev8514;

    dev->h_total = (dev->htotal + 1);
    if (dev->h_total == 8) /*Default to 1024x768 87hz 8514/A htotal timings if it goes to 0.*/
        dev->h_total = 0x9e;

    dev->hdisp = (dev->hdisped + 1) << 3;

    dev->vdisp = (dev->v_disp + 1) >> 1;
    if ((dev->vdisp == 478) || (dev->vdisp == 598) || (dev->vdisp == 766) || (dev->vdisp == 898) || (dev->vdisp == 1022))
        dev->vdisp += 2;

    dev->v_total = dev->v_total_reg + 1;
    if (dev->v_total == 1)
        dev->v_total = 0x0669;

    dev->v_syncstart = dev->v_sync_start + 1;
    if (dev->v_syncstart == 1)
        dev->v_syncstart = 0x0601;

    mach->accel.clock_sel_mode = 0;
    mach_log("ATI Mode: set=%02x, dispcntl=%02x, h_total=%d, hdisp=%d, vdisp=%d, v_total=%04x, v_syncstart=%04x, hsync_start=%d, hsync_width=%d, clocksel=%02x, advancedcntl=%02x.\n", mach->shadow_set & 0x03, dev->disp_cntl, dev->h_total, dev->hdisp, dev->vdisp, dev->v_total, dev->v_syncstart, dev->hsync_start, dev->hsync_width, mach->accel.clock_sel & 0xff, dev->accel.advfunc_cntl & 0x05);
    if ((dev->disp_cntl_2 >> 5) == 1) { /*Enable the 8514/A subsystem and set modes according to the shadow sets if needed.*/
        switch (mach->shadow_set & 0x03) {
            case 0x01:
                if (!(dev->accel.advfunc_cntl & 0x04)) {
                    dev->hdisp = 640;
                    dev->vdisp = 480;
                    if (ATI_8514A_ULTRA) {
                        dev->h_total = (mach->eeprom.data[0x11] & 0xff) + 1;
                        dev->v_total = mach->eeprom.data[0x0d] + 1;
                        dev->v_syncstart = mach->eeprom.data[9] + 1;
                        mach->accel.clock_sel_mode = (mach->eeprom.data[4] & 0xff) << 2;
                    } else {
                        mach_log("Mach: EEPROM 640x480: %04x.\n", mach->eeprom.data[7]);
                        switch (mach->eeprom.data[7] & 0xff) {
                            case 0x00: /*640x480 60Hz Non-interlaced*/
                            default:
                                dev->h_total = 0x64;
                                dev->v_total = 0x0419;
                                dev->v_syncstart = 0x03d7;
                                mach->accel.clock_sel_mode = 0x50;
                                break;
                            case 0x01: /*640x480 72Hz Non-interlaced*/
                                dev->h_total = 0x6a;
                                dev->v_total = 0x040c;
                                dev->v_syncstart = 0x03d1;
                                mach->accel.clock_sel_mode = 0x24;
                                break;
                            case 0x03: /*640x480 72Hz Non-interlaced Alt*/
                                dev->h_total = 0x71;
                                dev->v_total = 0x04ca;
                                dev->v_syncstart = 0x0422;
                                mach->accel.clock_sel_mode = 0x6c;
                                break;
                        }
                    }
                }
                break;
            case 0x02:
                if (dev->accel.advfunc_cntl & 0x04) {
                    dev->hdisp = 1024;
                    dev->vdisp = 768;
                    if (ATI_8514A_ULTRA) {
                        dev->h_total = ((mach->eeprom.data[0x11] >> 8) & 0xff) + 1;
                        dev->v_total = mach->eeprom.data[0x0c] + 1;
                        dev->v_syncstart = mach->eeprom.data[8] + 1;
                        mach->accel.clock_sel_mode = ((mach->eeprom.data[4] >> 8) & 0xff) << 2;
                    } else {
                        mach_log("Mach: EEPROM 1024x768: %04x.\n", mach->eeprom.data[9]);
                        switch (mach->eeprom.data[9] & 0xff) {
                            case 0x00: /*1024x768 76Hz Non-interlaced*/
                                dev->h_total = 0xa3;
                                dev->v_total = 0x064b;
                                dev->v_syncstart = 0x060c;
                                mach->accel.clock_sel_mode = 0x2c;
                                break;

                            case 0x01: /*1024x768 87Hz Interlaced*/
                            default:
                                dev->h_total = 0x9e;
                                dev->v_total = 0x0669;
                                dev->v_syncstart = 0x0601;
                                mach->accel.clock_sel_mode = 0x1c;
                                break;
                            case 0x02: /*1024x768 60Hz Non-interlaced*/
                                dev->h_total = 0xa8;
                                dev->v_total = 0x064a;
                                dev->v_syncstart = 0x0603;
                                mach->accel.clock_sel_mode = 0x3c;
                                break;
                            case 0x04: /*1024x768 70Hz Non-interlaced*/
                                dev->h_total = 0xa6;
                                dev->v_total = 0x064a;
                                dev->v_syncstart = 0x0603;
                                mach->accel.clock_sel_mode = 0x38;
                                break;
                            case 0x08: /*1024x768 72Hz Non-interlaced*/
                                dev->h_total = 0xa1;
                                dev->v_total = 0x064a;
                                dev->v_syncstart = 0x0603;
                                mach->accel.clock_sel_mode = 0x38;
                                break;
                            case 0x82: /*1024x768 66Hz Non-interlaced*/
                                dev->h_total = 0xac;
                                dev->v_total = 0x065c;
                                dev->v_syncstart = 0x060b;
                                mach->accel.clock_sel_mode = 0x38;
                                break;
                        }
                    }
                }
                break;

            default:
                break;
        }
        svga_recalctimings(svga);
    } else if ((dev->disp_cntl_2 >> 5) == 2) { /*Reset 8514/A to defaults if needed.*/
        if (dev->accel.advfunc_cntl & 0x04) {
            if (dev->hdisp == 640) {
                dev->hdisp = 1024;
                dev->vdisp = 768;
                if (ATI_8514A_ULTRA) {
                    dev->h_total = ((mach->eeprom.data[0x11] >> 8) & 0xff) + 1;
                    dev->v_total = mach->eeprom.data[0x0c] + 1;
                    dev->v_syncstart = mach->eeprom.data[8] + 1;
                    mach->accel.clock_sel_mode = ((mach->eeprom.data[4] >> 8) & 0xff) << 2;
                } else {
                    mach_log("Mach Reset: EEPROM 1024x768: %04x.\n", mach->eeprom.data[9]);
                    switch (mach->eeprom.data[9] & 0xff) {
                        case 0x00: /*1024x768 76Hz Non-interlaced*/
                            dev->h_total = 0xa3;
                            dev->v_total = 0x064b;
                            dev->v_syncstart = 0x060c;
                            mach->accel.clock_sel_mode = 0x2c;
                            break;
                        case 0x01: /*1024x768 87Hz Interlaced*/
                        default:
                            dev->h_total = 0x9e;
                            dev->v_total = 0x0669;
                            dev->v_syncstart = 0x0601;
                            mach->accel.clock_sel_mode = 0x1c;
                            break;
                        case 0x02: /*1024x768 60Hz Non-interlaced*/
                            dev->h_total = 0xa8;
                            dev->v_total = 0x064a;
                            dev->v_syncstart = 0x0603;
                            mach->accel.clock_sel_mode = 0x3c;
                            break;
                        case 0x04: /*1024x768 70Hz Non-interlaced*/
                            dev->h_total = 0xa6;
                            dev->v_total = 0x064a;
                            dev->v_syncstart = 0x0603;
                            mach->accel.clock_sel_mode = 0x38;
                            break;
                        case 0x08: /*1024x768 72Hz Non-interlaced*/
                            dev->h_total = 0xa1;
                            dev->v_total = 0x064a;
                            dev->v_syncstart = 0x0603;
                            mach->accel.clock_sel_mode = 0x38;
                            break;
                        case 0x82: /*1024x768 66Hz Non-interlaced*/
                            dev->h_total = 0xac;
                            dev->v_total = 0x065c;
                            dev->v_syncstart = 0x060b;
                            mach->accel.clock_sel_mode = 0x38;
                            break;
                    }
                }
                svga_recalctimings(svga);
            }
        } else {
            if (dev->hdisp == 1024) {
                dev->hdisp = 640;
                dev->vdisp = 480;
                if (ATI_8514A_ULTRA) {
                    dev->h_total = (mach->eeprom.data[0x11] & 0xff) + 1;
                    dev->v_total = mach->eeprom.data[0x0d] + 1;
                    dev->v_syncstart = mach->eeprom.data[9] + 1;
                    mach->accel.clock_sel_mode = (mach->eeprom.data[4] & 0xff) << 2;
                } else {
                    mach_log("Mach: EEPROM 640x480: %04x.\n", mach->eeprom.data[7]);
                    switch (mach->eeprom.data[7] & 0xff) {
                        case 0x00: /*640x480 60Hz Non-interlaced*/
                        default:
                            dev->h_total = 0x64;
                            dev->v_total = 0x0419;
                            dev->v_syncstart = 0x03d7;
                            mach->accel.clock_sel_mode = 0x50;
                            break;
                        case 0x01: /*640x480 72Hz Non-interlaced*/
                            dev->h_total = 0x6a;
                            dev->v_total = 0x040c;
                            dev->v_syncstart = 0x03d1;
                            mach->accel.clock_sel_mode = 0x24;
                            break;
                        case 0x03: /*640x480 72Hz Non-interlaced Alt*/
                            dev->h_total = 0x71;
                            dev->v_total = 0x04ca;
                            dev->v_syncstart = 0x0422;
                            mach->accel.clock_sel_mode = 0x6c;
                            break;
                    }
                }
                svga_recalctimings(svga);
            }
        }
    } else /*No change (type 0) or reset type 3.*/
        svga_recalctimings(svga);
}

void
ati8514_recalctimings(svga_t *svga)
{
    mach_t       *mach = (mach_t *) svga->ext8514;
    ibm8514_t    *dev  = (ibm8514_t *) svga->dev8514;
    int           _8514_modes = 0;

    mach_log("ON=%d, vgahdisp=%d.\n", dev->on, svga->hdisp);
    if (dev->on) {
        dev->interlace                  = !!(dev->disp_cntl & 0x10);
        dev->pitch                      = dev->ext_pitch;
        dev->rowoffset                  = dev->ext_crt_pitch;
        dev->rowcount                   = !!(dev->disp_cntl & 0x08);
        dev->accel.ge_offset            = (mach->accel.ge_offset_lo | (mach->accel.ge_offset_hi << 16)) << 2;
        mach->accel.crt_offset          = (mach->accel.crt_offset_lo | (mach->accel.crt_offset_hi << 16)) << 2;

        switch (mach->accel.clock_sel_mode) {
            case 0x1c:
                dev->interlace = 1;
                _8514_modes = 1;
                break;
            case 0x24:
            case 0x2c:
            case 0x38:
            case 0x3c:
            case 0x50:
            case 0x6c:
                dev->interlace = 0;
                _8514_modes = 2;
                break;
            default:
                break;
        }

        if (_8514_modes)
            dev->ven_clock = mach->accel.clock_sel_mode & 0x7c;
        else
            dev->ven_clock = mach->accel.clock_sel & 0x7c;

        dev->accel.ge_offset           -= mach->accel.crt_offset;

        mach_log("HDISP=%d, VDISP=%d, shadowset=%x, 8514/A mode=%x, clocksel=%02x.\n",
                 dev->hdisp, dev->vdisp, mach->shadow_set & 0x03, dev->accel.advfunc_cntl & 0x05, mach->accel.clock_sel & 0x01);

        mach->accel.src_pitch = dev->pitch;
        mach->accel.dst_pitch = dev->pitch;
        mach->accel.src_ge_offset = (mach->accel.ge_offset_lo | (mach->accel.ge_offset_hi << 16)) << 2;
        mach->accel.dst_ge_offset = (mach->accel.ge_offset_lo | (mach->accel.ge_offset_hi << 16)) << 2;
        mach->accel.src_ge_offset -= mach->accel.crt_offset;
        mach->accel.dst_ge_offset -= mach->accel.crt_offset;

        mach_log("8514/A ON, pitch=%d, GE offset=%08x.\n", ((mach->accel.ge_pitch & 0xff) << 3), dev->accel.ge_offset);

        dev->h_disp = dev->hdisp;
        dev->dispend = dev->vdisp;
        if (dev->dispend == 600)
            dev->h_disp = 800;
        else if (dev->h_disp == 640)
            dev->dispend = 480;

        dev->h_disp_time = dev->h_disp >> 3;

        svga->clock_8514 = (cpuclock * (double) (1ULL << 32)) / svga->getclock8514((dev->ven_clock >> 2) & 0x0f, svga->clock_gen8514) / 2.0;
        if (dev->ven_clock & 0x40)
            svga->clock_8514 *= 2.0;

        if (dev->interlace) {
            dev->dispend >>= 1;
            svga->clock_8514 /= 2.0;
        }

        mach_log("cntl=%d, hv(%d,%d), pitch=%d, rowoffset=%d, gextconfig=%03x, shadow=%x interlace=%d.\n",
                 dev->accel.advfunc_cntl & 0x04, dev->h_disp, dev->dispend, dev->pitch, dev->rowoffset,
                 mach->accel.ext_ge_config & 0xcec0, mach->shadow_set & 3, dev->interlace);
        if (dev->vram_512k_8514) {
            if (dev->h_disp == 640)
                dev->pitch = 640;
            else
                dev->pitch = 1024;
        }
        dev->accel_bpp = 8;
        svga->render8514 = ibm8514_render_8bpp;

    } else
        dev->mode = VGA_MODE;
}

static void
mach_recalctimings(svga_t *svga)
{
    mach_t       *mach = (mach_t *) svga->priv;
    ibm8514_t    *dev  = (ibm8514_t *) svga->dev8514;
    int           clock_sel = 0x00;
    int           _8514_modes = 0;

    if (mach->regs[0xad] & 0x08)
        svga->hblankstart    = ((mach->regs[0x0d] >> 2) << 8) + svga->crtc[2];

    if (svga->miscout & 0x04)
        clock_sel |= 0x01;
    if (svga->miscout & 0x08)
        clock_sel |= 0x02;
    if (mach->regs[0xb9] & 0x02)
        clock_sel |= 0x04;
    if (mach->regs[0xbe] & 0x10)
        clock_sel |= 0x08;

    svga->interlace = !!(mach->regs[0xbe] & 0x02);
    if (svga->interlace)
        svga->dispend >>= 1;

    if (ATI_MACH32) {
        if (mach->regs[0xad] & 0x04)
            svga->memaddr_latch |= 0x40000;

        if (mach->regs[0xad] & 0x08)
            svga->memaddr_latch |= 0x80000;
    }

    if (mach->regs[0xa3] & 0x10)
        svga->memaddr_latch |= 0x10000;

    if (mach->regs[0xb0] & 0x40)
        svga->memaddr_latch |= 0x20000;

    if ((mach->regs[0xb6] & 0x18) >= 0x10) {
        svga->hdisp <<= 1;
        svga->htotal <<= 1;
        svga->dots_per_clock <<= 1;
        svga->rowoffset <<= 1;
    }

    if (mach->regs[0xb0] & 0x20) {
        if ((mach->regs[0xb6] & 0x18) >= 0x10)
            svga->packed_4bpp = 1;
        else
            svga->packed_4bpp = 0;
    } else
        svga->packed_4bpp = 0;

    if (!ATI_MACH32) {
        if ((mach->regs[0xb6] & 0x18) == 0x08) {
            svga->hdisp <<= 1;
            svga->htotal <<= 1;
            svga->dots_per_clock <<= 1;
            svga->ati_4color = 1;
        } else
            svga->ati_4color = 0;
    }

    mach_log("ON=%d, override=%d, gelo=%04x, gehi=%04x, crtlo=%04x, crthi=%04x, vgahdisp=%d, ibmon=%x, ation=%x, graph1=%x.\n", dev->on, svga->override, mach->accel.ge_offset_lo, mach->accel.ge_offset_hi, mach->accel.crt_offset_lo, mach->accel.crt_offset_hi, svga->hdisp, dev->accel.advfunc_cntl & 0x01, mach->accel.clock_sel & 0x01, svga->gdcreg[6] & 0x01);

    if (dev->on) {
        dev->memaddr_latch              = 0; /*(mach->accel.crt_offset_lo | (mach->accel.crt_offset_hi << 16)) << 2;*/
        dev->interlace                  = !!(dev->disp_cntl & 0x10);
        dev->pitch                      = dev->ext_pitch;
        dev->rowoffset                  = dev->ext_crt_pitch;
        dev->rowcount                   = !!(dev->disp_cntl & 0x08);
        dev->accel.ge_offset            = (mach->accel.ge_offset_lo | (mach->accel.ge_offset_hi << 16));
        mach->accel.crt_offset          = (mach->accel.crt_offset_lo | (mach->accel.crt_offset_hi << 16));

        switch (mach->accel.clock_sel_mode) {
            case 0x1c:
                dev->interlace = 1;
                _8514_modes = 1;
                break;
            case 0x24:
            case 0x2c:
            case 0x38:
            case 0x3c:
            case 0x50:
            case 0x6c:
                dev->interlace = 0;
                _8514_modes = 2;
                break;
            default:
                break;
        }

        if (_8514_modes)
            dev->ven_clock = mach->accel.clock_sel_mode & 0x7c;
        else
            dev->ven_clock = mach->accel.clock_sel & 0x7c;

        if (ATI_MACH32) {
            mach_log("Mach32: Clock=%02x, double=%02x, h_total=%02x.\n", (dev->ven_clock >> 2) & 0x0f, dev->ven_clock & 0x40, dev->h_total);
            svga->clock_8514 = (cpuclock * (double) (1ULL << 32)) / svga->getclock8514((dev->ven_clock >> 2) & 0x0f, svga->clock_gen8514) / 2.0;
        } else {
            mach_log("Mach8: Clock=%02x, double=%02x, h_total=%02x, selmode=%02x.\n", (dev->ven_clock >> 2) & 0x0f, dev->ven_clock & 0x40, dev->h_total, mach->accel.clock_sel_mode);
            svga->clock_8514 = (cpuclock * (double) (1ULL << 32)) / svga->getclock8514((dev->ven_clock >> 2) & 0x0f, svga->clock_gen8514) / 2.0;
            if ((((dev->ven_clock >> 2) & 0x0f) == 0x09) && (dev->h_total == 0x6b))
                svga->clock_8514 /= 2.0;
        }
        if (dev->ven_clock & 0x40)
            svga->clock_8514 *= 2.0;

        if (dev->bpp) {
            dev->accel.ge_offset <<= 1;
            mach->accel.crt_offset <<= 1;
        } else {
            dev->accel.ge_offset <<= 2;
            mach->accel.crt_offset <<= 2;
        }

        if (ATI_MACH32 && !dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
            dev->accel.ge_offset <<= 1;
            mach->accel.crt_offset <<= 1;
        }

        dev->accel.ge_offset           -= mach->accel.crt_offset;

        mach_log("RowCount=%x, rowoffset=%x, pitch=%d, geoffset=%x, crtoffset=%x.\n", dev->rowcount, dev->rowoffset, dev->pitch, dev->accel.ge_offset, mach->accel.crt_offset);
        mach_log("HDISP=%d, VDISP=%d, shadowset=%x, 8514/A mode=%x, clocksel=%02x, interlace=%x.\n",
                 dev->hdisp, dev->vdisp, mach->shadow_set & 0x03, dev->accel.advfunc_cntl & 0x04,
                 mach->accel.clock_sel & 0xfe, dev->interlace);

        dev->h_disp = dev->hdisp;
        dev->dispend = dev->vdisp;
        if (dev->dispend == 959) { /*FIXME: vertical resolution mess on EEPROM tests on Mach8*/
            dev->dispend++;
            dev->dispend >>= 1;
        } else if (dev->dispend == 600)
            dev->h_disp = 800;
        else if (dev->h_disp == 640)
            dev->dispend = 480;

        dev->h_disp_time = dev->h_disp >> 3;

        mach_log("8514/A modes=%d, clocksel=%02x, clkselmode=%02x, divide reg ibm=%02x, divide reg vga=%02x, vgainterlace=%x, interlace=%x, htotal=%02x.\n", _8514_modes, mach->accel.clock_sel & 0xfe, mach->accel.clock_sel_mode & 0xfe, mach->accel.clock_sel & 0x40, mach->regs[0xb8] & 0x40, svga->interlace, dev->interlace, dev->htotal);

        if (dev->interlace) {
            dev->dispend >>= 1;
            svga->clock_8514 /= 2.0;
        }

        if (ATI_MACH32) {
            switch ((mach->shadow_set >> 8) & 0x03) {
                case 0x00:
                    mach->accel.src_pitch = dev->pitch;
                    mach->accel.dst_pitch = dev->pitch;
                    mach->accel.src_ge_offset = (mach->accel.ge_offset_lo | (mach->accel.ge_offset_hi << 16));
                    mach->accel.dst_ge_offset = (mach->accel.ge_offset_lo | (mach->accel.ge_offset_hi << 16));
                    if (dev->bpp) {
                        mach->accel.src_ge_offset <<= 1;
                        mach->accel.dst_ge_offset <<= 1;
                    } else {
                        mach->accel.src_ge_offset <<= 2;
                        mach->accel.dst_ge_offset <<= 2;
                    }
                    if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
                        mach->accel.src_ge_offset <<= 1;
                        mach->accel.dst_ge_offset <<= 1;
                    }
                    mach->accel.src_ge_offset -= mach->accel.crt_offset;
                    mach->accel.dst_ge_offset -= mach->accel.crt_offset;
                    dev->accel.src_pitch = mach->accel.src_pitch;
                    dev->accel.dst_pitch = mach->accel.dst_pitch;
                    dev->accel.src_ge_offset = mach->accel.src_ge_offset;
                    dev->accel.dst_ge_offset = mach->accel.dst_ge_offset;
                    break;
                case 0x01:
                    mach->accel.dst_pitch = dev->pitch;
                    mach->accel.dst_ge_offset = (mach->accel.ge_offset_lo | (mach->accel.ge_offset_hi << 16));
                    if (dev->bpp)
                        mach->accel.dst_ge_offset <<= 1;
                    else
                        mach->accel.dst_ge_offset <<= 2;

                    if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00))
                        mach->accel.dst_ge_offset <<= 1;

                    mach->accel.dst_ge_offset -= mach->accel.crt_offset;
                    dev->accel.dst_pitch = mach->accel.dst_pitch;
                    dev->accel.dst_ge_offset = mach->accel.dst_ge_offset;
                    break;
                case 0x02:
                    mach->accel.src_pitch = dev->pitch;
                    mach->accel.src_ge_offset = (mach->accel.ge_offset_lo | (mach->accel.ge_offset_hi << 16));
                    if (dev->bpp)
                        mach->accel.src_ge_offset <<= 1;
                    else
                        mach->accel.src_ge_offset <<= 2;

                    if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00))
                        mach->accel.src_ge_offset <<= 1;

                    mach->accel.src_ge_offset -= mach->accel.crt_offset;
                    dev->accel.src_pitch = mach->accel.src_pitch;
                    dev->accel.src_ge_offset = mach->accel.src_ge_offset;
                    break;
                default:
                    break;
            }
            mach_log("cntl=%d, clksel=%x, hv(%d,%d), pitch=%d, rowoffset=%d, gextconfig=%03x, shadow=%x interlace=%d, vgahdisp=%d.\n",
                     dev->accel.advfunc_cntl & 0x04, mach->accel.clock_sel & 0x01, dev->h_disp, dev->dispend, dev->pitch, dev->rowoffset,
                     mach->accel.ext_ge_config & 0xcec0, mach->shadow_set & 3, dev->interlace, svga->hdisp);
            mach_log("EXTGECONFIG bits 11-15=%04x.\n", mach->accel.ext_ge_config & 0x8800);
            if ((mach->accel.ext_ge_config & 0x800) || (!(mach->accel.ext_ge_config & 0x8000) && !(mach->accel.ext_ge_config & 0x800))) {
                mach_log("hv=%d,%d, pitch=%d, rowoffset=%d, gextconfig=%03x, bpp=%d, shadow=%x, vgahdisp=%d.\n",
                         dev->h_disp, dev->dispend, dev->pitch, dev->ext_crt_pitch, mach->accel.ext_ge_config & 0xcec0,
                         dev->accel_bpp, mach->shadow_set & 0x03, svga->hdisp);

                switch (dev->accel_bpp) {
                    case 8:
                        if ((mach->accel.ext_ge_config & 0x30) == 0x00) {
                            if (dev->vram_512k_8514) {
                                if (dev->h_disp == 640)
                                    dev->pitch = 640;
                                else
                                    dev->pitch = 1024;
                            }
                        }
                        svga->render8514 = ibm8514_render_8bpp;
                        break;
                    case 15:
                        svga->render8514 = ibm8514_render_15bpp;
                        break;
                    case 16:
                        svga->render8514 = ibm8514_render_16bpp;
                        break;
                    case 24:
                        mach_log("GEConfig24bpp: %03x.\n", mach->accel.ext_ge_config & 0x600);
                        svga->render8514 = ati_render_24bpp;
                        break;
                    case 32:
                        mach_log("GEConfig32bpp: %03x.\n", mach->accel.ext_ge_config & 0x600);
                        svga->render8514 = ati_render_32bpp;
                        break;

                    default:
                        break;
                }
            }
        } else {
            mach->accel.src_pitch = dev->pitch;
            mach->accel.dst_pitch = dev->pitch;
            mach->accel.src_ge_offset = (mach->accel.ge_offset_lo | (mach->accel.ge_offset_hi << 16));
            mach->accel.dst_ge_offset = (mach->accel.ge_offset_lo | (mach->accel.ge_offset_hi << 16));
            mach->accel.src_ge_offset <<= 2;
            mach->accel.dst_ge_offset <<= 2;
            mach->accel.src_ge_offset -= mach->accel.crt_offset;
            mach->accel.dst_ge_offset -= mach->accel.crt_offset;

            mach_log("cntl=%d, clksel=%x, hv(%d,%d), pitch=%d, rowoffset=%d, gextconfig=%03x, shadow=%x interlace=%d, vgahdisp=%d.\n",
                     dev->accel.advfunc_cntl & 0x04, mach->accel.clock_sel & 0x01, dev->h_disp, dev->dispend, dev->pitch, dev->rowoffset,
                     mach->accel.ext_ge_config & 0xcec0, mach->shadow_set & 0x03, dev->interlace, svga->hdisp);
            if (dev->vram_512k_8514) {
                if (dev->h_disp == 640)
                    dev->pitch = 640;
                else
                    dev->pitch = 1024;
            }
            dev->accel_bpp = 8;
            svga->render8514 = ibm8514_render_8bpp;
        }
    } else {
        dev->mode = VGA_MODE;
        if (!svga->scrblank && svga->attr_palette_enable) {
            mach_log("GDCREG5=%02x, ATTR10=%02x, ATI B0 bit 5=%02x, ON=%d, char_width=%d, seqreg1 bit 3=%x, clk_sel=%02x.\n",
                     svga->gdcreg[5] & 0x60, svga->attrregs[0x10] & 0x40, mach->regs[0xb0] & 0x20, dev->on, svga->char_width, svga->seqregs[1] & 0x08, clock_sel);
            if (ATI_MACH32)
                svga->clock = (cpuclock * (double) (1ULL << 32)) / svga->getclock(clock_sel, svga->clock_gen);
            else
                svga->clock = (cpuclock * (double) (1ULL << 32)) / svga->getclock(clock_sel ^ 0x08, svga->clock_gen);

            switch ((mach->regs[0xb8] >> 6) & 3) {
                case 1:
                    svga->clock *= 2.0;
                    break;
                case 2:
                    svga->clock *= 3.0;
                    break;
                case 3:
                    svga->clock *= 4.0;
                    break;
                default:
                    break;
            }

            mach_log("VGA clock sel=%02x, divide reg=%02x, miscout bits2-3=%x, machregbe bit4=%02x, machregb9 bit1=%02x, charwidth=%d, htotal=%02x, hdisptime=%02x, seqregs1 bit 3=%02x.\n", clock_sel, (mach->regs[0xb8] >> 6) & 3, svga->miscout & 0x0c, mach->regs[0xbe] & 0x10, mach->regs[0xb9] & 0x02, svga->char_width, svga->htotal, svga->hdisp_time, svga->seqregs[1] & 8);
            if ((svga->gdcreg[6] & 0x01) || (svga->attrregs[0x10] & 0x01)) {
                if ((svga->gdcreg[5] & 0x40) || (svga->attrregs[0x10] & 0x40) || (mach->regs[0xb0] & 0x20)) {
                    svga->map8 = svga->pallook;
                    mach_log("Lowres=%x, seqreg[1]bit3=%x.\n", svga->lowres, svga->seqregs[1] & 8);
                    if (svga->lowres)
                        svga->render = svga_render_8bpp_lowres;
                    else {
                        svga->render = svga_render_8bpp_highres;
                        if (!svga->packed_4bpp) {
                            svga->memaddr_latch <<= 1;
                            svga->rowoffset <<= 1;
                        }
                    }
                }
            }
        }
    }
}

static void
mach_accel_out_fifo(mach_t *mach, svga_t *svga, ibm8514_t *dev, uint16_t port, uint16_t val, int len)
{
    int frgd_sel;
    int bkgd_sel;
    int mono_src;

    if (port & 0x8000) {
        if ((port & 0x06) != 0x06) {
            if ((port != 0xe2e8) && (port != 0xe2e9) && (port != 0xe6e8) && (port != 0xe6e9)) {
                if (port & 0x4000)
                    port &= ~0x4000;
            }
        }
    }

    mach_log("[%04X:%08X]: Port FIFO OUT=%04x, val=%04x, len=%d.\n", CS, cpu_state.pc, port, val, len);

    switch (port) {
        case 0x2e8:
            mach_log("HTOTAL=%04x, len=%d, set=%x, ATI mode bit=%x.\n", val, len, mach->shadow_set & 0x03, mach->accel.clock_sel & 0x01);
            if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                if (!(mach->shadow_cntl & 0x04))
                    dev->htotal = val;
            }
            svga_recalctimings(svga);
            break;

        case 0xae8:
            if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                if (!(mach->shadow_cntl & 0x04)) {
                    WRITE8(port, dev->hsync_start, val);
                }
            }
            svga_recalctimings(svga);
            break;

        case 0xee8:
            if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                if (!(mach->shadow_cntl & 0x04)) {
                    WRITE8(port, dev->hsync_width, val);
                }
            }
            svga_recalctimings(svga);
            break;

        case 0x6e8:
            if (len == 2) {
                mach_log("HDISP and HTOTAL=%04x, len=%d, set=%x, ATI mode bit=%x.\n", val, len, mach->shadow_set & 0x03, mach->accel.clock_sel & 0x01);
                if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                    if ((!(mach->shadow_cntl & 0x04)) && ((val >> 8) & 0xff))
                        dev->htotal = (val >> 8) & 0xff;

                    if (!(mach->shadow_cntl & 0x08)) {
                        if ((dev->htotal || (mach->accel.clock_sel & 0x01)) && (val & 0xff)) {
                            WRITE8(port, dev->hdisped, val);
                        }
                    }
                }
            } else {
                mach_log("HDISP and HTOTAL=%02x, len=%d, set=%x, ATI mode bit=%x.\n", val, len, mach->shadow_set & 0x03, mach->accel.clock_sel & 0x01);
                if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                    if (!(mach->shadow_cntl & 0x08)) {
                        if ((dev->htotal || (mach->accel.clock_sel & 0x01)) && (val & 0xff)) {
                            WRITE8(port, dev->hdisped, val);
                        }
                    }
                }
            }
            mach_log("[%04X:%08X]: ATI 8514/A: (0x%04x): hdisp=0x%02x, shadowcntl=%02x, shadowset=%02x.\n",
                    CS, cpu_state.pc, port, val, mach->shadow_cntl & 0x08, mach->shadow_set & 0x03);
            svga_recalctimings(svga);
            break;

        case 0x6e9:
            if (len == 1) {
                mach_log("HDISP and HTOTAL+1=%02x, len=%d, set=%x, ATI mode bit=%x.\n", val, len, mach->shadow_set & 0x03, mach->accel.clock_sel & 0x01);
                if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                    if (!(mach->shadow_cntl & 0x04) && val) {
                        dev->htotal = val;
                    }
                }
            }
            svga_recalctimings(svga);
            break;

        case 0x12e8:
            if (len == 2) {
                if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                    if (!(mach->shadow_cntl & 0x10) && val) {
                        dev->v_total_reg = val;
                        dev->v_total_reg &= 0x1fff;
                    }
                }
            } else {
                if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                    if (!(mach->shadow_cntl & 0x10)) {
                        WRITE8(port, dev->v_total_reg, val);
                        dev->v_total_reg &= 0x1fff;
                    }
                }
            }
            mach_log("[%04X:%08X]: ATI 8514/A: (0x%04x): hdisp=0x%02x.\n", CS, cpu_state.pc, port, val);
            svga_recalctimings(svga);
            break;

        case 0x12e9:
            if (len == 1) {
                if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) {
                    if (!(mach->shadow_cntl & 0x10)) { /*For 8514/A mode, take the shadow sets into account.*/
                        WRITE8(port, dev->v_total_reg, val >> 8);
                        dev->v_total_reg &= 0x1fff;
                    }
                }
                mach_log("[%04X:%08X]: ATI 8514/A: (0x%04x): hdisp=0x%02x.\n", CS, cpu_state.pc, port, val);
            }
            svga_recalctimings(svga);
            break;

        case 0x16e8:
            if (len == 2) {
                if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                    if (!(mach->shadow_cntl & 0x20) && val) {
                        dev->v_disp = val;
                        dev->v_disp &= 0x1fff;
                    }
                }
                mach_log("ATI 8514/A: V_DISP write 16E8=%d, vdisp2=%d.\n", dev->v_disp, dev->v_disp2);
                mach_log("ATI 8514/A: (0x%04x): vdisp=0x%02x.\n", port, val);
            } else {
                if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                    if (!(mach->shadow_cntl & 0x20)) {
                        WRITE8(port, dev->v_disp, val);
                        dev->v_disp &= 0x1fff;
                    }
                }
            }
            svga_recalctimings(svga);
            break;
        case 0x16e9:
            if (len == 1) {
                if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                    if (!(mach->shadow_cntl & 0x20)) {
                        WRITE8(port, dev->v_disp, val >> 8);
                        dev->v_disp &= 0x1fff;
                    }
                }
                mach_log("ATI 8514/A: V_DISP write 16E8=%d, vdisp2=%d.\n", dev->v_disp, dev->v_disp2);
                mach_log("ATI 8514/A: (0x%04x): vdisp=0x%02x.\n", port, val);
            }
            svga_recalctimings(svga);
            break;

        case 0x1ae8:
            if (len == 2) {
                if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                    if (!(mach->shadow_cntl & 0x10) && val) {
                        dev->v_sync_start = val;
                        dev->v_sync_start &= 0x1fff;
                    }
                }
                mach_log("ATI 8514/A: V_SYNCSTART write 1AE8 = %d\n", dev->v_syncstart);
                mach_log("ATI 8514/A: (0x%04x): vsyncstart=0x%02x.\n", port, val);
            } else {
                if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                    if (!(mach->shadow_cntl & 0x10)) {
                        WRITE8(port, dev->v_sync_start, val);
                        dev->v_sync_start &= 0x1fff;
                    }
                }
            }
            svga_recalctimings(svga);
            break;
        case 0x1ae9:
            if (len == 1) {
                if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) { /*For 8514/A mode, take the shadow sets into account.*/
                    if (!(mach->shadow_cntl & 0x10)) {
                        WRITE8(port, dev->v_sync_start, val >> 8);
                        dev->v_sync_start &= 0x1fff;
                    }
                }
                mach_log("ATI 8514/A: V_SYNCSTART write 1AE8 = %d\n", dev->v_syncstart);
                mach_log("ATI 8514/A: (0x%04x): vsyncstart=0x%02x.\n", port, val);
            }
            svga_recalctimings(svga);
            break;

        case 0x1ee8:
        case 0x1ee9:
            svga_recalctimings(svga);
            break;

        case 0x22e8:
            if ((mach->accel.clock_sel & 0x01) || (!(mach->accel.clock_sel & 0x01) && (mach->shadow_set & 0x03))) {
                if ((mach->shadow_cntl & 0x03) == 0x00)
                    dev->disp_cntl = val;
            }

            if (((mach->shadow_cntl & 0x03) == 0x00) || !dev->local)
                dev->disp_cntl_2 = val;

            mach_log("ATI 8514/A: DISP_CNTL write %04x=%02x, written=%02x, interlace=%02x, shadowset=%02x, shadowcntl=%02x.\n",
                     port, val & 0x70, dev->disp_cntl & 0x70, dev->disp_cntl & 0x10, mach->shadow_set & 0x03, mach->shadow_cntl & 0x03);
            svga_recalctimings(svga);
            break;

        case 0x42e8:
        case 0x42e9:
            mach_log("VBLANK status=%02x, val=%02x.\n", dev->subsys_stat, val);
            if (len == 2)
                dev->subsys_cntl = val;
            else {
                WRITE8(port, dev->subsys_cntl, val);
            }
            dev->subsys_stat &= ~val;
            if ((dev->subsys_cntl & 0xc000) == 0x8000) {
                mach->force_busy = 0;
                dev->force_busy = 0;
                dev->force_busy2 = 0;
            }
            break;

        case 0x46e8:
        case 0x46e9:
            mach_log("0x%04x write: VGA subsystem enable add-on=%02x.\n", port, val);
            break;

        case 0x4ae8:
        case 0x4ae9:
            WRITE8(port, dev->accel.advfunc_cntl, val);
            if (len == 2) {
                WRITE8(port + 1, dev->accel.advfunc_cntl, val >> 8);
            }
            dev->on = dev->accel.advfunc_cntl & 0x01;
            dev->vendor_mode = 0;
            if (dev->_8514crt) {
                if (mach->regs[0xb0] & 0x20) {
                    dev->on = 1;
                    dev->vendor_mode = !!(ATI_MACH32);
                }
            }

            dev->mode = IBM_MODE;
            mach_log("[%04X:%08X]: ATI 8514/A: (0x%04x): ON=%d, valxor=%x, shadow crt=%x, hdisp=%d, vdisp=%d, extmode=%02x, accelbpp=%d, crt=%d, crtres=%d.\n",
                     CS, cpu_state.pc, port, val & 0x01, dev->on, dev->accel.advfunc_cntl & 0x04, dev->hdisp, dev->vdisp, mach->regs[0xb0] & 0x20, dev->accel_bpp, dev->_8514crt, mach->crt_resolution);

            if (ATI_MACH32) {
                mach_set_resolution(mach, svga);
                mach32_updatemapping(mach, svga);
            } else {
                dev->ext_pitch = 1024;
                dev->ext_crt_pitch = 128;
                mach_set_resolution(mach, svga);
            }
            mach_log("Vendor IBM mode set %s resolution.\n", (dev->accel.advfunc_cntl & 0x04) ? "2: 1024x768" : "1: 640x480");
            break;

        case 0x82e8:
            ibm8514_accel_out_fifo(svga, port, val, len);
            mach_log("DSTY=%04x, len=%d.\n", val & 0x07ff, len);
            break;

        case 0x86e8:
            ibm8514_accel_out_fifo(svga, port, val, len);
            mach_log("DSTX=%04x, len=%d.\n", val & 0x07ff, len);
            break;

        case 0x8ae8:
            ibm8514_accel_out_fifo(svga, port, val, len);
            mach_log("SRCY=%04x, len=%d.\n", val & 0x07ff, len);
            if (len == 2) {
                mach->accel.src_y = val & 0x07ff;
                mach->accel.src_y_scan = ((int64_t)(val & 0x07ff));
            }
            break;

        case 0x8ee8:
            ibm8514_accel_out_fifo(svga, port, val, len);
            mach_log("SRCX=%04x, len=%d.\n", val & 0x07ff, len);
            if (len == 2) {
                mach->accel.src_x = val & 0x07ff;
                mach->accel.src_x_scan = ((int64_t)(val & 0x07ff));
            }
            break;

        case 0x92e8:
            ibm8514_accel_out_fifo(svga, port, val, len);
            break;

        case 0x96e8:
            ibm8514_accel_out_fifo(svga, port, val, len);
            if (len == 2)
                mach->accel.test = val & 0x1fff;
            break;

        case 0x9ae8:
            mach->accel.cmd_type = -1;
            ibm8514_accel_out_fifo(svga, port, val, len);
            break;

        case 0x9ee8:
            ibm8514_accel_out_fifo(svga, port, val, len);
            break;

        case 0xa2e8:
        case 0xe2e8:
            if (port == 0xe2e8) {
                mach_log("%04X: Background Color=%04x, pix=%d, len=%d.\n", port, val, dev->accel.cmd_back, len);
                if (len == 2) {
                    if (!dev->accel.cmd_back) {
                        if (mach->accel.cmd_type >= 0) {
                            if (mach_pixel_read(mach))
                                break;

                            mach_log("ATI transfer.\n");
                            mach_accel_out_pixtrans(svga, mach, dev, val);
                        } else {
                            if (ibm8514_cpu_dest(svga))
                                break;

                            mach_log("IBM transfer.\n");
                            ibm8514_accel_out_pixtrans(svga, port, val, len);
                        }
                    } else {
                        dev->accel.bkgd_color = val;
                        mach_log("%04X: CMDBack BKGDCOLOR, sy=%d, height=%d, cmdtype=%d, val=%04x.\n", port, dev->accel.sy, mach->accel.height, mach->accel.cmd_type, val);
                    }
                } else {
                    if (!dev->accel.cmd_back) {
                        if (mach->accel.cmd_type >= 0) {
                            if (mach_pixel_read(mach))
                                break;

                            mach->accel.pix_trans[1] = val;
                        }
                    }
                }
            } else {
                if (len == 2)
                    dev->accel.bkgd_color = val;

                mach_log("%04X: Background Color=%04x.\n", port, val);
            }
            break;

        case 0xa6e8:
        case 0xe6e8:
            if (port == 0xe6e8) {
                mach_log("%04X: Foreground Color=%04x, pix=%d, len=%d.\n", port, val, dev->accel.cmd_back, len);
                if (len == 2) {
                    if (!dev->accel.cmd_back) {
                        if (mach->accel.cmd_type >= 0) {
                            if (mach_pixel_read(mach))
                                break;

                            mach_log("ATI transfer.\n");
                            mach_accel_out_pixtrans(svga, mach, dev, val);
                        } else {
                            if (ibm8514_cpu_dest(svga))
                                break;

                            mach_log("IBM transfer.\n");
                            ibm8514_accel_out_pixtrans(svga, port, val, len);
                        }
                    } else
                        dev->accel.frgd_color = val;
                } else {
                    if (!dev->accel.cmd_back) {
                        if (mach->accel.cmd_type >= 0) {
                            if (mach_pixel_read(mach))
                                break;

                            mach->accel.pix_trans[1] = val;
                        }
                    }
                }
            } else {
                if (len == 2)
                    dev->accel.frgd_color = val;

                mach_log("%04X: Foreground Color=%04x.\n", port, val);
            }
            break;

        case 0xe2e9:
        case 0xe6e9:
            mach_log("Write PORT=%04x, 8514/A=%x, val0=%02x, sy=%d, len=%d, dx=%d, dy=%d.\n", port, dev->accel.cmd_back, val, dev->accel.sy, len, dev->accel.dx, dev->accel.dy);
            if (len == 1) {
                if (!dev->accel.cmd_back) {
                    if (mach->accel.cmd_type >= 0) {
                        if (mach_pixel_read(mach))
                            break;

                        mach->accel.pix_trans[0] = val;
                        frgd_sel = (mach->accel.dp_config >> 13) & 7;
                        bkgd_sel = (mach->accel.dp_config >> 7) & 3;
                        mono_src = (mach->accel.dp_config >> 5) & 3;

                        switch (mach->accel.dp_config & 0x200) {
                            case 0x000: /*8-bit size*/
                                if (mono_src == 2) {
                                    if ((frgd_sel != 2) && (bkgd_sel != 2)) {
                                        mach_accel_start(mach->accel.cmd_type, 1, 8, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), 0, svga, mach, dev);
                                    } else
                                        mach_accel_start(mach->accel.cmd_type, 1, 1, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), svga, mach, dev);
                                } else
                                    mach_accel_start(mach->accel.cmd_type, 1, 1, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), svga, mach, dev);
                                break;
                            case 0x200: /*16-bit size*/
                                if (mono_src == 2) {
                                    if ((frgd_sel != 2) && (bkgd_sel != 2)) {
                                        if (mach->accel.dp_config & 0x1000)
                                            mach_accel_start(mach->accel.cmd_type, 1, 16, mach->accel.pix_trans[1] | (mach->accel.pix_trans[0] << 8), 0, svga, mach, dev);
                                        else
                                            mach_accel_start(mach->accel.cmd_type, 1, 16, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), 0, svga, mach, dev);
                                    } else
                                        mach_accel_start(mach->accel.cmd_type, 1, 2, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), svga, mach, dev);
                                } else
                                    mach_accel_start(mach->accel.cmd_type, 1, 2, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), svga, mach, dev);
                                break;

                            default:
                                break;
                        }
                    }
                }
            }
            mach_log("Write Port=%04x, Busy=%02x.\n", port, dev->force_busy2);
            break;

        case 0xaae8:
            if (len == 2)
                mach->accel.dst_clr_cmp_mask = val;
            fallthrough;
        case 0xaee8:
        case 0xb2e8:
        case 0xb6e8:
        case 0xbae8:
            ibm8514_accel_out_fifo(svga, port, val, len);
            break;

        case 0xbee8:
            ibm8514_accel_out_fifo(svga, port, val, len);
            if (len == 2) {
                if ((dev->accel.multifunc_cntl >> 12) == 5) {
                    if (!ATI_MACH32) {
                        dev->ext_pitch = 1024;
                        dev->ext_crt_pitch = 128;
                        svga_recalctimings(svga);
                    }
                }
            }
            break;

            /*ATI Mach8/32 specific registers*/
        case 0x2ee:
        case 0x2ef:
            if (len == 2) {
                mach->overscan_col_8 = val & 0xff;
                mach->overscan_b_col_24 = (val >> 8) & 0xff;
            } else {
                if (port & 1)
                    mach->overscan_b_col_24 = val;
                else
                    mach->overscan_col_8 = val;
            }
            svga_recalctimings(svga);
            break;
        case 0x6ee:
        case 0x6ef:
            if (len == 2) {
                mach->overscan_g_col_24 = val & 0xff;
                mach->overscan_r_col_24 = (val >> 8) & 0xff;
            } else {
                if (port & 1)
                    mach->overscan_r_col_24 = val;
                else
                    mach->overscan_g_col_24 = val;
            }
            svga_recalctimings(svga);
            break;

        case 0xaee:
        case 0xaef:
            WRITE8(port, mach->cursor_offset_lo_reg, val);
            if (len == 2) {
                WRITE8(port + 1, mach->cursor_offset_lo_reg, val >> 8);
            }
            mach->cursor_offset_lo = mach->cursor_offset_lo_reg;
            dev->hwcursor.addr = ((mach->cursor_offset_lo | (mach->cursor_offset_hi << 16)) << 2);
            if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00))
                dev->hwcursor.addr <<= 1;
            break;

        case 0xeee:
        case 0xeef:
            WRITE8(port, mach->cursor_offset_hi_reg, val);
            if (len == 2) {
                WRITE8(port + 1, mach->cursor_offset_hi_reg, val >> 8);
            }
            dev->hwcursor.ena = !!(mach->cursor_offset_hi_reg & 0x8000);
            mach->cursor_offset_hi = mach->cursor_offset_hi_reg & 0x0f;
            dev->hwcursor.addr = ((mach->cursor_offset_lo | (mach->cursor_offset_hi << 16)) << 2);
            if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00))
                dev->hwcursor.addr <<= 1;
            break;

        case 0x12ee:
        case 0x12ef:
            WRITE8(port, mach->cursor_x, val);
            if (len == 2) {
                WRITE8(port + 1, mach->cursor_x, val >> 8);
            }
            dev->hwcursor.x = mach->cursor_x & 0x7ff;
            break;

        case 0x16ee:
        case 0x16ef:
            WRITE8(port, mach->cursor_y, val);
            if (len == 2) {
                WRITE8(port + 1, mach->cursor_y, val >> 8);
            }
            dev->hwcursor.y = mach->cursor_y & 0xfff;
            break;

        case 0x1aee:
        case 0x1aef:
            WRITE8(port, mach->cursor_col_b, val);
            if (len == 2) {
                WRITE8(port + 1, mach->cursor_col_b, val >> 8);
            }
            mach->cursor_col_0 = mach->cursor_col_b & 0xff;
            mach->cursor_col_1 = (mach->cursor_col_b >> 8) & 0xff;
            mach_log("ATI 8514/A: (0x%04x) Cursor Color B, val=0x%02x, len=%d, extended 8514/A mode bpp=%d.\n", port, val, len, dev->accel_bpp);
            break;

        case 0x1eee:
        case 0x1eef:
            WRITE8(port, mach->cursor_vh_offset, val);
            if (len == 2) {
                WRITE8(port + 1, mach->cursor_vh_offset, val >> 8);
            }
            dev->hwcursor.xoff = mach->cursor_vh_offset & 0x3f;
            dev->hwcursor.yoff = (mach->cursor_vh_offset >> 8) & 0x3f;
            break;

        case 0x22ee:
            if (mach->pci_bus) {
                mach->pci_cntl_reg = val;
                mach_log("PCI Control Reg=%02x.\n", val);
                mach32_updatemapping(mach, svga);
            }
            break;

        case 0x26ee:
        case 0x26ef:
            WRITE8(port, mach->accel.crt_pitch, val);
            if (len == 2) {
                WRITE8(port + 1, mach->accel.crt_pitch, val >> 8);
            }

            dev->ext_crt_pitch = mach->accel.crt_pitch & 0xff;

            if (dev->accel_bpp > 8) {
                if (dev->accel_bpp == 24) {
                    dev->ext_crt_pitch *= 3;
                } else if (dev->accel_bpp == 32)
                    dev->ext_crt_pitch <<= 2;
                else
                    dev->ext_crt_pitch <<= 1;
            }

            if (len == 2) {
                dev->_8514crt = 0;
                if (!(dev->accel.advfunc_cntl & 0x01) && ATI_MACH32) {
                    dev->on = 1;
                    dev->vendor_mode = 1;
                }
            } else
                dev->_8514crt = 1;

            if ((dev->mode != VGA_MODE) && ATI_MACH32)
                mach_set_resolution(mach, svga);
            else
                svga_recalctimings(svga);

            if (ATI_GRAPHICS_ULTRA || ATI_MACH32)
                mach32_updatemapping(mach, svga);

            mach_log("ATI 8514/A: (0x%04x) CRT Pitch, val=0x%02x, crtpitch=%x, len=%d, extended 8514/A mode bpp=%d, enable_on=%d.\n", port, val, dev->ext_crt_pitch, len, dev->accel_bpp, dev->on);
            break;

        case 0x2aee:
        case 0x2aef:
            if (len == 2)
                mach->accel.crt_offset_lo = val;
            else {
                WRITE8(port, mach->accel.crt_offset_lo, val);
            }
            mach_log("ATI 8514/A: (0x%04x) CRT Offset Low val=0x%02x, len=%d.\n", port, val, len);
            break;

        case 0x2eee:
        case 0x2eef:
            mach->accel.crt_offset_hi = val & 0x0f;
            mach_log("ATI 8514/A: (0x%04x) CRT Offset High val=0x%02x, len=%d.\n", port, val, len);
            break;

        case 0x32ee:
        case 0x32ef:
            if (len == 2)
                mach->local_cntl = val;
            else {
                WRITE8(port, mach->local_cntl, val);
            }
            if (ATI_GRAPHICS_ULTRA || ATI_MACH32)
                mach32_updatemapping(mach, svga);
            break;

        case 0x36ee:
        case 0x36ef:
            if (len == 2) {
                if (ATI_MACH32)
                    mach->misc = val;
            } else {
                if (ATI_MACH32)
                    WRITE8(port, mach->misc, val);
            }
            mach->misc &= 0xfff0;
            break;

        case 0x3aee:
        case 0x3aef:
            if (len == 2)
                mach->cursor_col_0_rg = val;
            else {
                WRITE8(port, mach->cursor_col_0_rg, val);
            }
            mach->ext_cur_col_0_g = mach->cursor_col_0_rg & 0xff;
            mach->ext_cur_col_0_r = (mach->cursor_col_0_rg >> 8) & 0xff;
            mach_log("ATI 8514/A: (0x%04x) Cursor Color 0 RG, val=0x%02x, len=%d, extended 8514/A mode bpp=%d.\n", port, val, len, dev->accel_bpp);
            break;

        case 0x3eee:
        case 0x3eef:
            if (len == 2)
                mach->cursor_col_1_rg = val;
            else {
                WRITE8(port, mach->cursor_col_1_rg, val);
            }
            mach->ext_cur_col_1_g = mach->cursor_col_1_rg & 0xff;
            mach->ext_cur_col_1_r = (mach->cursor_col_1_rg >> 8) & 0xff;
            mach_log("ATI 8514/A: (0x%04x) Cursor Color 1 RG, val=0x%02x, len=%d, extended 8514/A mode bpp=%d.\n", port, val, len, dev->accel_bpp);
            break;

        case 0x42ee:
        case 0x42ef:
            if (len == 2)
                mach->accel.test2 = val;
            else {
                WRITE8(port, mach->accel.test2, val);
            }
            mach_log("ATI 8514/A: (0x%04x) MEM_BNDRY val=%04x, memory part=%06x, gdcreg6=%02x.\n", port, val, (mach->accel.test2 & 0x0f) << 18, svga->gdcreg[6] & 0x0c);
            mach32_updatemapping(mach, svga);
            break;

        case 0x46ee:
        case 0x46ef:
            if (len == 2)
                mach->shadow_cntl = val;
            else {
                WRITE8(port, mach->shadow_cntl, val);
            }
            mach_log("ATI 8514/A: (0x%04x) val=%02x.\n", port, val);
            break;

        case 0x4aee:
        case 0x4aef:
            WRITE8(port, mach->accel.clock_sel, val);
            if (len == 2) {
                WRITE8(port + 1, mach->accel.clock_sel, val >> 8);
            }
            if (!(dev->accel.advfunc_cntl & 0x01))
                dev->on = mach->accel.clock_sel & 0x01;
            else {
                if (!(mach->regs[0xb0] & 0x20) && !(mach->accel.clock_sel & 0x01))
                    dev->on = 0;
            }

            dev->vendor_mode = 1;
            dev->mode = ATI_MODE;

            mach_log("[%04X:%08X]: ATI 8514/A: (0x%04x): ON=%d, val=%04x, xor=%d, hdisp=%d, vdisp=%d, accelbpp=%d.\n",
                     CS, cpu_state.pc, port, mach->accel.clock_sel & 0x01, val, dev->on, dev->hdisp, dev->vdisp, dev->accel_bpp);
            mach_log("Vendor ATI mode set %s resolution.\n",
                     (dev->accel.advfunc_cntl & 0x04) ? "2: 1024x768" : "1: 640x480");

            mach_set_resolution(mach, svga);
            if (ATI_GRAPHICS_ULTRA || ATI_MACH32)
                mach32_updatemapping(mach, svga);
            break;

        case 0x52ee:
        case 0x52ef:
            mach_log("ATI 8514/A: (0x%04x) ScratchPad0 val=%04x.\n", port, val);
            if (len == 2)
                mach->accel.scratch0 = val;
            else {
                WRITE8(port, mach->accel.scratch0, val);
            }
            break;

        case 0x56ee:
        case 0x56ef:
            mach_log("ATI 8514/A: (0x%04x) ScratchPad1 val=%04x.\n", port, val);
            if (len == 2)
                mach->accel.scratch1 = val;
            else {
                WRITE8(port, mach->accel.scratch1, val);
            }
            break;

        case 0x5aee:
        case 0x5aef:
            WRITE8(port, mach->shadow_set, val);
            if (len == 2) {
                WRITE8(port + 1, mach->shadow_set, val >> 8);
            }
            mach_log("ATI 8514/A: (0x%04x) val=0x%02x, len=%d.\n", port, val, len);
            if ((mach->shadow_set & 0x03) == 0x00)
                mach_log("Primary CRT register set.\n");
            else if ((mach->shadow_set & 0x03) == 0x01)
                mach_log("CRT Shadow Set 1: 640x480.\n");
            else if ((mach->shadow_set & 0x03) == 0x02)
                mach_log("CRT Shadow Set 2: 1024x768.\n");
            break;

        case 0x5eee:
        case 0x5eef:
            if (len == 2)
                mach->memory_aperture = val;
            else {
                WRITE8(port, mach->memory_aperture, val);
            }
            mach_log("Memory Aperture = %04x.\n", mach->memory_aperture);
            if (!mach->pci_bus)
                mach->linear_base = (mach->memory_aperture & 0xff00) << 12;

            if (ATI_GRAPHICS_ULTRA || ATI_MACH32)
                mach32_updatemapping(mach, svga);
            break;

        case 0x6aee:
        case 0x6aef:
            if (len == 2)
                mach->accel.max_waitstates = val;
            else {
                WRITE8(port, mach->accel.max_waitstates, val);
            }
            mach_log("ATI 8514/A: (0x%04x) val=0x%02x, len=%d.\n", port, val, len);
            break;

        case 0x6eee:
        case 0x6eef:
            if (len == 2)
                mach->accel.ge_offset_lo = val;
            else {
                WRITE8(port, mach->accel.ge_offset_lo, val);
            }
            mach_log("ATI 8514/A: (0x%04x) GE Offset Low val=0x%02x, geoffset=%04x, len=%d.\n", port, val, dev->accel.ge_offset, len);
            svga_recalctimings(svga);
            break;

        case 0x72ee:
        case 0x72ef:
            if (len == 2)
                mach->accel.ge_offset_hi = val;
            else {
                WRITE8(port, mach->accel.ge_offset_hi, val);
            }
            mach_log("ATI 8514/A: (0x%04x) GE Offset High val=0x%02x, geoffset=%04x, len=%d.\n", port, val, dev->accel.ge_offset, len);
            svga_recalctimings(svga);
            break;

        case 0x76ee:
        case 0x76ef:
            if (len == 2)
                mach->accel.ge_pitch = val;
            else {
                WRITE8(port, mach->accel.ge_pitch, val);
            }
            dev->ext_pitch = ((mach->accel.ge_pitch & 0xff) << 3);
            mach_log("ATI 8514/A: (0x%04x) GE Pitch val=0x%02x.\n", port, val);
            svga_recalctimings(svga);
            break;

        case 0x7aee:
        case 0x7aef:
            WRITE8(port, mach->accel.ext_ge_config, val);
            if (len == 2) {
                WRITE8(port + 1, mach->accel.ext_ge_config, val >> 8);
            }

            if (ATI_MACH32) {
                if (mach->accel.crt_pitch & 0xff)
                    dev->ext_crt_pitch = mach->accel.crt_pitch & 0xff;

                switch (mach->accel.ext_ge_config & 0x30) {
                    case 0x00:
                    case 0x10:
                        dev->bpp = 0;
                        dev->accel_bpp = 8;
                        break;
                    case 0x20:
                        dev->bpp = 1;
                        dev->ext_crt_pitch <<= 1;
                        switch (mach->accel.ext_ge_config & 0xc0) {
                            case 0x00:
                                dev->accel_bpp = 15;
                                break;
                            case 0x40:
                                dev->accel_bpp = 16;
                                break;
                            default: /*TODO: 655RGB and 664RGB*/
                                break;
                        }
                        break;
                    case 0x30:
                        dev->bpp = 0;
                        if (mach->accel.ext_ge_config & 0x200) {
                            dev->ext_crt_pitch <<= 2;
                            dev->accel_bpp = 32;
                        } else {
                            dev->ext_crt_pitch *= 3;
                            dev->accel_bpp = 24;
                        }
                        break;

                    default:
                        break;
                }
                svga_set_ramdac_type(svga, !!(mach->accel.ext_ge_config & 0x4000));
                mach_log("ATI 8514/A: (0x%04x) Extended Configuration=%04x, val=%04x.\n", port, mach->accel.ext_ge_config, val);
                if (dev->mode != VGA_MODE)
                    mach_set_resolution(mach, svga);
                else
                    svga_recalctimings(svga);

                mach32_updatemapping(mach, svga);
            } else {
                if (mach->accel.ext_ge_config & 0x80)
                    ati_eeprom_write(&mach->eeprom, !!(mach->accel.ext_ge_config & 0x04), !!(mach->accel.ext_ge_config & 0x02), !!(mach->accel.ext_ge_config & 0x01));
            }
            break;

        case 0x7eee:
        case 0x7eef:
            if (len == 2)
                mach->accel.eeprom_control = val;
            else {
                WRITE8(port, mach->accel.eeprom_control, val);
            }
            mach_log("%04X write val=%04x, actual=%04x, len=%d.\n", port, mach->accel.eeprom_control, val, len);
            break;

        case 0x82ee:
            mach->accel.patt_data_idx_reg = val & 0x1f;
            mach->accel.patt_data_idx = mach->accel.patt_data_idx_reg;

            mach_log("Write Port 82ee: Pattern Data Index=%d, idx for color=%d.\n", val & 0x1f, mach->accel.color_pattern_idx);

            if (mach->accel.patt_data_idx_reg < 0x10)
                mach->accel.color_pattern_idx = mach->accel.patt_idx;
            else
                mach->accel.color_pattern_idx = 0;
            break;

        case 0x8eee:
            if (len == 2) {
                if (mach->accel.patt_data_idx_reg < 0x10) {
                    if (dev->bpp) {
                        mach->accel.color_pattern_hicol[mach->accel.patt_data_idx] = val;
                        mach_log("Write Port 8eee: Color Pattern Word Data[%d]=%04x.\n", mach->accel.patt_data_idx, val);
                        mach->accel.patt_data_idx++;
                    } else {
                        mach->accel.color_pattern[mach->accel.patt_data_idx] = val & 0xff;
                        mach->accel.color_pattern[mach->accel.patt_data_idx + 1] = (val >> 8) & 0xff;
                        mach_log("Write Port 8eee: Color Pattern Word Data[%d]=%04x.\n", mach->accel.patt_data_idx, val);
                        mach->accel.patt_data_idx += 2;
                    }
                } else {
                    mach->accel.mono_pattern_normal[mach->accel.patt_data_idx - 0x10] = val & 0xff;
                    mach->accel.mono_pattern_normal[(mach->accel.patt_data_idx + 1) - 0x10] = (val >> 8) & 0xff;
                    mach_log("Write Port 8eee: Mono Pattern Word Data[%d]=%04x.\n", mach->accel.patt_data_idx - 0x10, val);
                    mach->accel.patt_data_idx += 2;
                }
            }
            break;

        case 0x92ee:
            mach_log("Write port 92ee, malatch=%08x.\n", svga->memaddr_latch);
            break;

        case 0x96ee:
            if (len == 2) {
                mach->accel.bres_count = val & 0x7ff;
                mach_log("BresenhamDraw=%04x.\n", mach->accel.dp_config);
                dev->data_available = 0;
                dev->data_available2 = 0;
                mach->accel.cmd_type = 1;
                frgd_sel = (mach->accel.dp_config >> 13) & 7;
                bkgd_sel = (mach->accel.dp_config >> 7) & 3;
                mono_src = (mach->accel.dp_config >> 5) & 3;

                dev->accel.cmd_back = 1;
                if ((mono_src == 2) || (bkgd_sel == 2) || (frgd_sel == 2) || mach_pixel_read(mach))
                    dev->accel.cmd_back = 0;

                mach_accel_start(mach->accel.cmd_type, 0, -1, -1, 0, svga, mach, dev);
            }
            break;

        case 0x9aee:
            mach->accel.line_idx = val & 0x07;
            break;

        case 0xa2ee:
            mach_log("Line OPT=%04x.\n", val);
            if (len == 2) {
                mach->accel.linedraw_opt = val;
                mach->accel.bbottom = dev->accel.clip_bottom;
                mach->accel.btop = dev->accel.clip_top;
                mach->accel.bleft = dev->accel.clip_left;
                mach->accel.bright = dev->accel.clip_right;
                if (mach->accel.linedraw_opt & 0x100) {
                    mach->accel.bbottom = 2047;
                    mach->accel.btop = 0;
                    mach->accel.bleft = 0;
                    mach->accel.bright = 2047;
                }
            }
            break;

        case 0xa6ee:
            if (len == 2)
                mach->accel.dest_x_start = val & 0x7ff;
            break;

        case 0xaaee:
            if (len == 2)
                mach->accel.dest_x_end = val & 0x7ff;
            break;

        case 0xaeee:
            if (len == 2) {
                mach->accel.dest_y_end = val & 0x7ff;
                if ((val + 1) == 0x10000) {
                    mach_log("Dest_Y_end overflow val=%04x, DPCONFIG=%04x\n", val, mach->accel.dp_config);
                    mach->accel.dest_y_end = 0;
                }
                dev->data_available  = 0;
                dev->data_available2 = 0;
                mach_log("BitBLT=%04x, pattidx=%d.\n", mach->accel.dp_config, mach->accel.patt_idx);
                mach_log(".\n");
                mach->accel.cmd_type = 2; /*Non-conforming BitBLT from dest_y_end register (0xaeee)*/

                frgd_sel = (mach->accel.dp_config >> 13) & 7;
                bkgd_sel = (mach->accel.dp_config >> 7) & 3;
                mono_src = (mach->accel.dp_config >> 5) & 3;

                dev->accel.cmd_back = 1;
                if ((mono_src == 2) || (bkgd_sel == 2) || (frgd_sel == 2) || mach_pixel_read(mach))
                    dev->accel.cmd_back = 0;

                mach_accel_start(mach->accel.cmd_type, 0, -1, -1, 0, svga, mach, dev);
            }
            break;

        case 0xb2ee:
            if (len == 2)
                mach->accel.src_x_start = val & 0x7ff;
            break;

        case 0xb6ee:
            dev->accel.bkgd_mix = val & 0x1f;
            dev->accel.bkgd_sel = (mach->accel.dp_config >> 7) & 3;
            break;

        case 0xbaee:
            dev->accel.frgd_mix = val & 0x1f;
            dev->accel.frgd_sel = (mach->accel.dp_config >> 13) & 3;
            break;

        case 0xbeee:
            if (len == 2)
                mach->accel.src_x_end = val & 0x7ff;
            break;

        case 0xc2ee:
            mach->accel.src_y_dir = (val & 1) ? 1 : -1;
            mach_log("Source Y Direction=%x.\n", val);
            break;

        case 0xc6ee:
            if (len == 2) {
                mach->accel.cmd_type = 0;
                mach_log("TODO: Short Stroke.\n");
                frgd_sel = (mach->accel.dp_config >> 13) & 7;
                bkgd_sel = (mach->accel.dp_config >> 7) & 3;
                mono_src = (mach->accel.dp_config >> 5) & 3;

                dev->accel.cmd_back = 1;
                if ((mono_src == 2) || (bkgd_sel == 2) || (frgd_sel == 2) || mach_pixel_read(mach))
                    dev->accel.cmd_back = 0;
            }
            break;

        case 0xcaee:
            if (len == 2) {
                mach->accel.scan_to_x = (val & 0x7ff);
                if ((val + 1) == 0x10000) {
                    mach_log("Scan_to_X overflow val = %04x\n", val);
                    mach->accel.scan_to_x = 0;
                }
                dev->data_available  = 0;
                dev->data_available2 = 0;
                mach->accel.cmd_type = 5; /*Horizontal Raster Draw from scan_to_x register (0xcaee)*/
                mach_log("ScanToX len=%d.\n", val);
                mach_log(".\n");

                frgd_sel = (mach->accel.dp_config >> 13) & 7;
                bkgd_sel = (mach->accel.dp_config >> 7) & 3;
                mono_src = (mach->accel.dp_config >> 5) & 3;

                dev->accel.cmd_back = 1;
                if ((mono_src == 2) || (bkgd_sel == 2) || (frgd_sel == 2) || mach_pixel_read(mach))
                    dev->accel.cmd_back = 0;

                mach_log("ScanToX=%04x, mono_src=%d, bkgd_sel=%d, frgd_sel=%d, pixread=%x.\n", mach->accel.dp_config, mono_src, bkgd_sel, frgd_sel, mach_pixel_read(mach));
                mach_accel_start(mach->accel.cmd_type, 0, -1, -1, 0, svga, mach, dev);
            }
            break;

        case 0xceee:
            mach_log("Data Path Configuration (%04x) write val=%04x, len=%d.\n", port, val, len);
            if (len == 2) {
                dev->data_available  = 0;
                dev->data_available2 = 0;
                mach->accel.dp_config = val;
            }
            break;

        case 0xd2ee:
            mach->accel.patt_len = val & 0x1f;
            mach_log("Write Port d2ee: Pattern Length=%d, val=%04x.\n", val & 0x1f, val);
            mach->accel.mono_pattern_enable = !!(val & 0x80);
            if (len == 2) {
                mach->accel.block_write_mono_pattern_enable = !!(val & 0x8000);
                mach->accel.patt_len_reg = val;
            }
            break;

        case 0xd6ee:
            mach->accel.patt_idx = val & 0x1f;
            frgd_sel = (mach->accel.dp_config >> 13) & 7;

            if ((frgd_sel == 5) && (dev->accel_bpp >= 24) && (mach->accel.patt_len == 0x17))
                mach->accel.color_pattern_idx = 0;

            mach_log("Write Port d6ee: Pattern Index=%d.\n", val & 0x1f);
            break;

        case 0xdaee:
            if (len == 2) {
                dev->accel.multifunc[2] = val & 0x7ff;
                dev->accel.clip_left = dev->accel.multifunc[2];
                if (val & 0x800)
                    dev->accel.clip_left |= ~0x7ff;
            }
            mach_log("DAEE (extclipl) write val=%d, left=%d.\n", val, dev->accel.clip_left);
            break;

        case 0xdeee:
            if (len == 2) {
                dev->accel.multifunc[1] = val & 0x7ff;
                dev->accel.clip_top = dev->accel.multifunc[1];
                if (val & 0x800) {
                    dev->accel.clip_top |= ~0x7ff;
                }
            }
            mach_log("DEEE (extclipt) write val = %d\n", val);
            break;

        case 0xe2ee:
            if (len == 2) {
                dev->accel.multifunc[4] = val & 0x7ff;
                dev->accel.clip_right = dev->accel.multifunc[4];
                if (val & 0x800)
                    dev->accel.clip_right |= ~0x7ff;
            }
            mach_log("E2EE (extclipr) write val = %d\n", val);
            break;

        case 0xe6ee:
            if (len == 2) {
                dev->accel.multifunc[3] = val & 0x7ff;
                dev->accel.clip_bottom = dev->accel.multifunc[3];
                if (val & 0x800)
                    dev->accel.clip_bottom |= ~0x7ff;
            }
            mach_log("E6EE (extclipb) write val = %d\n", val);
            break;

        case 0xeeee:
            mach_log("EEEE val=%04x, len=%d.\n", val, len);
            if (len == 2)
                mach->accel.dest_cmp_fn = val;
            break;

        case 0xf2ee:
            mach_log("F2EE val=%04x, len=%d.\n", val, len);
            if (len == 2)
                mach->accel.dst_clr_cmp_mask = val;
            break;

        case 0xfeee:
            if (len == 2) {
                mach->accel.line_array[mach->accel.line_idx] = val;
                mach_log("mach->accel.line_array[%02X] = %04X\n", mach->accel.line_idx, val);
                dev->accel.cur_x                             = mach->accel.line_array[(mach->accel.line_idx == 4) ? 4 : 0];
                dev->accel.cur_y                             = mach->accel.line_array[(mach->accel.line_idx == 5) ? 5 : 1];
                mach->accel.cx_end_line                      = mach->accel.line_array[2];
                mach->accel.cy_end_line                      = mach->accel.line_array[3];
                if ((mach->accel.line_idx == 3) || (mach->accel.line_idx == 5)) {
                    mach->accel.cmd_type = (mach->accel.line_idx == 5) ? 4 : 3;
                    frgd_sel = (mach->accel.dp_config >> 13) & 7;
                    bkgd_sel = (mach->accel.dp_config >> 7) & 3;
                    mono_src = (mach->accel.dp_config >> 5) & 3;

                    dev->accel.cmd_back = 1;
                    if ((mono_src == 2) || (bkgd_sel == 2) || (frgd_sel == 2) || mach_pixel_read(mach))
                        dev->accel.cmd_back = 0;

                    if ((mach->accel.cmd_type == 3) && !dev->accel.cmd_back && (mach->accel.dp_config == 0x0000)) /*Avoid a hang with a dummy command.*/
                        dev->accel.cmd_back = 1;

                    mach_log("LineDraw type=%x, dpconfig=%04x.\n", mach->accel.cmd_type, mach->accel.dp_config);
                    mach_accel_start(mach->accel.cmd_type, 0, -1, -1, 0, svga, mach, dev);
                    mach->accel.line_idx = (mach->accel.line_idx == 5) ? 4 : 2;
                    break;
                }
                mach->accel.line_idx++;
            }
            break;

        default:
            mach_log("Unknown or reserved write to %04x, val=%04x, len=%d, latch=%08x.\n", port, val, len, svga->memaddr_latch);
            break;
    }
}

static uint16_t
mach_accel_in_fifo(mach_t *mach, svga_t *svga, ibm8514_t *dev, uint16_t port, int len)
{
    const uint16_t *vram_w = (uint16_t *) dev->vram;
    uint16_t        temp = 0x0000;
    int             cmd;
    int             frgd_sel;
    int             bkgd_sel;
    int             mono_src;

    switch (port) {
        case 0x82e8:
            if ((mach->accel.cmd_type == 3) || (mach->accel.cmd_type == 4))
                temp = mach->accel.cy_end_line;
            else
                temp = ibm8514_accel_in_fifo(svga, port, len);
            break;

        case 0x86e8:
            if ((mach->accel.cmd_type == 3) || (mach->accel.cmd_type == 4))
                temp = mach->accel.cx_end_line;
            else
                temp = ibm8514_accel_in_fifo(svga, port, len);
            break;

        case 0x92e8:
        case 0x96e8:
        case 0xc2e8:
        case 0xc6e8:
            temp = ibm8514_accel_in_fifo(svga, port, len);
            break;

        case 0x9ae8:
        case 0xdae8:
            if (len == 2) {
                if (dev->fifo_idx <= 8) {
                    for (int i = 1; i <= dev->fifo_idx; i++)
                        temp |= (1 << (7 - (i - 1)));
                } else
                    temp = 0x00ff;

                if (dev->fifo_idx > 0)
                    dev->fifo_idx--;

                if (dev->force_busy) {
                    temp |= 0x0200; /*Hardware busy*/
                    if (mach->accel.cmd_type >= 0) {
                        frgd_sel = (mach->accel.dp_config >> 13) & 7;
                        bkgd_sel = (mach->accel.dp_config >> 7) & 3;
                        mono_src = (mach->accel.dp_config >> 5) & 3;
                        switch (mach->accel.cmd_type) {
                            case 2:
                                if (dev->accel.sy >= mach->accel.height)
                                    dev->force_busy = 0;
                                else if ((mono_src == 2) || (frgd_sel == 2) || (bkgd_sel == 2))
                                    dev->force_busy = 0;
                                else if (!dev->accel.cmd_back)
                                    dev->force_busy = 0;

                                mach_log("2Force Busy=%d, frgdsel=%d, bkgdsel=%d, monosrc=%d, read=%d, dpconfig=%04x, back=%d.\n", dev->force_busy, frgd_sel, bkgd_sel, mono_src, mach_pixel_read(mach), mach->accel.dp_config, dev->accel.cmd_back);
                                break;
                            case 5:
                                if (dev->accel.sx >= mach->accel.width)
                                    dev->force_busy = 0;
                                break;
                            default:
                                if (dev->accel.sy < 0)
                                    dev->force_busy = 0;
                                break;
                        }
                    } else {
                        switch (dev->accel.cmd >> 13) {
                            case 2:
                            case 3:
                            case 4:
                            case 6:
                                if (dev->accel.sy < 0)
                                    dev->force_busy = 0;
                                break;
                            default:
                                if (!dev->accel.sy)
                                    dev->force_busy = 0;
                                break;
                        }
                    }
                }

                if (dev->data_available) {
                    temp |= 0x0100; /*Read Data available*/
                    if (mach->accel.cmd_type >= 0) {
                        switch (mach->accel.cmd_type) {
                            case 2:
                                if (dev->accel.sy >= mach->accel.height)
                                    dev->data_available = 0;
                                break;
                            case 5:
                                if (dev->accel.sx >= mach->accel.width)
                                    dev->data_available = 0;
                                break;
                            default:
                                if (dev->accel.sy < 0)
                                    dev->data_available = 0;
                                break;
                        }
                    } else {
                        switch (dev->accel.cmd >> 13) {
                            case 2:
                            case 3:
                            case 4:
                            case 6:
                                if (dev->accel.sy < 0)
                                    dev->data_available = 0;
                                break;
                            default:
                                if (!dev->accel.sy)
                                    dev->data_available = 0;
                                break;
                        }
                    }
                }
            }
            mach_log("[%04X:%08X]: 9AE8: Temp = %04x, len = %d\n\n", CS, cpu_state.pc, temp, len);
            break;
        case 0x9ae9:
        case 0xdae9:
            if (len == 1) {
                dev->fifo_idx = 0;

                if (dev->force_busy2)
                    temp |= 0x02; /*Hardware busy*/

                dev->force_busy2 = 0;

                if (dev->data_available2) {
                    temp |= 0x01; /*Read Data available*/
                    if (mach->accel.cmd_type >= 0) {
                        switch (mach->accel.cmd_type) {
                            case 2:
                                if (dev->accel.sy >= mach->accel.height)
                                    dev->data_available2 = 0;
                                break;
                            case 5:
                                if (dev->accel.sx >= mach->accel.width)
                                    dev->data_available2 = 0;
                                break;
                            default:
                                if (dev->accel.sy < 0)
                                    dev->data_available2 = 0;
                                break;
                        }
                    } else {
                        switch (dev->accel.cmd >> 13) {
                            case 2:
                            case 3:
                            case 4:
                            case 6:
                                if (dev->accel.sy < 0)
                                    dev->data_available2 = 0;
                                break;
                            default:
                                if (!dev->accel.sy)
                                    dev->data_available2 = 0;
                                break;
                        }
                    }
                }
            }
            mach_log("[%04X:%08X]: 9AE9: Temp = %04x, len = %d\n\n", CS, cpu_state.pc, temp, len);
            break;

        case 0xe2e8:
        case 0xe6e8:
            if (mach->accel.cmd_type >= 0) {
                if (mach_pixel_read(mach)) {
                    cmd = -1;
                    if (len == 1) {
                        READ_PIXTRANS_BYTE_IO(dev->accel.dx, 1)
                        temp = mach->accel.pix_trans[1];
                    } else {
                        if ((mach->accel.cmd_type == 3) || (mach->accel.cmd_type == 4)) {
                            READ_PIXTRANS_WORD(dev->accel.cx, 0)
                        } else {
                            READ_PIXTRANS_WORD(dev->accel.dx, 0)
                        }
                        mach_accel_out_pixtrans(svga, mach, dev, temp);
                    }
                }
            } else {
                if (ibm8514_cpu_dest(svga)) {
                    cmd = (dev->accel.cmd >> 13);
                    if (len == 2) {
                        READ_PIXTRANS_WORD(dev->accel.cx, 0)
                        if (dev->subsys_stat & INT_VSY) {
                            dev->force_busy = 1;
                            dev->data_available = 1;
                        }
                        if (dev->accel.input3)
                            temp = 0xffff;

                        mach_log("Opcode=%d, Len=%d, port=%04x, input=%d, temp=%04x, fullcmd=%04x, crx=%d, cry=%d, frgdsel=%x, bkgdsel=%x.\n", cmd, len, port, dev->accel.input, temp, dev->accel.cmd, dev->accel.cx, dev->accel.cy, dev->accel.frgd_sel, dev->accel.bkgd_sel);

                        if (dev->accel.input) {
                            ibm8514_accel_out_pixtrans(svga, port, temp & 0xff, len);
                            if (dev->accel.odd_in) { /*WORDs on odd destination scan lengths.*/
                                dev->accel.odd_in = 0;
                                temp &= ~0xff00;
                                READ_HIGH(dev->accel.dest + dev->accel.cx, temp);
                            }
                            ibm8514_accel_out_pixtrans(svga, port, (temp >> 8) & 0xff, len);
                        } else
                            ibm8514_accel_out_pixtrans(svga, port, temp, len);
                    }
                }
            }
            break;
        case 0xe2e9:
        case 0xe6e9:
            if (mach->accel.cmd_type >= 0) {
                mach_log("%04x pixtrans read, len=%d.\n", port, len);
                if (mach_pixel_read(mach)) {
                    if (len == 1) {
                        cmd = -1;
                        READ_PIXTRANS_BYTE_IO(dev->accel.dx, 0)

                        temp = mach->accel.pix_trans[0];
                        frgd_sel = (mach->accel.dp_config >> 13) & 7;
                        bkgd_sel = (mach->accel.dp_config >> 7) & 3;
                        mono_src = (mach->accel.dp_config >> 5) & 3;

                        switch (mach->accel.dp_config & 0x200) {
                            case 0x000: /*8-bit size*/
                                if (mono_src == 2) {
                                    if ((frgd_sel != 2) && (bkgd_sel != 2)) {
                                        mach_accel_start(mach->accel.cmd_type, 1, 8, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), 0, svga, mach, dev);
                                    } else
                                        mach_accel_start(mach->accel.cmd_type, 1, 1, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), svga, mach, dev);
                                } else
                                    mach_accel_start(mach->accel.cmd_type, 1, 1, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), svga, mach, dev);
                                break;
                            case 0x200: /*16-bit size*/
                                if (mono_src == 2) {
                                    if ((frgd_sel != 2) && (bkgd_sel != 2)) {
                                        if (mach->accel.dp_config & 0x1000)
                                            mach_accel_start(mach->accel.cmd_type, 1, 16, mach->accel.pix_trans[1] | (mach->accel.pix_trans[0] << 8), 0, svga, mach, dev);
                                        else
                                            mach_accel_start(mach->accel.cmd_type, 1, 16, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), 0, svga, mach, dev);
                                    } else
                                        mach_accel_start(mach->accel.cmd_type, 1, 2, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), svga, mach, dev);
                                } else
                                    mach_accel_start(mach->accel.cmd_type, 1, 2, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), svga, mach, dev);
                                break;

                            default:
                                break;
                        }
                    }
                }
            }
            break;

        case 0x82ee:
            if (len == 2)
                temp = mach->accel.patt_data_idx;
            break;

        case 0x86ee:
        case 0x86ef:
            temp = 0x0000;
            break;

        case 0x8eee:
            if (len == 2)
                temp = mach->accel.ext_ge_config;
            else
                temp = mach->accel.ext_ge_config & 0xff;

            mach_log("ExtGE Read = %04x, len=%d.\n", temp, len);
            break;
        case 0x8eef:
            if (len == 1)
                temp = mach->accel.ext_ge_config >> 8;
            break;

        case 0x92ee:
            if (len == 2)
                temp = mach->accel.eeprom_control;
            else
                temp = mach->accel.eeprom_control & 0xff;

            mach_log("EEPROM cntl read=%04x, len=%d.\n", temp, len);
            break;
        case 0x92ef:
            if (len == 1)
                temp = mach->accel.eeprom_control >> 8;

            mach_log("EEPROM cntl read+1=%02x, len=%d.\n", temp, len);
            break;

        case 0x96ee:
            if (len == 2)
                temp = mach->accel.test;
            else
                temp = mach->accel.test & 0xff;
            break;
        case 0x96ef:
            if (len == 1)
                temp = mach->accel.test >> 8;
            break;

        case 0x9aee:
            if (len == 2) {
                if (dev->fifo_idx <= 16) {
                    for (int i = 1; i <= dev->fifo_idx; i++)
                        temp |= (1 << (15 - (i - 1)));
                } else
                    temp = 0xffff;

                if (dev->fifo_idx > 0)
                    dev->fifo_idx--;
            }
            break;

        case 0xa2ee:
            if (len == 2)
                temp = mach->accel.linedraw_opt;
            else
                temp = mach->accel.linedraw_opt & 0xff;
            break;
        case 0xa2ef:
            if (len == 1)
                temp = mach->accel.linedraw_opt >> 8;
            break;

        case 0xb2ee:
            if (len == 2) {
                temp = dev->hdisped & 0xff;
                temp |= (dev->htotal << 8);
            } else {
                temp = dev->hdisped;
            }
            mach_log("B2EE read=%02x.\n", temp & 0xff);
            break;
        case 0xb2ef:
            if (len == 1)
                temp = dev->htotal;
            break;

        case 0xb6ee:
            temp = dev->hsync_start;
            break;

        case 0xbaee:
            temp = dev->hsync_width;
            break;

        case 0xc2ee:
            if (len == 2)
                temp = dev->v_total_reg;
            else
                temp = dev->v_total_reg & 0xff;
            break;
        case 0xc2ef:
            if (len == 1)
                temp = dev->v_total_reg >> 8;
            break;

        case 0xc6ee:
            if (len == 2)
                temp = dev->v_disp;
            else
                temp = dev->v_disp & 0xff;
            break;
        case 0xc6ef:
            if (len == 1)
                temp = dev->v_disp >> 8;
            break;

        case 0xcaee:
            if (len == 2)
                temp = dev->v_sync_start;
            else
                temp = dev->v_sync_start & 0xff;
            break;
        case 0xcaef:
            if (len == 1)
                temp = dev->v_sync_start >> 8;
            break;

        case 0xceee:
            mach_log("CEEE read=%d.\n", len);
            if (len == 2)
                temp = dev->vc & 0x7ff;
            else
                temp = dev->vc & 0xff;
            break;
        case 0xceef:
            mach_log("CEEF read=%d.\n", len);
            if (len == 1)
                temp = (dev->vc >> 8) & 0x07;
            break;

        case 0xdaee:
            if (len == 2) {
                if (ATI_MACH32)
                    temp = mach->accel.src_x;
            } else {
                if (ATI_MACH32)
                    temp = mach->accel.src_x & 0xff;
            }
            break;
        case 0xdaef:
            if (len == 1) {
                if (ATI_MACH32)
                    temp = mach->accel.src_x >> 8;
            }
            break;

        case 0xdeee:
            if (len == 2) {
                if (ATI_MACH32)
                    temp = mach->accel.src_y;
            } else {
                if (ATI_MACH32)
                    temp = mach->accel.src_y & 0xff;
            }
            break;
        case 0xdeef:
            if (len == 1) {
                if (ATI_MACH32)
                    temp = mach->accel.src_y >> 8;
            }
            break;

        case 0xfaee:
            if (len == 2) {
                if (ATI_MACH32) {
                    if (mach->pci_bus)
                        temp = 0x0017;
                    else
                        temp = 0x22f7;
                }
            } else {
                if (ATI_MACH32) {
                    if (mach->pci_bus)
                        temp = 0x17;
                    else
                        temp = 0xf7;
                }
            }
            break;
        case 0xfaef:
            if (len == 1) {
                if (ATI_MACH32) {
                    if (mach->pci_bus)
                        temp = 0x00;
                    else
                        temp = 0x22;
                }
            }
            break;

        default:
            break;
    }

    mach_log("[%04X:%08X]: Port FIFO IN=%04x, temp=%04x, len=%d.\n", CS, cpu_state.pc, port, temp, len);

    return temp;
}

static uint8_t
mach_accel_in_call(uint16_t port, mach_t *mach, svga_t *svga, ibm8514_t *dev)
{
    uint8_t temp = 0;
    uint8_t fifo_test_tag[16] = { 0x7c, 0x64, 0x60, 0x5c, 0x58, 0x54, 0x50, 0x68, 0x38, 0x24, 0x10, 0x0c, 0x08, 0x04, 0x00, 0x4c};
    int16_t clip_t = dev->accel.clip_top;
    int16_t clip_l = dev->accel.clip_left;
    int16_t clip_b = dev->accel.clip_bottom;
    int16_t clip_r = dev->accel.clip_right;
    uint16_t clip_b_ibm = dev->accel.clip_bottom;
    uint16_t clip_r_ibm = dev->accel.clip_right;
    int cmd = dev->accel.cmd >> 13;

    switch (port) {
        case 0x2e8:
        case 0x2e9:
        case 0x6e8:
        case 0x22e8:
        case 0x26e8:
        case 0x26e9:
        case 0x2ee8:
        case 0x2ee9:
            temp = ibm8514_accel_in(port, svga);
            break;

        case 0x42e8:
        case 0x42e9:
            if (!(port & 1)) {
                if ((dev->subsys_cntl & INT_VSY) && !(dev->subsys_stat & INT_VSY) && (dev->vc == dev->dispend))
                    temp |= INT_VSY;

                if (mach->accel.cmd_type == -1) {
                    if (cmd == 6) {
                        if ((dev->subsys_cntl & INT_GE_BSY) &&
                            !(dev->subsys_stat & INT_GE_BSY) &&
                            (dev->accel.dx >= clip_l) &&
                            (dev->accel.dx <= clip_r_ibm) &&
                            (dev->accel.dy >= clip_t) &&
                            (dev->accel.dy <= clip_b_ibm))
                            temp |= INT_GE_BSY;
                    } else {
                        if ((dev->subsys_cntl & INT_GE_BSY) &&
                            !(dev->subsys_stat & INT_GE_BSY) &&
                            (dev->accel.cx >= clip_l) &&
                            (dev->accel.cx <= clip_r_ibm) &&
                            (dev->accel.cy >= clip_t) &&
                            (dev->accel.cy <= clip_b_ibm))
                            temp |= INT_GE_BSY;
                    }
                } else {
                    switch (mach->accel.cmd_type) {
                        case 2:
                        case 5:
                            if ((dev->subsys_cntl & INT_GE_BSY) &&
                                !(dev->subsys_stat & INT_GE_BSY) &&
                                (dev->accel.dx >= clip_l) &&
                                (dev->accel.dx <= clip_r) &&
                                (dev->accel.dy >= clip_t) &&
                                (dev->accel.dy <= clip_b))
                                temp |= INT_GE_BSY;
                            break;
                        case 1:
                        case 3:
                        case 4:
                            if ((dev->subsys_cntl & INT_GE_BSY) &&
                                !(dev->subsys_stat & INT_GE_BSY) &&
                                (dev->accel.cx >= clip_l) &&
                                (dev->accel.cx <= clip_r) &&
                                (dev->accel.cy >= clip_t) &&
                                (dev->accel.cy <= clip_b))
                                temp |= INT_GE_BSY;
                            break;
                        default:
                            break;
                    }
                }

                if (!dev->fifo_idx && !dev->on) {
                    dev->force_busy = 0;
                    dev->force_busy2 = 0;
                    mach->force_busy = 0;
                    dev->data_available = 0;
                    dev->data_available2 = 0;
                    temp |= INT_FIFO_EMP;
                    mach_log("Fifo Empty.\n");
                }
                temp |= (dev->subsys_stat | (dev->vram_512k_8514 ? 0x00 : 0x80));
                if (mach->accel.ext_ge_config & 0x08)
                    temp |= ((mach->accel.ext_ge_config & 0x07) << 4);
                else
                    temp |= 0x20;

                mach_log("0x%04x read: Subsystem Status=%02x, monitoralias=%02x.\n", port, temp, mach->accel.ext_ge_config & 0x07);
            }
            break;

            /*ATI Mach8/32 specific registers*/
        case 0x12ee:
        case 0x12ef:
            READ8(port, mach->config1);
            break;

        case 0x16ee:
        case 0x16ef:
            READ8(port, mach->config2);
            break;

        case 0x1aee:
            if (dev->fifo_idx > 0)
                dev->fifo_idx--;
            if (mach->fifo_test_idx > 0)
                mach->fifo_test_idx--;
            fallthrough;
        case 0x1aef:
            mach_log("FIFO Test IDX=%d, Data=%04x.\n", mach->fifo_test_idx, mach->fifo_test_data[mach->fifo_test_idx]);
            READ8(port, mach->fifo_test_data[mach->fifo_test_idx]);
            if (!mach->fifo_test_idx && ((mach->accel.dp_config == 0xaaaa) || (mach->accel.dp_config == 0x5555)))
                mach->accel.dp_config = 0x2011;
            break;

        case 0x22ee:
            if (mach->pci_bus)
                temp = mach->pci_cntl_reg;
            break;

        case 0x32ee:
        case 0x32ef:
            READ8(port, mach->local_cntl);
            break;

        case 0x36ee:
        case 0x36ef:
            if (ATI_MACH32) {
                READ8(port, mach->misc);
                if (!(port & 1)) {
                    temp &= ~0x0c;
                    switch (dev->vram_amount) {
                        case 1024:
                            temp |= 0x04;
                            break;
                        case 2048:
                            temp |= 0x08;
                            break;
                        case 4096:
                            temp |= 0x0c;
                            break;

                        default:
                            break;
                    }
                }
            }
            break;

        case 0x3aee:
        case 0x3aef:
            if (port & 1)
                temp = 0x01;
            else
                temp = fifo_test_tag[dev->fifo_idx];
            break;

        case 0x42ee:
        case 0x42ef:
            READ8(port, mach->accel.test2);
            break;

        case 0x46ee:
        case 0x46ef:
            READ8(port, mach->shadow_cntl);
            break;

        case 0x4aee:
        case 0x4aef:
            READ8(port, mach->accel.clock_sel);
            break;

        case 0x52ee:
        case 0x52ef:
            READ8(port, mach->accel.scratch0);
            mach_log("ScratchPad0=%x.\n", mach->accel.scratch0);
            if (mach->accel.scratch0 == 0x1234)
                temp = 0x0000;
            break;

        case 0x56ee:
        case 0x56ef:
            READ8(port, mach->accel.scratch1);
            mach_log("ScratchPad1=%x.\n", mach->accel.scratch1);
            break;

        case 0x5eee:
        case 0x5eef:
            if (mach->pci_bus)
                mach->memory_aperture = (mach->memory_aperture & ~0xfff0) | ((mach->linear_base >> 20) << 4);

            READ8(port, mach->memory_aperture);
            break;

        case 0x62ee:
            temp = mach->accel.clip_overrun;
            mach_log("ClipOverrun = %02x.\n", temp);
            break;
        case 0x62ef:
            if (mach->force_busy)
                temp |= 0x20;

            mach->force_busy = 0;

            if (ati_eeprom_read(&mach->eeprom))
                temp |= 0x40;

            mach_log("Mach busy temp=%02x.\n", temp);
            break;

        case 0x6aee:
        case 0x6aef:
            READ8(port, mach->accel.max_waitstates);
            break;

        case 0x72ee:
        case 0x72ef:
            READ8(port, (mach->accel.bleft));
            break;

        case 0x76ee:
        case 0x76ef:
            READ8(port, (mach->accel.btop));
            break;

        case 0x7aee:
        case 0x7aef:
            READ8(port, (mach->accel.bright));
            break;

        case 0x7eee:
        case 0x7eef:
            READ8(port, (mach->accel.bbottom));
            break;

        default:
            break;
    }
    mach_log("[%04X:%08X]: Port NORMAL IN=%04x, temp=%04x.\n", CS, cpu_state.pc, port, temp);

    return temp;
}

static void
ati8514_accel_outb(uint16_t port, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *)priv;
    mach_t *mach = (mach_t *)svga->ext8514;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if (port & 0x8000) { /*Command FIFO*/
        if (dev->accel.cmd_back) {
            mach->fifo_test_data[dev->fifo_idx] = val;
            dev->fifo_idx++;
            if (dev->fifo_idx > 16)
                dev->fifo_idx = 16;

            mach->fifo_test_idx = dev->fifo_idx;
        }
    }
    dev->accel_out_fifo(svga, port, val, 1);
    mach_log("%04X:%08X: OUTB port=%04x, val=%02x, fifo idx=%d.\n", CS, cpu_state.pc, port, val, dev->fifo_idx);
}

static void
ati8514_accel_outw(uint16_t port, uint16_t val, void *priv)
{
    svga_t *svga = (svga_t *)priv;
    mach_t *mach = (mach_t *)svga->ext8514;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if (port == 0xf6ee)
        port = 0x82e8;

    if (port & 0x8000) { /*Command FIFO*/
        if (dev->accel.cmd_back) {
            mach->fifo_test_data[dev->fifo_idx] = val;
            dev->fifo_idx++;
            if (dev->fifo_idx > 16)
                dev->fifo_idx = 16;

            mach->fifo_test_idx = dev->fifo_idx;
        }
    }
    dev->accel_out_fifo(svga, port, val, 2);
    mach_log("%04X:%08X: OUTW port=%04x, val=%04x, fifo idx=%d.\n", CS, cpu_state.pc, port, val, dev->fifo_idx);
}

static void
ati8514_accel_outl(uint16_t port, uint32_t val, void *priv)
{
    svga_t *svga = (svga_t *)priv;
    mach_t *mach = (mach_t *)svga->ext8514;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if (port == 0xf6ee)
        port = 0x82e8;

    if (port & 0x8000) { /*Command FIFO*/
        if (dev->accel.cmd_back) {
            mach->fifo_test_data[dev->fifo_idx] = val;
            dev->fifo_idx++;
            if (dev->fifo_idx > 16)
                dev->fifo_idx = 16;

            mach->fifo_test_idx = dev->fifo_idx;
        }
    }
    dev->accel_out_fifo(svga, port, val, 2);
    mach_log("OUTL port=%04x, val=%08x, fifo idx=%d.\n", port, val, dev->fifo_idx);
}

static void
mach_accel_outb(uint16_t port, uint8_t val, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if (port & 0x8000) { /*Command FIFO*/
        if (dev->accel.cmd_back) {
            mach->fifo_test_data[dev->fifo_idx] = val;
            dev->fifo_idx++;
            if (dev->fifo_idx > 16)
                dev->fifo_idx = 16;

            mach->fifo_test_idx = dev->fifo_idx;
        }
    }
    dev->accel_out_fifo(mach, port, val, 1);
    mach_log("%04X:%08X: OUTB port=%04x, val=%02x, fifo idx=%d.\n", CS, cpu_state.pc, port, val, dev->fifo_idx);
}

static void
mach_accel_outw(uint16_t port, uint16_t val, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if (port == 0xf6ee)
        port = 0x82e8;

    if (port & 0x8000) { /*Command FIFO*/
        if (dev->accel.cmd_back) {
            mach->fifo_test_data[dev->fifo_idx] = val;
            dev->fifo_idx++;
            if (dev->fifo_idx > 16)
                dev->fifo_idx = 16;

            mach->fifo_test_idx = dev->fifo_idx;
        }
    }
    dev->accel_out_fifo(mach, port, val, 2);
    mach_log("%04X:%08X: OUTW port=%04x, val=%04x, fifo idx=%d.\n", CS, cpu_state.pc, port, val, dev->fifo_idx);
}

static void
mach_accel_outl(uint16_t port, uint32_t val, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if (port == 0xf6ee)
        port = 0x82e8;

    if (port & 0x8000) { /*Command FIFO*/
        if (dev->accel.cmd_back) {
            mach->fifo_test_data[dev->fifo_idx] = val;
            dev->fifo_idx++;
            if (dev->fifo_idx > 16)
                dev->fifo_idx = 16;

            mach->fifo_test_idx = dev->fifo_idx;
        }
    }
    dev->accel_out_fifo(mach, port, val, 2);
    mach_log("OUTL port=%04x, val=%08x, fifo idx=%d.\n", port, val, dev->fifo_idx);
}

static uint8_t
ati8514_accel_in(uint16_t port, svga_t *svga)
{
    return mach_accel_in_call(port, (mach_t *) svga->ext8514, svga, (ibm8514_t *) svga->dev8514);
}

static uint8_t
ati8514_accel_inb(uint16_t port, void *priv)
{
    svga_t *svga = (svga_t *)priv;
    mach_t *mach = (mach_t *)svga->ext8514;
    uint8_t temp;

    if (port & 0x8000)
        temp = mach_accel_in_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, 1);
    else
        temp = ati8514_accel_in(port, svga);

    mach_log("%04X:%08X: INB port=%04x, temp=%02x.\n", CS, cpu_state.pc, port, temp);
    return temp;
}

static uint16_t
ati8514_accel_inw(uint16_t port, void *priv)
{
    svga_t *svga = (svga_t *)priv;
    mach_t *mach = (mach_t *)svga->ext8514;
    uint16_t temp;

    if (port & 0x8000)
        temp = mach_accel_in_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, 2);
    else {
        temp = ati8514_accel_in(port, svga);
        temp |= (ati8514_accel_in(port + 1, svga) << 8);
    }

    mach_log("%04X:%08X: INW port=%04x, temp=%04x.\n", CS, cpu_state.pc, port, temp);
    return temp;
}

static uint32_t
ati8514_accel_inl(uint16_t port, void *priv)
{
    svga_t *svga = (svga_t *)priv;
    mach_t *mach = (mach_t *)svga->ext8514;
    uint32_t temp;

    if (port & 0x8000)
        temp = mach_accel_in_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, 2);
    else {
        temp = ati8514_accel_in(port, svga);
        temp |= (ati8514_accel_in(port + 1, svga) << 8);
    }
    return temp;
}

static uint8_t
mach_accel_in(uint16_t port, mach_t *mach)
{
    svga_t *svga = &mach->svga;
    return mach_accel_in_call(port, mach, svga, (ibm8514_t *) svga->dev8514);
}

static uint8_t
mach_accel_inb(uint16_t port, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    uint8_t temp;

    if (port & 0x8000)
        temp = mach_accel_in_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, 1);
    else
        temp = mach_accel_in(port, mach);

    mach_log("%04X:%08X: INB port=%04x, temp=%02x.\n", CS, cpu_state.pc, port, temp);
    return temp;
}

static uint16_t
mach_accel_inw(uint16_t port, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    uint16_t temp;

    if (port & 0x8000)
        temp = mach_accel_in_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, 2);
    else {
        temp = mach_accel_in(port, mach);
        temp |= (mach_accel_in(port + 1, mach) << 8);
    }

    mach_log("%04X:%08X: INW port=%04x, temp=%04x.\n", CS, cpu_state.pc, port, temp);
    return temp;
}

static uint32_t
mach_accel_inl(uint16_t port, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    uint32_t temp;

    if (port & 0x8000)
        temp = mach_accel_in_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, 2);
    else {
        temp = mach_accel_in(port, mach);
        temp |= (mach_accel_in(port + 1, mach) << 8);
    }
    return temp;
}

static __inline void
mach32_write_common(uint32_t addr, uint8_t val, int linear, mach_t *mach, svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    int     writemask2 = svga->writemask;
    int     reset_wm   = 0;
    latch8514_t vall;
    uint8_t wm = svga->writemask;
    uint8_t i;

    cycles -= svga->monitor->mon_video_timing_write_b;

    if (linear) {
        if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00))
            addr <<= 1;

        addr &= dev->vram_mask;
        dev->changedvram[addr >> 12] = svga->monitor->mon_changeframecount;
        if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
            switch (addr & 0x06) {
                case 0x00:
                case 0x06:
                    dev->vram[addr] = val & 0x0f;
                    dev->vram[addr + 1] = (val >> 4) & 0x0f;
                    break;
                case 0x02:
                    dev->vram[addr + 2] = val & 0x0f;
                    dev->vram[addr + 3] = (val >> 4) & 0x0f;
                    break;
                case 0x04:
                    dev->vram[addr - 2] = val & 0x0f;
                    dev->vram[addr - 1] = (val >> 4) & 0x0f;
                    break;
                default:
                    break;
            }
        } else
            dev->vram[addr] = val;

        return;
    }

    if (!(svga->gdcreg[6] & 1))
        svga->fullchange = 2;

    if (svga->chain4) {
        writemask2 = 1 << (addr & 3);
        addr &= ~3;
    } else if (svga->chain2_write) {
        writemask2 &= ~0xa;
        if (addr & 1)
            writemask2 <<= 1;

        addr &= ~1;
        addr &= dev->vram_mask;
    } else {
        writemask2 = 1 << (addr & 3);
        addr &= ~3;
        addr &= dev->vram_mask;
    }
    addr &= svga->decode_mask;

    if (addr >= dev->vram_size) {
        mach_log("WriteOver! %x.\n", addr);
        return;
    }

    addr &= dev->vram_mask;
    dev->changedvram[addr >> 12] = svga->monitor->mon_changeframecount;

    switch (svga->writemode) {
        case 0:
            val = ((val >> (svga->gdcreg[3] & 7)) | (val << (8 - (svga->gdcreg[3] & 7))));
            if ((svga->gdcreg[8] == 0xff) && !(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || svga->set_reset_disabled)) {
                for (i = 0; i < 4; i++) {
                    if (writemask2 & (1 << i))
                        dev->vram[addr | i] = val;
                }
                return;
            } else {
                for (i = 0; i < 4; i++) {
                    if (svga->gdcreg[1] & (1 << i))
                        vall.b[i] = !!(svga->gdcreg[0] & (1 << i)) * 0xff;
                    else
                        vall.b[i] = val;
                }
            }
            break;
        case 1:
            for (i = 0; i < 4; i++) {
                if (writemask2 & (1 << i))
                    dev->vram[addr | i] = dev->latch.b[i];
            }
            return;
        case 2:
            for (i = 0; i < 4; i++)
                vall.b[i] = !!(val & (1 << i)) * 0xff;

            if (!(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || svga->set_reset_disabled)) {
                for (i = 0; i < 4; i++) {
                    if (writemask2 & (1 << i))
                        dev->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (dev->latch.b[i] & ~svga->gdcreg[8]);
                }
                return;
            }
            break;
        case 3:
            val = ((val >> (svga->gdcreg[3] & 7)) | (val << (8 - (svga->gdcreg[3] & 7))));
            wm  = svga->gdcreg[8];
            svga->gdcreg[8] &= val;

            for (i = 0; i < 4; i++)
                vall.b[i] = !!(svga->gdcreg[0] & (1 << i)) * 0xff;

            reset_wm = 1;
            break;
        default:
            break;
    }

    switch (svga->gdcreg[3] & 0x18) {
        case 0x00: /* Set */
            for (i = 0; i < 4; i++) {
                if (writemask2 & (1 << i))
                    dev->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (dev->latch.b[i] & ~svga->gdcreg[8]);
            }
            break;
        case 0x08: /* AND */
            for (i = 0; i < 4; i++) {
                if (writemask2 & (1 << i))
                    dev->vram[addr | i] = (vall.b[i] | ~svga->gdcreg[8]) & dev->latch.b[i];
            }
            break;
        case 0x10: /* OR */
            for (i = 0; i < 4; i++) {
                if (writemask2 & (1 << i))
                    dev->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | dev->latch.b[i];
            }
            break;
        case 0x18: /* XOR */
            for (i = 0; i < 4; i++) {
                if (writemask2 & (1 << i))
                    dev->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) ^ dev->latch.b[i];
            }
            break;

        default:
            break;
    }

    if (reset_wm)
        svga->gdcreg[8] = wm;
}

static void
mach32_write(uint32_t addr, uint8_t val, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    xga_write_test(addr, val, svga);
    addr = (addr & svga->banked_mask) + svga->write_bank;

    if (mach->accel.test2 & 0x10) {
        if (addr < ((mach->accel.test2 & 0x0f) << 18))
            return;
    }

    if ((ATI_MACH32 && !dev->vram_512k_8514) && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
        addr <<= 1;
        switch (addr & 0x06) {
            case 0x00:
            case 0x06:
                mach32_write_common(addr, val & 0x0f, 0, mach, svga);
                mach32_write_common(addr + 1, (val >> 4) & 0x0f, 0, mach, svga);
                break;
            case 0x02:
                mach32_write_common(addr + 2, val & 0x0f, 0, mach, svga);
                mach32_write_common(addr + 3, (val >> 4) & 0x0f, 0, mach, svga);
                break;
            case 0x04:
                mach32_write_common(addr - 2, val & 0x0f, 0, mach, svga);
                mach32_write_common(addr - 1, (val >> 4) & 0x0f, 0, mach, svga);
                break;
            default:
                break;
        }
    } else
        mach32_write_common(addr, val, 0, mach, svga);

    mach_log("Writeb banked=%08x.\n", addr);
}

static void
mach32_writew(uint32_t addr, uint16_t val, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    xga_write_test(addr, val, svga);
    addr = (addr & svga->banked_mask) + svga->write_bank;

    if (mach->accel.test2 & 0x10) {
        if (addr < ((mach->accel.test2 & 0x0f) << 18))
            return;
    }

    if ((ATI_MACH32 && !dev->vram_512k_8514) && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
        addr <<= 1;
        if (addr & 0x04) {
            mach32_write_common(addr - 2, val & 0x0f, 0, mach, svga);
            mach32_write_common(addr - 1, (val >> 4) & 0x0f, 0, mach, svga);
            mach32_write_common(addr + 2, (val >> 8) & 0x0f, 0, mach, svga);
            mach32_write_common(addr + 3, (val >> 12) & 0x0f, 0, mach, svga);
        } else {
            mach32_write_common(addr, val & 0x0f, 0, mach, svga);
            mach32_write_common(addr + 1, (val >> 4) & 0x0f, 0, mach, svga);
            mach32_write_common(addr + 4, (val >> 8) & 0x0f, 0, mach, svga);
            mach32_write_common(addr + 5, (val >> 12) & 0x0f, 0, mach, svga);
        }
    } else {
        mach32_write_common(addr, val & 0xff, 0, mach, svga);
        mach32_write_common(addr + 1, val >> 8, 0, mach, svga);
    }
    mach_log("Writew banked=%08x.\n", addr);
}

static void
mach32_writel(uint32_t addr, uint32_t val, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    xga_write_test(addr, val, svga);
    addr = (addr & svga->banked_mask) + svga->write_bank;

    if (mach->accel.test2 & 0x10) {
        if (addr < ((mach->accel.test2 & 0x0f) << 18))
            return;
    }

    if ((ATI_MACH32 && !dev->vram_512k_8514) && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
        addr <<= 1;
        mach32_write_common(addr, val & 0x0f, 0, mach, svga);
        mach32_write_common(addr + 1, (val >> 4) & 0x0f, 0, mach, svga);
        mach32_write_common(addr + 4, (val >> 8) & 0x0f, 0, mach, svga);
        mach32_write_common(addr + 5, (val >> 12) & 0x0f, 0, mach, svga);
        mach32_write_common(addr + 2, (val >> 16) & 0x0f, 0, mach, svga);
        mach32_write_common(addr + 3, (val >> 20) & 0x0f, 0, mach, svga);
        mach32_write_common(addr + 6, (val >> 24) & 0x0f, 0, mach, svga);
        mach32_write_common(addr + 7, (val >> 28) & 0x0f, 0, mach, svga);
    } else {
        mach32_write_common(addr, val & 0xff, 0, mach, svga);
        mach32_write_common(addr + 1, val >> 8, 0, mach, svga);
        mach32_write_common(addr + 2, val >> 16, 0, mach, svga);
        mach32_write_common(addr + 3, val >> 24, 0, mach, svga);
    }

    mach_log("Writel banked=%08x.\n", addr);
}

static __inline void
mach32_svga_write(uint32_t addr, uint8_t val, void *priv)
{
    svga_t *svga       = (svga_t *) priv;
    mach_t *mach       = (mach_t *) svga->priv;
    int     writemask2 = svga->writemask;
    int     reset_wm   = 0;
    latch_t vall;
    uint8_t wm         = svga->writemask;
    uint8_t count;
    uint8_t i;

    cycles -= svga->monitor->mon_video_timing_write_b;

    xga_write_test(addr, val, svga);
    addr = svga_decode_addr(svga, addr, 1);

    if (addr == 0xffffffff) {
        mach_log("WriteCommon Over.\n");
        return;
    }

    if (mach->accel.test2 & 0x10) {
        if (addr >= ((mach->accel.test2 & 0x0f) << 18))
            return;
    }

    if (!(svga->gdcreg[6] & 1))
        svga->fullchange = 2;

    if (((svga->chain4 && (svga->packed_chain4 || svga->force_old_addr)) || svga->fb_only) && (svga->writemode < 4)) {
        writemask2 = 1 << (addr & 3);
        addr &= ~3;
    } else if (svga->chain4 && (svga->writemode < 4)) {
        writemask2 = 1 << (addr & 3);
        addr = ((addr & 0xfffc) << 2) | ((addr & 0x30000) >> 14) | (addr & ~0x3ffff);
    } else if (svga->chain2_write) {
        writemask2 &= ~0xa;
        if (addr & 1)
            writemask2 <<= 1;
        addr &= ~1;
        addr <<= 2;
    } else
        addr <<= 2;

    addr &= svga->decode_mask;

    if (addr >= svga->vram_max) {
        mach_log("WriteBankedOver=%08x, val=%02x.\n", addr & svga->vram_mask, val);
        return;
    }

    addr &= svga->vram_mask;
    svga->changedvram[addr >> 12] = svga->monitor->mon_changeframecount;

    count = 4;

    switch (svga->writemode) {
        case 0:
            val = ((val >> (svga->gdcreg[3] & 7)) | (val << (8 - (svga->gdcreg[3] & 7))));
            if ((svga->gdcreg[8] == 0xff) && !(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || svga->set_reset_disabled)) {
                for (i = 0; i < count; i++) {
                    if (writemask2 & (1 << i))
                        svga->vram[addr | i] = val;
                }
                return;
            } else {
                for (i = 0; i < count; i++) {
                    if (svga->gdcreg[1] & (1 << i))
                        vall.b[i] = !!(svga->gdcreg[0] & (1 << i)) * 0xff;
                    else
                        vall.b[i] = val;
                }
            }
            break;
        case 1:
            for (i = 0; i < count; i++) {
                if (writemask2 & (1 << i))
                    svga->vram[addr | i] = svga->latch.b[i];
            }
            return;
        case 2:
            for (i = 0; i < count; i++)
                vall.b[i] = !!(val & (1 << i)) * 0xff;

            if (!(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || svga->set_reset_disabled)) {
                for (i = 0; i < count; i++) {
                    if (writemask2 & (1 << i))
                        svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (svga->latch.b[i] & ~svga->gdcreg[8]);
                }
                return;
            }
            break;
        case 3:
            val = ((val >> (svga->gdcreg[3] & 7)) | (val << (8 - (svga->gdcreg[3] & 7))));
            wm  = svga->gdcreg[8];
            svga->gdcreg[8] &= val;

            for (i = 0; i < count; i++)
                vall.b[i] = !!(svga->gdcreg[0] & (1 << i)) * 0xff;

            reset_wm = 1;
            break;
        default:
            return;
    }

    switch (svga->gdcreg[3] & 0x18) {
        case 0x00: /* Set */
            for (i = 0; i < count; i++) {
                if (writemask2 & (1 << i))
                    svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (svga->latch.b[i] & ~svga->gdcreg[8]);
            }
            break;
        case 0x08: /* AND */
            for (i = 0; i < count; i++) {
                if (writemask2 & (1 << i))
                    svga->vram[addr | i] = (vall.b[i] | ~svga->gdcreg[8]) & svga->latch.b[i];
            }
            break;
        case 0x10: /* OR */
            for (i = 0; i < count; i++) {
                if (writemask2 & (1 << i))
                    svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | svga->latch.b[i];
            }
            break;
        case 0x18: /* XOR */
            for (i = 0; i < count; i++) {
                if (writemask2 & (1 << i))
                    svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) ^ svga->latch.b[i];
            }
            break;

        default:
            break;
    }

    if (reset_wm)
        svga->gdcreg[8] = wm;
}

static __inline void
mach32_svga_writew(uint32_t addr, uint16_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    mach_t *mach = (mach_t *) svga->priv;

    if (!svga->fast) {
        mach32_svga_write(addr, val, priv);
        mach32_svga_write(addr + 1, val >> 8, priv);
        return;
    }

    cycles -= svga->monitor->mon_video_timing_write_w;

    xga_write_test(addr, val & 0xff, svga);
    xga_write_test(addr + 1, val >> 8, svga);
    addr = svga_decode_addr(svga, addr, 1);

    if (addr == 0xffffffff)
        return;

    if (mach->accel.test2 & 0x10) {
        if (addr >= ((mach->accel.test2 & 0x0f) << 18))
            return;
    }

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return;
    addr &= svga->vram_mask;

    svga->changedvram[addr >> 12]   = svga->monitor->mon_changeframecount;
    *(uint16_t *) &svga->vram[addr] = val;
}

static __inline void
mach32_svga_writel(uint32_t addr, uint32_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    mach_t *mach = (mach_t *) svga->priv;

    if (!svga->fast) {
        mach32_svga_write(addr, val, priv);
        mach32_svga_write(addr + 1, val >> 8, priv);
        mach32_svga_write(addr + 2, val >> 16, priv);
        mach32_svga_write(addr + 3, val >> 24, priv);
        return;
    }

    cycles -= svga->monitor->mon_video_timing_write_l;

    xga_write_test(addr, val & 0xff, svga);
    xga_write_test(addr + 1, (val >> 8) & 0xff, svga);
    xga_write_test(addr + 2, (val >> 16) & 0xff, svga);
    xga_write_test(addr + 3, (val >> 24) & 0xff, svga);
    addr = svga_decode_addr(svga, addr, 1);

    if (addr == 0xffffffff)
        return;

    if (mach->accel.test2 & 0x10) {
        if (addr >= ((mach->accel.test2 & 0x0f) << 18))
            return;
    }

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return;

    addr &= svga->vram_mask;

    svga->changedvram[addr >> 12]   = svga->monitor->mon_changeframecount;
    *(uint32_t *) &svga->vram[addr] = val;
}

static __inline void
mach32_writew_linear(uint32_t addr, uint16_t val, mach_t *mach)
{
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    cycles -= svga->monitor->mon_video_timing_write_w;
    if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00))
        addr <<= 1;

    addr &= dev->vram_mask;
    dev->changedvram[addr >> 12] = svga->monitor->mon_changeframecount;
    if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
        if (addr & 0x04) {
            dev->vram[addr - 2] = val & 0x0f;
            dev->vram[addr - 1] = (val >> 4) & 0x0f;
            dev->vram[addr + 2] = (val >> 8) & 0x0f;
            dev->vram[addr + 3] = (val >> 12) & 0x0f;
        } else {
            dev->vram[addr] = val & 0x0f;
            dev->vram[addr + 1] = (val >> 4) & 0x0f;
            dev->vram[addr + 4] = (val >> 8) & 0x0f;
            dev->vram[addr + 5] = (val >> 12) & 0x0f;
        }
    } else
        *(uint16_t *) &dev->vram[addr] = val;
}

static __inline void
mach32_writel_linear(uint32_t addr, uint32_t val, mach_t *mach)
{
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    cycles -= svga->monitor->mon_video_timing_write_l;

    if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00))
        addr <<= 1;

    addr &= dev->vram_mask;
    dev->changedvram[addr >> 12] = svga->monitor->mon_changeframecount;
    if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
        dev->vram[addr] = val & 0x0f;
        dev->vram[addr + 1] = (val >> 4) & 0x0f;
        dev->vram[addr + 4] = (val >> 8) & 0x0f;
        dev->vram[addr + 5] = (val >> 12) & 0x0f;
        dev->vram[addr + 2] = (val >> 16) & 0x0f;
        dev->vram[addr + 3] = (val >> 20) & 0x0f;
        dev->vram[addr + 6] = (val >> 24) & 0x0f;
        dev->vram[addr + 7] = (val >> 28) & 0x0f;
    } else
        *(uint32_t *) &dev->vram[addr] = val;
}

static __inline uint8_t
mach32_read_common(uint32_t addr, int linear, mach_t *mach, svga_t *svga)
{
    ibm8514_t       *dev        = (ibm8514_t *) svga->dev8514;
    uint32_t         latch_addr = 0;
    int              readplane  = svga->readplane;
    uint8_t          count;
    uint8_t          temp;
    uint8_t          ret = 0x00;

    cycles -= svga->monitor->mon_video_timing_read_b;

    if (linear) {
        if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00))
            addr <<= 1;

        addr &= dev->vram_mask;
        if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
            switch ((addr & 0x06) >> 1) {
                case 0x00:
                case 0x03:
                    ret = dev->vram[addr] & 0x0f;
                    ret |= (dev->vram[addr + 1] << 4);
                    break;
                case 0x01:
                    ret = dev->vram[addr + 2] & 0x0f;
                    ret |= (dev->vram[addr + 3] << 4);
                    break;
                case 0x02:
                    ret = dev->vram[addr - 2] & 0x0f;
                    ret |= (dev->vram[addr - 1] << 4);
                    break;
                default:
                    break;
            }
        } else
            ret = dev->vram[addr];

        return ret;
    }

    count = 2;

    latch_addr = (addr << count) & svga->decode_mask;
    count      = (1 << count);

    if (svga->chain4) {
        addr &= svga->decode_mask;
        if (addr >= dev->vram_size) {
            mach_log("ReadOver! (chain4) %x.\n", addr);
            return 0xff;
        }
        latch_addr = (addr & dev->vram_mask) & ~3;
        for (uint8_t i = 0; i < count; i++)
            dev->latch.b[i] = dev->vram[latch_addr | i];
        return dev->vram[addr & dev->vram_mask];
    } else if (svga->chain2_read) {
        readplane = (readplane & 2) | (addr & 1);
        addr &= ~1;
        addr &= dev->vram_mask;
    } else {
        addr &= svga->decode_mask;
        if (addr >= dev->vram_size) {
            mach_log("ReadOver! (normal) %x.\n", addr);
            return 0xff;
        }
        latch_addr = (addr & dev->vram_mask) & ~3;
        for (uint8_t i = 0; i < count; i++)
            dev->latch.b[i] = dev->vram[latch_addr | i];

        mach_log("Read (normal) addr=%06x, ret=%02x.\n", addr, dev->vram[addr & dev->vram_mask]);
        return dev->vram[addr & dev->vram_mask];
    }

    addr &= svga->decode_mask;

    /* standard VGA latched access */
    if (latch_addr >= dev->vram_size) {
        mach_log("Over VRAM Latch addr=%x.\n", latch_addr);
        for (uint8_t i = 0; i < count; i++)
            dev->latch.b[i] = 0xff;
    } else {
        latch_addr &= dev->vram_mask;

        for (uint8_t i = 0; i < count; i++)
            dev->latch.b[i] = dev->vram[latch_addr | i];
    }

    if (addr >= dev->vram_size) {
        mach_log("ReadOver! (chain2) %x.\n", addr);
        return 0xff;
    }

    addr &= dev->vram_mask;

    if (svga->readmode) {
        temp = 0xff;

        for (uint8_t pixel = 0; pixel < 8; pixel++) {
            for (uint8_t plane = 0; plane < count; plane++) {
                if (svga->colournocare & (1 << plane)) {
                    /* If we care about a plane, and the pixel has a mismatch on it, clear its bit. */
                    if (((dev->latch.b[plane] >> pixel) & 1) != ((svga->colourcompare >> plane) & 1))
                        temp &= ~(1 << pixel);
                }
            }
        }

        ret = temp;
    } else
        ret = dev->vram[addr | readplane];

    mach_log("ReadMode=%02x, addr=%06x, ret=%02x.\n", svga->readmode, addr, ret);
    return ret;
}

static uint8_t
mach32_read(uint32_t addr, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    uint8_t ret = 0x00;

    (void) xga_read_test(addr, svga);
    addr = (addr & svga->banked_mask) + svga->read_bank;

    if ((ATI_MACH32 && !dev->vram_512k_8514) && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
        addr <<= 1;
        switch ((addr & 0x06) >> 1) {
            case 0x00:
            case 0x03:
                ret = mach32_read_common(addr, 0, mach, svga) & 0x0f;
                ret |= (mach32_read_common(addr + 1, 0, mach, svga) << 4);
                break;
            case 0x01:
                ret = mach32_read_common(addr + 2, 0, mach, svga) & 0x0f;
                ret |= (mach32_read_common(addr + 3, 0, mach, svga) << 4);
                break;
            case 0x02:
                ret = mach32_read_common(addr - 2, 0, mach, svga) & 0x0f;
                ret |= (mach32_read_common(addr - 1, 0, mach, svga) << 4);
                break;
            default:
                break;
        }
    } else
        ret = mach32_read_common(addr, 0, mach, svga);

    mach_log("Readb banked=%08x.\n", addr);
    return ret;
}

static uint16_t
mach32_readw(uint32_t addr, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    uint16_t ret;

    (void) xga_read_test(addr, svga);
    addr = (addr & svga->banked_mask) + svga->read_bank;

    if ((ATI_MACH32 && !dev->vram_512k_8514) && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
        addr <<= 1;
        if (addr & 0x04) {
            ret = mach32_read_common(addr - 2, 0, mach, svga) & 0x0f;
            ret |= (mach32_read_common(addr - 1, 0, mach, svga) << 4);
            ret |= (mach32_read_common(addr + 2, 0, mach, svga) << 8);
            ret |= (mach32_read_common(addr + 3, 0, mach, svga) << 12);
        } else {
            ret = mach32_read_common(addr, 0, mach, svga) & 0x0f;
            ret |= (mach32_read_common(addr + 1, 0, mach, svga) << 4);
            ret |= (mach32_read_common(addr + 4, 0, mach, svga) << 8);
            ret |= (mach32_read_common(addr + 5, 0, mach, svga) << 12);
        }
    } else {
        ret = mach32_read_common(addr, 0, mach, svga);
        ret |= (mach32_read_common(addr + 1, 0, mach, svga) << 8);
    }
    mach_log("Readw banked=%08x.\n", addr);
    return ret;
}

static uint32_t
mach32_readl(uint32_t addr, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    uint32_t ret;

    (void) xga_read_test(addr, svga);
    addr = (addr & svga->banked_mask) + svga->read_bank;

    if ((ATI_MACH32 && !dev->vram_512k_8514) && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
        addr <<= 1;
        ret = mach32_read_common(addr, 0, mach, svga) & 0x0f;
        ret |= (mach32_read_common(addr + 1, 0, mach, svga) << 4);
        ret |= (mach32_read_common(addr + 4, 0, mach, svga) << 8);
        ret |= (mach32_read_common(addr + 5, 0, mach, svga) << 12);
        ret |= (mach32_read_common(addr + 2, 0, mach, svga) << 16);
        ret |= (mach32_read_common(addr + 3, 0, mach, svga) << 20);
        ret |= (mach32_read_common(addr + 6, 0, mach, svga) << 24);
        ret |= (mach32_read_common(addr + 7, 0, mach, svga) << 28);
    } else {
        ret = mach32_read_common(addr, 0, mach, svga);
        ret |= (mach32_read_common(addr + 1, 0, mach, svga) << 8);
        ret |= (mach32_read_common(addr + 2, 0, mach, svga) << 16);
        ret |= (mach32_read_common(addr + 3, 0, mach, svga) << 24);
    }
    mach_log("Readl banked=%08x.\n", addr);
    return ret;
}

static __inline uint16_t
mach32_readw_linear(uint32_t addr, mach_t *mach)
{
    svga_t          *svga       = &mach->svga;
    ibm8514_t       *dev        = (ibm8514_t *) svga->dev8514;
    uint16_t         ret;

    cycles -= svga->monitor->mon_video_timing_read_w;
    if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
        addr <<= 1;
        addr &= dev->vram_mask;
        if (addr & 0x04) {
            ret = dev->vram[addr - 2] & 0x0f;
            ret |= (dev->vram[addr - 1] << 4);
            ret |= (dev->vram[addr + 2] << 8);
            ret |= (dev->vram[addr + 3] << 12);
        } else {
            ret = dev->vram[addr] & 0x0f;
            ret |= (dev->vram[addr + 1] << 4);
            ret |= (dev->vram[addr + 4] << 8);
            ret |= (dev->vram[addr + 5] << 12);
        }
    } else
        ret = *(uint16_t *) &dev->vram[addr & dev->vram_mask];

    return ret;
}

static __inline uint32_t
mach32_readl_linear(uint32_t addr, mach_t *mach)
{
    svga_t          *svga       = &mach->svga;
    ibm8514_t       *dev        = (ibm8514_t *) svga->dev8514;
    uint32_t         ret;

    cycles -= svga->monitor->mon_video_timing_read_l;
    if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00)) {
        addr <<= 1;
        addr &= dev->vram_mask;
        ret = dev->vram[addr] & 0x0f;
        ret |= (dev->vram[addr + 1] << 4);
        ret |= (dev->vram[addr + 4] << 8);
        ret |= (dev->vram[addr + 5] << 12);
        ret |= (dev->vram[addr + 2] << 16);
        ret |= (dev->vram[addr + 3] << 20);
        ret |= (dev->vram[addr + 6] << 24);
        ret |= (dev->vram[addr + 7] << 28);
    } else
        ret = *(uint32_t *) &dev->vram[addr & dev->vram_mask];

    return ret;
}

static void
mach32_ap_writeb(uint32_t addr, uint8_t val, void *priv)
{
    mach_t          *mach  = (mach_t *) priv;
    svga_t          *svga  = &mach->svga;
    const ibm8514_t *dev   = (ibm8514_t *) svga->dev8514;
    uint8_t port_dword     = addr & 0xfc;
    uint16_t actual_port   = 0x02e8 + (addr & 1) + (port_dword << 8);
    uint16_t actual_port_ext = 0x02ee + (addr & 1) + (port_dword << 8);

    if (((mach->local_cntl & 0x20) || (mach->pci_cntl_reg & 0x80)) &&
        (((addr - mach->linear_base) >= ((mach->ap_size << 20) - 0x200)) && ((addr - mach->linear_base) < (mach->ap_size << 20))) &&
        (svga->mapping.base == 0xa0000)) {
        if (addr & 0x100) {
            mach_log("Port WORDB Write=%04x.\n", actual_port_ext);
            mach_accel_outb(actual_port_ext, val, mach);
        } else {
            mach_log("Port WORDB Write=%04x.\n", actual_port);
            mach_accel_outb(actual_port, val, mach);
        }
    } else {
        mach_log("Linear WORDB Write=%08x, val=%02x, ON=%x, dpconfig=%04x, apsize=%08x, addr=%08x.\n",
            addr - mach->linear_base, val, dev->on, mach->accel.dp_config, mach->ap_size << 20, addr);

        if (dev->on)
            mach32_write_common(addr, val, 1, mach, svga);
        else
            svga_write_linear(addr, val, svga);
    }
}

static void
mach32_ap_writew(uint32_t addr, uint16_t val, void *priv)
{
    mach_t          *mach  = (mach_t *) priv;
    svga_t          *svga  = &mach->svga;
    const ibm8514_t *dev   = (ibm8514_t *) svga->dev8514;
    uint8_t port_dword     = addr & 0xfc;
    uint16_t actual_port   = 0x02e8 + (port_dword << 8);
    uint16_t actual_port_ext = 0x02ee + (port_dword << 8);

    if (((mach->local_cntl & 0x20) || (mach->pci_cntl_reg & 0x80)) &&
        (((addr - mach->linear_base) >= ((mach->ap_size << 20) - 0x200)) && ((addr - mach->linear_base) < (mach->ap_size << 20))) &&
        (svga->mapping.base == 0xa0000)) {
        if (addr & 0x100) {
            mach_log("Port WORDW Write=%04x, localcntl=%02x, pcicntl=%02x, actual addr=%08x, val=%04x.\n", actual_port_ext, mach->local_cntl & 0x20, mach->pci_cntl_reg & 0x80, addr, val);
            mach_accel_outw(actual_port_ext, val, mach);
        } else {
            mach_log("Port WORDW Write=%04x, localcntl=%02x, pcicntl=%02x, actual addr=%08x, val=%04x.\n", actual_port, mach->local_cntl & 0x20, mach->pci_cntl_reg & 0x80, addr, val);
            mach_accel_outw(actual_port, val, mach);
        }
    } else {
        mach_log("Linear WORDW Write=%08x, val=%04x, ON=%x, dpconfig=%04x, apsize=%08x, addr=%08x.\n",
            addr - mach->linear_base, val, dev->on, mach->accel.dp_config, mach->ap_size << 20, addr);

        if (dev->on)
            mach32_writew_linear(addr, val, mach);
        else
            svga_writew_linear(addr, val, svga);
    }
}

static void
mach32_ap_writel(uint32_t addr, uint32_t val, void *priv)
{
    mach_t          *mach  = (mach_t *) priv;
    svga_t          *svga  = &mach->svga;
    const ibm8514_t *dev   = (ibm8514_t *) svga->dev8514;
    uint8_t port_dword     = addr & 0xfc;
    uint16_t actual_port   = 0x02e8 + (port_dword << 8);
    uint16_t actual_port_ext = 0x02ee + (port_dword << 8);

    if (((mach->local_cntl & 0x20) || (mach->pci_cntl_reg & 0x80)) &&
        (((addr - mach->linear_base) >= ((mach->ap_size << 20) - 0x200)) && ((addr - mach->linear_base) < (mach->ap_size << 20))) &&
        (svga->mapping.base == 0xa0000)) {
        if (addr & 0x100) {
            mach_log("Port WORDL Write=%04x, localcntl=%02x, pcicntl=%02x.\n", actual_port_ext, mach->local_cntl & 0x20, mach->pci_cntl_reg & 0x80);
            mach_accel_outl(actual_port_ext, val, mach);
        } else {
            mach_log("Port WORDL Write=%04x, localcntl=%02x, pcicntl=%02x.\n", actual_port, mach->local_cntl & 0x20, mach->pci_cntl_reg & 0x80);
            mach_accel_outl(actual_port, val, mach);
        }
    } else {
        mach_log("Linear WORDL Write=%08x, val=%08x, ON=%x, dpconfig=%04x, apsize=%08x, addr=%08x.\n",
            addr - mach->linear_base, val, dev->on, mach->accel.dp_config, mach->ap_size << 20, addr);

        if (dev->on)
            mach32_writel_linear(addr, val, mach);
        else
            svga_writel_linear(addr, val, svga);
    }
}

static uint8_t
mach32_ap_readb(uint32_t addr, void *priv)
{
    mach_t          *mach  = (mach_t *) priv;
    svga_t          *svga  = &mach->svga;
    const ibm8514_t *dev   = (ibm8514_t *) svga->dev8514;
    uint8_t temp;
    uint8_t port_dword     = addr & 0xfc;
    uint16_t actual_port   = 0x02e8 + (addr & 1) + (port_dword << 8);
    uint16_t actual_port_ext = 0x02ee + (addr & 1) + (port_dword << 8);

    if (((mach->local_cntl & 0x20) || (mach->pci_cntl_reg & 0x80)) &&
        (((addr - mach->linear_base) >= ((mach->ap_size << 20) - 0x200)) && ((addr - mach->linear_base) < (mach->ap_size << 20))) &&
        (svga->mapping.base == 0xa0000)) {
        if (addr & 0x100)
            temp = mach_accel_inb(actual_port_ext, mach);
        else
            temp = mach_accel_inb(actual_port, mach);
    } else {
        if (dev->on)
            temp = mach32_read_common(addr, 1, mach, svga);
        else
            temp = svga_read_linear(addr, svga);

        mach_log("Linear WORDB Read=%08x, ret=%02x, fast=%d.\n", addr, temp, svga->fast);
    }

    return temp;
}

static uint16_t
mach32_ap_readw(uint32_t addr, void *priv)
{
    mach_t          *mach  = (mach_t *) priv;
    svga_t          *svga  = &mach->svga;
    const ibm8514_t *dev   = (ibm8514_t *) svga->dev8514;
    uint16_t temp;
    uint8_t port_dword     = addr & 0xfc;
    uint16_t actual_port   = 0x02e8 + (port_dword << 8);
    uint16_t actual_port_ext = 0x02ee + (port_dword << 8);

    if (((mach->local_cntl & 0x20) || (mach->pci_cntl_reg & 0x80)) &&
        (((addr - mach->linear_base) >= ((mach->ap_size << 20) - 0x200)) && ((addr - mach->linear_base) < (mach->ap_size << 20))) &&
        (svga->mapping.base == 0xa0000)) {
        if (addr & 0x100) {
            temp = mach_accel_inw(actual_port_ext, mach);
            mach_log("Port WORDW Read=%04x.\n", actual_port_ext);
        } else {
            temp = mach_accel_inw(actual_port, mach);
            mach_log("Port WORDW Read=%04x.\n", actual_port);
        }
    } else {
        if (dev->on)
            temp = mach32_readw_linear(addr, mach);
        else
            temp = svga_readw_linear(addr, svga);

        mach_log("Linear WORDW Read=%08x, ret=%04x.\n", addr, temp);
    }

    return temp;
}

static uint32_t
mach32_ap_readl(uint32_t addr, void *priv)
{
    mach_t          *mach  = (mach_t *) priv;
    svga_t          *svga  = &mach->svga;
    const ibm8514_t *dev   = (ibm8514_t *) svga->dev8514;
    uint32_t temp;
    uint8_t port_dword     = addr & 0xfc;
    uint16_t actual_port   = 0x02e8 + (port_dword << 8);
    uint16_t actual_port_ext = 0x02ee + (port_dword << 8);

    if (((mach->local_cntl & 0x20) || (mach->pci_cntl_reg & 0x80)) &&
        (((addr - mach->linear_base) >= ((mach->ap_size << 20) - 0x200)) && ((addr - mach->linear_base) < (mach->ap_size << 20))) &&
        (svga->mapping.base == 0xa0000)) {
        if (addr & 0x100) {
            temp = mach_accel_inl(actual_port_ext, mach);
            mach_log("Port WORDL Read=%04x.\n", actual_port_ext);
        } else {
            temp = mach_accel_inl(actual_port, mach);
            mach_log("Port WORDL Read=%04x.\n", actual_port);
        }
    } else {
        if (dev->on)
            temp = mach32_readl_linear(addr, mach);
        else
            temp = svga_readl_linear(addr, svga);

        mach_log("Linear WORDL Read=%08x, ret=%08x, ON%d.\n", addr, temp, dev->on);
    }

    return temp;
}

static void
mach32_updatemapping(mach_t *mach, svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    xga_t     *xga = (xga_t *) svga->xga;

    if (mach->pci_bus && (!(mach->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM))) {
        mach_log("No Mapping.\n");
        mem_mapping_disable(&svga->mapping);
        mem_mapping_disable(&mach->mmio_linear_mapping);
        return;
    }

    if (mach->regs[0xbd] & 0x04) {
        mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
        svga->banked_mask = 0xffff;
    } else {
        switch (svga->gdcreg[6] & 0x0c) {
            case 0x0: /*128k at A0000*/
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
                svga->banked_mask = 0xffff;
                break;
            case 0x4: /*64k at A0000*/
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                svga->banked_mask = 0xffff;
                if (xga_active && (svga->xga != NULL)) {
                    xga->on = 0;
                    mem_mapping_set_handler(&svga->mapping, svga_read, svga_readw, svga_readl, svga_write, svga_writew, svga_writel);
                    mem_mapping_set_p(&svga->mapping, svga);
                }
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

    mach_log("Linear base=%08x, aperture=%04x, localcntl=%02x, svgagdc=%x.\n",
             mach->linear_base, mach->memory_aperture, mach->local_cntl, svga->gdcreg[6] & 0x0c);
    if (mach->linear_base) {
        if (((mach->memory_aperture & 3) == 1) && !mach->pci_bus) {
            /*1 MB aperture*/
            mach->ap_size = 1;
            mach_log("Linear Enabled APSIZE=1.\n");
            mem_mapping_set_addr(&mach->mmio_linear_mapping, mach->linear_base, mach->ap_size << 20);
        } else {
            /*4 MB aperture*/
            mach->ap_size = 4;
            mach_log("Linear Enabled APSIZE=4.\n");
            mem_mapping_set_addr(&mach->mmio_linear_mapping, mach->linear_base, mach->ap_size << 20);
        }
    } else {
        mach->ap_size = 4;
        mach_log("Linear Disabled APSIZE=4.\n");
        mem_mapping_disable(&mach->mmio_linear_mapping);
    }

    if (ATI_MACH32) {
        if (dev->on && dev->vendor_mode) {
            mach_log("Mach32 banked mapping.\n");
            mem_mapping_set_handler(&svga->mapping, mach32_read, mach32_readw, mach32_readl, mach32_write, mach32_writew, mach32_writel);
            mem_mapping_set_p(&svga->mapping, mach);
        } else {
            mach_log("IBM compatible banked mapping.\n");
            mem_mapping_set_handler(&svga->mapping, svga_read, svga_readw, svga_readl, mach32_svga_write, mach32_svga_writew, mach32_svga_writel);
            mem_mapping_set_p(&svga->mapping, svga);
        }
    } else {
        mem_mapping_set_handler(&svga->mapping, svga_read, svga_readw, svga_readl, svga_write, svga_writew, svga_writel);
        mem_mapping_set_p(&svga->mapping, svga);
    }
}

static void
mach32_hwcursor_draw(svga_t *svga, int displine)
{
    const mach_t *mach   = (mach_t *) svga->priv;
    ibm8514_t    *dev    = (ibm8514_t *) svga->dev8514;
    uint16_t      dat;
    int           comb;
    int           offset;
    uint32_t      color0;
    uint32_t      color1;
    uint32_t      *p;
    int           x_pos;
    int           y_pos;
    int           shift = 0;

    offset = dev->hwcursor_latch.x - dev->hwcursor_latch.xoff;
    if (!dev->vram_512k_8514 && ((mach->accel.ext_ge_config & 0x30) == 0x00))
        shift = 1;

    mach_log("BPP=%d, displine=%d.\n", dev->accel_bpp, displine);
    switch (dev->accel_bpp) {
        default:
        case 8:
            color0 = dev->pallook[mach->cursor_col_0];
            color1 = dev->pallook[mach->cursor_col_1];
            mach_log("4/8BPP: Color0=%08x, Color1=%08x, interlace=%x, oddeven=%d.\n", color0, color1, dev->interlace, dev->hwcursor_oddeven);
            break;
        case 15:
            color0 = video_15to32[((mach->ext_cur_col_0_r << 16) | (mach->ext_cur_col_0_g << 8) | mach->cursor_col_0) & 0xffff];
            color1 = video_15to32[((mach->ext_cur_col_1_r << 16) | (mach->ext_cur_col_1_g << 8) | mach->cursor_col_1) & 0xffff];
            break;
        case 16:
            color0 = video_16to32[((mach->ext_cur_col_0_r << 16) | (mach->ext_cur_col_0_g << 8) | mach->cursor_col_0) & 0xffff];
            color1 = video_16to32[((mach->ext_cur_col_1_r << 16) | (mach->ext_cur_col_1_g << 8) | mach->cursor_col_1) & 0xffff];
            break;
        case 24:
        case 32:
            color0 = ((mach->ext_cur_col_0_r << 16) | (mach->ext_cur_col_0_g << 8) | mach->cursor_col_0);
            color1 = ((mach->ext_cur_col_1_r << 16) | (mach->ext_cur_col_1_g << 8) | mach->cursor_col_1);
            mach_log("24/32BPP: Color0=%08x, Color1=%08x.\n", color0, color1);
            break;
    }

    if (dev->interlace && dev->hwcursor_oddeven)
        dev->hwcursor_latch.addr += (16 >> shift);

    for (int x = 0; x < 64; x += (8 >> shift)) {
        if (shift) {
            dat = dev->vram[(dev->hwcursor_latch.addr) & dev->vram_mask] & 0x0f;
            dat |= (dev->vram[(dev->hwcursor_latch.addr + 1) & dev->vram_mask] << 4);
            dat |= (dev->vram[(dev->hwcursor_latch.addr + 2) & dev->vram_mask] << 8);
            dat |= (dev->vram[(dev->hwcursor_latch.addr + 3) & dev->vram_mask] << 12);
            mach_log("4bpp Data=%04x.\n", dat);
        } else {
            dat = dev->vram[dev->hwcursor_latch.addr & dev->vram_mask];
            dat |= (dev->vram[(dev->hwcursor_latch.addr + 1) & dev->vram_mask] << 8);
            mach_log("8bppplus Data=%04x.\n", dat);
        }
        for (int xx = 0; xx < (8 >> shift); xx++) {
            comb = (dat >> (xx << 1)) & 0x03;

            y_pos = displine;
            x_pos = offset + svga->x_add;
            p     = buffer32->line[y_pos];

            if (offset >= dev->hwcursor_latch.x) {
                mach_log("COMB=%d.\n", comb);
                switch (comb) {
                    case 0:
                        p[x_pos] = color0;
                        break;
                    case 1:
                        p[x_pos] = color1;
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
        dev->hwcursor_latch.addr += 2;
    }

    if (dev->interlace && !dev->hwcursor_oddeven)
        dev->hwcursor_latch.addr += (16 >> shift);
}

static void
ati8514_io_set(svga_t *svga)
{
    io_sethandler(0x2e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x6e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xae8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xee8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x12e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x16e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x1ae8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x1ee8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x22e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x26e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x2ee8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x42e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x4ae8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x52e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x56e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x5ae8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x5ee8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x82e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x86e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x8ae8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x8ee8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x92e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x96e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x9ae8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x9ee8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xa2e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xa6e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xaae8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xaee8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xb2e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xb6e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xbae8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xbee8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xe2e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);

    io_sethandler(0xc2e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xc6e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xcae8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xcee8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xd2e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xd6e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xdae8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xdee8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xe6e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xeae8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xeee8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xf2e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xf6e8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xfae8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xfee8, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);

    io_sethandler(0x02ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x06ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x0aee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x0eee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x12ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x16ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x1aee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x1eee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x22ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x26ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x2aee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x2eee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x32ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x36ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x3aee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x3eee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x42ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x46ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x4aee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x52ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x56ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x5aee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x5eee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x62ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x66ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x6aee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x6eee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x72ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x76ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x7aee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x7eee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x82ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x86ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x8aee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x8eee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x92ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x96ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0x9aee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xa2ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xa6ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xaaee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xaeee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xb2ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xb6ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xbaee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xbeee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xc2ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xc6ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xcaee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xceee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xd2ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xd6ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xdaee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xdeee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xe2ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xe6ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xeaee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xeeee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xf2ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xf6ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xfaee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xfeee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
}

static void
mach_io_remove(mach_t *mach)
{
    io_removehandler(0x2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x12e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x16e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x1ae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x1ee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x22e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x26e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x2ee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x42e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x4ae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x52e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x56e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x5ae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x5ee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x82e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x86e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x8ae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x8ee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x92e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x96e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x9ae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x9ee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xa2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xa6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xaae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xaee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xb2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xb6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xbae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xbee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xe2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);

    io_removehandler(0xc2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xc6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xcae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xcee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xd2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xd6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xdae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xdee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xe6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xeae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xeee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xf2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xf6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xfae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xfee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);

    io_removehandler(0x02ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x06ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x0aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x0eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x12ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x16ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x1aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x1eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x22ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x26ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x2aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x2eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x32ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x36ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x3aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x3eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x42ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x46ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x4aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x52ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x56ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x5aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x5eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x62ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x66ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x6aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x6eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x72ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x76ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x7aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x7eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x82ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x86ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x8aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x8eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x92ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x96ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0x9aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xa2ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xa6ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xaaee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xaeee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xb2ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xb6ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xbaee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xbeee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xc2ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xc6ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xcaee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xceee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xd2ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xd6ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xdaee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xdeee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xe2ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xe6ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xeaee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xeeee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xf2ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xf6ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xfaee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_removehandler(0xfeee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
}

static void
mach_io_set(mach_t *mach)
{
    io_sethandler(0x2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x12e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x16e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x1ae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x1ee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x22e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x26e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x2ee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x42e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x4ae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x52e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x56e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x5ae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x5ee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x82e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x86e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x8ae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x8ee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x92e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x96e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x9ae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x9ee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xa2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xa6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xaae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xaee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xb2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xb6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xbae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xbee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xe2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);

    io_sethandler(0xc2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xc6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xcae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xcee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xd2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xd6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xdae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xdee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xe6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xeae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xeee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xf2e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xf6e8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xfae8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xfee8, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);

    io_sethandler(0x02ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x06ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x0aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x0eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x12ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x16ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x1aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x1eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x22ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x26ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x2aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x2eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x32ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x36ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x3aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x3eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x42ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x46ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x4aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x52ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x56ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x5aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x5eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x62ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x66ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x6aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x6eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x72ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x76ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x7aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x7eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x82ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x86ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x8aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x8eee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x92ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x96ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0x9aee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xa2ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xa6ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xaaee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xaeee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xb2ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xb6ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xbaee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xbeee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xc2ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xc6ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xcaee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xceee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xd2ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xd6ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xdaee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xdeee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xe2ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xe6ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xeaee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xeeee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xf2ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xf6ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xfaee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xfeee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
}

static uint8_t
mach_mca_read(int port, void *priv)
{
    const mach_t *mach = (mach_t *) priv;

    mach_log("[%04X]: MCA read port = %x, val = %02x.\n", CS, port & 7, mach->pos_regs[port & 7]);
    return mach->pos_regs[port & 7];
}

static void
mach_mca_write(int port, uint8_t val, void *priv)
{
    mach_t       *mach = (mach_t *) priv;

    if (port < 0x102)
        return;

    mach->pos_regs[port & 7] = val;
    mach_log("[%04X]: MCA write port = %x, val = %02x, biosaddr = %05x.\n",
             CS, port & 7, mach->pos_regs[port & 7], (((mach->pos_regs[3] & 0x3e) << 0x0c) >> 1) + 0xc0000);
    mem_mapping_disable(&mach->bios_rom.mapping);
    mem_mapping_disable(&mach->bios_rom2.mapping);
    if (mach->pos_regs[2] & 0x01) {
        mem_mapping_enable(&mach->bios_rom.mapping);
        mem_mapping_enable(&mach->bios_rom2.mapping);
    }
}

static uint8_t
mach_mca_feedb(void *priv)
{
    const mach_t *mach = (mach_t *) priv;

    mach_log("FeedB = %x.\n", mach->pos_regs[2] & 0x01);
    return mach->pos_regs[2] & 0x01;
}

static void
mach_mca_reset(void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t    *svga = &mach->svga;
    ibm8514_t *dev  = (ibm8514_t *) svga->dev8514;

    mach_log("MCA reset.\n");
    dev->on = 0;
    mach_mca_write(0x102, 0, mach);
    svga_set_poll(svga);
}

uint8_t
ati8514_mca_read(int port, void *priv)
{
    const svga_t    *svga = (svga_t *) priv;
    const ibm8514_t *dev  = (ibm8514_t *) svga->dev8514;

    return (dev->pos_regs[port & 7]);
}

void
ati8514_mca_write(int port, uint8_t val, void *priv)
{
    svga_t    *svga = (svga_t *) priv;
    ibm8514_t *dev  = (ibm8514_t *) svga->dev8514;

    if (port < 0x102)
        return;

    dev->pos_regs[port & 7] = val;
    mach_log("[%04X]: MCA write port = %x, val = %02x, biosaddr = %05x.\n",
             CS, port & 7, dev->pos_regs[port & 7], (((dev->pos_regs[3] & 0x3e) << 0x0c) >> 1) + 0xc0000);
    mem_mapping_disable(&dev->bios_rom.mapping);

    if (dev->pos_regs[2] & 0x01)
        mem_mapping_enable(&dev->bios_rom.mapping);
}

void
ati8514_pos_write(uint16_t port, uint8_t val, void *priv)
{
    ati8514_mca_write(port, val, priv);
}

static uint8_t
mach32_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    const mach_t *mach = (mach_t *) priv;
    uint8_t       ret  = 0x00;

    if ((addr >= 0x30) && (addr <= 0x33) && !mach->has_bios)
        return ret;

    switch (addr) {
        case 0x00:
            ret = 0x02; /*ATI*/
            break;
        case 0x01:
            ret = 0x10;
            break;

        case 0x02:
            ret = 0x58;
            break;
        case 0x03:
            ret = 0x41;
            break;

        case PCI_REG_COMMAND:
            ret = mach->pci_regs[PCI_REG_COMMAND] | 0x80; /*Respond to IO and memory accesses*/
            break;

        case 0x07:
            ret = 0x01; /*Medium DEVSEL timing*/
            break;

        case 0x0a:
            ret = 0x00; /*Supports VGA interface*/
            break;
        case 0x0b:
            ret = 0x03;
            break;

        case 0x10:
            ret = 0x00; /*Linear frame buffer address*/
            break;
        case 0x11:
            ret = 0x00;
            break;
        case 0x12:
            ret = mach->linear_base >> 16;
            break;
        case 0x13:
            ret = mach->linear_base >> 24;
            break;

        case 0x30:
            ret = (mach->pci_regs[0x30] & 0x01); /*BIOS ROM address*/
            break;
        case 0x31:
            ret = 0x00;
            break;
        case 0x32:
            ret = mach->pci_regs[0x32];
            break;
        case 0x33:
            ret = mach->pci_regs[0x33];
            break;

        case 0x3c:
            ret = mach->int_line;
            break;
        case 0x3d:
            ret = PCI_INTA;
            break;

        default:
            break;
    }

    return ret;
}

static void
mach32_pci_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    if ((addr >= 0x30) && (addr <= 0x33) && !mach->has_bios)
        return;

    switch (addr) {
        case PCI_REG_COMMAND:
            mach->pci_regs[PCI_REG_COMMAND] = val & 0x27;
            if (val & PCI_COMMAND_IO) {
                mach_log("Remove and set handlers.\n");
                io_removehandler(0x01ce, 2,  mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
                io_removehandler(0x02ea, 4,  mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
                io_removehandler(0x03c0, 32, mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
                mach_io_remove(mach);
                io_sethandler(0x01ce, 2,  mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
                io_sethandler(0x02ea, 4,  mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
                io_sethandler(0x03c0, 32, mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
                mach_io_set(mach);
            } else {
                mach_log("Remove handlers.\n");
                io_removehandler(0x01ce, 2,  mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
                io_removehandler(0x02ea, 4,  mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
                io_removehandler(0x03c0, 32, mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
                mach_io_remove(mach);
            }
            mach32_updatemapping(mach, &mach->svga);
            break;

        case 0x12:
            mach->linear_base = (mach->linear_base & 0xff000000) | ((val & 0xc0) << 16);
            mach32_updatemapping(mach, &mach->svga);
            break;
        case 0x13:
            mach->linear_base = (mach->linear_base & 0xc00000) | (val << 24);
            mach32_updatemapping(mach, &mach->svga);
            break;

        case 0x30:
        case 0x32:
        case 0x33:
            mach->pci_regs[addr] = val;
            if (mach->pci_regs[0x30] & 0x01) {
                uint32_t bios_addr = (mach->pci_regs[0x32] << 16) | (mach->pci_regs[0x33] << 24);
                mach_log("Mach32 bios_rom enabled at %08x\n", bios_addr);
                mem_mapping_set_addr(&mach->bios_rom.mapping, bios_addr, 0x8000);
            } else {
                mach_log("Mach32 bios_rom disabled\n");
                mem_mapping_disable(&mach->bios_rom.mapping);
            }
            return;

        case 0x3c:
            mach->int_line = val;
            break;

        default:
            break;
    }
}

static void
mach_vblank_start(mach_t *mach, svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    dev->subsys_stat |= INT_VSY;
}

static void
mach_combo_vblank_start(void *priv)
{
    svga_t *svga = (svga_t *) priv;
    mach_t *mach = (mach_t *) svga->priv;

    mach_vblank_start(mach, svga);
}

static void
ati8514_vblank_start(void *priv)
{
    svga_t *svga = (svga_t *) priv;
    mach_t *mach = (mach_t *) svga->ext8514;

    mach_vblank_start(mach, svga);
}

static void
mach_combo_accel_out_fifo(void *priv, uint16_t port, uint16_t val, int len)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    mach_log("Accel OUT Combo=%04x, val=%04x, len=%d.\n", port, val, len);
    mach_accel_out_fifo(mach, svga, dev, port, val, len);
}

static void
ati8514_accel_out_fifo(void *priv, uint16_t port, uint16_t val, int len)
{
    svga_t *svga = (svga_t *) priv;
    mach_t *mach = (mach_t *) svga->ext8514;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    mach_accel_out_fifo(mach, svga, dev, port, val, len);
}

static void
mach_disable_handlers(mach_t *mach)
{
    if (mach->pci_bus) {
        io_removehandler(0x01ce, 2,  mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
        io_removehandler(0x02ea, 4,  mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
        io_removehandler(0x03c0, 32, mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
        mach_io_remove(mach);
    }

    mem_mapping_disable(&mach->mmio_linear_mapping);
    mem_mapping_disable(&mach->svga.mapping);
    if (mach->pci_bus && mach->has_bios)
        mem_mapping_disable(&mach->bios_rom.mapping);

    /* Save all the mappings and the timers because they are part of linked lists. */
    reset_state->mmio_linear_mapping = mach->mmio_linear_mapping;
    reset_state->svga.mapping        = mach->svga.mapping;
    reset_state->bios_rom.mapping    = mach->bios_rom.mapping;

    reset_state->svga.timer          = mach->svga.timer;
}

static void
mach_reset(void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if (reset_state != NULL) {
        dev->on = 0;
        dev->vendor_mode = 0;
        dev->_8514on = 0;
        dev->_8514crt = 0;
        mach_disable_handlers(mach);
        mach->force_busy      = 0;
        dev->force_busy       = 0;
        dev->force_busy2      = 0;
        if (mach->pci_bus)
            reset_state->pci_slot = mach->pci_slot;

        *mach = *reset_state;
    }
}

uint8_t
ati8514_bios_rom_readb(uint32_t addr, void *priv)
{
    const ibm8514_t *dev = (ibm8514_t *) priv;
    const rom_t  *rom = &dev->bios_rom;
    uint8_t ret = 0xff;

    mach_log("%04X:%08X: ROM1RB=%05x, ", CS, cpu_state.pc, addr);
    addr &= rom->mask;

    ret = rom->rom[addr];
    mach_log("BIOS: ReadBAddr1=%04x, ret=%02x.\n", addr, ret);
    return (ret);
}

uint16_t
ati8514_bios_rom_readw(uint32_t addr, void *priv)
{
    const ibm8514_t *dev = (ibm8514_t *) priv;
    const rom_t  *rom = &dev->bios_rom;
    uint16_t ret = 0xffff;

    mach_log("%04X:%08X: ROM1RW=%05x, ", CS, cpu_state.pc, addr);
    addr &= rom->mask;

    ret = (*(uint16_t *) &(rom->rom[addr]));
    mach_log("BIOS: ReadWAddr1=%04x, ret=%04x.\n", addr, ret);
    return (ret);
}

uint32_t
ati8514_bios_rom_readl(uint32_t addr, void *priv)
{
    const ibm8514_t *dev = (ibm8514_t *) priv;
    const rom_t  *rom = &dev->bios_rom;
    uint32_t ret = 0xffffffff;

    mach_log("%04X:%08X: ROM1RL=%05x, ", CS, cpu_state.pc, addr);
    addr &= rom->mask;

    ret = (*(uint32_t *) &(rom->rom[addr]));
    mach_log("BIOS: ReadLAddr1=%04x, ret=%08x.\n", addr, ret);
    return (ret);
}

static void *
mach8_init(const device_t *info)
{
    mach_t    *mach;
    svga_t    *svga;
    ibm8514_t *dev;

    mach             = calloc(1, sizeof(mach_t));
    reset_state      = calloc(1, sizeof(mach_t));

    svga             = &mach->svga;
    dev              = (ibm8514_t *) calloc(1, sizeof(ibm8514_t));

    svga->dev8514    = dev;

    mach->pci_bus    = !!(info->flags & DEVICE_PCI);
    mach->vlb_bus    = !!(info->flags & DEVICE_VLB);
    mach->mca_bus    = !!(info->flags & DEVICE_MCA);
    dev->type        = info->flags;
    dev->local       = info->local & 0xff;
    mach->has_bios   = !(info->local & 0xff00);
    mach->ramdac_type = mach->pci_bus ? device_get_config_int("ramdac") : ATI_68875;
    dev->vram_amount = device_get_config_int("memory");
    dev->vram_512k_8514 = dev->vram_amount == 512;

    if (ATI_MACH32) {
        if (mach->pci_bus) {
            if (mach->has_bios) {
                rom_init(&mach->bios_rom,
                         BIOS_MACH32_PCI_ROM_PATH,
                         0xc0000, 0x8000, 0x7fff,
                         0, MEM_MAPPING_EXTERNAL);
            }
        }
        else if (mach->vlb_bus)
            rom_init(&mach->bios_rom,
                     BIOS_MACH32_VLB_ROM_PATH,
                     0xc0000, 0x8000, 0x7fff,
                     0, MEM_MAPPING_EXTERNAL);
        else if (mach->mca_bus) {
            rom_init(&mach->bios_rom,
                     BIOS_MACH32_MCA_ROM_PATH,
                     0xc0000, 0x8000, 0x7fff,
                     0, MEM_MAPPING_EXTERNAL);
            rom_init(&mach->bios_rom2,
                     BIOS_MACH32_MCA_ROM_PATH,
                     0xc8000, 0x1000, 0x0fff,
                     0x8000, MEM_MAPPING_EXTERNAL);
        } else {
            rom_init(&mach->bios_rom,
                     BIOS_MACH32_ISA_ROM_PATH,
                     0xc0000, 0x8000, 0x7fff,
                     0, MEM_MAPPING_EXTERNAL);
        }
    } else
        rom_init(&mach->bios_rom,
                 BIOS_MACH8_VGA_ROM_PATH,
                 0xc0000, 0x8000, 0x7fff,
                 0, MEM_MAPPING_EXTERNAL);

    if (ATI_MACH32) {
        svga_init(info, svga, mach, dev->vram_amount << 10, /*default: 2MB for Mach32*/
                      mach_recalctimings,
                      mach_in, mach_out,
                      mach32_hwcursor_draw,
                      NULL);
        dev->vram_size   = dev->vram_amount << 10;
        dev->vram        = calloc(dev->vram_size, 1);
        dev->changedvram = calloc((dev->vram_size >> 12) + 1, 1);
        dev->vram_mask   = dev->vram_size - 1;
        dev->hwcursor.cur_ysize = 64;
        mach->config1 = 0x20;
        if (mach->pci_bus && (mach->ramdac_type == ATI_68860))
            svga->ramdac = device_add(&ati68860_ramdac_device);
        else
            svga->ramdac = device_add(&ati68875_ramdac_device);
        if (mach->vlb_bus) {
            video_inform(VIDEO_FLAG_TYPE_8514, &timing_mach32_vlb);
            if (!is486)
                mach->config1 |= 0x0a;
            else
                mach->config1 |= 0x0c;
            mach->config1 |= 0x0400;
            svga->clock_gen = device_add(&ati18811_1_mach32_device);
        } else if (mach->mca_bus) {
            video_inform(VIDEO_FLAG_TYPE_8514, &timing_mach32_mca);
            if (is286 && !is386)
                mach->config1 |= 0x04;
            else
                mach->config1 |= 0x06;
            mach->config1 |= 0x0400;
            svga->clock_gen = device_add(&ati18811_1_mach32_device);
        } else if (mach->pci_bus) {
            video_inform(VIDEO_FLAG_TYPE_8514, &timing_mach32_pci);
            mach->config1 |= 0x0e;
            if (mach->ramdac_type == ATI_68860)
                mach->config1 |= 0x0a00;
            else
                mach->config1 |= 0x0400;
            mach->config2 |= 0x2000;
            svga->clock_gen = device_add(&ati18811_1_mach32_device);
        } else {
            video_inform(VIDEO_FLAG_TYPE_8514, &timing_gfxultra_isa);
            mach->config1 |= 0x0400;
            svga->clock_gen = device_add(&ati18811_1_mach32_device);
        }
        mem_mapping_add(&mach->mmio_linear_mapping, 0, 0, mach32_ap_readb, mach32_ap_readw, mach32_ap_readl, mach32_ap_writeb, mach32_ap_writew, mach32_ap_writel, NULL, MEM_MAPPING_EXTERNAL, mach);
        mem_mapping_disable(&mach->mmio_linear_mapping);

        mem_mapping_set_handler(&svga->mapping, svga_read, svga_readw, svga_readl, mach32_svga_write, mach32_svga_writew, mach32_svga_writel);
    } else {
        svga_init(info, svga, mach, (512 << 10), /*default: 512kB VGA for 28800-6 + 1MB for Mach8*/
                      mach_recalctimings,
                      mach_in, mach_out,
                      NULL,
                      NULL);
        dev->vram_size   = (dev->vram_amount << 10);
        dev->vram        = calloc(dev->vram_size, 1);
        dev->changedvram = calloc((dev->vram_size >> 12) + 1, 1);
        dev->vram_mask   = dev->vram_size - 1;
        video_inform(VIDEO_FLAG_TYPE_8514, &timing_gfxultra_isa);
        mach->config1 = 0x01 | 0x08 | 0x80;
        if (dev->vram_amount >= 1024)
            mach->config1 |= 0x20;

        mach->config2 = 0x02;
        svga->clock_gen = device_add(&ati18811_1_mach32_device);
    }
    dev->bpp            = 0;
    svga->getclock      = ics2494_getclock;
    svga->clock_gen8514 = svga->clock_gen;
    svga->getclock8514  = svga->getclock;

    dev->on = 0;
    dev->pitch = 1024;
    dev->ext_pitch = 1024;
    dev->ext_crt_pitch = 0x80;
    dev->accel_bpp = 8;
    svga->force_old_addr = 1;
    svga->miscout = 1;
    svga->bpp = 8;
    svga->packed_chain4 = 1;
    dev->rowoffset = 0x80;
    io_sethandler(0x01ce, 2,  mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
    io_sethandler(0x03c0, 32, mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
    io_sethandler(0x02ea, 4,  mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
    mach_io_set(mach);
    mach->accel.cmd_type = -2;
    dev->accel.cmd_back = 1;
    dev->mode = IBM_MODE;

    if (ATI_MACH32) {
        svga->decode_mask     = (4 << 20) - 1;
        mach->cursor_col_1    = 0xff;
        mach->ext_cur_col_1_r = 0xff;
        mach->ext_cur_col_1_g = 0xff;
        if (mach->vlb_bus)
            ati_eeprom_load(&mach->eeprom, "mach32_vlb.nvr", 1);
        else if (mach->mca_bus) {
            ati_eeprom_load(&mach->eeprom, "mach32_mca.nvr", 1);
            mem_mapping_disable(&mach->bios_rom.mapping);
            mem_mapping_disable(&mach->bios_rom2.mapping);
            mach->pos_regs[0] = 0x89;
            mach->pos_regs[1] = 0x80;
            mca_add(mach_mca_read, mach_mca_write, mach_mca_feedb, mach_mca_reset, mach);
        } else if (mach->pci_bus) {
            ati_eeprom_load(&mach->eeprom, "mach32_pci.nvr", 1);
            if (mach->has_bios) {
                mem_mapping_disable(&mach->bios_rom.mapping);
                pci_add_card(PCI_ADD_NORMAL, mach32_pci_read, mach32_pci_write, mach, &mach->pci_slot);
            } else
                pci_add_card(PCI_ADD_VIDEO, mach32_pci_read, mach32_pci_write, mach, &mach->pci_slot);

            mach->pci_regs[PCI_REG_COMMAND] = 0x87;
            mach->pci_regs[0x30]            = 0x00;
            mach->pci_regs[0x32]            = 0x0c;
            mach->pci_regs[0x33]            = 0x00;
        } else
            ati_eeprom_load(&mach->eeprom, "mach32.nvr", 1);
    } else
        ati_eeprom_load_mach8_vga(&mach->eeprom, "mach8.nvr");

    dev->accel_out_fifo       = mach_combo_accel_out_fifo;
    dev->vblank_start         = mach_combo_vblank_start;

    *reset_state = *mach;

    return mach;
}

void
ati8514_init(svga_t *svga, void *ext8514, void *dev8514)
{
    mach_t *mach = (mach_t *) ext8514;
    ibm8514_t *dev = (ibm8514_t *) dev8514;

    /*Init as 1024x768 87hz interlaced first, per 8514/A.*/
    dev->on = 0;
    dev->pitch = 1024;
    dev->ext_pitch = 1024;
    dev->ext_crt_pitch = 0x80;
    dev->accel_bpp = 8;
    dev->rowoffset = 0x80;
    dev->hdisped = 0x7f;
    dev->v_disp = 0x05ff;
    dev->htotal = 0x9d;
    dev->v_total_reg = 0x0668;
    dev->v_sync_start = 0x0600;
    dev->disp_cntl = 0x33;
    mach->accel.clock_sel = 0x1c;
    dev->accel.cmd_back = 1;
    dev->mode = IBM_MODE;

    io_sethandler(0x02ea, 4, ati8514_in, NULL, NULL, ati8514_out, NULL, NULL, svga);
    ati8514_io_set(svga);
    mach->accel.cmd_type = -2;
    mach->mca_bus = !!(dev->type & DEVICE_MCA);

    mach->config1 = 0x08 | 0x80;

    if (mach->mca_bus)
        mach->config1 |= 0x04;

    if (dev->vram_amount >= 1024)
        mach->config1 |= 0x20;

    mach->config2 = 0x01 | 0x02;

    dev->accel_out_fifo       = ati8514_accel_out_fifo;
    dev->vblank_start         = ati8514_vblank_start;
    svga->clock_gen8514       = device_add(&ati18811_1_mach32_device);
    svga->getclock8514        = ics2494_getclock;
}

static int
mach8_vga_available(void)
{
    return rom_present(BIOS_MACH8_VGA_ROM_PATH);
}

static int
mach32_isa_available(void)
{
    return rom_present(BIOS_MACH32_ISA_ROM_PATH);
}

static int
mach32_vlb_available(void)
{
    return rom_present(BIOS_MACH32_VLB_ROM_PATH);
}

static int
mach32_mca_available(void)
{
    return rom_present(BIOS_MACH32_MCA_ROM_PATH);
}

static int
mach32_pci_available(void)
{
    return rom_present(BIOS_MACH32_PCI_ROM_PATH);
}

static void
mach_close(void *priv)
{
    mach_t    *mach = (mach_t *) priv;
    svga_t    *svga = &mach->svga;
    ibm8514_t *dev  = (ibm8514_t *) svga->dev8514;

    if (dev) {
        free(dev->vram);
        free(dev->changedvram);

        free(dev);
    }

    svga_close(svga);

    free(reset_state);
    reset_state = NULL;

    free(mach);
}

static void
mach_speed_changed(void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;

    svga_recalctimings(svga);
}

static void
mach_force_redraw(void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;

    svga->fullchange = svga->monitor->mon_changeframecount;
}

// clang-format off
static const device_config_t mach8_config[] = {
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1024,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "512 KB", .value =  512 },
            { .description = "1 MB",   .value = 1024 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t mach32_config[] = {
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2048,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "512 KB", .value =  512 },
            { .description = "1 MB",   .value = 1024 },
            { .description = "2 MB",   .value = 2048 },
            { .description = "4 MB",   .value = 4096 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t mach32_pci_config[] = {
    {
        .name           = "ramdac",
        .description    = "RAMDAC type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = ATI_68860,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "ATI 68860", .value = ATI_68860 },
            { .description = "ATI 68875", .value = ATI_68875 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2048,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "512 KB", .value =  512 },
            { .description = "1 MB",   .value = 1024 },
            { .description = "2 MB",   .value = 2048 },
            { .description = "4 MB",   .value = 4096 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t mach8_vga_isa_device = {
    .name          = "ATI Mach8 (ATI Graphics Ultra) (ISA)",
    .internal_name = "mach8_vga_isa",
    .flags         = DEVICE_ISA,
    .local         = ATI_38800_TYPE,
    .init          = mach8_init,
    .close         = mach_close,
    .reset         = mach_reset,
    .available     = mach8_vga_available,
    .speed_changed = mach_speed_changed,
    .force_redraw  = mach_force_redraw,
    .config        = mach8_config
};

const device_t mach32_isa_device = {
    .name          = "ATI Mach32 (ISA)",
    .internal_name = "mach32_isa",
    .flags         = DEVICE_ISA,
    .local         = ATI_68800_TYPE,
    .init          = mach8_init,
    .close         = mach_close,
    .reset         = mach_reset,
    .available     = mach32_isa_available,
    .speed_changed = mach_speed_changed,
    .force_redraw  = mach_force_redraw,
    .config        = mach32_config
};

const device_t mach32_vlb_device = {
    .name          = "ATI Mach32 (VLB)",
    .internal_name = "mach32_vlb",
    .flags         = DEVICE_VLB,
    .local         = ATI_68800_TYPE,
    .init          = mach8_init,
    .close         = mach_close,
    .reset         = mach_reset,
    .available     = mach32_vlb_available,
    .speed_changed = mach_speed_changed,
    .force_redraw  = mach_force_redraw,
    .config        = mach32_config
};

const device_t mach32_mca_device = {
    .name          = "ATI Mach32 (MCA)",
    .internal_name = "mach32_mca",
    .flags         = DEVICE_MCA,
    .local         = ATI_68800_TYPE,
    .init          = mach8_init,
    .close         = mach_close,
    .reset         = mach_reset,
    .available     = mach32_mca_available,
    .speed_changed = mach_speed_changed,
    .force_redraw  = mach_force_redraw,
    .config        = mach32_config
};

const device_t mach32_pci_device = {
    .name          = "ATI Mach32 (PCI)",
    .internal_name = "mach32_pci",
    .flags         = DEVICE_PCI,
    .local         = ATI_68800_TYPE,
    .init          = mach8_init,
    .close         = mach_close,
    .reset         = mach_reset,
    .available     = mach32_pci_available,
    .speed_changed = mach_speed_changed,
    .force_redraw  = mach_force_redraw,
    .config        = mach32_pci_config
};

const device_t mach32_onboard_pci_device = {
    .name          = "ATI Mach32 (PCI) On-Board",
    .internal_name = "mach32_pci_onboard",
    .flags         = DEVICE_PCI,
    .local         = ATI_68800_TYPE | 0x100,
    .init          = mach8_init,
    .close         = mach_close,
    .reset         = mach_reset,
    .available     = NULL,
    .speed_changed = mach_speed_changed,
    .force_redraw  = mach_force_redraw,
    .config        = mach32_pci_config
};
