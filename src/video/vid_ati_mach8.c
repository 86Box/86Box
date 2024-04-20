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
 *
 *
 * Authors: TheCollector1995.
 *
 *          Copyright 2022-2024 TheCollector1995.
 */
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
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_ati_eeprom.h>
#include <86box/vid_ati_mach8.h>

#define BIOS_MACH8_VGA_ROM_PATH  "roms/video/mach8/BIOS.BIN"
#define BIOS_MACH32_ISA_ROM_PATH "roms/video/mach32/ATi Mach32 Graphics Pro ISA.BIN"
#define BIOS_MACH32_VLB_ROM_PATH "roms/video/mach32/MACH32VLB.VBI"
#define BIOS_MACH32_MCA_ROM_PATH "roms/video/mach32/MACH32MCA_Olivetti.BIN"
#define BIOS_MACH32_PCI_ROM_PATH "roms/video/mach32/intelopt_00000.rom"

static video_timings_t timing_gfxultra_isa = { .type = VIDEO_ISA, .write_b = 3, .write_w = 3, .write_l = 6, .read_b = 5, .read_w = 5, .read_l = 10 };
static video_timings_t timing_mach32_vlb   = { .type = VIDEO_BUS, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };
static video_timings_t timing_mach32_mca   = { .type = VIDEO_MCA, .write_b = 4, .write_w = 5, .write_l = 10, .read_b = 5, .read_w = 5, .read_l = 10 };
static video_timings_t timing_mach32_pci   = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };

static void     mach_accel_outb(uint16_t port, uint8_t val, void *priv);
static void     mach_accel_outw(uint16_t port, uint16_t val, void *priv);
static uint8_t  mach_accel_inb(uint16_t port, void *priv);
static uint16_t mach_accel_inw(uint16_t port, void *priv);
static uint8_t  mach_in(uint16_t addr, void *priv);

#ifdef ATI_8514_ULTRA
static void     ati8514_accel_outb(uint16_t port, uint8_t val, void *priv);
static void     ati8514_accel_outw(uint16_t port, uint16_t val, void *priv);
static void     ati8514_accel_outl(uint16_t port, uint32_t val, void *priv);
static uint8_t  ati8514_accel_inb(uint16_t port, void *priv);
static uint16_t ati8514_accel_inw(uint16_t port, void *priv);
static uint32_t ati8514_accel_inl(uint16_t port, void *priv);
#endif


static void mach32_updatemapping(mach_t *mach, svga_t *svga);

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

#define WRITE8(addr, var, val)                        \
    switch ((addr) & 1) {                             \
        case 0:                                       \
            var = (var & 0xff00) | (val);             \
            break;                                    \
        case 1:                                       \
            var = (var & 0x00ff) | ((val) << 8);      \
            break;                                    \
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
        if (dev->bpp) { \
            if (n == 0) \
                mach->accel.pix_trans[(n)] = vram_w[(dev->accel.dest + (cx) + (n)) & (dev->vram_mask >> 1)] & 0xff; \
            else \
                mach->accel.pix_trans[(n)] = vram_w[(dev->accel.dest + (cx) + (n)) & (dev->vram_mask >> 1)] >> 8; \
        } else { \
            mach->accel.pix_trans[(n)] = dev->vram[(dev->accel.dest + (cx) + (n)) & dev->vram_mask]; \
        } \
    }

#define READ_PIXTRANS_WORD(cx, n)                                                                    \
    if ((cmd == 0) || (cmd == 1) || (cmd == 5) || (mach->accel.cmd_type == -1)) { \
        if (dev->bpp) \
            temp = vram_w[((dev->accel.cy * dev->pitch) + (cx) + (n)) & (dev->vram_mask >> 1)];             \
        else { \
            temp = dev->vram[((dev->accel.cy * dev->pitch) + (cx) + (n)) & dev->vram_mask];             \
            temp |= (dev->vram[((dev->accel.cy * dev->pitch) + (cx) + (n + 1)) & dev->vram_mask] << 8); \
        } \
    } else if ((mach->accel.cmd_type == 2) || (mach->accel.cmd_type == 5)) { \
        if (dev->bpp) \
            temp = vram_w[((dev->accel.dest) + (cx) + (n)) & (dev->vram_mask >> 1)];                           \
        else { \
            temp = dev->vram[((dev->accel.dest) + (cx) + (n)) & dev->vram_mask];                           \
            temp |= (dev->vram[((dev->accel.dest) + (cx) + (n + 1)) & dev->vram_mask] << 8);               \
        } \
    } else if ((mach->accel.cmd_type == 3) || (mach->accel.cmd_type == 4)) { \
        if (dev->bpp) \
            temp = vram_w[((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (cx) + (n)) & (dev->vram_mask >> 1)]; \
        else { \
            temp = dev->vram[((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (cx) + (n)) & dev->vram_mask]; \
            temp |= (dev->vram[((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (cx) + (n + 1)) & dev->vram_mask] << 8); \
        } \
    }

#define READ(addr, dat) \
        if (dev->bpp) \
            dat = vram_w[(addr) & (dev->vram_mask >> 1)]; \
        else \
            dat = dev->vram[(addr) & (dev->vram_mask)];

#define MIX(mixmode, dest_dat, src_dat)                                                       \
    {                                                                                         \
        switch ((mixmode) ? (dev->accel.frgd_mix & 0x1f) : (dev->accel.bkgd_mix & 0x1f)) {    \
            case 0x00:                                                                        \
                dest_dat = ~dest_dat;                                                         \
                break;                                                                        \
            case 0x01:                                                                        \
                dest_dat = 0;                                                                 \
                break;                                                                        \
            case 0x02:                                                                        \
                dest_dat = ~0;                                                                \
                break;                                                                        \
            case 0x03:                                                                        \
                dest_dat = dest_dat;                                                          \
                break;                                                                        \
            case 0x04:                                                                        \
                dest_dat = ~src_dat;                                                          \
                break;                                                                        \
            case 0x05:                                                                        \
                dest_dat = src_dat ^ dest_dat;                                                \
                break;                                                                        \
            case 0x06:                                                                        \
                dest_dat = ~(src_dat ^ dest_dat);                                             \
                break;                                                                        \
            case 0x07:                                                                        \
                dest_dat = src_dat;                                                           \
                break;                                                                        \
            case 0x08:                                                                        \
                dest_dat = ~(src_dat & dest_dat);                                             \
                break;                                                                        \
            case 0x09:                                                                        \
                dest_dat = ~src_dat | dest_dat;                                               \
                break;                                                                        \
            case 0x0a:                                                                        \
                dest_dat = src_dat | ~dest_dat;                                               \
                break;                                                                        \
            case 0x0b:                                                                        \
                dest_dat = src_dat | dest_dat;                                                \
                break;                                                                        \
            case 0x0c:                                                                        \
                dest_dat = src_dat & dest_dat;                                                \
                break;                                                                        \
            case 0x0d:                                                                        \
                dest_dat = src_dat & ~dest_dat;                                               \
                break;                                                                        \
            case 0x0e:                                                                        \
                dest_dat = ~src_dat & dest_dat;                                               \
                break;                                                                        \
            case 0x0f:                                                                        \
                dest_dat = ~(src_dat | dest_dat);                                             \
                break;                                                                        \
            case 0x10:                                                                        \
                dest_dat = MIN(src_dat, dest_dat);                                            \
                break;                                                                        \
            case 0x11:                                                                        \
                dest_dat = dest_dat - src_dat;                                                \
                break;                                                                        \
            case 0x12:                                                                        \
                dest_dat = src_dat - dest_dat;                                                \
                break;                                                                        \
            case 0x13:                                                                        \
                dest_dat = src_dat + dest_dat;                                                \
                break;                                                                        \
            case 0x14:                                                                        \
                dest_dat = MAX(src_dat, dest_dat);                                            \
                break;                                                                        \
            case 0x15:                                                                        \
                dest_dat = (dest_dat - src_dat) / 2;                                          \
                break;                                                                        \
            case 0x16:                                                                        \
                dest_dat = (src_dat - dest_dat) / 2;                                          \
                break;                                                                        \
            case 0x17:                                                                        \
                dest_dat = (dest_dat + src_dat) / 2;                                          \
                break;                                                                        \
            case 0x18:                                                                        \
                dest_dat = MAX(0, (dest_dat - src_dat));                                      \
                break;                                                                        \
            case 0x19:                                                                        \
                dest_dat = MAX(0, (dest_dat - src_dat));                                      \
                break;                                                                        \
            case 0x1a:                                                                        \
                dest_dat = MAX(0, (src_dat - dest_dat));                                      \
                break;                                                                        \
            case 0x1b:                                                                        \
                dest_dat = MIN(0xff, (dest_dat + src_dat));                                   \
                break;                                                                        \
            case 0x1c:                                                                        \
                dest_dat = MAX(0, (dest_dat - src_dat)) / 2;                                  \
                break;                                                                        \
            case 0x1d:                                                                        \
                dest_dat = MAX(0, (dest_dat - src_dat)) / 2;                                  \
                break;                                                                        \
            case 0x1e:                                                                        \
                dest_dat = MAX(0, (src_dat - dest_dat)) / 2;                                  \
                break;                                                                        \
            case 0x1f:                                                                        \
                dest_dat = (0xff < (src_dat + dest_dat)) ? 0xff : ((src_dat + dest_dat) / 2); \
                break;                                                                        \
        }                                                                                     \
    }


#define WRITE(addr, dat)                                         \
    if (dev->bpp) { \
        vram_w[((addr)) & (dev->vram_mask >> 1)]                = dat; \
        dev->changedvram[(((addr)) & (dev->vram_mask >> 1)) >> 11] = changeframecount; \
    } else { \
        dev->vram[((addr)) & (dev->vram_mask)]                = dat; \
        dev->changedvram[(((addr)) & (dev->vram_mask)) >> 12] = changeframecount; \
    }

static int
mach_pixel_write(mach_t *mach)
{
    if (mach->accel.dp_config & 1)
        return 1;

    return 0;
}

static int
mach_pixel_read(mach_t *mach)
{
    if (mach->accel.dp_config & 1)
        return 0;

    return 1;
}

static void
mach_accel_start(int cmd_type, int cpu_input, int count, uint32_t mix_dat, uint32_t cpu_dat, mach_t *mach, ibm8514_t *dev)
{
    int           compare_mode;
    uint16_t      poly_src     = 0;
    uint16_t      rd_mask      = dev->accel.rd_mask;
    uint16_t      wrt_mask     = dev->accel.wrt_mask;
    uint16_t      dest_cmp_clr = dev->accel.color_cmp;
    int           frgd_sel;
    int           bkgd_sel;
    int           mono_src;
    int           compare  = 0;
    uint16_t      src_dat  = 0;
    uint16_t      dest_dat = 0;
    uint16_t      old_dest_dat;
    uint16_t     *vram_w    = (uint16_t *) dev->vram;
    uint16_t      mix       = 0;
    int16_t       clip_l    = dev->accel.clip_left & 0x7ff;
    int16_t       clip_t    = dev->accel.clip_top & 0x7ff;
    int16_t       clip_r    = dev->accel.multifunc[4] & 0x7ff;
    int16_t       clip_b    = dev->accel.multifunc[3] & 0x7ff;
    uint32_t      mono_dat0 = 0;
    uint32_t      mono_dat1 = 0;

    if (!dev->bpp) {
        rd_mask &= 0xff;
        dest_cmp_clr &= 0xff;
    }

    compare_mode = (mach->accel.dest_cmp_fn >> 3) & 7;
    frgd_sel     = (mach->accel.dp_config >> 13) & 7;
    bkgd_sel     = (mach->accel.dp_config >> 7) & 3;
    mono_src     = (mach->accel.dp_config >> 5) & 3;

    mach->accel.ge_offset = (mach->accel.ge_offset_lo | (mach->accel.ge_offset_hi << 16));

    if ((mono_src == 2) || (bkgd_sel == 2) || (frgd_sel == 2) || mach_pixel_read(mach)) {
        mach->force_busy  = 1;
        dev->force_busy   = 1;
        dev->force_busy2  = 1;
    }

    if (cpu_input) {
        if (dev->bpp) {
            if ((mach->accel.dp_config & 0x200) && (count == 2)) {
                count >>= 1;
            }
        }
    }

    if ((dev->accel_bpp == 8) || (dev->accel_bpp == 15) || (dev->accel_bpp == 16) || (dev->accel_bpp == 24)) {
        if (cpu_input && (cmd_type == 2))
            mach_log("RdMask=%04x, DPCONFIG=%04x, Clipping: l=%d, r=%d, t=%d, b=%d, LineDrawOpt=%04x, BPP=%d, CMDType = %d, offs=%08x, cnt = %d, input = %d, mono_src = %d, frgdsel = %d, d(%d,%d), dstxend = %d, pitch = %d, extcrt = %d, rw = %x, monpattern = %x.\n", rd_mask, mach->accel.dp_config, clip_l, clip_r, clip_t, clip_b, mach->accel.linedraw_opt, dev->accel_bpp, cmd_type, mach->accel.ge_offset, count, cpu_input, mono_src, frgd_sel, dev->accel.cur_x, dev->accel.cur_y, mach->accel.dest_x_end, dev->ext_pitch, dev->ext_crt_pitch, mach->accel.dp_config & 1, mach->accel.mono_pattern_enable);
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

                mach->accel.color_pattern_idx = ((dev->accel.cx + (dev->accel.cy << 3)) & mach->accel.patt_len);

                mach->accel.stepx = (mach->accel.linedraw_opt & 0x20) ? 1 : -1;
                mach->accel.stepy = (mach->accel.linedraw_opt & 0x80) ? 1 : -1;

                mach_log("Extended bresenham, CUR(%d,%d), DEST(%d,%d), width = %d, options = %04x, dpconfig = %04x, opt_ena = %03x.\n", dev->accel.dx, dev->accel.dy, dev->accel.cx, dev->accel.cy, mach->accel.width, mach->accel.linedraw_opt, mach->accel.dp_config, mach->accel.max_waitstates & 0x100);

                if ((mono_src == 2) || (bkgd_sel == 2) || (frgd_sel == 2) || mach_pixel_read(mach)) {
                    if (mach_pixel_write(mach)) {
                        dev->data_available  = 0;
                        dev->data_available2 = 0;
                        return;
                    } else if (mach_pixel_read(mach)) {
                        dev->data_available  = 1;
                        dev->data_available2 = 1;
                        return;
                    }
                }
            }

            if (frgd_sel == 5) {
                for (int x = 0; x <= mach->accel.patt_len; x++) {
                    mach->accel.color_pattern[x] = mach->accel.patt_data[x & mach->accel.patt_len];
                }

                /*The destination coordinates should match the pattern index.*/
                if (mach->accel.color_pattern_idx != mach->accel.patt_idx)
                    mach->accel.color_pattern_idx = mach->accel.patt_idx;
            }

            if (mono_src == 1) {
                count               = mach->accel.width;
                mix_dat             = mach->accel.patt_data[0x10];
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
                            if (dev->bpp) {
                                READ((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), mix);
                            } else {
                                READ((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), mix);
                            }
                            mix = (mix & rd_mask) == rd_mask;
                            break;

                        default:
                            break;
                    }

                    if (((dev->accel.dx) >= clip_l) && ((dev->accel.dx) <= clip_r) && ((dev->accel.dy) >= clip_t) && ((dev->accel.dy) <= clip_b)) {
                        switch (mix ? frgd_sel : bkgd_sel) {
                            case 0:
                                src_dat = dev->accel.bkgd_color;
                                break;
                            case 1:
                                src_dat = dev->accel.frgd_color;
                                break;
                            case 2:
                                src_dat = cpu_dat;
                                break;
                            case 3:
                                if (mach_pixel_read(mach))
                                    src_dat = cpu_dat;
                                else {
                                    if (dev->bpp) {
                                        READ((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), src_dat);
                                    } else {
                                        READ((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), src_dat);
                                    }
                                    if (mono_src == 3) {
                                        src_dat = (src_dat & rd_mask) == rd_mask;
                                    }
                                }
                                break;
                            case 5:
                                if (mix) {
                                    src_dat = mach->accel.color_pattern[((dev->accel.dx) + ((dev->accel.dy) << 3)) & mach->accel.patt_len];
                                } else
                                    src_dat = 0;
                                break;

                            default:
                                break;
                        }

                        if (mach->accel.linedraw_opt & 0x02) {
                            if (dev->bpp) {
                                READ((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), poly_src);
                            } else {
                                READ((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), poly_src);
                            }
                            poly_src = ((poly_src & rd_mask) == rd_mask);
                            if (poly_src)
                                mach->accel.poly_fill = !mach->accel.poly_fill;
                        }

                        if (mach->accel.poly_fill || !(mach->accel.linedraw_opt & 0x02)) {
                            if (dev->bpp) {
                                READ((mach->accel.ge_offset << 1) + ((dev->accel.dy) * (dev->pitch)) + (dev->accel.dx), dest_dat);
                            } else {
                                READ((mach->accel.ge_offset << 2) + ((dev->accel.dy) * (dev->pitch)) + (dev->accel.dx), dest_dat);
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
                                if (mach_pixel_write(mach)) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                }
                            }

                            if (mach->accel.dp_config & 0x10) {
                                if (mach->accel.linedraw_opt & 0x04) {
                                    if (((mono_src != 1) && (dev->accel.sx < mach->accel.width)) || ((mono_src == 1) && count)) {
                                        if (dev->bpp) {
                                            WRITE((mach->accel.ge_offset << 1) + ((dev->accel.dy) * (dev->pitch)) + (dev->accel.dx), dest_dat);
                                        } else {
                                            WRITE((mach->accel.ge_offset << 2) + ((dev->accel.dy) * (dev->pitch)) + (dev->accel.dx), dest_dat);
                                        }
                                    }
                                } else {
                                    if (dev->bpp) {
                                        WRITE((mach->accel.ge_offset << 1) + ((dev->accel.dy) * (dev->pitch)) + (dev->accel.dx), dest_dat);
                                    } else {
                                        WRITE((mach->accel.ge_offset << 2) + ((dev->accel.dy) * (dev->pitch)) + (dev->accel.dx), dest_dat);
                                    }
                                }
                            }
                        }
                    }

                    if ((mono_src == 1) && !count)
                        break;
                    else if ((mono_src != 1) && (dev->accel.sx >= mach->accel.width))
                        break;

                    if (dev->bpp)
                        cpu_dat >>= 16;
                    else
                        cpu_dat >>= 8;

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
                            if (dev->bpp) {
                                READ((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), mix);
                            } else {
                                READ((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), mix);
                            }
                            mix = (mix & rd_mask) == rd_mask;
                            break;

                        default:
                            break;
                    }

                    if (((dev->accel.dx) >= clip_l) && ((dev->accel.dx) <= clip_r) && ((dev->accel.dy) >= clip_t) && ((dev->accel.dy) <= clip_b)) {
                        switch (mix ? frgd_sel : bkgd_sel) {
                            case 0:
                                src_dat = dev->accel.bkgd_color;
                                break;
                            case 1:
                                src_dat = dev->accel.frgd_color;
                                break;
                            case 2:
                                src_dat = cpu_dat;
                                break;
                            case 3:
                                if (mach_pixel_read(mach))
                                    src_dat = cpu_dat;
                                else {
                                    if (dev->bpp) {
                                        READ((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), src_dat);
                                    } else {
                                        READ((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), src_dat);
                                    }
                                    if (mono_src == 3) {
                                        src_dat = (src_dat & rd_mask) == rd_mask;
                                    }
                                }
                                break;
                            case 5:
                                if (mix) {
                                    src_dat = mach->accel.color_pattern[((dev->accel.dx) + ((dev->accel.dy) << 3)) & mach->accel.patt_len];
                                } else
                                    src_dat = 0;
                                break;

                            default:
                                break;
                        }

                        if (mach->accel.linedraw_opt & 0x02) {
                            if (dev->bpp) {
                                READ((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), poly_src);
                            } else {
                                READ((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), poly_src);
                            }
                            poly_src = ((poly_src & rd_mask) == rd_mask);
                            if (poly_src)
                                mach->accel.poly_fill = !mach->accel.poly_fill;
                        }

                        if (mach->accel.poly_fill || !(mach->accel.linedraw_opt & 0x02)) {
                            if (dev->bpp) {
                                READ((mach->accel.ge_offset << 1) + ((dev->accel.dy) * (dev->pitch)) + (dev->accel.dx), dest_dat);
                            } else {
                                READ((mach->accel.ge_offset << 2) + ((dev->accel.dy) * (dev->pitch)) + (dev->accel.dx), dest_dat);
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
                                if (mach_pixel_write(mach)) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                }
                            }

                            if (mach->accel.dp_config & 0x10) {
                                if (mach->accel.linedraw_opt & 0x04) {
                                    if (((mono_src != 1) && (dev->accel.sx < mach->accel.width)) || ((mono_src == 1) && count)) {
                                        if (dev->bpp) {
                                            WRITE((mach->accel.ge_offset << 1) + ((dev->accel.dy) * (dev->pitch)) + (dev->accel.dx), dest_dat);
                                        } else {
                                            WRITE((mach->accel.ge_offset << 2) + ((dev->accel.dy) * (dev->pitch)) + (dev->accel.dx), dest_dat);
                                        }
                                    }
                                } else {
                                    if (dev->bpp) {
                                        WRITE((mach->accel.ge_offset << 1) + ((dev->accel.dy) * (dev->pitch)) + (dev->accel.dx), dest_dat);
                                    } else {
                                        WRITE((mach->accel.ge_offset << 2) + ((dev->accel.dy) * (dev->pitch)) + (dev->accel.dx), dest_dat);
                                    }
                                }
                            }
                        }
                    }

                    if ((mono_src == 1) && !count)
                        break;
                    else if ((mono_src != 1) && (dev->accel.sx >= mach->accel.width))
                        break;

                    if (dev->bpp)
                        cpu_dat >>= 16;
                    else
                        cpu_dat >>= 8;

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

                if (mach->accel.dp_config == 0x5211) {
                    if (mach->accel.dest_x_end == 1024) {
                        goto skip_dx;
                    }
                }
                /*Destination Width*/
                if (mach->accel.dest_x_start != dev->accel.dx)
                    mach->accel.dest_x_start = dev->accel.dx;

skip_dx:
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
                    if (dev->accel.dx > 0)
                        dev->accel.dx--;
                    mach_log("BitBLT: Dst Negative X, dxstart = %d, end = %d, width = %d, dx = %d, dpconfig = %04x.\n", mach->accel.dest_x_start, mach->accel.dest_x_end, mach->accel.width, dev->accel.dx, mach->accel.dp_config);
                } else {
                    mach->accel.stepx = 1;
                    mach->accel.width = 0;
                    mach_log("BitBLT: Dst Indeterminate X, dpconfig = %04x, destxend = %d, destxstart = %d.\n", mach->accel.dp_config, mach->accel.dest_x_end, mach->accel.dest_x_start);
                }

                dev->accel.sx                 = 0;
                mach->accel.poly_fill         = 0;
                mach->accel.color_pattern_idx = ((dev->accel.dx + (dev->accel.dy << 3)) & mach->accel.patt_len);
                if ((dev->accel_bpp == 24) && (mono_src != 1)) {
                    if (mach->accel.color_pattern_idx == mach->accel.patt_len)
                        mach->accel.color_pattern_idx = mach->accel.patt_data_idx;
                } else if ((dev->accel_bpp == 24) && (frgd_sel == 5) && (mono_src == 1) && (mach->accel.patt_len_reg & 0x4000))
                    mach->accel.color_pattern_idx = 0;

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

                dev->accel.sy = 0;
                if (dev->bpp)
                    dev->accel.dest = (mach->accel.ge_offset << 1) + (dev->accel.dy * (dev->pitch));
                else
                    dev->accel.dest = (mach->accel.ge_offset << 2) + (dev->accel.dy * (dev->pitch));

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
                    mach_log("BitBLT: Src Positive X: wh(%d,%d), srcwidth = %d, coordinates: %d,%d px, start: %d, end: %d px, stepx = %d, dpconfig = %04x, oddwidth = %d.\n", mach->accel.width, mach->accel.height, mach->accel.src_width, dev->accel.cx, dev->accel.cy, mach->accel.src_x_start, mach->accel.src_x_end, mach->accel.src_stepx, mach->accel.dp_config, mach->accel.src_width & 1);
                } else if (mach->accel.sx_end < mach->accel.sx_start) {
                    mach->accel.src_width = (mach->accel.sx_start - mach->accel.sx_end);
                    mach->accel.src_stepx = -1;
                    if (dev->accel.cx > 0)
                        dev->accel.cx--;
                    mach_log("BitBLT: Src Negative X: width = %d, coordinates: %d,%d px, end: %d px, stepx = %d, dpconfig = %04x, oddwidth = %d.\n", mach->accel.src_width, dev->accel.cx, dev->accel.cy, mach->accel.src_x_end, mach->accel.src_stepx, mach->accel.dp_config, mach->accel.src_width & 1);
                } else {
                    mach->accel.src_stepx = 1;
                    mach->accel.src_width = 0;
                    mach_log("BitBLT: Src Indeterminate X: width = %d, coordinates: %d,%d px, end: %d px, stepx = %d, dpconfig = %04x, oddwidth = %d.\n", mach->accel.src_width, dev->accel.cx, dev->accel.cy, mach->accel.src_x_end, mach->accel.src_stepx, mach->accel.dp_config, mach->accel.src_width & 1);
                }
                mach->accel.sx = 0;
                if (dev->bpp)
                    dev->accel.src = (mach->accel.ge_offset << 1) + (dev->accel.cy * (dev->pitch));
                else
                    dev->accel.src = (mach->accel.ge_offset << 2) + (dev->accel.cy * (dev->pitch));

                if ((dev->accel_bpp == 24) && (frgd_sel == 5))
                    mach_log("BitBLT=%04x, WH(%d,%d), SRCWidth=%d, c(%d,%d), s(%d,%d).\n", mach->accel.dp_config, mach->accel.width, mach->accel.height, mach->accel.src_width, dev->accel.dx, dev->accel.dy, dev->accel.cx, dev->accel.cy);
                else if (mach->accel.dp_config & 0x02)
                    mach_log("BitBLT=%04x, Pitch=%d, C(%d,%d), D(%d,%d), SRCWidth=%d, SRCXStep=%d, WH(%d,%d), clipt=%d, clipb=%d, geoffset=%08x.\n", mach->accel.dp_config, dev->ext_pitch, mach->accel.src_x, mach->accel.src_y, dev->accel.cur_x, dev->accel.cur_y, mach->accel.src_width, mach->accel.src_stepx, mach->accel.width, mach->accel.height, clip_t, clip_b, (mach->accel.ge_offset << 2));

                if (mono_src == 1) {
                    if ((mach->accel.mono_pattern_enable) && !(mach->accel.patt_len_reg & 0x4000)) {
                        mono_dat0 = mach->accel.patt_data[0x10];
                        mono_dat0 |= (mach->accel.patt_data[0x11] << 8);
                        mono_dat0 |= (mach->accel.patt_data[0x12] << 16);
                        mono_dat0 |= (mach->accel.patt_data[0x13] << 24);
                        mono_dat1 = mach->accel.patt_data[0x14];
                        mono_dat1 |= (mach->accel.patt_data[0x15] << 8);
                        mono_dat1 |= (mach->accel.patt_data[0x16] << 16);
                        mono_dat1 |= (mach->accel.patt_data[0x17] << 24);

                        for (uint8_t y = 0; y < 8; y++) {
                            for (uint8_t x = 0; x < 8; x++) {
                                uint32_t temp                      = (y & 4) ? mono_dat1 : mono_dat0;
                                mach->accel.mono_pattern[y][7 - x] = (temp >> (x + ((y & 3) << 3))) & 1;
                            }
                        }
                    }
                }

                if ((mono_src == 2) || (bkgd_sel == 2) || (frgd_sel == 2) || mach_pixel_read(mach)) {
                    if (mach_pixel_write(mach)) {
                        dev->data_available  = 0;
                        dev->data_available2 = 0;
                        return;
                    } else if (mach_pixel_read(mach)) {
                        dev->data_available  = 1;
                        dev->data_available2 = 1;
                        return;
                    }
                }
            }

            if (mono_src == 1) {
                if (!mach->accel.mono_pattern_enable && !(mach->accel.patt_len_reg & 0x4000)) {
                    count               = mach->accel.width;
                    mix_dat             = mach->accel.patt_data[0x10] ^ ((mach->accel.patt_idx & 1) ? 0xff : 0);
                    dev->accel.temp_cnt = 8;
                }
            }

            if (frgd_sel == 5) {
                if (dev->bpp) {
                    for (int x = 0; x <= mach->accel.patt_len; x += 2) {
                        mach->accel.color_pattern_word[x + (mach->accel.color_pattern_idx & 1)] = (mach->accel.patt_data[x & mach->accel.patt_len] & 0xff);
                        mach->accel.color_pattern_word[x + (mach->accel.color_pattern_idx & 1)] |= (mach->accel.patt_data[(x + 1) & mach->accel.patt_len] << 8);
                    }
                } else {
                    if ((dev->accel_bpp == 24) && (mach->accel.patt_len < 3)) {
                        for (int x = 0; x <= mach->accel.patt_len; x++) {
                            mach->accel.color_pattern[x] = mach->accel.patt_data[x];
                            mach_log("BITBLT: Color Pattern 24bpp[%d]=%02x, dataidx=%d, pattlen=%d.\n", x, mach->accel.color_pattern[x], mach->accel.patt_data_idx, mach->accel.patt_len);
                        }
                    } else {
                        for (int x = 0; x <= mach->accel.patt_len; x++) {
                            mach->accel.color_pattern[x] = mach->accel.patt_data[x & mach->accel.patt_len];
                        }
                    }
                }

                /*The destination coordinates should match the pattern index.*/
                if (mach->accel.color_pattern_idx != mach->accel.patt_idx)
                    mach->accel.color_pattern_idx = mach->accel.patt_idx;
            }

            if (mach->accel.dy_end == mach->accel.dy_start) {
                mach_log("No DEST.\n");
                return;
            }

            if ((mono_src == 3) || (bkgd_sel == 3) || (frgd_sel == 3)) {
                if (mach->accel.sx_end == mach->accel.sx_start) {
                    mach_log("No SRC.\n");
                    return;
                }
            }

            if (cpu_input) {
                if (mach->accel.dp_config == 0x3251) {
                    if (dev->accel.sy == mach->accel.height)
                        return;
                }
            }

            while (count--) {
                switch (mono_src) {
                    case 0:
                        mix = 1;
                        break;
                    case 1:
                        if (mach->accel.mono_pattern_enable)
                            mix = mach->accel.mono_pattern[dev->accel.dy & 7][dev->accel.dx & 7];
                        else {
                            if ((dev->accel_bpp == 24) && (frgd_sel == 5) && (mach->accel.patt_len_reg & 0x4000))
                                mix = 1;
                            else {
                                if (!dev->accel.temp_cnt) {
                                    dev->accel.temp_cnt = 8;
                                    mix_dat >>= 8;
                                }
                                mix = (mix_dat & 0x80);
                                dev->accel.temp_cnt--;
                                mix_dat <<= 1;
                                mix_dat |= 1;
                            }
                        }
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

                if (((dev->accel.dx) >= clip_l) && ((dev->accel.dx) <= clip_r) && ((dev->accel.dy) >= clip_t) && ((dev->accel.dy) <= clip_b)) {
                    if ((mach->accel.dp_config & 0x02) || (mach->accel.linedraw_opt & 0x02)) {
                        READ(dev->accel.src + dev->accel.cx, poly_src);
                        poly_src = ((poly_src & rd_mask) == rd_mask);
                        if (poly_src)
                            mach->accel.poly_fill ^= 1;
                    }

                    if (mach->accel.poly_fill || !(mach->accel.dp_config & 0x02) || !(mach->accel.linedraw_opt & 0x02)) {
                        switch (mix ? frgd_sel : bkgd_sel) {
                            case 0:
                                src_dat = dev->accel.bkgd_color;
                                break;
                            case 1:
                                src_dat = dev->accel.frgd_color;
                                break;
                            case 2:
                                src_dat = cpu_dat;
                                break;
                            case 3:
                                if (mach_pixel_read(mach))
                                    src_dat = cpu_dat;
                                else {
                                    READ(dev->accel.src + dev->accel.cx, src_dat);
                                    if (mono_src == 3)
                                        src_dat = (src_dat & rd_mask) == rd_mask;
                                }
                                break;
                            case 5:
                                if (mix) {
                                    if (dev->bpp)
                                        src_dat = mach->accel.color_pattern_word[mach->accel.color_pattern_idx];
                                    else
                                        src_dat = mach->accel.color_pattern[mach->accel.color_pattern_idx];
                                } else
                                    src_dat = 0;
                                break;

                            default:
                                break;
                        }

                        if ((dev->accel_bpp == 24) && (mono_src == 1) && (frgd_sel == 5) && (mach->accel.patt_len_reg & 0x4000)) {
                            if (dev->accel.sy & 1) {
                                READ(dev->accel.dest + dev->accel.dx - dev->ext_pitch, dest_dat);
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
                            if (mach_pixel_write(mach)) {
                                old_dest_dat = dest_dat;
                                MIX(mix, dest_dat, src_dat);
                                dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                            }
                        }

                        if (mach->accel.dp_config & 0x10) {
                            if ((dev->accel_bpp == 24) && (mono_src == 1) && (frgd_sel == 5) && (mach->accel.patt_len_reg & 0x4000)) {
                                if (dev->accel.sy & 1) {
                                    WRITE(dev->accel.dest + dev->accel.dx - dev->ext_pitch, dest_dat);
                                } else {
                                    WRITE(dev->accel.dest + dev->accel.dx, dest_dat);
                                }
                            } else {
                                WRITE(dev->accel.dest + dev->accel.dx, dest_dat);
                            }
                        }
                    }
                }

                if (dev->bpp)
                    cpu_dat >>= 16;
                else
                    cpu_dat >>= 8;

                if ((mono_src == 3) || (frgd_sel == 3) || (bkgd_sel == 3) || (mach->accel.dp_config & 0x02)) {
                    dev->accel.cx += mach->accel.src_stepx;
                    mach->accel.sx++;
                    if (mach->accel.sx >= mach->accel.src_width) {
                        mach->accel.sx = 0;
                        if (mach->accel.src_stepx == -1)
                            dev->accel.cx += mach->accel.src_width;
                        else
                            dev->accel.cx -= mach->accel.src_width;

                        dev->accel.cy += (mach->accel.src_y_dir ? 1 : -1);
                        if (dev->bpp)
                            dev->accel.src = (mach->accel.ge_offset << 1) + (dev->accel.cy * (dev->pitch));
                        else
                            dev->accel.src = (mach->accel.ge_offset << 2) + (dev->accel.cy * (dev->pitch));
                    }
                }

                dev->accel.dx += mach->accel.stepx;

                if ((dev->accel_bpp == 8) || ((dev->accel_bpp == 24) && (mach->accel.patt_len >= 3) && (mono_src != 1)))
                    mach->accel.color_pattern_idx = (mach->accel.color_pattern_idx + mach->accel.stepx) & mach->accel.patt_len;

                if ((dev->accel_bpp == 24) && (mach->accel.color_pattern_idx == mach->accel.patt_len) && (mach->accel.patt_len >= 3) && (mono_src != 1)) {
                    mach->accel.color_pattern_idx = mach->accel.patt_data_idx;
                } else if ((dev->accel_bpp == 24) && (mach->accel.patt_len < 3)) {
                    if (mach->accel.patt_len == 2) {
                        mach->accel.color_pattern_idx++;
                        if (mach->accel.color_pattern_idx == 3)
                            mach->accel.color_pattern_idx = 0;
                    } else
                        mach->accel.color_pattern_idx = (mach->accel.color_pattern_idx + mach->accel.stepx) & mach->accel.patt_len;

                } else if ((dev->accel_bpp == 24) && (mach->accel.patt_len_reg & 0x4000) && (frgd_sel == 5)) {
                    mach->accel.color_pattern_idx++;
                    if (mach->accel.color_pattern_idx == 3)
                        mach->accel.color_pattern_idx = 0;
                }

                if (dev->bpp) {
                    mach->accel.color_pattern_idx = (mach->accel.color_pattern_idx + mach->accel.stepx) & mach->accel.patt_len;
                    mach->accel.color_pattern_idx = (mach->accel.color_pattern_idx + mach->accel.stepx) & mach->accel.patt_len;
                }

                dev->accel.sx++;
                if ((dev->accel.sx >= mach->accel.width) || (dev->accel.dx >= 0x600)) {
                    dev->accel.sx         = 0;
                    if (mach->accel.stepx == -1)
                        dev->accel.dx += mach->accel.width;
                    else
                        dev->accel.dx -= mach->accel.width;

                    dev->accel.dy += mach->accel.stepy;
                    dev->accel.sy++;

                    mach->accel.poly_fill = 0;
                    if (dev->bpp)
                        dev->accel.dest = (mach->accel.ge_offset << 1) + (dev->accel.dy * (dev->pitch));
                    else
                        dev->accel.dest = (mach->accel.ge_offset << 2) + (dev->accel.dy * (dev->pitch));

                    if ((mono_src == 1) && (dev->accel_bpp == 24) && (frgd_sel == 5))
                        mach->accel.color_pattern_idx = 0;
                    else
                        mach->accel.color_pattern_idx = ((dev->accel.dx + (dev->accel.dy << 3)) & mach->accel.patt_len);

                    if ((dev->accel_bpp == 24) && (mach->accel.color_pattern_idx == mach->accel.patt_len) && (mono_src != 1))
                        mach->accel.color_pattern_idx = 0;
                    if ((mono_src == 1) && !mach->accel.mono_pattern_enable && !(mach->accel.patt_len_reg & 0x4000)) {
                        dev->accel.cur_x = dev->accel.dx;
                        dev->accel.cur_y = dev->accel.dy;
                        return;
                    }
                    if (dev->accel.sy >= mach->accel.height) {
                        if ((mono_src == 2) || (mono_src == 3) || (frgd_sel == 3) || (bkgd_sel == 3) || (mach->accel.dp_config & 0x02) || (mach->accel.linedraw_opt & 0x02))
                            return;
                        if ((mono_src == 1) && (frgd_sel == 5) && (dev->accel_bpp == 24) && (mach->accel.patt_len_reg & 0x4000))
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

                mach_log("Linedraw: c(%d,%d), d(%d,%d), cend(%d,%d), bounds: l=%d, r=%d, t=%d, b=%d.\n", dev->accel.cur_x, dev->accel.cur_y, dev->accel.dx, dev->accel.dy, mach->accel.cx_end_line, mach->accel.cy_end_line, mach->accel.bleft, mach->accel.bright, mach->accel.btop, mach->accel.bbottom);

                if ((mono_src == 2) || (bkgd_sel == 2) || (frgd_sel == 2) || mach_pixel_read(mach)) {
                    if (mach_pixel_write(mach)) {
                        dev->data_available  = 0;
                        dev->data_available2 = 0;
                        return;
                    } else if (mach_pixel_read(mach)) {
                        dev->data_available  = 1;
                        dev->data_available2 = 1;
                        return;
                    }
                }
            }

            if (frgd_sel == 5) {
                for (int x = 0; x <= mach->accel.patt_len; x++) {
                    mach->accel.color_pattern[x] = mach->accel.patt_data[x & mach->accel.patt_len];
                }
            }

            if (mono_src == 1) {
                mix_dat             = mach->accel.patt_data[0x10];
                dev->accel.temp_cnt = 8;
            }

            count             = (dev->accel.dx > dev->accel.dy) ? (dev->accel.dx >> 1) : (dev->accel.dy >> 1);
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

                        if ((dev->accel.cx >= clip_l) && (dev->accel.cx <= clip_r) && (dev->accel.cy >= clip_t) && (dev->accel.cy <= clip_b)) {
                            mach->accel.clip_overrun = 0;
                            switch (mix ? frgd_sel : bkgd_sel) {
                                case 0:
                                    src_dat = dev->accel.bkgd_color;
                                    break;
                                case 1:
                                    src_dat = dev->accel.frgd_color;
                                    break;
                                case 2:
                                    src_dat = cpu_dat;
                                    break;
                                case 3:
                                    if (mach_pixel_read(mach))
                                        src_dat = cpu_dat;
                                    else
                                        src_dat = 0;
                                    break;
                                case 5:
                                    if (mix)
                                        src_dat = mach->accel.color_pattern[((dev->accel.cx) + ((dev->accel.cy) << 3)) & mach->accel.patt_len];
                                    else
                                        src_dat = 0;
                                    break;

                                default:
                                    break;
                            }

                            if (dev->bpp) {
                                READ((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                            } else {
                                READ((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
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
                                if (mach_pixel_write(mach)) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                }
                            }
                            if (mach->accel.linedraw_opt & 0x04) {
                                if (count) {
                                    if (dev->bpp) {
                                        WRITE((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                    } else {
                                        WRITE((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                    }
                                }
                            } else {
                                if (dev->bpp) {
                                    WRITE((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                } else {
                                    WRITE((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                }
                            }
                        } else
                            mach->accel.clip_overrun = ((mach->accel.clip_overrun + 1) & 0x0f);

                        if (!count)
                            break;

                        if (dev->bpp)
                            cpu_dat >>= 16;
                        else
                            cpu_dat >>= 8;

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

                        if ((dev->accel.cx >= clip_l) && (dev->accel.cx <= clip_r) && (dev->accel.cy >= clip_t) && (dev->accel.cy <= clip_b)) {
                            mach->accel.clip_overrun = 0;
                            if (mach->accel.linedraw_opt & 0x02) {
                                READ((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), poly_src);
                                if (poly_src)
                                    mach->accel.poly_fill = !mach->accel.poly_fill;
                            }

                            switch (mix ? frgd_sel : bkgd_sel) {
                                case 0:
                                    src_dat = dev->accel.bkgd_color;
                                    break;
                                case 1:
                                    src_dat = dev->accel.frgd_color;
                                    break;
                                case 2:
                                    src_dat = cpu_dat;
                                    break;
                                case 3:
                                    if (mach_pixel_read(mach))
                                        src_dat = cpu_dat;
                                    else {
                                        src_dat = 0;
                                    }
                                    break;
                                case 5:
                                    if (mix) {
                                        src_dat = mach->accel.color_pattern[((dev->accel.cx) + ((dev->accel.cy) << 3)) & mach->accel.patt_len];
                                    } else
                                        src_dat = 0;
                                    break;

                                default:
                                    break;
                            }

                            if (dev->bpp) {
                                READ((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                            } else {
                                READ((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
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
                                if (mach_pixel_write(mach)) {
                                    old_dest_dat = dest_dat;
                                    if (mach->accel.poly_fill || !(mach->accel.linedraw_opt & 0x02)) {
                                        MIX(mix, dest_dat, src_dat);
                                    }
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                }
                            }

                            if ((mach->accel.dp_config & 0x10) && (cmd_type == 3)) {
                                if (mach->accel.linedraw_opt & 0x04) {
                                    if (dev->accel.sx < mach->accel.width) {
                                        if (dev->bpp) {
                                            WRITE((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                        } else {
                                            WRITE((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                        }
                                    }
                                } else {
                                    if (dev->bpp) {
                                        WRITE((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                    } else {
                                        WRITE((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                    }
                                }
                            }
                        } else
                            mach->accel.clip_overrun = ((mach->accel.clip_overrun + 1) & 0x0f);

                        if (dev->accel.sx >= mach->accel.width)
                            break;

                        if (dev->bpp)
                            cpu_dat >>= 16;
                        else
                            cpu_dat >>= 8;

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

                        if ((dev->accel.cx >= clip_l) && (dev->accel.cx <= clip_r) && (dev->accel.cy >= clip_t) && (dev->accel.cy <= clip_b)) {
                            mach->accel.clip_overrun = 0;
                            switch (mix ? frgd_sel : bkgd_sel) {
                                case 0:
                                    src_dat = dev->accel.bkgd_color;
                                    break;
                                case 1:
                                    src_dat = dev->accel.frgd_color;
                                    break;
                                case 2:
                                    src_dat = cpu_dat;
                                    break;
                                case 3:
                                    if (mach_pixel_read(mach))
                                        src_dat = cpu_dat;
                                    else {
                                        src_dat = 0;
                                    }
                                    break;
                                case 5:
                                    if (mix) {
                                        src_dat = mach->accel.color_pattern[((dev->accel.cx) + ((dev->accel.cy) << 3)) & mach->accel.patt_len];
                                    } else
                                        src_dat = 0;
                                    break;

                                default:
                                    break;
                            }

                            if (dev->bpp) {
                                READ((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                            } else {
                                READ((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
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
                                if (mach_pixel_write(mach)) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                }
                            }

                            if ((mach->accel.dp_config & 0x10) && (cmd_type == 3)) {
                               if (mach->accel.linedraw_opt & 0x04) {
                                    if (count) {
                                        if (dev->bpp) {
                                            WRITE((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                        } else {
                                            WRITE((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                        }
                                    }
                                } else {
                                    if (dev->bpp) {
                                        WRITE((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                    } else {
                                        WRITE((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                    }
                                }
                            }
                        } else
                            mach->accel.clip_overrun = ((mach->accel.clip_overrun + 1) & 0x0f);

                        if (!count)
                            break;

                        if (dev->bpp)
                            cpu_dat >>= 16;
                        else
                            cpu_dat >>= 8;

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

                        if ((dev->accel.cx >= clip_l) && (dev->accel.cx <= clip_r) && (dev->accel.cy >= clip_t) && (dev->accel.cy <= clip_b)) {
                            mach->accel.clip_overrun = 0;

                            switch (mix ? frgd_sel : bkgd_sel) {
                                case 0:
                                    src_dat = dev->accel.bkgd_color;
                                    break;
                                case 1:
                                    src_dat = dev->accel.frgd_color;
                                    break;
                                case 2:
                                    src_dat = cpu_dat;
                                    break;
                                case 3:
                                    if (mach_pixel_read(mach))
                                        src_dat = cpu_dat;
                                    else {
                                        src_dat = 0;
                                    }
                                    break;
                                case 5:
                                    if (mix) {
                                        src_dat = mach->accel.color_pattern[((dev->accel.cx) + ((dev->accel.cy) << 3)) & mach->accel.patt_len];
                                    } else
                                        src_dat = 0;
                                    break;

                                default:
                                    break;
                            }

                            if (dev->bpp) {
                                READ((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                            } else {
                                READ((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
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
                                if (mach_pixel_write(mach)) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                }
                            }

                            if ((mach->accel.dp_config & 0x10) && (cmd_type == 3)) {
                                if (mach->accel.linedraw_opt & 0x04) {
                                    if (dev->accel.sx < mach->accel.width) {
                                        if (dev->bpp) {
                                            WRITE((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                        } else {
                                            WRITE((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                        }
                                    }
                                } else {
                                    if (dev->bpp) {
                                        WRITE((mach->accel.ge_offset << 1) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                    } else {
                                        WRITE((mach->accel.ge_offset << 2) + ((dev->accel.cy) * (dev->pitch)) + (dev->accel.cx), dest_dat);
                                    }
                                }
                            }
                        } else
                            mach->accel.clip_overrun = ((mach->accel.clip_overrun + 1) & 0x0f);

                        if (dev->accel.sx >= mach->accel.width)
                            break;

                        if (dev->bpp)
                            cpu_dat >>= 16;
                        else
                            cpu_dat >>= 8;

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
            dev->accel.cur_x                                = mach->accel.line_array[(cmd_type == 4) ? 4 : 0];
            dev->accel.cur_y                                = mach->accel.line_array[(cmd_type == 4) ? 5 : 1];
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
                if ((dev->accel_bpp == 24) && (mach->accel.patt_len < 0x17))
                    mach->accel.color_pattern_idx = 0;

                /*Step Y*/
                mach->accel.dy_start = dev->accel.cur_y;
                if (dev->accel.cur_y >= 0x600)
                    mach->accel.dy_start |= ~0x5ff;
                mach->accel.dy_end = mach->accel.dest_y_end;
                if (mach->accel.dest_y_end >= 0x600)
                    mach->accel.dy_end |= ~0x5ff;

                if (mach->accel.dy_end > mach->accel.dy_start) {
                    mach->accel.stepy = 1;
                } else if (mach->accel.dy_end < mach->accel.dy_start) {
                    mach->accel.stepy = -1;
                } else {
                    mach->accel.stepy = 0;
                }

                if (dev->bpp)
                    dev->accel.dest = (mach->accel.ge_offset << 1) + (dev->accel.dy * (dev->pitch));
                else
                    dev->accel.dest = (mach->accel.ge_offset << 2) + (dev->accel.dy * (dev->pitch));

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
                if (dev->bpp)
                    dev->accel.src = (mach->accel.ge_offset << 1) + (dev->accel.cy * (dev->pitch));
                else
                    dev->accel.src = (mach->accel.ge_offset << 2) + (dev->accel.cy * (dev->pitch));

                if ((dev->accel_bpp == 24) && (frgd_sel == 5)) {
                    if (mach->accel.patt_len == 0x17)
                        mach->accel.color_pattern_idx = 0;
                    dev->accel.x1 = dev->accel.dx + mach->accel.width;
                    if (dev->accel.x1 == dev->pitch) {
                        dev->accel.x2 = mach->accel.width & 1;
                    } else if ((dev->accel.x1 == mach->accel.width) && (dev->accel.dy & 1) && !dev->accel.y1 && dev->accel.x2) {
                        if (mach->accel.patt_len == 0x17)
                            mach->accel.color_pattern_idx = 3;
                        dev->accel.x3 = 1;
                    } else
                        dev->accel.x3 = 0;
                } else
                    mach_log("ScanToX=%04x, Pitch=%d, C(%d,%d), SRCWidth=%d, WH(%d,%d), geoffset=%08x.\n", mach->accel.dp_config, dev->ext_pitch, dev->accel.cx, dev->accel.cy, mach->accel.src_width, mach->accel.width, mach->accel.height, (mach->accel.ge_offset << 1));

                dev->accel.y1 = 0;

                if ((mono_src == 2) || (bkgd_sel == 2) || (frgd_sel == 2) || mach_pixel_read(mach)) {
                    if (mach_pixel_write(mach)) {
                        dev->data_available  = 0;
                        dev->data_available2 = 0;
                        return;
                    } else if (mach_pixel_read(mach)) {
                        dev->data_available  = 1;
                        dev->data_available2 = 1;
                        return;
                    }
                }
            }

            if (mono_src == 1) {
                count               = mach->accel.width;
                mix_dat             = mach->accel.patt_data[0x10];
                dev->accel.temp_cnt = 8;
            }

            if (frgd_sel == 5) {
                if (dev->accel_bpp != 24) {
                    for (int x = 0; x <= mach->accel.patt_len; x++) {
                        mach->accel.color_pattern[x] = mach->accel.patt_data[x & mach->accel.patt_len];
                    }
                } else {
                    if (mach->accel.patt_len == 0x17) {
                        for (int x = 0; x <= mach->accel.patt_len; x++) {
                            mach->accel.color_pattern_full[x] = mach->accel.patt_data[x];
                            mach_log("ScanToX: Color Pattern 24bpp[%d]=%02x, dataidx=%d, pattlen=%d.\n", x, mach->accel.color_pattern_full[x], mach->accel.patt_data_idx, mach->accel.patt_len);
                        }
                    } else {
                        for (int x = 0; x <= mach->accel.patt_len; x++) {
                            mach->accel.color_pattern[x] = mach->accel.patt_data[x];
                            mach_log("ScanToX: Color Pattern 24bpp[%d]=%02x, dataidx=%d, pattlen=%d.\n", x, mach->accel.color_pattern[x], mach->accel.patt_data_idx, mach->accel.patt_len);
                        }
                    }
                }
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
                        READ(dev->accel.src + (dev->accel.cx), mix);
                        mix = (mix & rd_mask) == rd_mask;
                        break;

                    default:
                        break;
                }

                if (((dev->accel.dx) >= clip_l) && ((dev->accel.dx) <= clip_r) && ((dev->accel.dy) >= clip_t) && ((dev->accel.dy) <= clip_b)) {
                    switch (mix ? frgd_sel : bkgd_sel) {
                        case 0:
                            src_dat = dev->accel.bkgd_color;
                            break;
                        case 1:
                            src_dat = dev->accel.frgd_color;
                            break;
                        case 2:
                            src_dat = cpu_dat;
                            break;
                        case 3:
                            if (mach_pixel_read(mach))
                                src_dat = cpu_dat;
                            else {
                                READ(dev->accel.src + dev->accel.cx, src_dat);
                                if (mono_src == 3)
                                    src_dat = (src_dat & rd_mask) == rd_mask;
                            }
                            break;
                        case 5:
                            if (mix) {
                                if (dev->accel_bpp == 24) {
                                    if (mach->accel.patt_len == 0x17)
                                        src_dat = mach->accel.color_pattern_full[mach->accel.color_pattern_idx];
                                    else
                                        src_dat = mach->accel.color_pattern[mach->accel.color_pattern_idx];
                                } else
                                    src_dat = mach->accel.color_pattern[(dev->accel.dx + (dev->accel.dy << 3)) & mach->accel.patt_len];
                            } else
                                src_dat = 0;
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
                        if (mach_pixel_write(mach)) {
                            old_dest_dat = dest_dat;
                            MIX(mix, dest_dat, src_dat);
                            dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                        }
                    }

                    if (mach->accel.dp_config & 0x10) {
                        WRITE(dev->accel.dest + (dev->accel.dx), dest_dat);
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
                    if (mach->accel.src_stepx == -1) {
                        dev->accel.cx += mach->accel.src_width;
                    } else
                        dev->accel.cx -= mach->accel.src_width;
                    dev->accel.cy += (mach->accel.src_y_dir ? 1 : -1);
                    if (dev->bpp)
                        dev->accel.src = (mach->accel.ge_offset << 1) + (dev->accel.cy * (dev->pitch));
                    else
                        dev->accel.src = (mach->accel.ge_offset << 2) + (dev->accel.cy * (dev->pitch));
                }

                dev->accel.dx += mach->accel.stepx;
                if ((dev->accel_bpp == 24) && (mach->accel.patt_len == 0x17)) {
                    mach->accel.color_pattern_idx++;
                    if (dev->accel.x3) {
                        if (mach->accel.color_pattern_idx == 9)
                            mach->accel.color_pattern_idx = 3;
                    } else {
                        if (mach->accel.color_pattern_idx == 6)
                            mach->accel.color_pattern_idx = 0;
                    }
                } else if ((dev->accel_bpp == 24) && (mach->accel.patt_len < 3)) {
                    mach->accel.color_pattern_idx++;
                    if (mach->accel.color_pattern_idx == 3)
                        mach->accel.color_pattern_idx = 0;
                } else
                    mach->accel.color_pattern_idx = (mach->accel.color_pattern_idx + mach->accel.stepx) & mach->accel.patt_len;

                dev->accel.sx++;
                if (dev->accel.sx >= mach->accel.width) {
                    dev->accel.sx = 0;
                    dev->accel.dy += mach->accel.stepy;
                    if (dev->bpp)
                        dev->accel.dest = (mach->accel.ge_offset << 1) + (dev->accel.dy * (dev->pitch));
                    else
                        dev->accel.dest = (mach->accel.ge_offset << 2) + (dev->accel.dy * (dev->pitch));
                    if (mach->accel.line_idx == 2) {
                        mach->accel.line_array[0] = dev->accel.dx;
                        mach->accel.line_array[4] = dev->accel.dx;
                    }
                    return;
                }
            }
            break;

        default:
            break;
    }
}

static void
mach_accel_out_pixtrans(mach_t *mach, ibm8514_t *dev, uint16_t val)
{
    int frgd_sel;
    int bkgd_sel;
    int mono_src;

    frgd_sel = (mach->accel.dp_config >> 13) & 7;
    bkgd_sel = (mach->accel.dp_config >> 7) & 3;
    mono_src = (mach->accel.dp_config >> 5) & 3;

    if ((mach->accel.dp_config & 4) && (mach->accel.cmd_type != 5)) {
        val = (val >> 8) | (val << 8);
    }

    switch (mach->accel.dp_config & 0x200) {
        case 0x000: /*8-bit size*/
            if (mono_src == 2) {
                if ((frgd_sel != 2) && (bkgd_sel != 2)) {
                    if (mach->accel.dp_config & 0x1000)
                        val = (val >> 8) | (val << 8);
                    mach_accel_start(mach->accel.cmd_type, 1, 8, val | (val << 16), 0, mach, dev);
                } else
                    mach_accel_start(mach->accel.cmd_type, 1, 1, -1, val | (val << 16), mach, dev);
            } else
                mach_accel_start(mach->accel.cmd_type, 1, 1, -1, val | (val << 16), mach, dev);
            break;
        case 0x200: /*16-bit size*/
            if (mono_src == 2) {
                if ((frgd_sel != 2) && (bkgd_sel != 2)) {
                    if (mach->accel.dp_config & 0x1000)
                        val = (val >> 8) | (val << 8);
                    mach_accel_start(mach->accel.cmd_type, 1, 16, val | (val << 16), 0, mach, dev);
                } else
                    mach_accel_start(mach->accel.cmd_type, 1, 2, -1, val | (val << 16), mach, dev);
            } else
                mach_accel_start(mach->accel.cmd_type, 1, 2, -1, val | (val << 16), mach, dev);
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
    const ibm8514_t *dev  = (ibm8514_t *) svga->dev8514;
    uint8_t          old;
    uint8_t          rs2;
    uint8_t          rs3;

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x1ce:
            mach->index = val;
            break;
        case 0x1cf:
            old                     = mach->regs[mach->index];
            mach->regs[mach->index] = val;
            mach_log("ATI VGA write reg=0x%02X, val=0x%02X\n", mach->index, val);
            switch (mach->index) {
                case 0xa3:
                    if ((old ^ val) & 0x10)
                        svga_recalctimings(svga);
                    break;
                case 0xa7:
                    if ((old ^ val) & 0x80)
                        svga_recalctimings(svga);
                    break;
                case 0xad:
                    if ((dev->local & 0xff) >= 0x02) {
                        if ((old ^ val) & 0x0c)
                            svga_recalctimings(svga);
                    }
                    break;
                case 0xb0:
                    if ((old ^ val) & 0x60)
                        svga_recalctimings(svga);
                    break;
                case 0xae:
                case 0xb2:
                case 0xbe:
                    mach_log("ATI VGA write reg=0x%02X, val=0x%02X\n", mach->index, val);
                    if (mach->regs[0xbe] & 0x08) { /* Read/write bank mode */
                        mach->bank_r = (((mach->regs[0xb2] & 1) << 3) | ((mach->regs[0xb2] & 0xe0) >> 5));
                        mach->bank_w = ((mach->regs[0xb2] & 0x1e) >> 1);
                        if ((dev->local & 0xff) >= 0x02) {
                            mach->bank_r |= ((mach->regs[0xae] & 0x0c) << 2);
                            mach->bank_w |= ((mach->regs[0xae] & 3) << 4);
                        }
                        if (dev->on[0] || dev->on[1])
                            mach_log("Separate B2Bank = %02x, AEbank = %02x.\n", mach->regs[0xb2], mach->regs[0xae]);
                    } else { /* Single bank mode */
                        mach->bank_w = ((mach->regs[0xb2] & 0x1e) >> 1);
                        if ((dev->local & 0xff) >= 0x02) {
                            mach->bank_w |= ((mach->regs[0xae] & 3) << 4);
                        }
                        mach->bank_r = mach->bank_w;
                        if (dev->on[0] || dev->on[1])
                            mach_log("Single B2Bank = %02x, AEbank = %02x.\n", mach->regs[0xb2], mach->regs[0xae]);
                    }
                    svga->read_bank  = mach->bank_r << 16;
                    svga->write_bank = mach->bank_w << 16;

                    if (mach->index == 0xbe) {
                        if ((old ^ val) & 0x10)
                            svga_recalctimings(svga);
                    }
                    break;
                case 0xbd:
                    if ((old ^ val) & 4) {
                        mach32_updatemapping(mach, svga);
                    }
                    break;
                case 0xb3:
                    ati_eeprom_write(&mach->eeprom, val & 8, val & 2, val & 1);
                    break;
                case 0xb6:
                    if ((old ^ val) & 0x10)
                        svga_recalctimings(svga);
                    break;
                case 0xb8:
                    if ((dev->local & 0xff) >= 0x02) {
                        if ((old ^ val) & 0x40)
                            svga_recalctimings(svga);
                    } else {
                        if ((old ^ val) & 0xc0)
                            svga_recalctimings(svga);
                    }
                    break;
                case 0xb9:
                    if ((old ^ val) & 2)
                        svga_recalctimings(svga);
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
            if ((dev->local & 0xff) >= 0x02) {
                if (mach->pci_bus && !mach->ramdac_type)
                    ati68860_ramdac_out((addr & 3) | (rs2 << 2) | (rs3 << 3), val, svga->ramdac, svga);
                else
                    ati68875_ramdac_out(addr, rs2, rs3, val, svga->ramdac, svga);
            } else
                svga_out(addr, val, svga);
            return;

        case 0x3C6:
        case 0x3C7:
        case 0x3C8:
        case 0x3C9:
            rs2 = !!(mach->regs[0xa0] & 0x20);
            rs3 = !!(mach->regs[0xa0] & 0x40);
            if ((dev->local & 0xff) >= 0x02) {
                if (mach->pci_bus && !mach->ramdac_type)
                    ati68860_ramdac_out((addr & 3) | (rs2 << 2) | (rs3 << 3), val, svga->ramdac, svga);
                else
                    ati68875_ramdac_out(addr, rs2, rs3, val, svga->ramdac, svga);
            } else
                svga_out(addr, val, svga);
            return;

        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if (svga->crtcreg & 0x20)
                return;
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80) && !(mach->regs[0xb4] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80) && !(mach->regs[0xb4] & 0x80))
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

static uint8_t
mach_in(uint16_t addr, void *priv)
{
    mach_t          *mach = (mach_t *) priv;
    svga_t          *svga = &mach->svga;
    const ibm8514_t *dev  = (ibm8514_t *) svga->dev8514;
    uint8_t          temp = 0xff;
    uint8_t          rs2;
    uint8_t          rs3;

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

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
                case 0xb0:
                    temp = mach->regs[0xb0] | 0x80;
                    temp &= ~0x18;
                    if ((dev->local & 0xff) >= 0x02) { /*Mach32 VGA 1MB memory*/
                        temp |= 0x08;
                    } else { /*ATI 28800 VGA 512kB memory*/
                        temp |= 0x10;
                    }
                    break;
                case 0xb7:
                    temp = mach->regs[0xb7] & ~8;
                    if (ati_eeprom_read(&mach->eeprom))
                        temp |= 8;
                    break;

                case 0xbd:
                    temp = mach->regs[0xbd] | 0x10;
                    break;

                default:
                    temp = mach->regs[mach->index];
                    break;
            }
            break;

        case 0x2ea:
        case 0x2eb:
        case 0x2ec:
        case 0x2ed:
            rs2 = !!(mach->accel.ext_ge_config & 0x1000);
            rs3 = !!(mach->accel.ext_ge_config & 0x2000);
            if ((dev->local & 0xff) >= 0x02) {
                if (mach->pci_bus && !mach->ramdac_type)
                    temp = ati68860_ramdac_in((addr & 3) | (rs2 << 2) | (rs3 << 3), svga->ramdac, svga);
                else
                    temp = ati68875_ramdac_in(addr, rs2, rs3, svga->ramdac, svga);
            } else
                temp = svga_in(addr, svga);
            break;

        case 0x3C6:
        case 0x3C7:
        case 0x3C8:
        case 0x3C9:
            rs2 = !!(mach->regs[0xa0] & 0x20);
            rs3 = !!(mach->regs[0xa0] & 0x40);
            if ((dev->local & 0xff) >= 0x02) {
                if (mach->pci_bus && !mach->ramdac_type)
                    temp = ati68860_ramdac_in((addr & 3) | (rs2 << 2) | (rs3 << 3), svga->ramdac, svga);
                else
                    temp = ati68875_ramdac_in(addr, rs2, rs3, svga->ramdac, svga);
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


#ifdef ATI_8514_ULTRA
static void
ati8514_out(uint16_t addr, uint8_t val, void *priv)
{
    mach_log("ADDON OUT addr=%03x, val=%02x.\n", addr, val);
    svga_out(addr, val, priv);
}

static uint8_t
ati8514_in(uint16_t addr, void *priv)
{
    uint8_t temp = 0xff;

    temp = svga_in(addr, priv);

    mach_log("ADDON IN addr=%03x, temp=%02x.\n", addr, temp);
    return temp;
}

void
ati8514_recalctimings(svga_t *svga)
{
    const mach_t *mach = (mach_t *) svga->ext8514;
    ibm8514_t    *dev  = (ibm8514_t *) svga->dev8514;

    mach_log("ON0=%d, ON1=%d, vgahdisp=%d.\n", dev->on[0], dev->on[1], svga->hdisp);
    if (dev->on[0] || dev->on[1]) {
        dev->h_disp                     = dev->hdisp;
        dev->h_total                    = dev->htotal + 1;
        dev->h_blankstart               = dev->hblankstart;
        dev->h_blank_end_val            = dev->hblank_end_val;
        dev->v_total                    = dev->vtotal;
        dev->v_syncstart                = dev->vsyncstart;
        dev->rowcount                   = !!(dev->disp_cntl & 0x08);
        dev->dispend                    = dev->vdisp;

        if (dev->dispend == 766)
            dev->dispend += 2;

        if (dev->dispend == 598)
            dev->dispend += 2;

        if (dev->accel.advfunc_cntl & 4) {
            if (dev->h_disp != 800) {
                dev->h_disp = 1024;
                dev->dispend = 768;
            }
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 44900000.0;
        } else {
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 25175000.0;
        }

        if (dev->interlace) {
            dev->dispend >>= 1;
            dev->v_syncstart >>= 2;
            dev->v_total >>= 2;
        } else {
            dev->v_syncstart >>= 1;
            dev->v_total >>= 1;
        }

        dev->pitch = dev->ext_pitch;
        dev->rowoffset = dev->ext_crt_pitch;
        mach_log("cntl=%d, hv(%d,%d), pitch=%d, rowoffset=%d, advfunc_cntl=%x, shadow=%x.\n", dev->accel.advfunc_cntl & 4, dev->h_disp, dev->dispend, dev->pitch, dev->rowoffset, dev->accel.advfunc_cntl & 4, mach->shadow_set & 3);
        svga->map8 = dev->pallook;
        svga->render8514 = ibm8514_render_8bpp;
    } else {
        if (!(svga->gdcreg[6] & 1) && !(svga->attrregs[0x10] & 1)) { /*Text mode*/
            if (svga->seqregs[1] & 8) {                              /*40 column*/
                svga->render = svga_render_text_40;
            } else {
                svga->render = svga_render_text_80;
            }
        }
    }
}
#endif

static void
mach_recalctimings(svga_t *svga)
{
    mach_t       *mach = (mach_t *) svga->priv;
    ibm8514_t    *dev  = (ibm8514_t *) svga->dev8514;
    int           clock_sel;

    if (mach->regs[0xad] & 0x08)
        svga->hblankstart    = ((mach->regs[0x0d] >> 2) << 8) + svga->crtc[2];

    clock_sel = ((svga->miscout >> 2) & 3) | ((mach->regs[0xbe] & 0x10) >> 1) | ((mach->regs[0xb9] & 2) << 1);

    if ((dev->local & 0xff) >= 0x02) {
        if (mach->regs[0xad] & 0x04)
            svga->ma_latch |= 0x40000;

        if (mach->regs[0xad] & 0x08)
            svga->ma_latch |= 0x80000;
    }

    if (mach->regs[0xa3] & 0x10)
        svga->ma_latch |= 0x10000;

    if (mach->regs[0xb0] & 0x40)
        svga->ma_latch |= 0x20000;

    if ((mach->regs[0xb6] & 0x18) >= 0x10) {
        svga->hdisp <<= 1;
        svga->htotal <<= 1;
        svga->dots_per_clock <<= 1;
        svga->rowoffset <<= 1;
        svga->gdcreg[5] &= ~0x40;
    }

    if (mach->regs[0xb0] & 0x20) {
        svga->gdcreg[5] |= 0x40;
        if ((mach->regs[0xb6] & 0x18) >= 0x10)
            svga->packed_4bpp = 1;
        else
            svga->packed_4bpp = 0;
    } else
        svga->packed_4bpp = 0;

    if ((dev->local & 0xff) < 0x02) {
        if ((mach->regs[0xb6] & 0x18) == 8) {
            svga->hdisp <<= 1;
            svga->htotal <<= 1;
            svga->dots_per_clock <<= 1;
            svga->ati_4color = 1;
        } else
            svga->ati_4color = 0;
    }

    svga->render8514 = ibm8514_render_blank;
    mach_log("ON[0]=%d, ON[1]=%d, exton[0]=%d, exton[1]=%d, vendormode0=%d, vendormode1=%d.\n", dev->on[0], dev->on[1], mach->ext_on[0], mach->ext_on[1], dev->vendor_mode[0], dev->vendor_mode[1]);
    if (dev->on[0] || dev->on[1]) {
        mach_log("8514/A ON.\n");
        dev->h_total                    = dev->htotal + 1;
        dev->h_blankstart               = dev->hblankstart;
        dev->h_blank_end_val            = dev->hblank_end_val;
        dev->v_total                    = dev->v_total_reg + 1;
        dev->v_syncstart                = dev->v_sync_start + 1;
        dev->rowcount                   = !!(dev->disp_cntl & 0x08);

        mach_log("VDISP=%d.\n", dev->vdisp);
        if ((dev->hdisp == 800) || (dev->hdisp == 1280)) {
            /*For VESA modes in ATI 8514/A mode.*/
            dev->h_disp = dev->hdisp;
            dev->dispend = dev->vdisp;
        } else {
            /*Default modes for 8514/A mode*/
            if (dev->accel.advfunc_cntl & 0x04) {
                dev->h_disp = 1024;
                dev->dispend = 768;
            } else {
                dev->h_disp = 640;
                dev->dispend = 480;
            }
        }

        if (dev->dispend == 766)
            dev->dispend += 2;

        if (dev->dispend == 598)
            dev->dispend += 2;

        if (dev->dispend == 478)
            dev->dispend += 2;

        if ((dev->local & 0xff) >= 0x02) {
            if ((dev->accel.advfunc_cntl ^ dev->modechange) & 0x04) {
                if ((mach->shadow_set ^ mach->compat_mode) & 0x03)
                    svga->clock8514 = (cpuclock * (double) (1ULL << 32)) / svga->getclock((mach->accel.clock_sel >> 2) & 0x0f, svga->clock_gen);
                else
                    svga->clock8514 = (cpuclock * (double) (1ULL << 32)) / 44900000.0;
            } else {
                if ((mach->shadow_set ^ mach->compat_mode) & 0x03)
                    svga->clock8514 = (cpuclock * (double) (1ULL << 32)) / svga->getclock((mach->accel.clock_sel >> 2) & 0x0f, svga->clock_gen);
                else
                    svga->clock8514 = (cpuclock * (double) (1ULL << 32)) / 25175000.0;
            }

            if (dev->interlace) {
                dev->dispend >>= 1;
                dev->v_syncstart >>= 2;
                dev->v_total >>= 2;
            } else {
                dev->v_syncstart >>= 1;
                dev->v_total >>= 1;
            }

            mach_log("HDISP=%d.\n", dev->h_disp);
            dev->pitch = dev->ext_pitch;
            dev->rowoffset = dev->ext_crt_pitch;
            if ((mach->accel.ext_ge_config & 0x800) || (!(mach->accel.ext_ge_config & 0x8000) && !(mach->accel.ext_ge_config & 0x800))) {
                if ((mach->accel.ext_ge_config & 0x30) == 0x20) {
                    if ((mach->accel.ext_ge_config & 0xc0) == 0x40)
                        dev->accel_bpp = 16;
                    else
                        dev->accel_bpp = 15;
                } else if ((mach->accel.ext_ge_config & 0x30) == 0x30) {
                    if (mach->accel.ext_ge_config & 0x200)
                        dev->accel_bpp = 32;
                    else
                        dev->accel_bpp = 24;
                } else
                    dev->accel_bpp = 8;

                mach_log("hv(%d,%d), pitch=%d, rowoffset=%d, gextconfig=%03x, bpp=%d, shadow=%x, vgahdisp=%d.\n", dev->h_disp, dev->dispend, dev->pitch, dev->ext_crt_pitch, mach->accel.ext_ge_config & 0xcec0, dev->accel_bpp, mach->shadow_set & 3, svga->hdisp);
                switch (dev->accel_bpp) {
                    case 8:
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
                        if (mach->accel.ext_ge_config & 0x400)
                            svga->render8514 = ibm8514_render_BGR;
                        else
                            svga->render8514 = ibm8514_render_24bpp;
                        break;
                    case 32:
                        mach_log("GEConfig32bpp: %03x.\n", mach->accel.ext_ge_config & 0x600);
                        if (mach->accel.ext_ge_config & 0x400)
                            svga->render8514 = ibm8514_render_ABGR8888;
                        else
                            svga->render8514 = ibm8514_render_32bpp;
                        break;

                    default:
                        break;
                }
            }
            switch (mach->regs[0xb8] & 0xc0) {
                case 0x40:
                    svga->clock8514 *= 2;
                    break;
                case 0x80:
                    svga->clock8514 *= 3;
                    break;
                case 0xc0:
                    svga->clock8514 *= 4;
                    break;

                default:
                    break;
            }
        } else {
            if ((dev->accel.advfunc_cntl ^ dev->modechange) & 0x04) {
                if ((mach->shadow_set ^ mach->compat_mode) & 0x03)
                    svga->clock8514 = (cpuclock * (double) (1ULL << 32)) / svga->getclock((mach->accel.clock_sel >> 2) & 0x0f, svga->clock_gen);
                else
                    svga->clock8514 = (cpuclock * (double) (1ULL << 32)) / 44900000.0;
            } else {
                if ((mach->shadow_set ^ mach->compat_mode) & 0x03)
                    svga->clock8514 = (cpuclock * (double) (1ULL << 32)) / svga->getclock((mach->accel.clock_sel >> 2) & 0x0f, svga->clock_gen);
                else
                    svga->clock8514 = (cpuclock * (double) (1ULL << 32)) / 25175000.0;
            }

            if (dev->interlace) {
                dev->dispend >>= 1;
                dev->v_syncstart >>= 2;
                dev->v_total >>= 2;
            } else {
                dev->v_syncstart >>= 1;
                dev->v_total >>= 1;
            }

            dev->pitch = dev->ext_pitch;
            dev->rowoffset = dev->ext_crt_pitch;
            mach_log("cntl=%d, hv(%d,%d), pitch=%d, rowoffset=%d, gextconfig=%03x, shadow=%x interlace=%d.\n", dev->accel.advfunc_cntl & 4, dev->h_disp, dev->dispend, dev->pitch, dev->rowoffset, mach->accel.ext_ge_config & 0xcec0, mach->shadow_set & 3, dev->interlace);
            svga->map8 = dev->pallook;
            svga->render8514 = ibm8514_render_8bpp;
            if (mach->regs[0xb8] & 0x40)
                svga->clock8514 *= 2;
        }
    }

    if (!svga->scrblank && (svga->crtc[0x17] & 0x80) && svga->attr_palette_enable) {
        if (((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1))) {
            mach_log("VGA clock=%02x.\n", mach->regs[0xa7] & 0x80);
            svga->clock = (cpuclock * (double) (1ULL << 32)) / svga->getclock(clock_sel, svga->clock_gen);
            if (mach->regs[0xa7] & 0x80)
                svga->clock *= 3;
            switch (svga->gdcreg[5] & 0x60) {
                case 0x00:
                    if (svga->seqregs[1] & 8) /*Low res (320)*/
                        svga->render = svga_render_4bpp_lowres;
                    else
                        svga->render = svga_render_4bpp_highres;
                    break;
                case 0x20:                    /*4 colours*/
                    if (svga->seqregs[1] & 8) /*Low res (320)*/
                        svga->render = svga_render_2bpp_lowres;
                    else
                        svga->render = svga_render_2bpp_highres;
                    break;
                case 0x40:
                case 0x60: /*256+ colours*/
                    switch (svga->bpp) {
                        default:
                        case 8:
                            svga->map8 = svga->pallook;
                            mach_log("Lowres=%x, seqreg[1]bit3=%x.\n", svga->lowres, svga->seqregs[1] & 8);
                            if (svga->lowres)
                                svga->render = svga_render_8bpp_lowres;
                            else {
                                svga->render = svga_render_8bpp_highres;
                                if (!svga->packed_4bpp) {
                                    svga->ma_latch <<= 1;
                                    svga->rowoffset <<= 1;
                                }
                            }
                            break;
                    }
                    break;

                default:
                    break;
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

    mach_log("[%04X:%08X]: Port FIFO OUT=%04x, val=%04x, len=%d.\n", CS, cpu_state.pc, port, val, len);

    switch (port) {
        case 0x82e8:
        case 0xc2e8:
        case 0xf6ee:
            if (len == 1)
                dev->accel.cur_y = (dev->accel.cur_y & 0x700) | val;
            else
                dev->accel.cur_y = val & 0x7ff;
            break;
        case 0x82e9:
        case 0xc2e9:
        case 0xf6ef:
            if (len == 1)
                dev->accel.cur_y = (dev->accel.cur_y & 0xff) | ((val & 0x07) << 8);
            break;

        case 0x86e8:
        case 0xc6e8:
            if (len == 1)
                dev->accel.cur_x = (dev->accel.cur_x & 0x700) | val;
            else
                dev->accel.cur_x = val & 0x7ff;
            break;
        case 0x86e9:
        case 0xc6e9:
            if (len == 1) {
                dev->accel.cur_x = (dev->accel.cur_x & 0xff) | ((val & 0x07) << 8);
            }
            break;

        case 0x8ae8:
        case 0xcae8:
            if (len == 1)
                dev->accel.desty_axstp = (dev->accel.desty_axstp & 0x3f00) | val;
            else {
                mach->accel.src_y      = val;
                dev->accel.desty       = val & 0x07ff;
                dev->accel.desty_axstp = val & 0x3fff;
                if (val & 0x2000)
                    dev->accel.desty_axstp |= ~0x1fff;
            }
            break;
        case 0x8ae9:
        case 0xcae9:
            if (len == 1) {
                dev->accel.desty_axstp = (dev->accel.desty_axstp & 0xff) | ((val & 0x3f) << 8);
                if (val & 0x20)
                    dev->accel.desty_axstp |= ~0x1fff;
            }
            break;

        case 0x8ee8:
        case 0xcee8:
            if (len == 1)
                dev->accel.destx_distp = (dev->accel.destx_distp & 0x3f00) | val;
            else {
                mach->accel.src_x      = val;
                dev->accel.destx       = val & 0x07ff;
                dev->accel.destx_distp = val & 0x3fff;
                if (val & 0x2000)
                    dev->accel.destx_distp |= ~0x1fff;
            }
            break;
        case 0x8ee9:
        case 0xcee9:
            if (len == 1) {
                dev->accel.destx_distp = (dev->accel.destx_distp & 0xff) | ((val & 0x3f) << 8);
                if (val & 0x20)
                    dev->accel.destx_distp |= ~0x1fff;
            }
            break;

        case 0x92e8:
            if (len != 1)
                dev->test = val;
            fallthrough;

        case 0xd2e8:
            if (len == 1)
                dev->accel.err_term = (dev->accel.err_term & 0x3f00) | val;
            else {
                dev->accel.err_term = val & 0x3fff;
                if (val & 0x2000)
                    dev->accel.err_term |= ~0x1fff;
            }
            break;
        case 0x92e9:
        case 0xd2e9:
            if (len == 1) {
                dev->accel.err_term = (dev->accel.err_term & 0xff) | ((val & 0x3f) << 8);
                if (val & 0x20)
                    dev->accel.err_term |= ~0x1fff;
            }
            break;

        case 0x96e8:
        case 0xd6e8:
            if (len == 1)
                dev->accel.maj_axis_pcnt = (dev->accel.maj_axis_pcnt & 0x0700) | val;
            else {
                mach->accel.test         = val & 0x1fff;
                dev->accel.maj_axis_pcnt = val & 0x07ff;
                dev->accel.maj_axis_pcnt_no_limit = val;
            }
            break;
        case 0x96e9:
        case 0xd6e9:
            if (len == 1) {
                dev->accel.maj_axis_pcnt = (dev->accel.maj_axis_pcnt & 0xff) | ((val & 0x07) << 8);
            }
            break;

        case 0x9ae8:
        case 0xdae8:
            dev->accel.ssv_state = 0;
            if (len == 1)
                dev->accel.cmd = (dev->accel.cmd & 0xff00) | val;
            else {
                dev->data_available  = 0;
                dev->data_available2 = 0;
                dev->accel.cmd       = val;
                mach_log("CMD8514=%04x, len=%d, pixcntl=%02x.\n", val, len, dev->accel.multifunc[0x0a]);
                mach->accel.cmd_type = -1;
                if (port == 0xdae8) {
                    if (dev->accel.cmd & 0x100)
                        dev->accel.cmd_back = 0;
                }
                ibm8514_accel_start(-1, 0, -1, 0, svga, len);
            }
            break;
        case 0x9ae9:
        case 0xdae9:
            if (len == 1) {
                dev->data_available  = 0;
                dev->data_available2 = 0;
                dev->accel.cmd       = (dev->accel.cmd & 0xff) | (val << 8);
                mach->accel.cmd_type = -1;
                if (port == 0xdae9) {
                    if (dev->accel.cmd & 0x100)
                        dev->accel.cmd_back = 0;
                }
                ibm8514_accel_start(-1, 0, -1, 0, svga, len);
            }
            break;

        case 0x9ee8:
        case 0xdee8:
            dev->accel.ssv_state = 1;
            if (len == 1)
                dev->accel.short_stroke = (dev->accel.short_stroke & 0xff00) | val;
            else {
                dev->accel.short_stroke = val;
                dev->accel.cx           = dev->accel.cur_x;
                dev->accel.cy           = dev->accel.cur_y;

                if (dev->accel.cur_x >= 0x600) {
                    dev->accel.cx |= ~0x5ff;
                }
                if (dev->accel.cur_y >= 0x600) {
                    dev->accel.cy |= ~0x5ff;
                }

                if (dev->accel.cmd & 0x1000) {
                    ibm8514_short_stroke_start(-1, 0, -1, 0, svga, dev->accel.short_stroke & 0xff, len);
                    ibm8514_short_stroke_start(-1, 0, -1, 0, svga, dev->accel.short_stroke >> 8, len);
                } else {
                    ibm8514_short_stroke_start(-1, 0, -1, 0, svga, dev->accel.short_stroke >> 8, len);
                    ibm8514_short_stroke_start(-1, 0, -1, 0, svga, dev->accel.short_stroke & 0xff, len);
                }
            }
            break;
        case 0x9ee9:
        case 0xdee9:
            if (len == 1) {
                dev->accel.short_stroke = (dev->accel.short_stroke & 0xff) | (val << 8);
                dev->accel.cx           = dev->accel.cur_x;
                dev->accel.cy           = dev->accel.cur_y;

                if (dev->accel.cur_x >= 0x600) {
                    dev->accel.cx |= ~0x5ff;
                }
                if (dev->accel.cur_y >= 0x600) {
                    dev->accel.cy |= ~0x5ff;
                }

                if (dev->accel.cmd & 0x1000) {
                    ibm8514_short_stroke_start(-1, 0, -1, 0, svga, dev->accel.short_stroke & 0xff, len);
                    ibm8514_short_stroke_start(-1, 0, -1, 0, svga, dev->accel.short_stroke >> 8, len);
                } else {
                    ibm8514_short_stroke_start(-1, 0, -1, 0, svga, dev->accel.short_stroke >> 8, len);
                    ibm8514_short_stroke_start(-1, 0, -1, 0, svga, dev->accel.short_stroke & 0xff, len);
                }
            }
            break;

        case 0xa2e8:
        case 0xe2e8:
            if (port == 0xe2e8) {
                if (dev->accel.cmd_back) {
                    if (len == 1)
                        dev->accel.bkgd_color = (dev->accel.bkgd_color & 0x00ff) | val;
                    else
                        dev->accel.bkgd_color = val;
                } else {
                    if (len == 1) {
                        if (mach->accel.cmd_type >= 0) {
                            if (mach_pixel_read(mach))
                                break;
                            mach->accel.pix_trans[1] = val;
                        }
                    } else {
                        if (mach->accel.cmd_type >= 0) {
                            if (mach_pixel_read(mach))
                                break;
                            mach_accel_out_pixtrans(mach, dev, val);
                        } else {
                            if (ibm8514_cpu_dest(svga))
                                break;
                            ibm8514_accel_out_pixtrans(svga, port, val, len);
                        }
                    }
                }
            } else {
                if (len == 1)
                    dev->accel.bkgd_color = (dev->accel.bkgd_color & 0x00ff) | val;
                else
                    dev->accel.bkgd_color = val;
            }
            break;
        case 0xa2e9:
        case 0xe2e9:
            if (port == 0xe2e9) {
                if (dev->accel.cmd_back) {
                    if (len == 1)
                        dev->accel.bkgd_color = (dev->accel.bkgd_color & 0xff00) | (val << 8);
                } else {
                    if (len == 1) {
                        if (mach->accel.cmd_type >= 0) {
                            if (mach_pixel_read(mach))
                                break;
                            mach->accel.pix_trans[0] = val;
                            frgd_sel                 = (mach->accel.dp_config >> 13) & 7;
                            bkgd_sel                 = (mach->accel.dp_config >> 7) & 3;
                            mono_src                 = (mach->accel.dp_config >> 5) & 3;

                            switch (mach->accel.dp_config & 0x200) {
                                case 0x000: /*8-bit size*/
                                    if (mono_src == 2) {
                                        if ((frgd_sel != 2) && (bkgd_sel != 2)) {
                                            mach_accel_start(mach->accel.cmd_type, 1, 8, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), 0, mach, dev);
                                        } else
                                            mach_accel_start(mach->accel.cmd_type, 1, 1, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), mach, dev);
                                    } else
                                        mach_accel_start(mach->accel.cmd_type, 1, 1, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), mach, dev);
                                    break;
                                case 0x200: /*16-bit size*/
                                    if (mono_src == 2) {
                                        if ((frgd_sel != 2) && (bkgd_sel != 2)) {
                                            if (mach->accel.dp_config & 0x1000)
                                                mach_accel_start(mach->accel.cmd_type, 1, 16, mach->accel.pix_trans[1] | (mach->accel.pix_trans[0] << 8), 0, mach, dev);
                                            else
                                                mach_accel_start(mach->accel.cmd_type, 1, 16, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), 0, mach, dev);
                                        } else
                                            mach_accel_start(mach->accel.cmd_type, 1, 2, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), mach, dev);
                                    } else
                                        mach_accel_start(mach->accel.cmd_type, 1, 2, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), mach, dev);
                                    break;

                                default:
                                    break;
                            }
                        }
                    }
                }
            } else {
                if (len == 1)
                    dev->accel.bkgd_color = (dev->accel.bkgd_color & 0xff00) | (val << 8);
            }
            break;

        case 0xa6e8:
        case 0xe6e8:
            if (port == 0xe6e8) {
                if (dev->accel.cmd_back) {
                    if (len == 1)
                        dev->accel.frgd_color = (dev->accel.frgd_color & 0x00ff) | val;
                    else
                        dev->accel.frgd_color = val;
                } else {
                    if (len == 1) {
                        if (mach->accel.cmd_type >= 0) {
                            if (mach_pixel_read(mach))
                                break;
                            mach->accel.pix_trans[1] = val;
                        }
                    } else {
                        if (mach->accel.cmd_type >= 0) {
                            if (mach_pixel_read(mach))
                                break;
                            mach_accel_out_pixtrans(mach, dev, val);
                        } else {
                            if (ibm8514_cpu_dest(svga))
                                break;
                            ibm8514_accel_out_pixtrans(svga, port, val, len);
                        }
                    }
                }
            } else {
                if (len == 1)
                    dev->accel.frgd_color = (dev->accel.frgd_color & 0x00ff) | val;
                else
                    dev->accel.frgd_color = val;
            }
            break;
        case 0xa6e9:
        case 0xe6e9:
            if (port == 0xe6e9) {
                if (dev->accel.cmd_back) {
                    if (len == 1)
                        dev->accel.frgd_color = (dev->accel.frgd_color & 0xff00) | (val << 8);
                } else {
                    if (len == 1) {
                        if (mach->accel.cmd_type >= 0) {
                            if (mach_pixel_read(mach))
                                break;
                            mach->accel.pix_trans[0] = val;
                            frgd_sel                 = (mach->accel.dp_config >> 13) & 7;
                            bkgd_sel                 = (mach->accel.dp_config >> 7) & 3;
                            mono_src                 = (mach->accel.dp_config >> 5) & 3;

                            switch (mach->accel.dp_config & 0x200) {
                                case 0x000: /*8-bit size*/
                                    if (mono_src == 2) {
                                        if ((frgd_sel != 2) && (bkgd_sel != 2)) {
                                            mach_accel_start(mach->accel.cmd_type, 1, 8, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), 0, mach, dev);
                                        } else
                                            mach_accel_start(mach->accel.cmd_type, 1, 1, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), mach, dev);
                                    } else
                                        mach_accel_start(mach->accel.cmd_type, 1, 1, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), mach, dev);
                                    break;
                                case 0x200: /*16-bit size*/
                                    if (mono_src == 2) {
                                        if ((frgd_sel != 2) && (bkgd_sel != 2)) {
                                            if (mach->accel.dp_config & 0x1000)
                                                mach_accel_start(mach->accel.cmd_type, 1, 16, mach->accel.pix_trans[1] | (mach->accel.pix_trans[0] << 8), 0, mach, dev);
                                            else
                                                mach_accel_start(mach->accel.cmd_type, 1, 16, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), 0, mach, dev);
                                        } else
                                            mach_accel_start(mach->accel.cmd_type, 1, 2, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), mach, dev);
                                    } else
                                        mach_accel_start(mach->accel.cmd_type, 1, 2, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), mach, dev);
                                    break;

                                default:
                                    break;
                            }
                        }
                    }
                }
            } else {
                if (len == 1)
                    dev->accel.frgd_color = (dev->accel.frgd_color & 0xff00) | (val << 8);
            }
            break;

        case 0xaae8:
        case 0xeae8:
            if (len == 1)
                dev->accel.wrt_mask = (dev->accel.wrt_mask & 0x00ff) | val;
            else {
                dev->accel.wrt_mask = val;
                mach_log("WrtMask=%04x.\n", val);
            }
            break;
        case 0xaae9:
        case 0xeae9:
            if (len == 1)
                dev->accel.wrt_mask = (dev->accel.wrt_mask & 0xff00) | (val << 8);
            break;

        case 0xaee8:
        case 0xeee8:
            if (len == 1)
                dev->accel.rd_mask = (dev->accel.rd_mask & 0x00ff) | val;
            else {
                dev->accel.rd_mask = val;
                mach_log("ReadMask=%04x.\n", val);
            }
            break;
        case 0xaee9:
        case 0xeee9:
            if (len == 1)
                dev->accel.rd_mask = (dev->accel.rd_mask & 0xff00) | (val << 8);
            break;

        case 0xb2e8:
        case 0xf2e8:
            if (len == 1)
                dev->accel.color_cmp = (dev->accel.color_cmp & 0x00ff) | val;
            else
                dev->accel.color_cmp = val;
            break;
        case 0xb2e9:
        case 0xf2e9:
            if (len == 1)
                dev->accel.color_cmp = (dev->accel.color_cmp & 0xff00) | (val << 8);
            break;

        case 0xb6e8:
        case 0xf6e8:
            dev->accel.bkgd_mix = val & 0xff;
            break;

        case 0xbae8:
        case 0xfae8:
            dev->accel.frgd_mix = val & 0xff;
            break;

        case 0xbee8:
        case 0xfee8:
            if (len == 1)
                dev->accel.multifunc_cntl = (dev->accel.multifunc_cntl & 0xff00) | val;
            else {
                dev->accel.multifunc_cntl                             = val;
                dev->accel.multifunc[dev->accel.multifunc_cntl >> 12] = dev->accel.multifunc_cntl & 0xfff;
                if ((dev->accel.multifunc_cntl >> 12) == 1)
                    dev->accel.clip_top = val & 0x7ff;

                if ((dev->accel.multifunc_cntl >> 12) == 2)
                    dev->accel.clip_left = val & 0x7ff;

                if ((dev->accel.multifunc_cntl >> 12) == 3)
                    dev->accel.multifunc[3] = val & 0x7ff;

                if ((dev->accel.multifunc_cntl >> 12) == 4)
                    dev->accel.multifunc[4] = val & 0x7ff;

                mach_log("CLIPBOTTOM=%d, CLIPRIGHT=%d, bpp=%d, pitch=%d.\n", dev->accel.multifunc[3], dev->accel.multifunc[4], dev->accel_bpp, dev->pitch);
                if ((dev->accel.multifunc_cntl >> 12) == 5) {
                    if ((dev->local & 0xff) < 0x02)
                        dev->ext_crt_pitch = 128;
                }
                if (port == 0xfee8)
                    dev->accel.cmd_back = 1;
                else
                    dev->accel.cmd_back = 0;
            }
            break;
        case 0xbee9:
        case 0xfee9:
            if (len == 1) {
                dev->accel.multifunc_cntl                             = (dev->accel.multifunc_cntl & 0xff) | (val << 8);
                dev->accel.multifunc[dev->accel.multifunc_cntl >> 12] = dev->accel.multifunc_cntl & 0xfff;
                if ((dev->accel.multifunc_cntl >> 12) == 1)
                    dev->accel.clip_top = dev->accel.multifunc_cntl & 0x7ff;

                if ((dev->accel.multifunc_cntl >> 12) == 2)
                    dev->accel.clip_left = dev->accel.multifunc_cntl & 0x7ff;

                if ((dev->accel.multifunc_cntl >> 12) == 5) {
                    if ((dev->local & 0xff) < 0x02)
                        dev->ext_crt_pitch = 128;
                }
                if (port == 0xfee9)
                    dev->accel.cmd_back = 1;
                else
                    dev->accel.cmd_back = 0;
            }
            break;

            /*ATI Mach8/32 specific registers*/
        case 0x82ee:
            mach->accel.patt_data_idx = val & 0x1f;
            mach_log("Pattern Data Index = %d.\n", val & 0x1f);
            break;

        case 0x8eee:
            if (len == 1) {
                mach->accel.patt_data[mach->accel.patt_data_idx] = val;
            } else {
                mach->accel.patt_data[mach->accel.patt_data_idx]     = val & 0xff;
                mach->accel.patt_data[mach->accel.patt_data_idx + 1] = (val >> 8) & 0xff;
                if (mach->accel.mono_pattern_enable)
                    mach->accel.patt_data_idx = (mach->accel.patt_data_idx + 2) & 0x17;
                else {
                    frgd_sel = (mach->accel.dp_config >> 13) & 7;
                    mono_src = (mach->accel.dp_config >> 5) & 3;
                    if ((dev->accel_bpp == 24) && (mach->accel.patt_len == 0x17) && (frgd_sel == 5)) {
                        mach->accel.patt_data_idx += 2;
                        dev->accel.y1 = 1;
                    } else {
                        if (dev->accel_bpp == 24)
                            mach->accel.patt_data_idx += 2;
                        else
                            mach->accel.patt_data_idx = (mach->accel.patt_data_idx + 2) & mach->accel.patt_len;
                    }
                    mach_log("ExtCONFIG = %04x, Pattern Mono = %04x, selidx = %d, dataidx = %d, bit 0 = %02x len = %d.\n", mach->accel.ext_ge_config, val, mach->accel.patt_idx, mach->accel.patt_data_idx, val & 1, mach->accel.patt_len);
                }
            }
            break;
        case 0x8eef:
            if (len == 1) {
                mach->accel.patt_data[mach->accel.patt_data_idx + 1] = val;
                if (mach->accel.mono_pattern_enable)
                    mach->accel.patt_data_idx = (mach->accel.patt_data_idx + 2) & 7;
                else {
                    frgd_sel = (mach->accel.dp_config >> 13) & 7;
                    if ((dev->accel_bpp == 24) && (mach->accel.patt_len == 0x17) && (frgd_sel == 5)) {
                        mach->accel.patt_data_idx += 2;
                        dev->accel.y1 = 1;
                    } else
                        mach->accel.patt_data_idx = (mach->accel.patt_data_idx + 2) & mach->accel.patt_len;
                }
            }
            break;

        case 0x96ee:
            if (len == 1)
                mach->accel.bres_count = (mach->accel.bres_count & 0x700) | val;
            else {
                mach->accel.bres_count = val & 0x7ff;
                mach_log("BresenhamDraw = %04x.\n", mach->accel.dp_config);
                dev->data_available  = 0;
                dev->data_available2 = 0;
                mach->accel.cmd_type = 1;
                mach_accel_start(mach->accel.cmd_type, 0, -1, -1, 0, mach, dev);
            }
            break;
        case 0x96ef:
            if (len == 1) {
                mach->accel.bres_count = (mach->accel.bres_count & 0xff) | ((val & 0x07) << 8);
                mach_log("96EE (2) line draw.\n");
                dev->data_available  = 0;
                dev->data_available2 = 0;
                mach->accel.cmd_type = 1;
                mach_accel_start(mach->accel.cmd_type, 0, -1, -1, 0, mach, dev);
            }
            break;

        case 0x9aee:
            mach->accel.line_idx = val & 0x07;
            break;

        case 0xa2ee:
            mach_log("Line OPT = %04x\n", val);
            if (len == 1)
                mach->accel.linedraw_opt = (mach->accel.linedraw_opt & 0xff00) | val;
            else {
                mach->accel.linedraw_opt = val;
                mach->accel.bbottom = dev->accel.multifunc[3] & 0x7ff;
                mach->accel.btop = dev->accel.clip_top & 0x7ff;
                mach->accel.bleft = dev->accel.clip_left & 0x7ff;
                mach->accel.bright = dev->accel.multifunc[4] & 0x7ff;
                if (mach->accel.linedraw_opt & 0x100) {
                    mach->accel.bbottom = 2047;
                    mach->accel.btop = 0;
                    mach->accel.bleft = 0;
                    mach->accel.bright = 2047;
                }
            }
            break;
        case 0xa2ef:
            if (len == 1) {
                mach->accel.linedraw_opt = (mach->accel.linedraw_opt & 0x00ff) | (val << 8);
            }
            break;

        case 0xa6ee:
            if (len == 1)
                mach->accel.dest_x_start = (mach->accel.dest_x_start & 0x700) | val;
            else
                mach->accel.dest_x_start = val & 0x7ff;
            break;
        case 0xa6ef:
            if (len == 1)
                mach->accel.dest_x_start = (mach->accel.dest_x_start & 0x0ff) | ((val & 0x07) << 8);
            break;

        case 0xaaee:
            if (len == 1)
                mach->accel.dest_x_end = (mach->accel.dest_x_end & 0x700) | val;
            else {
                mach->accel.dest_x_end = val & 0x7ff;
            }
            break;
        case 0xaaef:
            if (len == 1)
                mach->accel.dest_x_end = (mach->accel.dest_x_end & 0x0ff) | ((val & 0x07) << 8);
            break;

        case 0xaeee:
            if (len == 1)
                mach->accel.dest_y_end = (mach->accel.dest_y_end & 0x700) | val;
            else {
                mach->accel.dest_y_end = val & 0x7ff;
                if ((val + 1) == 0x10000) {
                    mach_log("Dest_Y_end overflow val = %04x\n", val);
                    mach->accel.dest_y_end = 0;
                }
                dev->data_available  = 0;
                dev->data_available2 = 0;
                mach_log("BitBLT = %04x.\n", mach->accel.dp_config);
                mach->accel.cmd_type = 2; /*Non-conforming BitBLT from dest_y_end register (0xaeee)*/
                mach_accel_start(mach->accel.cmd_type, 0, -1, -1, 0, mach, dev);
            }
            break;
        case 0xaeef:
            if (len == 1) {
                mach->accel.dest_y_end = (mach->accel.dest_y_end & 0x0ff) | ((val & 0x07) << 8);
                dev->data_available    = 0;
                dev->data_available2   = 0;
                mach->accel.cmd_type   = 2; /*Non-conforming BitBLT from dest_y_end register (0xaeee)*/
                mach_accel_start(mach->accel.cmd_type, 0, -1, -1, 0, mach, dev);
            }
            break;

        case 0xb2ee:
            if (len == 1)
                mach->accel.src_x_start = (mach->accel.src_x_start & 0x700) | val;
            else
                mach->accel.src_x_start = val & 0x7ff;
            break;
        case 0xb2ef:
            if (len == 1)
                mach->accel.src_x_start = (mach->accel.src_x_start & 0x0ff) | ((val & 0x07) << 8);
            break;

        case 0xb6ee:
            dev->accel.bkgd_mix = val & 0xff;
            break;

        case 0xbaee:
            dev->accel.frgd_mix = val & 0xff;
            break;

        case 0xbeee:
            if (len == 1)
                mach->accel.src_x_end = (mach->accel.src_x_end & 0x700) | val;
            else {
                mach->accel.src_x_end = val & 0x7ff;
            }
            break;
        case 0xbeef:
            if (len == 1)
                mach->accel.src_x_end = (mach->accel.src_x_end & 0x0ff) | ((val & 0x07) << 8);
            break;

        case 0xc2ee:
            mach->accel.src_y_dir = val & 1;
            break;

        case 0xc6ee:
            mach->accel.cmd_type = 0;
            mach_log("TODO: Short Stroke.\n");
            break;

        case 0xcaee:
            if (len == 1)
                mach->accel.scan_to_x = (mach->accel.scan_to_x & 0x700) | val;
            else {
                mach->accel.scan_to_x = (val & 0x7ff);
                if ((val + 1) == 0x10000) {
                    mach_log("Scan_to_X overflow val = %04x\n", val);
                    mach->accel.scan_to_x = 0;
                }
                dev->data_available  = 0;
                dev->data_available2 = 0;
                mach->accel.cmd_type = 5; /*Horizontal Raster Draw from scan_to_x register (0xcaee)*/
                mach_log("ScanToX = %04x.\n", mach->accel.dp_config);
                mach_accel_start(mach->accel.cmd_type, 0, -1, -1, 0, mach, dev);
            }
            break;
        case 0xcaef:
            if (len == 1) {
                mach->accel.scan_to_x = (mach->accel.scan_to_x & 0x0ff) | ((val & 0x07) << 8);
                dev->data_available   = 0;
                dev->data_available2  = 0;
                mach->accel.cmd_type  = 5; /*Horizontal Raster Draw from scan_to_x register (0xcaee)*/
                mach_accel_start(mach->accel.cmd_type, 0, -1, -1, 0, mach, dev);
            }
            break;

        case 0xceee:
            mach_log("CEEE write val = %04x.\n", val);
            if (len == 1)
                mach->accel.dp_config = (mach->accel.dp_config & 0xff00) | val;
            else {
                dev->data_available  = 0;
                dev->data_available2 = 0;
                mach->accel.dp_config = val;
            }
            break;
        case 0xceef:
            if (len == 1) {
                mach->accel.dp_config = (mach->accel.dp_config & 0x00ff) | (val << 8);
            }
            break;

        case 0xd2ee:
            mach->accel.patt_len = val & 0x1f;
            mach_log("Pattern Length = %d, val = %04x.\n", val & 0x1f, val);
            mach->accel.mono_pattern_enable = !!(val & 0x80);
            if (len != 1) {
                mach->accel.patt_len_reg = val;
            } else {
                mach->accel.patt_len_reg = (mach->accel.patt_len_reg & 0xff00) | val;
            }
            break;
        case 0xd2ef:
            if (len == 1)
                mach->accel.patt_len_reg = (mach->accel.patt_len_reg & 0x00ff) | (val << 8);
            break;

        case 0xd6ee:
            mach->accel.patt_idx = val & 0x1f;
            mach_log("Pattern Index = %d, val = %02x.\n", val & 0x1f, val);
            break;

        case 0xdaee:
            mach_log("DAEE (extclipl) write val = %d\n", val);
            if (len == 1)
                dev->accel.clip_left = (dev->accel.clip_left & 0x700) | val;
            else {
                dev->accel.clip_left = val & 0x7ff;
            }
            break;
        case 0xdaef:
            if (len == 1)
                dev->accel.clip_left = (dev->accel.clip_left & 0x0ff) | ((val & 0x07) << 8);
            break;

        case 0xdeee:
            mach_log("DEEE (extclipt) write val = %d\n", val);
            if (len == 1)
                dev->accel.clip_top = (dev->accel.clip_top & 0x700) | val;
            else {
                dev->accel.clip_top = val & 0x7ff;
            }
            break;
        case 0xdeef:
            if (len == 1)
                dev->accel.clip_top = (dev->accel.clip_top & 0x0ff) | ((val & 0x07) << 8);
            break;

        case 0xe2ee:
            mach_log("E2EE (extclipr) write val = %d\n", val);
            if (len == 1)
                dev->accel.multifunc[4] = (dev->accel.multifunc[4] & 0x700) | val;
            else {
                dev->accel.multifunc[4] = val & 0x7ff;
            }
            break;
        case 0xe2ef:
            if (len == 1)
                dev->accel.multifunc[4] = (dev->accel.multifunc[4] & 0x0ff) | ((val & 0x07) << 8);
            break;

        case 0xe6ee:
            mach_log("E6EE (extclipb) write val = %d\n", val);
            if (len == 1)
                dev->accel.multifunc[3] = (dev->accel.multifunc[3] & 0x700) | val;
            else {
                dev->accel.multifunc[3] = val & 0x7ff;
            }
            break;
        case 0xe6ef:
            if (len == 1)
                dev->accel.multifunc[3] = (dev->accel.multifunc[3] & 0x0ff) | ((val & 0x07) << 8);
            break;

        case 0xeeee:
            if (len == 1)
                mach->accel.dest_cmp_fn = (mach->accel.dest_cmp_fn & 0xff00) | val;
            else
                mach->accel.dest_cmp_fn = val;
            break;
        case 0xeeef:
            if (len == 1)
                mach->accel.dest_cmp_fn = (mach->accel.dest_cmp_fn & 0x00ff) | (val << 8);
            break;

        case 0xf2ee:
            mach_log("F2EE.\n");
            if (len == 1)
                mach->accel.dst_clr_cmp_mask = (mach->accel.dst_clr_cmp_mask & 0xff00) | val;
            else
                mach->accel.dst_clr_cmp_mask = val;
            break;
        case 0xf2ef:
            if (len == 1)
                mach->accel.dst_clr_cmp_mask = (mach->accel.dst_clr_cmp_mask & 0x00ff) | (val << 8);
            break;

        case 0xfeee:
            mach_log("LineDraw = %04x.\n", mach->accel.dp_config);
            if (len != 1) {
                mach->accel.line_array[mach->accel.line_idx] = val;
                dev->accel.cur_x                             = mach->accel.line_array[(mach->accel.line_idx == 4) ? 4 : 0];
                dev->accel.cur_y                             = mach->accel.line_array[(mach->accel.line_idx == 5) ? 5 : 1];
                mach->accel.cx_end_line                      = mach->accel.line_array[2];
                mach->accel.cy_end_line                      = mach->accel.line_array[3];
                if ((mach->accel.line_idx == 3) || (mach->accel.line_idx == 5)) {
                    mach->accel.cmd_type = (mach->accel.line_idx == 5) ? 4 : 3;
                    mach_accel_start(mach->accel.cmd_type, 0, -1, -1, 0, mach, dev);
                    mach->accel.line_idx = (mach->accel.line_idx == 5) ? 4 : 2;
                    break;
                }
                mach->accel.line_idx++;
            }
            break;

        default:
            break;
    }
}

static void
mach_accel_out_call(uint16_t port, uint8_t val, mach_t *mach, svga_t *svga, ibm8514_t *dev)
{
    uint8_t    old = 0;

    if (port != 0x7aee && port != 0x7aef && port != 0x42e8 && port != 0x42e9 && port != 0x46e8 && port != 0x46e9)
        mach_log("[%04X:%08X]: Port CALL OUT=%04x, val=%02x.\n", CS, cpu_state.pc, port, val);

    switch (port) {
        case 0x2e8:
        case 0x2e9:
            WRITE8(port, dev->htotal, val);
            dev->htotal &= 0x1ff;
            break;

        case 0x6e8:
            /*In preparation to switch from VGA to 8514/A mode*/
            if (!dev->on[0] || !dev->on[1] || (mach->accel.clock_sel & 0x01))
                dev->hdisped = val;

            mach_log("[%04X:%08X]: ATI 8514/A: H_DISP write 06E8 = %d, actual val=%d, set lock=%x, shadow set=%x, advfunc bit 2=%x, dispcntl=%02x, clocksel bit 0=%x.\n", CS, cpu_state.pc, dev->hdisp, ((val + 1) << 3), mach->shadow_cntl, mach->shadow_set, dev->accel.advfunc_cntl & 0x04, dev->disp_cntl & 0x60, mach->accel.clock_sel & 0x01);
            mach_log("ATI 8514/A: (0x%04x): hdisp=0x%02x.\n", port, val);
            break;
        case 0x6e9:
            dev->hdisped = (dev->hdisp >> 3) - 1;
            dev->v_disp = (dev->vdisp - 1) << 1;
            mach_log("ATI 8514/A: (0x%04x): hdisp=0x%02x.\n", port, val);
            break;

        case 0xae8:
            dev->hsync_start = val;
            dev->hblankstart = (dev->hsync_start & 0x07);
            mach_log("ATI 8514/A: H_SYNC_STRT write 0AE8 = %d\n", val + 1);
            break;

        case 0xee8:
            dev->hsync_width = val;
            dev->hblank_end_val = (dev->hblankstart + (dev->hsync_width & 0x1f) - 1) & 0x3f;
            mach_log("ATI 8514/A: H_SYNC_WID write 0EE8 = %d\n", val + 1);
            break;

        case 0x12e8:
        case 0x12e9:
            /*In preparation to switch from VGA to 8514/A mode*/
            if (!dev->on[0] || !dev->on[1] || (mach->accel.clock_sel & 0x01)) {
                WRITE8(port, dev->v_total_reg, val);
                dev->v_total_reg &= 0x1fff;
            }
            break;

        case 0x16e8:
        case 0x16e9:
            /*In preparation to switch from VGA to 8514/A mode*/
            if (!dev->on[0] || !dev->on[1] || (mach->accel.clock_sel & 0x01)) {
                WRITE8(port, dev->v_disp, val);
                dev->v_disp &= 0x1fff;
            }
            dev->modechange = dev->accel.advfunc_cntl & 0x04;
            mach->compat_mode = mach->shadow_set & 0x03;
            mach_log("ATI 8514/A: V_DISP write 16E8 = %d\n", dev->v_disp);
            mach_log("ATI 8514/A: (0x%04x): vdisp=0x%02x.\n", port, val);
            break;

        case 0x1ae8:
        case 0x1ae9:
            /*In preparation to switch from VGA to 8514/A mode*/
            if (!dev->on[0] || !dev->on[1] || (mach->accel.clock_sel & 0x01)) {
                WRITE8(port, dev->v_sync_start, val);
                dev->v_sync_start &= 0x1fff;
            }
            break;

        case 0x1ee8:
        case 0x1ee9:
            mach_log("ATI 8514/A: V_SYNC_WID write 1EE8 = %02x\n", val);
            break;

        case 0x22e8:
            dev->disp_cntl = val;
            dev->interlace = !!(val & 0x10);
            mach_log("ATI 8514/A: DISP_CNTL write %04x=%02x, interlace=%d.\n", port, dev->disp_cntl, dev->interlace);
            break;

        case 0x42e8:
            old = dev->subsys_stat;
            if (val & 1)
                dev->subsys_stat &= ~1;
            if (val & 2)
                dev->subsys_stat &= ~2;
            if (val & 4)
                dev->subsys_stat &= ~4;
            if (val & 8)
                dev->subsys_stat &= ~8;
            break;
        case 0x42e9:
            old = dev->subsys_cntl;
            dev->subsys_cntl = val;
            if ((old ^ val) & 1)
                dev->subsys_stat |= 1;
            if ((old ^ val) & 2)
                dev->subsys_stat |= 2;
            if ((old ^ val) & 4)
                dev->subsys_stat |= 4;
            if ((old ^ val) & 8)
                dev->subsys_stat |= 8;
            break;

        case 0x4ae8:
        case 0x4ae9:
            WRITE8(port, dev->accel.advfunc_cntl, val);
            dev->on[port & 1] = dev->accel.advfunc_cntl & 0x01;
            if ((dev->local & 0xff) < 0x02)
                dev->ext_crt_pitch = 128;

            vga_on = !dev->on[port & 1];
            mach->ext_on[port & 1] = dev->on[port & 1];
            mach32_updatemapping(mach, svga);
            dev->vendor_mode[port & 1] = 0;
            dev->hdisp = (dev->hdisped + 1) << 3;
            dev->vdisp = (dev->v_disp >> 1) + 1;
            mach_log("ATI 8514/A: (0x%04x): ON=%d, shadow crt=%x, hdisp=%d, vdisp=%d.\n", port, dev->on[port & 1], dev->accel.advfunc_cntl & 0x04, dev->hdisp, dev->vdisp);
            mach_log("IBM mode set %s resolution.\n", (dev->accel.advfunc_cntl & 0x04) ? "2: 1024x768" : "1: 640x480");
            svga_recalctimings(svga);
            break;

        /*ATI Mach8/32 specific registers*/
        case 0x2ee:
            mach_log("2EE write val = %02x.\n", val);
            break;
        case 0x2ef:
            mach_log("2EF write val = %02x.\n", val);
            break;

        case 0x6ee:
            mach_log("6EE write val = %02x.\n", val);
            break;
        case 0x6ef:
            mach_log("6EF write val = %02x.\n", val);
            break;

        case 0xaee:
        case 0xaef:
            WRITE8(port, mach->cursor_offset_lo_reg, val);
            mach->cursor_offset_lo = mach->cursor_offset_lo_reg;
            break;

        case 0xeee:
        case 0xeef:
            WRITE8(port, mach->cursor_offset_hi_reg, val);
            mach->cursor_offset_hi = mach->cursor_offset_hi_reg & 0x0f;
            dev->hwcursor.addr = (mach->cursor_offset_lo | (mach->cursor_offset_hi << 16)) << 2;
            dev->hwcursor.ena = !!(mach->cursor_offset_hi_reg & 0x8000);
            break;

        case 0x12ee:
        case 0x12ef:
            WRITE8(port, mach->cursor_x, val);
            dev->hwcursor.x = mach->cursor_x & 0x7ff;
            break;

        case 0x16ee:
        case 0x16ef:
            WRITE8(port, mach->cursor_y, val);
            dev->hwcursor.y = mach->cursor_y & 0xfff;
            break;

        case 0x1aee:
        case 0x1aef:
            WRITE8(port, mach->cursor_col_b, val);
            mach->cursor_col_0 = mach->cursor_col_b & 0xff;
            mach->cursor_col_1 = (mach->cursor_col_b >> 8) & 0xff;
            break;

        case 0x1eee:
        case 0x1eef:
            WRITE8(port, mach->cursor_vh_offset, val);
            dev->hwcursor.xoff = mach->cursor_vh_offset & 0x3f;
            dev->hwcursor.yoff = (mach->cursor_vh_offset >> 8) & 0x3f;
            break;

        case 0x22ee:
            if (mach->pci_bus) {
                mach->pci_cntl_reg = val;
                mach32_updatemapping(mach, svga);
            }
            break;

        case 0x26ee:
        case 0x26ef:
            WRITE8(port, mach->accel.crt_pitch, val);
            dev->ext_crt_pitch = mach->accel.crt_pitch & 0xff;
            if (dev->accel_bpp > 8) {
                if (dev->accel_bpp == 24)
                    dev->ext_crt_pitch *= 3;
                else if (dev->accel_bpp == 32)
                    dev->ext_crt_pitch <<= 2;
                else
                    dev->ext_crt_pitch <<= 1;
            }
            mach_log("ATI 8514/A: (0x%04x) val=%04x.\n", port, val);
            svga_recalctimings(svga);
            break;

        case 0x32ee:
        case 0x32ef:
            WRITE8(port, mach->local_cntl, val);
            mach32_updatemapping(mach, svga);
            break;

        case 0x36ee:
        case 0x36ef:
            mach_log("ATI 8514/A: (0x%04x) val = %04x.\n", port, val);
            WRITE8(port, mach->misc, val);
            mach->misc &= 0xfff0;
            break;

        case 0x3aee:
        case 0x3aef:
            WRITE8(port, mach->cursor_col_0_rg, val);
            mach->ext_cur_col_0_g = mach->cursor_col_0_rg & 0xff;
            mach->ext_cur_col_0_r = (mach->cursor_col_0_rg >> 8) & 0xff;
            break;

        case 0x3eee:
        case 0x3eef:
            WRITE8(port, mach->cursor_col_1_rg, val);
            mach->ext_cur_col_1_g = mach->cursor_col_1_rg & 0xff;
            mach->ext_cur_col_1_r = (mach->cursor_col_1_rg >> 8) & 0xff;
            break;

        case 0x42ee:
        case 0x42ef:
            mach_log("ATI 8514/A: (0x%04x) val = %04x.\n", port, val);
            WRITE8(port, mach->accel.test2, val);
            break;

        case 0x46ee:
        case 0x46ef:
            WRITE8(port, mach->shadow_cntl, val);
            mach_log("ATI 8514/A: (0x%04x) val = %04x.\n", port, val);
            break;

        case 0x4aee:
        case 0x4aef:
            WRITE8(port, mach->accel.clock_sel, val);
            dev->on[port & 1] = mach->accel.clock_sel & 0x01;
            mach_log("ATI 8514/A: (0x%04x): ON=%d, val=%04x, hdisp=%d, vdisp=%d.\n", port, dev->on[port & 1], val, dev->hdisp, dev->vdisp);
            mach_log("ATI mode set %s resolution.\n", (dev->accel.advfunc_cntl & 0x04) ? "2: 1024x768" : "1: 640x480");
            mach->ext_on[port & 1] = dev->on[port & 1];
            vga_on = !dev->on[port & 1];
            dev->vendor_mode[port & 1] = 1;
            dev->hdisp = (dev->hdisped + 1) << 3;
            dev->vdisp = (dev->v_disp >> 1) + 1;
            svga_recalctimings(svga);
            break;

        case 0x52ee:
        case 0x52ef:
            mach_log("ATI 8514/A: (0x%04x) val=%04x.\n", port, val);
            WRITE8(port, mach->accel.scratch0, val);
            break;

        case 0x56ee:
        case 0x56ef:
            mach_log("ATI 8514/A: (0x%04x) val=%04x.\n", port, val);
            WRITE8(port, mach->accel.scratch1, val);
            break;

        case 0x5aee:
        case 0x5aef:
            WRITE8(port, mach->shadow_set, val);
            mach_log("ATI 8514/A: (0x%04x) val=%04x.\n", port, val);
            if ((mach->shadow_set & 0x03) == 0x00)
                mach_log("Primary CRT register set.\n");
            else if ((mach->shadow_set & 0x03) == 0x01)
                mach_log("Shadow Set 1: 640x480.\n");
            else if ((mach->shadow_set & 0x03) == 0x02)
                mach_log("Shadow Set 2: 1024x768.\n");
            break;

        case 0x5eee:
        case 0x5eef:
            WRITE8(port, mach->memory_aperture, val);
            mach_log("Memory Aperture = %04x.\n", mach->memory_aperture);
            if (!mach->pci_bus)
                mach->linear_base = (mach->memory_aperture & 0xff00) << 12;

            mach32_updatemapping(mach, svga);
            break;

        case 0x62ee:
            mach_log("62EE write val = %04x, len = %d.\n", val, len);
            break;

        case 0x66ee:
            mach_log("66EE write val = %04x, len = %d.\n", val, len);
            break;

        case 0x6aee:
        case 0x6aef:
            WRITE8(port, mach->accel.max_waitstates, val);
            break;

        case 0x6eee:
        case 0x6eef:
            WRITE8(port, mach->accel.ge_offset_lo, val);
            dev->accel.ge_offset = mach->accel.ge_offset_lo;
            break;

        case 0x72ee:
        case 0x72ef:
            WRITE8(port, mach->accel.ge_offset_hi, val);
            dev->accel.ge_offset = mach->accel.ge_offset_lo | (mach->accel.ge_offset_hi << 16);
            break;

        case 0x76ee:
        case 0x76ef:
            WRITE8(port, mach->accel.ge_pitch, val);
            dev->ext_pitch = ((mach->accel.ge_pitch & 0xff) << 3);
            mach_log("ATI 8514/A: (0x%04x) val=%04x, extpitch=%d.\n", port, val, dev->ext_pitch);
            svga_recalctimings(svga);
            break;

        case 0x7aee:
        case 0x7aef:
            WRITE8(port, mach->accel.ext_ge_config, val);
            if ((dev->local & 0xff) >= 0x02) {
                if (mach->accel.crt_pitch & 0xff)
                    dev->ext_crt_pitch = mach->accel.crt_pitch & 0xff;
                switch (mach->accel.ext_ge_config & 0x30) {
                    case 0:
                    case 0x10:
                        dev->bpp = 0;
                        break;
                    case 0x20:
                        dev->bpp = 1;
                        dev->ext_crt_pitch <<= 1;
                        break;
                    case 0x30:
                        dev->bpp = 0;
                        if (mach->accel.ext_ge_config & 0x200)
                            dev->ext_crt_pitch <<= 2;
                        else
                            dev->ext_crt_pitch *= 3;
                        break;

                    default:
                        break;
                }
                svga_set_ramdac_type(svga, !!(mach->accel.ext_ge_config & 0x4000));
                dev->vendor_mode[port & 1] = 1;
                mach32_updatemapping(mach, svga);
                mach_log("ATI 8514/A: (0x%04x) val=%02x.\n", port, val);
                svga_recalctimings(svga);
            } else {
                if (mach->accel.ext_ge_config & 0x8080)
                    ati_eeprom_write(&mach->eeprom, mach->accel.ext_ge_config & 0x4040, mach->accel.ext_ge_config & 0x2020, mach->accel.ext_ge_config & 0x1010);
            }
            break;

        case 0x7eee:
        case 0x7eef:
            WRITE8(port, mach->accel.eeprom_control, val);
            ati_eeprom_write(&mach->eeprom, mach->accel.eeprom_control & 8, mach->accel.eeprom_control & 2, mach->accel.eeprom_control & 1);
            mach_log("ATI 8514/A: (0x%04x) val = %04x.\n", port, val);
            break;

        default:
            break;
    }
}

static void
mach_accel_out(uint16_t port, uint8_t val, mach_t *mach)
{
    svga_t    *svga = &mach->svga;

    mach_log("[%04X:%08X]: Port NORMAL OUT=%04x, val=%04x.\n", CS, cpu_state.pc, port, val);

    mach_accel_out_call(port, val, mach, svga, (ibm8514_t *) svga->dev8514);
}

static uint16_t
mach_accel_in_fifo(mach_t *mach, svga_t *svga, ibm8514_t *dev, uint16_t port, int len)
{
    const uint16_t *vram_w = (uint16_t *) dev->vram;
    uint16_t        temp = 0;
    int             cmd;
    int             frgd_sel;
    int             bkgd_sel;
    int             mono_src;

    switch (port) {
        case 0x82e8:
        case 0xc2e8:
            if (len != 1)
                temp = dev->accel.cur_y;
            break;

        case 0x86e8:
        case 0xc6e8:
            if (len != 1)
                temp = dev->accel.cur_x;
            break;

        case 0x92e8:
            if (len != 1)
                temp = dev->test;
            break;

        case 0x96e8:
            if (len != 1)
                temp = dev->accel.maj_axis_pcnt;
            break;

        case 0x9ae8:
        case 0xdae8:
            if (len != 1) {
                if (dev->force_busy)
                    temp |= 0x200; /*Hardware busy*/
                dev->force_busy = 0;
                if (dev->data_available) {
                    temp |= 0x100; /*Read Data available*/
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
                        if (dev->accel.sy < 0)
                            dev->data_available = 0;
                    }
                }
            }
            mach_log("[%04X:%08X]: 9AE8: Temp = %04x, len = %d\n\n", CS, cpu_state.pc, temp, len);
            break;
        case 0x9ae9:
        case 0xdae9:
            if (len == 1) {
                if (dev->force_busy2)
                    temp |= 2; /*Hardware busy*/
                dev->force_busy2 = 0;
                if (dev->data_available2) {
                    temp |= 1; /*Read Data available*/
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
                        if (dev->accel.sy < 0)
                            dev->data_available2 = 0;
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
                        if (mach->accel.cmd_type == 3) {
                            READ_PIXTRANS_WORD(dev->accel.cx, 0)
                        } else {
                            READ_PIXTRANS_WORD(dev->accel.dx, 0)
                        }
                        mach_accel_out_pixtrans(mach, dev, temp);
                    }
                }
            } else {
                if (ibm8514_cpu_dest(svga)) {
                    cmd = (dev->accel.cmd >> 13);
                    if (len != 1) {
                        READ_PIXTRANS_WORD(dev->accel.cx, 0)
                        if (dev->accel.input && !dev->accel.odd_in && !dev->accel.sx) {
                            temp &= ~0xff00;
                            temp |= (dev->vram[(dev->accel.newdest_in + dev->accel.cur_x) & dev->vram_mask] << 8);
                        }
                        if (dev->subsys_stat & 1) {
                            dev->force_busy = 1;
                            dev->data_available = 1;
                        }
                    }
                    ibm8514_accel_out_pixtrans(svga, port, temp, len);
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
                                        mach_accel_start(mach->accel.cmd_type, 1, 8, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), 0, mach, dev);
                                    } else
                                        mach_accel_start(mach->accel.cmd_type, 1, 1, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), mach, dev);
                                } else
                                    mach_accel_start(mach->accel.cmd_type, 1, 1, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), mach, dev);
                                break;
                            case 0x200: /*16-bit size*/
                                if (mono_src == 2) {
                                    if ((frgd_sel != 2) && (bkgd_sel != 2)) {
                                        if (mach->accel.dp_config & 0x1000)
                                            mach_accel_start(mach->accel.cmd_type, 1, 16, mach->accel.pix_trans[1] | (mach->accel.pix_trans[0] << 8), 0, mach, dev);
                                        else
                                            mach_accel_start(mach->accel.cmd_type, 1, 16, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), 0, mach, dev);
                                    } else
                                        mach_accel_start(mach->accel.cmd_type, 1, 2, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), mach, dev);
                                } else
                                    mach_accel_start(mach->accel.cmd_type, 1, 2, -1, mach->accel.pix_trans[0] | (mach->accel.pix_trans[1] << 8), mach, dev);
                                break;

                            default:
                                break;
                        }
                    }
                }
            }
            break;

        case 0xbee8:
        case 0xfee8:
            if (len != 1) {
                mach_log("Multifunc_cntl = %d.\n", dev->accel.multifunc_cntl >> 12);
                switch ((dev->accel.multifunc_cntl >> 12) & 0x0f) {
                    case 0:
                        temp = dev->accel.multifunc[0];
                        break;
                    case 1:
                        temp = dev->accel.clip_top;
                        break;
                    case 2:
                        temp = dev->accel.clip_left;
                        break;
                    case 3:
                        temp = dev->accel.multifunc[3];
                        break;
                    case 4:
                        temp = dev->accel.multifunc[4];
                        break;
                    case 5:
                        temp = dev->accel.multifunc[5];
                        break;
                    case 8:
                        temp = dev->accel.multifunc[8];
                        break;
                    case 9:
                        temp = dev->accel.multifunc[9];
                        break;
                    case 0x0a:
                        temp = dev->accel.multifunc[0x0a];
                        break;

                    default:
                        break;
                }
            }
            break;

        case 0x82ee:
            temp = mach->accel.patt_data_idx;
            break;

        case 0x86ee:
        case 0x86ef:
            temp = 0x0000;
            break;

        case 0x8eee:
            if (len == 1)
                temp = mach->accel.ext_ge_config & 0xff;
            else
                temp = mach->accel.ext_ge_config;

            mach_log("ExtGE Read = %04x, len=%d.\n", temp, len);
            break;
        case 0x8eef:
            if (len == 1)
                temp = mach->accel.ext_ge_config >> 8;
            break;

        case 0x92ee:
            if (len == 1)
                temp = mach->accel.eeprom_control & 0xff;
            else
                temp = mach->accel.eeprom_control;

            mach_log("EEPROM cntl read=%04x, len=%d.\n", temp, len);
            break;
        case 0x92ef:
            if (len == 1)
                temp = mach->accel.eeprom_control >> 8;

            mach_log("EEPROM cntl read+1=%02x, len=%d.\n", temp, len);
            break;

        case 0x96ee:
            if (len == 1)
                temp = dev->accel.maj_axis_pcnt & 0xff;
            else {
                temp = dev->accel.maj_axis_pcnt;
                if ((mach->accel.test == 0x1555) || (mach->accel.test == 0x0aaa))
                    temp = mach->accel.test;
            }
            break;
        case 0x96ef:
            if (len == 1)
                temp = dev->accel.maj_axis_pcnt >> 8;
            break;

        case 0xa2ee:
            if (len == 1)
                temp = mach->accel.linedraw_opt & 0xff;
            else
                temp = mach->accel.linedraw_opt;
            break;
        case 0xa2ef:
            if (len == 1)
                temp = mach->accel.linedraw_opt >> 8;
            break;

        case 0xb2ee:
            if (len == 1)
                temp = dev->hdisped;
            else {
                temp = dev->hdisped & 0xff;
                temp |= (dev->htotal << 8);
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
            if (len == 1)
                temp = dev->v_total_reg & 0xff;
            else {
                temp = dev->v_total_reg;
                mach_log("VTOTAL read=%d.\n", temp);
            }
            break;
        case 0xc2ef:
            if (len == 1)
                temp = dev->v_total_reg >> 8;
            break;

        case 0xc6ee:
            if (len == 1)
                temp = dev->v_disp & 0xff;
            else {
                temp = dev->v_disp;
                mach_log("VDISP read=%d.\n", temp);
            }
            break;
        case 0xc6ef:
            if (len == 1)
                temp = dev->v_disp >> 8;
            break;

        case 0xcaee:
            if (len == 1)
                temp = dev->v_sync_start & 0xff;
            else
                temp = dev->v_sync_start;
            break;
        case 0xcaef:
            if (len == 1)
                temp = dev->v_sync_start >> 8;
            break;

        case 0xceee:
            if (len == 1)
                temp = dev->vc & 0xff;
            else
                temp = dev->vc & 0x7ff;
            break;
        case 0xceef:
            if (len == 1)
                temp = (dev->vc >> 8) & 7;
            break;

        case 0xdaee:
            if (len != 1) {
                temp = mach->accel.src_x;
                if ((dev->local & 0xff) >= 0x02)
                    temp &= 0x7ff;
            } else
                temp = mach->accel.src_x & 0xff;
            break;
        case 0xdaef:
            if (len == 1)
                temp = mach->accel.src_x >> 8;
            break;

        case 0xdeee:
            if (len != 1) {
                temp = mach->accel.src_y;
                if ((dev->local & 0xff) >= 0x02)
                    temp &= 0x7ff;
            } else
                temp = mach->accel.src_y & 0xff;
            break;
        case 0xdeef:
            if (len == 1)
                temp = mach->accel.src_y >> 8;
            break;

        case 0xfaee:
            if (len != 1) {
                if (mach->pci_bus)
                    temp = 0x0017;
                else
                    temp = 0x22f7;
            } else {
                if (mach->pci_bus)
                    temp = 0x17;
                else
                    temp = 0xf7;
            }
            break;
        case 0xfaef:
            if (len == 1) {
                if (mach->pci_bus)
                    temp = 0x00;
                else
                    temp = 0x22;
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
    uint8_t         temp      = 0;
    uint16_t        vpos      = 0;
    uint16_t        vblankend = svga->vblankstart + svga->crtc[0x16];

    switch (port) {
        case 0x2e8:
            vpos = dev->vc & 0x7ff;
            if (vblankend > dev->v_total) {
                vblankend -= dev->v_total;
                if ((vpos >= svga->vblankstart) || (vpos <= vblankend))
                    temp |= 2;
            } else {
                 if ((vpos >= svga->vblankstart) && (vpos <= vblankend))
                    temp |= 2;
            }
            break;

        case 0x6e8:
            temp = dev->hdisped;
            break;

        case 0x22e8:
            temp = dev->disp_cntl;
            break;

        case 0x26e8:
        case 0x26e9:
            READ8(port, dev->htotal);
            break;

        case 0x2ee8:
            temp = dev->subsys_cntl;
            break;
        case 0x2ee9:
            temp = 0xff;
            break;

        case 0x42e8:
        case 0x42e9:
            vpos = dev->vc & 0x7ff;
            if (vblankend > dev->v_total) {
                vblankend -= dev->v_total;
                if ((vpos >= svga->vblankstart) || (vpos <= vblankend))
                    dev->subsys_stat |= 1;
            } else {
                 if ((vpos >= svga->vblankstart) && (vpos <= vblankend))
                    dev->subsys_stat |= 1;
            }

            if (port & 1)
                temp = 0x80;
            else {
                temp = dev->subsys_stat | 0x80;
                if (mach->accel.ext_ge_config & 0x08)
                    temp |= ((mach->accel.ext_ge_config & 0x07) << 4);
                else
                    temp |= 0x20;
            }
            mach_log("Subsystem Status=%04x, 4ae8 shadow set=%02x.\n", temp, dev->accel.advfunc_cntl & 4);
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
            READ8(port, mach->misc);

            if (!(port & 1)) {
                temp &= ~0x0c;
                switch (mach->memory) {
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
                        if ((dev->local & 0xff) < 0x02)
                            temp |= 0x04;
                        break;
                }
            }
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
#ifdef ATI_8514_ULTRA
            if (mach->mca_bus) {
                if (!(port & 1)) {
                    if (svga->ext8514 != NULL)
                        temp = dev->pos_regs[4];
                } else {
                    if (svga->ext8514 != NULL)
                        temp = dev->pos_regs[5];
                }
            } else {
                if (!(port & 1)) {
                    if (svga->ext8514 != NULL) {
                        temp = 0x20 | 0x80;
                    }
                }
            }
#endif
            break;

        case 0x56ee:
        case 0x56ef:
            READ8(port, mach->accel.scratch1);
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
    if (port != 0x62ee && port != 0x62ef && port != 0x42e8 && port != 0x42e9)
        mach_log("[%04X:%08X]: Port NORMAL IN=%04x, temp=%04x.\n", CS, cpu_state.pc, port, temp);

    return temp;
}

#ifdef ATI_8514_ULTRA
static void
ati8514_accel_out(uint16_t port, uint8_t val, svga_t *svga)
{
    mach_log("[%04X:%08X]: Port NORMAL OUT=%04x, val=%04x.\n", CS, cpu_state.pc, port, val);

    mach_accel_out_call(port, val, (mach_t *)svga->ext8514, svga, (ibm8514_t *) svga->dev8514);
}

static void
ati8514_accel_outb(uint16_t port, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *)priv;
    mach_t *mach = (mach_t *)svga->ext8514;

    if (port & 0x8000)
        mach_accel_out_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, val, 1);
    else
        ati8514_accel_out(port, val, svga);
}

static void
ati8514_accel_outw(uint16_t port, uint16_t val, void *priv)
{
    svga_t *svga = (svga_t *)priv;
    mach_t *mach = (mach_t *)svga->ext8514;

    if (port & 0x8000)
        mach_accel_out_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, val, 2);
    else {
        ati8514_accel_out(port, val, svga);
        ati8514_accel_out(port + 1, (val >> 8), svga);
    }
}

static void
ati8514_accel_outl(uint16_t port, uint32_t val, void *priv)
{
    svga_t *svga = (svga_t *)priv;
    mach_t *mach = (mach_t *)svga->ext8514;

    if (port & 0x8000) {
        mach_accel_out_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, val & 0xffff, 2);
        mach_accel_out_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port + 2, val >> 16, 2);
    } else {
        ati8514_accel_out(port, val, svga);
        ati8514_accel_out(port + 1, (val >> 8), svga);
        ati8514_accel_out(port + 2, (val >> 16), svga);
        ati8514_accel_out(port + 3, (val >> 24), svga);
    }
}
#endif

static void
mach_accel_outb(uint16_t port, uint8_t val, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;

    if (port & 0x8000)
        mach_accel_out_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, val, 1);
    else
        mach_accel_out(port, val, mach);
}

static void
mach_accel_outw(uint16_t port, uint16_t val, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;

    if (port & 0x8000)
        mach_accel_out_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, val, 2);
    else {
        mach_accel_out(port, val, mach);
        mach_accel_out(port + 1, (val >> 8), mach);
    }
}

static void
mach_accel_outl(uint16_t port, uint32_t val, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;

    if (port & 0x8000) {
        mach_accel_out_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, val & 0xffff, 2);
        mach_accel_out_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port + 2, val >> 16, 2);
    } else {
        mach_accel_out(port, val, mach);
        mach_accel_out(port + 1, (val >> 8), mach);
        mach_accel_out(port + 2, (val >> 16), mach);
        mach_accel_out(port + 3, (val >> 24), mach);
    }
}

#ifdef ATI_8514_ULTRA
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
    return temp;
}

static uint32_t
ati8514_accel_inl(uint16_t port, void *priv)
{
    svga_t *svga = (svga_t *)priv;
    mach_t *mach = (mach_t *)svga->ext8514;
    uint32_t temp;

    if (port & 0x8000) {
        temp = mach_accel_in_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, 2);
        temp = (mach_accel_in_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port + 2, 2) << 16);
    } else {
        temp = ati8514_accel_in(port, svga);
        temp |= (ati8514_accel_in(port + 1, svga) << 8);
        temp |= (ati8514_accel_in(port + 2, svga) << 16);
        temp |= (ati8514_accel_in(port + 3, svga) << 24);
    }
    return temp;
}
#endif

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
    return temp;
}

static uint32_t
mach_accel_inl(uint16_t port, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    svga_t *svga = &mach->svga;
    uint32_t temp;

    if (port & 0x8000) {
        temp = mach_accel_in_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port, 2);
        temp = (mach_accel_in_fifo(mach, svga, (ibm8514_t *) svga->dev8514, port + 2, 2) << 16);
    } else {
        temp = mach_accel_in(port, mach);
        temp |= (mach_accel_in(port + 1, mach) << 8);
        temp |= (mach_accel_in(port + 2, mach) << 16);
        temp |= (mach_accel_in(port + 3, mach) << 24);
    }
    return temp;
}

static uint32_t
mach32_decode_addr(svga_t *svga, uint32_t addr, int write)
{
    int memory_map_mode = (svga->gdcreg[6] >> 2) & 3;

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

    if (memory_map_mode <= 1) {
        if (write)
            addr = (addr & svga->banked_mask) + svga->write_bank;
        else
            addr = (addr & svga->banked_mask) + svga->read_bank;
    }

    return addr;
}

static __inline void
mach32_write_common(uint32_t addr, uint8_t val, int linear, mach_t *mach)
{
    svga_t *svga = &mach->svga;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    int     writemask2 = svga->writemask;
    int     reset_wm   = 0;
    latch_t vall;
    uint8_t wm = svga->writemask;
    uint8_t count;
    uint8_t i;

    cycles -= svga->monitor->mon_video_timing_write_b;

    if (!linear) {
        addr = mach32_decode_addr(svga, addr, 1);
        if (addr == 0xffffffff)
            return;
    }

    if (!(svga->gdcreg[6] & 1))
        svga->fullchange = 2;

    mach_log("WriteCommon chain4 = %x.\n", svga->chain4);
    if (((svga->chain4 && (svga->packed_chain4 || svga->force_old_addr)) || svga->fb_only) && (svga->writemode < 4)) {
        writemask2 = 1 << (addr & 3);
        addr &= ~3;
    } else if (svga->chain4 && (svga->writemode < 4)) {
        writemask2 = 1 << (addr & 3);
        if (!linear)
            addr &= ~3;

        addr = ((addr & 0xfffc) << 2) | ((addr & 0x30000) >> 14) | (addr & ~0x3ffff);
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

    if (addr >= dev->vram_size)
        return;

    addr &= dev->vram_mask;

    dev->changedvram[addr >> 12] = svga->monitor->mon_changeframecount;

    count = 4;

    switch (svga->writemode) {
        case 0:
            val = ((val >> (svga->gdcreg[3] & 7)) | (val << (8 - (svga->gdcreg[3] & 7))));
            if ((svga->gdcreg[8] == 0xff) && !(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || svga->set_reset_disabled)) {
                for (i = 0; i < count; i++) {
                    if (writemask2 & (1 << i))
                        dev->vram[addr | i] = val;
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
                    dev->vram[addr | i] = svga->latch.b[i];
            }
            return;
        case 2:
            for (i = 0; i < count; i++)
                vall.b[i] = !!(val & (1 << i)) * 0xff;

            if (!(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || svga->set_reset_disabled)) {
                for (i = 0; i < count; i++) {
                    if (writemask2 & (1 << i))
                        dev->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (svga->latch.b[i] & ~svga->gdcreg[8]);
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
            break;
    }

    switch (svga->gdcreg[3] & 0x18) {
        case 0x00: /* Set */
            for (i = 0; i < count; i++) {
                if (writemask2 & (1 << i))
                    dev->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (svga->latch.b[i] & ~svga->gdcreg[8]);
            }
            break;
        case 0x08: /* AND */
            for (i = 0; i < count; i++) {
                if (writemask2 & (1 << i))
                    dev->vram[addr | i] = (vall.b[i] | ~svga->gdcreg[8]) & svga->latch.b[i];
            }
            break;
        case 0x10: /* OR */
            for (i = 0; i < count; i++) {
                if (writemask2 & (1 << i))
                    dev->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | svga->latch.b[i];
            }
            break;
        case 0x18: /* XOR */
            for (i = 0; i < count; i++) {
                if (writemask2 & (1 << i))
                    dev->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) ^ svga->latch.b[i];
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
    mach32_write_common(addr, val, 0, mach);
}

static void
mach32_writew(uint32_t addr, uint16_t val, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    mach32_write_common(addr, val & 0xff, 0, mach);
    mach32_write_common(addr + 1, val >> 8, 0, mach);
}

static void
mach32_writel(uint32_t addr, uint32_t val, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    mach32_write_common(addr, val & 0xff, 0, mach);
    mach32_write_common(addr + 1, val >> 8, 0, mach);
    mach32_write_common(addr + 2, val >> 16, 0, mach);
    mach32_write_common(addr + 3, val >> 24, 0, mach);
}

static __inline uint8_t
mach32_read_common(uint32_t addr, int linear, mach_t *mach)
{
    svga_t          *svga       = &mach->svga;
    const ibm8514_t *dev        = (ibm8514_t *) svga->dev8514;
    uint32_t         latch_addr = 0;
    int              readplane  = svga->readplane;
    uint8_t          count;
    uint8_t          temp;
    uint8_t          ret;

    cycles -= svga->monitor->mon_video_timing_read_b;

    if (!linear) {
        addr = mach32_decode_addr(svga, addr, 0);
        if (addr == 0xffffffff)
            return 0xff;
    }

    count = 2;

    latch_addr = (addr << count) & svga->decode_mask;
    count      = (1 << count);

    mach_log("ReadCommon chain4 = %x.\n", svga->chain4);
    if ((svga->chain4 && (svga->packed_chain4 || svga->force_old_addr)) || svga->fb_only) {
        addr &= svga->decode_mask;
        if (addr >= dev->vram_size)
            return 0xff;
        latch_addr = (addr & dev->vram_mask) & ~3;
        for (uint8_t i = 0; i < count; i++)
            svga->latch.b[i] = dev->vram[latch_addr | i];
        return dev->vram[addr & dev->vram_mask];
    } else if (svga->chain4 && !svga->force_old_addr) {
        readplane = addr & 3;
        addr      = ((addr & 0xfffc) << 2) | ((addr & 0x30000) >> 14) | (addr & ~0x3ffff);
    } else if (svga->chain2_read) {
        readplane = (readplane & 2) | (addr & 1);
        addr &= ~1;
        addr &= dev->vram_mask;
    } else {
        addr &= svga->decode_mask;
        if (addr >= dev->vram_size)
            return 0xff;
        latch_addr = (addr & dev->vram_mask) & ~3;
        for (uint8_t i = 0; i < count; i++)
            svga->latch.b[i] = dev->vram[latch_addr | i];
        return dev->vram[addr & dev->vram_mask];
    }

    addr &= svga->decode_mask;

    /* standard VGA latched access */
    if (latch_addr >= dev->vram_size) {
        for (uint8_t i = 0; i < count; i++)
            svga->latch.b[i] = 0xff;
    } else {
        latch_addr &= dev->vram_mask;

        for (uint8_t i = 0; i < count; i++)
            svga->latch.b[i] = dev->vram[latch_addr | i];
    }

    if (addr >= dev->vram_size)
        return 0xff;

    addr &= dev->vram_mask;

    if (svga->readmode) {
        temp = 0xff;

        for (uint8_t pixel = 0; pixel < 8; pixel++) {
            for (uint8_t plane = 0; plane < count; plane++) {
                if (svga->colournocare & (1 << plane)) {
                    /* If we care about a plane, and the pixel has a mismatch on it, clear its bit. */
                    if (((svga->latch.b[plane] >> pixel) & 1) != ((svga->colourcompare >> plane) & 1))
                        temp &= ~(1 << pixel);
                }
            }
        }

        ret = temp;
    } else
        ret = dev->vram[addr | readplane];

    return ret;
}

static uint8_t
mach32_read(uint32_t addr, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    uint8_t          ret;

    ret  = mach32_read_common(addr, 0, mach);
    return ret;
}

static uint16_t
mach32_readw(uint32_t addr, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    uint16_t         ret;

    ret  = mach32_read_common(addr, 0, mach);
    ret  |= (mach32_read_common(addr + 1, 0, mach) << 8);
    return ret;
}

static uint32_t
mach32_readl(uint32_t addr, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    uint32_t         ret;

    ret  = mach32_read_common(addr, 0, mach);
    ret  |= (mach32_read_common(addr + 1, 0, mach) << 8);
    ret  |= (mach32_read_common(addr + 2, 0, mach) << 16);
    ret  |= (mach32_read_common(addr + 3, 0, mach) << 24);
    return ret;
}

static void
mach32_ap_writeb(uint32_t addr, uint8_t val, void *priv)
{
    mach_t          *mach  = (mach_t *) priv;
    svga_t          *svga  = &mach->svga;
    const ibm8514_t *dev   = (ibm8514_t *) svga->dev8514;
    uint8_t port_dword     = addr & 0xfc;

    if (((mach->local_cntl & 0x20) || (mach->pci_cntl_reg & 0x80)) &&
        ((addr >= ((mach->ap_size << 20) - 0x200)) && (addr < (mach->ap_size << 20)))) {
        if (addr & 0x100) {
            mach_log("Port WORDB Write=%04x.\n", 0x02ee + (port_dword << 8));
            mach_accel_outb(0x02ee + (addr & 1) + (port_dword << 8), val, mach);
        } else {
            mach_log("Port WORDB Write=%04x.\n", 0x02e8 + (port_dword << 8));
            mach_accel_outb(0x02e8 + (addr & 1) + (port_dword << 8), val, mach);
        }
    } else {
        mach_log("Linear WORDB Write=%08x, val=%02x.\n", addr, val);
        if (dev->on[0] || dev->on[1])
            mach32_write_common(addr, val, 1, mach);
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

    if (((mach->local_cntl & 0x20) || (mach->pci_cntl_reg & 0x80)) &&
        ((addr >= ((mach->ap_size << 20) - 0x200)) && (addr < (mach->ap_size << 20)))) {
        if (addr & 0x100) {
            mach_log("Port WORDW Write=%04x.\n", 0x02ee + (port_dword << 8));
            mach_accel_outw(0x02ee + (port_dword << 8), val, mach);
        } else {
            mach_log("Port WORDW Write=%04x.\n", 0x02e8 + (port_dword << 8));
            mach_accel_outw(0x02e8 + (port_dword << 8), val, mach);
        }
    } else {
        mach_log("Linear WORDW Write=%08x, val=%04x.\n", addr, val);
        if (dev->on[0] || dev->on[1]) {
            mach32_write_common(addr, val & 0xff, 1, mach);
            mach32_write_common(addr + 1, val >> 8, 1, mach);
        } else
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

    if (((mach->local_cntl & 0x20) || (mach->pci_cntl_reg & 0x80)) &&
        ((addr >= ((mach->ap_size << 20) - 0x200)) && (addr < (mach->ap_size << 20)))) {
        if (addr & 0x100) {
            mach_log("Port WORDL Write=%04x.\n", 0x02ee + (port_dword << 8));
            mach_accel_outw(0x02ee + (port_dword << 8), val & 0xffff, mach);
            mach_accel_outw(0x02ee + (port_dword << 8) + 4, val >> 16, mach);
        } else {
            mach_log("Port WORDL Write=%04x.\n", 0x02e8 + (port_dword << 8));
            mach_accel_outw(0x02e8 + (port_dword << 8), val & 0xffff, mach);
            mach_accel_outw(0x02e8 + (port_dword << 8) + 4, val >> 16, mach);
        }
    } else {
        mach_log("Linear WORDL Write=%08x, val=%08x.\n", addr, val);
        if (dev->on[0] || dev->on[1]) {
            mach32_write_common(addr, val & 0xff, 1, mach);
            mach32_write_common(addr + 1, val >> 8, 1, mach);
            mach32_write_common(addr + 2, val >> 16, 1, mach);
            mach32_write_common(addr + 3, val >> 24, 1, mach);
        } else
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

    if (((mach->local_cntl & 0x20) || (mach->pci_cntl_reg & 0x80)) &&
        ((addr >= ((mach->ap_size << 20) - 0x200)) && (addr < (mach->ap_size << 20)))) {
        if (addr & 0x100)
            temp = mach_accel_inb(0x02ee + (addr & 1) + (port_dword << 8), mach);
        else
            temp = mach_accel_inb(0x02e8 + (addr & 1) + (port_dword << 8), mach);
    } else {
        if (dev->on[0] || dev->on[1])
            temp = mach32_read_common(addr, 1, mach);
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

    if (((mach->local_cntl & 0x20) || (mach->pci_cntl_reg & 0x80)) &&
        ((addr >= ((mach->ap_size << 20) - 0x200)) && (addr < (mach->ap_size << 20)))) {
        if (addr & 0x100)
            temp = mach_accel_inw(0x02ee + (port_dword << 8), mach);
        else
            temp = mach_accel_inw(0x02e8 + (port_dword << 8), mach);
    } else {
        if (dev->on[0] || dev->on[1]) {
            temp  = mach32_read_common(addr, 1, mach);
            temp  |= (mach32_read_common(addr + 1, 1, mach) << 8);
        } else
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

    if (((mach->local_cntl & 0x20) || (mach->pci_cntl_reg & 0x80)) &&
        ((addr >= ((mach->ap_size << 20) - 0x200)) && (addr < (mach->ap_size << 20)))) {
        if (addr & 0x100) {
            temp = mach_accel_inw(0x02ee + (port_dword << 8), mach);
            temp |= (mach_accel_inw(0x02ee + (port_dword << 8) + 4, mach) << 8);
        } else {
            temp = mach_accel_inw(0x02e8 + (port_dword << 8), mach);
            temp |= (mach_accel_inw(0x02e8 + (port_dword << 8) + 4, mach) << 8);
        }
    } else {
        if (dev->on[0] || dev->on[1]) {
            temp  = mach32_read_common(addr, 1, mach);
            temp  |= (mach32_read_common(addr + 1, 1, mach) << 8);
            temp  |= (mach32_read_common(addr + 2, 1, mach) << 16);
            temp  |= (mach32_read_common(addr + 3, 1, mach) << 24);
        } else
            temp = svga_readl_linear(addr, svga);

        mach_log("Linear WORDL Read=%08x, ret=%08x, ON0=%d, ON1=%d.\n", addr, temp, dev->on[0], dev->on[1]);
    }

    return temp;
}

static void
mach32_updatemapping(mach_t *mach, svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if (mach->pci_bus && (!(mach->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM))) {
        mem_mapping_disable(&svga->mapping);
        mem_mapping_disable(&mach->mmio_linear_mapping);
        return;
    }

    if (mach->regs[0xbd] & 4) {
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

    mach_log("Linear base = %08x, aperture = %04x, localcntl = %02x svgagdc = %x.\n", mach->linear_base, mach->memory_aperture, mach->local_cntl, svga->gdcreg[6] & 0x0c);
    if (mach->linear_base) {
        if (((mach->memory_aperture & 3) == 1) && !mach->pci_bus) {
            /*1 MB aperture*/
            mach->ap_size = 1;
            mem_mapping_set_addr(&mach->mmio_linear_mapping, mach->linear_base, mach->ap_size << 20);
        } else {
            /*4 MB aperture*/
            mach->ap_size = 4;
            mem_mapping_set_addr(&mach->mmio_linear_mapping, mach->linear_base, mach->ap_size << 20);
        }
    } else {
        mach->ap_size = 4;
        mem_mapping_disable(&mach->mmio_linear_mapping);
    }
    if (((dev->local & 0xff) >= 0x02) && (dev->on[0] || dev->on[1]) && (mach->ext_on[0] || mach->ext_on[1]) && (dev->vendor_mode[0] || dev->vendor_mode[1])) {
        mach_log("ExtON.\n");
        mem_mapping_set_handler(&svga->mapping, mach32_read, mach32_readw, mach32_readl, mach32_write, mach32_writew, mach32_writel);
        mem_mapping_set_p(&svga->mapping, mach);
    } else {
        mach_log("ExtOFF.\n");
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
    int           offset = dev->hwcursor_latch.x - dev->hwcursor_latch.xoff;
    uint32_t      color0;
    uint32_t      color1;
    uint32_t      *p;
    int           x_pos;
    int           y_pos;

    mach_log("BPP=%d.\n", dev->accel_bpp);
    switch (dev->accel_bpp) {
        default:
        case 8:
            color0 = dev->pallook[mach->cursor_col_0];
            color1 = dev->pallook[mach->cursor_col_1];
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
            break;
    }

    if (dev->interlace && dev->hwcursor_oddeven)
        dev->hwcursor_latch.addr += 16;

    for (int x = 0; x < 64; x += 8) {
        dat = dev->vram[dev->hwcursor_latch.addr & dev->vram_mask] | (dev->vram[(dev->hwcursor_latch.addr + 1) & dev->vram_mask] << 8);
        for (int xx = 0; xx < 8; xx++) {
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
        dev->hwcursor_latch.addr += 16;
}

#ifdef ATI_8514_ULTRA
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
    io_sethandler(0xeeee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xf2ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xf6ee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
    io_sethandler(0xfeee, 0x0002, ati8514_accel_inb, ati8514_accel_inw, ati8514_accel_inl, ati8514_accel_outb, ati8514_accel_outw, ati8514_accel_outl, svga);
}
#endif

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
    io_sethandler(0xeeee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xf2ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
    io_sethandler(0xf6ee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
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
    mach_log("[%04X]: MCA write port = %x, val = %02x, biosaddr = %05x.\n", CS, port & 7, mach->pos_regs[port & 7], (((mach->pos_regs[3] & 0x3e) << 0x0c) >> 1) + 0xc0000);
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

    mem_mapping_disable(&mach->bios_rom.mapping);
    mem_mapping_disable(&mach->bios_rom2.mapping);
    mach_log("MCA reset.\n");
    dev->on[0] = 0;
    dev->on[1] = 0;
    vga_on = 1;
    mach_mca_write(0x102, 0, mach);
}

#ifdef ATI_8514_ULTRA
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
    mach_log("[%04X]: MCA write port = %x, val = %02x, biosaddr = %05x.\n", CS, port & 7, dev->pos_regs[port & 7], (((dev->pos_regs[3] & 0x3e) << 0x0c) >> 1) + 0xc0000);
    mem_mapping_disable(&dev->bios_rom.mapping);

    if (dev->pos_regs[2] & 0x01)
        mem_mapping_enable(&dev->bios_rom.mapping);
}
#endif

static uint8_t
mach32_pci_read(UNUSED(int func), int addr, void *priv)
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
mach32_pci_write(UNUSED(int func), int addr, uint8_t val, void *priv)
{
    mach_t *mach = (mach_t *) priv;
    if ((addr >= 0x30) && (addr <= 0x33) && !mach->has_bios)
        return;

    switch (addr) {
        case PCI_REG_COMMAND:
            mach->pci_regs[PCI_REG_COMMAND] = val & 0x27;
            if (val & PCI_COMMAND_IO) {
                io_removehandler(0x02ea, 4,  mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
                io_removehandler(0x03c0, 32, mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
                io_sethandler(0x02ea, 4,  mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
                io_sethandler(0x03c0, 32, mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
            } else {
                io_removehandler(0x02ea, 4,  mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
                io_removehandler(0x03c0, 32, mach_in, NULL, NULL, mach_out, NULL, NULL, mach);
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

static void *
mach8_init(const device_t *info)
{
    mach_t    *mach;
    svga_t    *svga;
    ibm8514_t *dev;

    mach             = calloc(1, sizeof(mach_t));

    svga             = &mach->svga;
    dev              = (ibm8514_t *) calloc(1, sizeof(ibm8514_t));

    svga->dev8514    = dev;

    mach->pci_bus    = !!(info->flags & DEVICE_PCI);
    mach->vlb_bus    = !!(info->flags & DEVICE_VLB);
    mach->mca_bus    = !!(info->flags & DEVICE_MCA);
    dev->type        = info->flags;
    dev->local       = info->local & 0xff;
    mach->has_bios   = !(info->local & 0xff00);
    mach->memory     = device_get_config_int("memory");
    mach->ramdac_type = mach->pci_bus ? device_get_config_int("ramdac") : 1;

    if ((dev->local & 0xff) >= 0x02) {
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

    if ((dev->local & 0xff) >= 0x02) {
        svga_init(info, svga, mach, mach->memory << 10, /*default: 2MB for Mach32*/
                      mach_recalctimings,
                      mach_in, mach_out,
                      mach32_hwcursor_draw,
                      NULL);
        dev->vram_size   = mach->memory << 10;
        dev->vram        = calloc(dev->vram_size, 1);
        dev->changedvram = calloc(dev->vram_size >> 12, 1);
        dev->vram_mask   = dev->vram_size - 1;
        dev->hwcursor.cur_ysize = 64;
        mach->config1 = 0x20;
        if (mach->pci_bus && !mach->ramdac_type)
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
            svga->clock_gen = device_add(&ati18811_0_device);
        } else if (mach->mca_bus) {
            video_inform(VIDEO_FLAG_TYPE_8514, &timing_mach32_mca);
            if (is286 && !is386)
                mach->config1 |= 0x04;
            else
                mach->config1 |= 0x06;
            mach->config1 |= 0x0400;
            svga->clock_gen = device_add(&ati18811_1_device);
        } else if (mach->pci_bus) {
            video_inform(VIDEO_FLAG_TYPE_8514, &timing_mach32_pci);
            mach->config1 |= 0x0e;
            if (mach->ramdac_type)
                mach->config1 |= 0x0400;
            else
                mach->config1 |= 0x0a00;
            mach->config2 |= 0x2000;
            svga->clock_gen = device_add(&ati18811_0_device);
        } else {
            video_inform(VIDEO_FLAG_TYPE_8514, &timing_gfxultra_isa);
            mach->config1 |= 0x0400;
            svga->clock_gen = device_add(&ati18811_0_device);
        }
        mem_mapping_add(&mach->mmio_linear_mapping, 0, 0, mach32_ap_readb, mach32_ap_readw, mach32_ap_readl, mach32_ap_writeb, mach32_ap_writew, mach32_ap_writel, NULL, MEM_MAPPING_EXTERNAL, mach);
        mem_mapping_disable(&mach->mmio_linear_mapping);
    } else {
        svga_init(info, svga, mach, (512 << 10), /*default: 512kB VGA for 28800-6 + 1MB for Mach8*/
                      mach_recalctimings,
                      mach_in, mach_out,
                      NULL,
                      NULL);
        dev->vram_size   = (1024 << 10);
        dev->vram        = calloc(dev->vram_size, 1);
        dev->changedvram = calloc(dev->vram_size >> 12, 1);
        dev->vram_mask   = dev->vram_size - 1;
        video_inform(VIDEO_FLAG_TYPE_8514, &timing_gfxultra_isa);
        mach->config1 = 0x01 | 0x02 | 0x20 | 0x08 | 0x80;
        mach->config2 = 0x02;
        svga->clock_gen = device_add(&ati18810_device);
    }
    dev->bpp = 0;
    svga->getclock  = ics2494_getclock;

    dev->on[0] = 0;
    dev->on[1] = 0;
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

    if ((dev->local & 0xff) >= 0x02) {
        svga->decode_mask     = (4 << 20) - 1;
        mach->cursor_col_1    = 0xff;
        mach->ext_cur_col_1_r = 0xff;
        mach->ext_cur_col_1_g = 0xff;
        io_sethandler(0xfaee, 0x0002, mach_accel_inb, mach_accel_inw, mach_accel_inl, mach_accel_outb, mach_accel_outw, mach_accel_outl, mach);
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

            mach->pci_regs[PCI_REG_COMMAND] = 0x83;
            mach->pci_regs[0x30]            = 0x00;
            mach->pci_regs[0x32]            = 0x0c;
            mach->pci_regs[0x33]            = 0x00;
        } else
            ati_eeprom_load(&mach->eeprom, "mach32.nvr", 1);
    } else
        ati_eeprom_load_mach8(&mach->eeprom, "mach8.nvr");

    timer_add(&svga->timer8514, ibm8514_poll, svga, 1);

    return mach;
}

#ifdef ATI_8514_ULTRA
void
ati8514_init(svga_t *svga, void *ext8514, void *dev8514)
{
    mach_t *mach = (mach_t *)ext8514;
    ibm8514_t *dev = (ibm8514_t *)dev8514;

    dev->on[0] = 0;
    dev->on[1] = 0;
    dev->ext_pitch = 1024;
    dev->ext_crt_pitch = 0x80;
    dev->accel_bpp = 8;
    dev->rowoffset = 0x80;
    dev->hdisp = 1024;
    dev->vdisp = 768;

    io_sethandler(0x02ea, 4, ati8514_in, NULL, NULL, ati8514_out, NULL, NULL, svga);
    ati8514_io_set(svga);
    mach->mca_bus = !!(dev->type & DEVICE_MCA);

    if (mach->mca_bus)
        mach->config1 = 0x02 | 0x04 | 0x08 | 0x20 | 0x80;
    else
        mach->config1 = 0x02 | 0x08 | 0x20 | 0x80;

    mach->config2 = 0x01 | 0x02;
}
#endif

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
static const device_config_t mach32_config[] = {
    {
        .name = "memory",
        .description = "Memory size",
        .type = CONFIG_SELECTION,
        .default_int = 2048,
        .selection = {
            {
                .description = "512 KB",
                .value = 512
            },
            {
                .description = "1 MB",
                .value = 1024
            },
            {
                .description = "2 MB",
                .value = 2048
            },
            {
                .description = "4 MB",
                .value = 4096
            },
            {
                .description = ""
            }
        }
    },
    {
        .type = CONFIG_END
    }
};

// clang-format off
static const device_config_t mach32_pci_config[] = {
    {
        .name = "ramdac",
        .description = "RAMDAC type",
        .type = CONFIG_SELECTION,
        .default_int = 0,
        .selection = {
            {
                .description = "ATI 68860",
                .value = 0
            },
            {
                .description = "ATI 68875",
                .value = 1
            },
            {
                .description = ""
            }
        }
    },
    {
        .name = "memory",
        .description = "Memory size",
        .type = CONFIG_SELECTION,
        .default_int = 2048,
        .selection = {
            {
                .description = "512 KB",
                .value = 512
            },
            {
                .description = "1 MB",
                .value = 1024
            },
            {
                .description = "2 MB",
                .value = 2048
            },
            {
                .description = "4 MB",
                .value = 4096
            },
            {
                .description = ""
            }
        }
    },
    {
        .type = CONFIG_END
    }
};

const device_t mach8_vga_isa_device = {
    .name = "ATI Mach8 (ATI Graphics Ultra) (ISA)",
    .internal_name = "mach8_vga_isa",
    .flags = DEVICE_ISA,
    .local = 1,
    .init = mach8_init,
    .close = mach_close,
    .reset = NULL,
    { .available = mach8_vga_available },
    .speed_changed = mach_speed_changed,
    .force_redraw = mach_force_redraw,
    .config = NULL
};

const device_t mach32_isa_device = {
    .name = "ATI Mach32 (ISA)",
    .internal_name = "mach32_isa",
    .flags = DEVICE_ISA,
    .local = 2,
    .init = mach8_init,
    .close = mach_close,
    .reset = NULL,
    { .available = mach32_isa_available },
    .speed_changed = mach_speed_changed,
    .force_redraw = mach_force_redraw,
    .config = mach32_config
};

const device_t mach32_vlb_device = {
    .name = "ATI Mach32 (VLB)",
    .internal_name = "mach32_vlb",
    .flags = DEVICE_VLB,
    .local = 2,
    .init = mach8_init,
    .close = mach_close,
    .reset = NULL,
    { .available = mach32_vlb_available },
    .speed_changed = mach_speed_changed,
    .force_redraw = mach_force_redraw,
    .config = mach32_config
};

const device_t mach32_mca_device = {
    .name = "ATI Mach32 (MCA)",
    .internal_name = "mach32_mca",
    .flags = DEVICE_MCA,
    .local = 2,
    .init = mach8_init,
    .close = mach_close,
    .reset = NULL,
    { .available = mach32_mca_available },
    .speed_changed = mach_speed_changed,
    .force_redraw = mach_force_redraw,
    .config = mach32_config
};

const device_t mach32_pci_device = {
    .name = "ATI Mach32 (PCI)",
    .internal_name = "mach32_pci",
    .flags = DEVICE_PCI,
    .local = 2,
    .init = mach8_init,
    .close = mach_close,
    .reset = NULL,
    { .available = mach32_pci_available },
    .speed_changed = mach_speed_changed,
    .force_redraw = mach_force_redraw,
    .config = mach32_pci_config
};

const device_t mach32_onboard_pci_device = {
    .name = "ATI Mach32 (PCI) On-Board",
    .internal_name = "mach32_pci_onboard",
    .flags = DEVICE_PCI,
    .local = 2 | 0x100,
    .init = mach8_init,
    .close = mach_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = mach_speed_changed,
    .force_redraw = mach_force_redraw,
    .config = mach32_pci_config
};

