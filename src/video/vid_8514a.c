/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the 8514/A card from IBM for the MCA bus and
 *          ISA bus clones.
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
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/mca.h>
#include <86box/rom.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_8514a.h>
#include <86box/vid_8514a_device.h>
#include <86box/vid_xga.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_ati_eeprom.h>
#include <86box/vid_ati_mach8.h>
#include "cpu.h"

#ifdef CLAMP
#    undef CLAMP
#endif

#define BIOS_MACH8_ROM_PATH  "roms/video/mach8/11301113140_4k.BIN"

static void     ibm8514_accel_outb(uint16_t port, uint8_t val, void *priv);
static void     ibm8514_accel_outw(uint16_t port, uint16_t val, void *priv);
static uint8_t  ibm8514_accel_inb(uint16_t port, void *priv);
static uint16_t ibm8514_accel_inw(uint16_t port, void *priv);

#ifdef ENABLE_IBM8514_LOG
int ibm8514_do_log = ENABLE_IBM8514_LOG;

static void
ibm8514_log(const char *fmt, ...)
{
    va_list ap;

    if (ibm8514_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ibm8514_log(fmt, ...)
#endif

static int16_t
CLAMP(int16_t in, int16_t min, int16_t max)
{
    if (in < min)
        return min;
    if (in > max)
        return max;

    return in;
}

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


#define READ_PIXTRANS_WORD(cx, n)                                                                   \
    if ((cmd <= 1) || (cmd == 5)) {                                                                 \
        temp = dev->vram[((dev->accel.cy * dev->pitch) + (cx) + (n)) & dev->vram_mask];             \
        temp |= (dev->vram[((dev->accel.cy * dev->pitch) + (cx) + (n + 1)) & dev->vram_mask] << 8); \
    } else {                                                                                        \
        temp = dev->vram[(dev->accel.dest + (cx) + (n)) & dev->vram_mask];                          \
        temp |= (dev->vram[(dev->accel.dest + (cx) + (n + 1)) & dev->vram_mask] << 8);              \
    }                                                                                               \

#define READ(addr, dat)                               \
    if (dev->bpp)                                     \
        dat = vram_w[(addr) & (dev->vram_mask >> 1)]; \
    else                                              \
        dat = (dev->vram[(addr) & (dev->vram_mask)]); \

#define READ_HIGH(addr, dat)                            \
    dat |= (dev->vram[(addr) & (dev->vram_mask)] << 8);

#define MIX(mixmode, dest_dat, src_dat)                                                               \
    {                                                                                                 \
        switch ((mixmode) ? (dev->accel.frgd_mix & 0x1f) : (dev->accel.bkgd_mix & 0x1f)) {            \
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
                dest_dat = ~src_dat | dest_dat;                                                       \
                break;                                                                                \
            case 0x0a:                                                                                \
                dest_dat = src_dat | ~dest_dat;                                                       \
                break;                                                                                \
            case 0x0b:                                                                                \
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
            case 0x11:                                                                                \
                dest_dat = dest_dat - src_dat;                                                        \
                break;                                                                                \
            case 0x12:                                                                                \
                dest_dat = src_dat - dest_dat;                                                        \
                break;                                                                                \
            case 0x13:                                                                                \
                dest_dat = src_dat + dest_dat;                                                        \
                break;                                                                                \
            case 0x14:                                                                                \
                dest_dat = MAX(src_dat, dest_dat);                                                    \
                break;                                                                                \
            case 0x15:                                                                                \
                dest_dat = (dest_dat - src_dat) >> 1;                                                 \
                break;                                                                                \
            case 0x16:                                                                                \
                dest_dat = (src_dat - dest_dat) >> 1;                                                 \
                break;                                                                                \
            case 0x17:                                                                                \
                dest_dat = (dest_dat + src_dat) >> 1;                                                 \
                break;                                                                                \
            case 0x18:                                                                                \
                dest_dat = MAX(0, (dest_dat - src_dat));                                              \
                break;                                                                                \
            case 0x19:                                                                                \
                dest_dat = MAX(0, (dest_dat - src_dat));                                              \
                break;                                                                                \
            case 0x1a:                                                                                \
                dest_dat = MAX(0, (src_dat - dest_dat));                                              \
                break;                                                                                \
            case 0x1b:                                                                                \
                if (dev->bpp)                                                                         \
                    dest_dat = MIN(0xffff, (dest_dat + src_dat));                                     \
                else                                                                                  \
                    dest_dat = MIN(0xff, (dest_dat + src_dat));                                       \
                break;                                                                                \
            case 0x1c:                                                                                \
                dest_dat = MAX(0, (dest_dat - src_dat)) / 2;                                          \
                break;                                                                                \
            case 0x1d:                                                                                \
                dest_dat = MAX(0, (dest_dat - src_dat)) / 2;                                          \
                break;                                                                                \
            case 0x1e:                                                                                \
                dest_dat = MAX(0, (src_dat - dest_dat)) / 2;                                          \
                break;                                                                                \
            case 0x1f:                                                                                \
                if (dev->bpp)                                                                         \
                    dest_dat = (0xffff < (src_dat + dest_dat)) ? 0xffff : ((src_dat + dest_dat) / 2); \
                else                                                                                  \
                    dest_dat = (0xff < (src_dat + dest_dat)) ? 0xff : ((src_dat + dest_dat) / 2);     \
                break;                                                                                \
        }                                                                                             \
    }

#define WRITE(addr, dat)                                                               \
    if (dev->bpp) {                                                                    \
        vram_w[((addr)) & (dev->vram_mask >> 1)]                    = dat;             \
        dev->changedvram[(((addr)) & (dev->vram_mask >> 1)) >> 11] = changeframecount; \
    } else {                                                                           \
        dev->vram[((addr)) & (dev->vram_mask)]                = dat;                   \
        dev->changedvram[(((addr)) & (dev->vram_mask)) >> 12] = changeframecount;      \
    }

int ibm8514_active = 0;

int
ibm8514_cpu_src(svga_t *svga)
{
    const ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if (!(dev->accel.cmd & 0x100))
        return 0;

    if (dev->accel.cmd & 1)
        return 1;

    return 0;
}

int
ibm8514_cpu_dest(svga_t *svga)
{
    const ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if (!(dev->accel.cmd & 0x100))
        return 0;

    if (dev->accel.cmd & 1)
        return 0;

    return 1;
}

void
ibm8514_accel_out_pixtrans(svga_t *svga, UNUSED(uint16_t port), uint32_t val, int len)
{
    ibm8514_t *dev       = (ibm8514_t *) svga->dev8514;
    uint8_t    nibble    = 0;
    uint32_t   pixelxfer = 0;
    uint32_t   monoxfer  = 0xffffffff;
    int        pixcnt    = 0;
    int        pixcntl   = (dev->accel.multifunc[0x0a] >> 6) & 3;
    int        frgd_mix  = (dev->accel.frgd_mix >> 5) & 3;
    int        bkgd_mix  = (dev->accel.bkgd_mix >> 5) & 3;
    int        cmd       = dev->accel.cmd >> 13;

    if (dev->accel.cmd & 0x100) {
        if (len == 2) {
            /*Bus size*/
            if (dev->accel.cmd & 0x200) /*16-bit*/
                pixcnt = 16;
            else /*8-bit*/
                pixcnt = 8;

            /*Pixel transfer data mode, can't be the same as Foreground/Background CPU data*/
            if (pixcntl == 2) {
                if ((frgd_mix == 2) || (bkgd_mix == 2))
                    pixelxfer = val;
                else {
                    if (dev->accel.cmd & 0x02) {
                        if (pixcnt == 16) {
                            if ((cmd >= 2) && (dev->accel.cmd & 0x1000))
                                val = (val >> 8) | (val << 8);
                        }
                        if ((cmd <= 2) || (cmd == 4) || (cmd == 6)) {
                            if ((dev->accel.cmd & 0x08) && (cmd >= 2))
                                monoxfer = val;
                            else {
                                if (val & 0x02)
                                    nibble |= 0x80;
                                if (val & 0x04)
                                    nibble |= 0x40;
                                if (val & 0x08)
                                    nibble |= 0x20;
                                if (val & 0x10)
                                    nibble |= 0x10;
                                if (val & 0x200)
                                    nibble |= 0x08;
                                if (val & 0x400)
                                    nibble |= 0x04;
                                if (val & 0x800)
                                    nibble |= 0x02;
                                if (val & 0x1000)
                                    nibble |= 0x01;

                                monoxfer = nibble;
                            }
                        } else
                            monoxfer = val;
                    } else
                        monoxfer = val;
                }
            } else
                pixelxfer = val;

            if (dev->accel.input)
                ibm8514_accel_start(pixcnt >> 1, 1, monoxfer & 0xff, pixelxfer & 0xff, svga, len);
            else
                ibm8514_accel_start(pixcnt, 1, monoxfer & 0xffff, pixelxfer & 0xffff, svga, len);
        }
    }
}

void
ibm8514_accel_out_fifo(svga_t *svga, uint16_t port, uint32_t val, int len)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if (port != 0x9ae8 && port != 0xe2e8)
        ibm8514_log("Port OUT FIFO=%04x, val=%04x, len=%d.\n", port, val, len);

    switch (port) {
        case 0x82e8:
        case 0xc2e8:
            dev->fifo_idx++;
            if (len == 2)
                dev->accel.cur_y = val & 0x7ff;
            break;

        case 0x86e8:
        case 0xc6e8:
            dev->fifo_idx++;
            if (len == 2)
                dev->accel.cur_x = val & 0x7ff;
            break;

        case 0x8ae8:
        case 0xcae8:
            dev->fifo_idx++;
            if (len == 2) {
                dev->accel.desty       = val & 0x7ff;
                dev->accel.desty_axstp = val & 0x3fff;
                if (val & 0x2000)
                    dev->accel.desty_axstp |= ~0x1fff;
            }
            break;

        case 0x8ee8:
        case 0xcee8:
            dev->fifo_idx++;
            if (len == 2) {
                dev->accel.destx       = val & 0x7ff;
                dev->accel.destx_distp = val & 0x3fff;
                if (val & 0x2000)
                    dev->accel.destx_distp |= ~0x1fff;
            }
            break;

        case 0x92e8:
            dev->fifo_idx++;
            if (len == 2)
                dev->test = val;
            fallthrough;

        case 0xd2e8:
            if (len == 2) {
                dev->accel.err_term = val & 0x3fff;
                if (val & 0x2000)
                    dev->accel.err_term |= ~0x1fff;
            }
            break;

        case 0x96e8:
        case 0xd6e8:
            dev->fifo_idx++;
            if (len == 2) {
                dev->accel.maj_axis_pcnt = val & 0x7ff;
                dev->accel.maj_axis_pcnt_no_limit = val;
            }
            break;

        case 0x9ae8:
        case 0xdae8:
            dev->fifo_idx++;
            dev->accel.ssv_state = 0;
            if (len == 2) {
                dev->data_available  = 0;
                dev->data_available2 = 0;
                dev->accel.cmd       = val;
                dev->accel.cmd_back  = 1;
                if (dev->accel.cmd & 0x100)
                    dev->accel.cmd_back = 0;

                ibm8514_log("8514/A CMD=%04x, back=%d.\n", dev->accel.cmd, dev->accel.cmd_back);
                ibm8514_accel_start(-1, 0, -1, 0, svga, len);
            }
            break;

        case 0x9ee8:
        case 0xdee8:
            dev->fifo_idx++;
            dev->accel.ssv_state = 1;
            if (len == 2) {
                dev->accel.short_stroke = val;

                dev->accel.cx = dev->accel.cur_x;
                if (dev->accel.cur_x >= 0x600)
                    dev->accel.cx |= ~0x5ff;

                dev->accel.cy = dev->accel.cur_y;
                if (dev->accel.cur_y >= 0x600)
                    dev->accel.cy |= ~0x5ff;

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
            dev->fifo_idx++;
            if (port == 0xe2e8) {
                if (len == 2) {
                    if (dev->accel.cmd_back)
                        dev->accel.bkgd_color = val;
                    else {
                        if (ibm8514_cpu_dest(svga))
                            break;
                        ibm8514_accel_out_pixtrans(svga, port, val, len);
                    }
                }
            } else {
                if (len == 2)
                    dev->accel.bkgd_color = val;
            }
            break;

        case 0xa6e8:
        case 0xe6e8:
            dev->fifo_idx++;
            if (port == 0xe6e8) {
                if (len == 2) {
                    if (dev->accel.cmd_back)
                        dev->accel.frgd_color = val;
                    else {
                        if (ibm8514_cpu_dest(svga))
                            break;
                        ibm8514_accel_out_pixtrans(svga, port, val, len);
                    }
                }
            } else {
                if (len == 2)
                    dev->accel.frgd_color = val;
            }
            break;

        case 0xaae8:
        case 0xeae8:
            dev->fifo_idx++;
            if (len == 2)
                dev->accel.wrt_mask = val;
            break;

        case 0xaee8:
        case 0xeee8:
            dev->fifo_idx++;
            if (len == 2)
                dev->accel.rd_mask = val;
            break;

        case 0xb2e8:
        case 0xf2e8:
            dev->fifo_idx++;
            if (len == 2)
                dev->accel.color_cmp = val;
            break;

        case 0xb6e8:
        case 0xf6e8:
            dev->fifo_idx++;
            dev->accel.bkgd_mix = val & 0xff;
            break;

        case 0xbae8:
        case 0xfae8:
            dev->fifo_idx++;
            dev->accel.frgd_mix = val & 0xff;
            break;

        case 0xbee8:
        case 0xfee8:
            dev->fifo_idx++;
            if (len == 2) {
                dev->accel.multifunc_cntl                             = val;
                dev->accel.multifunc[dev->accel.multifunc_cntl >> 12] = dev->accel.multifunc_cntl & 0xfff;

                if ((dev->accel.multifunc_cntl >> 12) == 1) {
                    dev->accel.clip_top = dev->accel.multifunc[1] & 0x3ff;
                    if (dev->accel.multifunc[1] & 0x400)
                        dev->accel.clip_top |= ~0x3ff;
                }

                if ((dev->accel.multifunc_cntl >> 12) == 2) {
                    dev->accel.clip_left = dev->accel.multifunc[2] & 0x3ff;
                    if (dev->accel.multifunc[2] & 0x400)
                        dev->accel.clip_left |= ~0x3ff;
                }
                if ((dev->accel.multifunc_cntl >> 12) == 3)
                    dev->accel.clip_bottom = dev->accel.multifunc[3] & 0x7ff;

                if ((dev->accel.multifunc_cntl >> 12) == 4)
                    dev->accel.clip_right = dev->accel.multifunc[4] & 0x7ff;

            }
            break;

        default:
            break;
    }
}

void
ibm8514_ramdac_out(uint16_t port, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    svga_out(port, val, svga);
}

uint8_t
ibm8514_ramdac_in(uint16_t port, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    uint8_t ret;

    ret = svga_in(port, svga);
    return ret;
}

static void
ibm8514_io_set(svga_t *svga)
{
    io_sethandler(0x2e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x2ea, 0x0004, ibm8514_ramdac_in, NULL, NULL, ibm8514_ramdac_out, NULL, NULL, svga);
    io_sethandler(0x6e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xae8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xee8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x12e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x16e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x1ae8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x1ee8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x22e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x26e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x2ee8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x42e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x4ae8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x52e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x56e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x5ae8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x5ee8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x82e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x86e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x8ae8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x8ee8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x92e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x96e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x9ae8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0x9ee8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xa2e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xa6e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xaae8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xaee8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xb2e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xb6e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xbae8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xbee8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xe2e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);

    io_sethandler(0xc2e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xc6e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xcae8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xcee8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xd2e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xd6e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xdae8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xdee8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xe6e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xeae8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xeee8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xf2e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xf6e8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xfae8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
    io_sethandler(0xfee8, 0x0002, ibm8514_accel_inb, ibm8514_accel_inw, NULL, ibm8514_accel_outb, ibm8514_accel_outw, NULL, svga);
}

void
ibm8514_accel_out(uint16_t port, uint32_t val, svga_t *svga, int len)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if (port & 0x8000)
        ibm8514_accel_out_fifo(svga, port, val, len);
    else {
        switch (port) {
            case 0x2e8:
            case 0x6e9:
                WRITE8(port, dev->htotal, val);
                ibm8514_log("IBM 8514/A compatible: (0x%04x): htotal=0x%02x.\n", port, val);
                svga_recalctimings(svga);
                break;

            case 0x6e8:
                /*In preparation to switch from VGA to 8514/A mode*/
                dev->hdisped = val;
                dev->hdisp = (dev->hdisped + 1) << 3;
                ibm8514_log("[%04X:%08X]: IBM 8514/A: (0x%04x): hdisp=0x%02x.\n", CS, cpu_state.pc, port, val);
                svga_recalctimings(svga);
                break;

            case 0xae8:
                dev->hsync_start = val;
                ibm8514_log("IBM 8514/A compatible: (0x%04x): val=0x%02x, hsync_start=%d.\n", port, val, (val + 1) << 3);
                svga_recalctimings(svga);
                break;

            case 0xee8:
                dev->hsync_width = val;
                ibm8514_log("IBM 8514/A compatible: (0x%04x): val=0x%02x, hsync_width=%d, hsyncpol=%02x.\n", port, val & 0x1f, ((val & 0x1f) + 1) << 3, val & 0x20);
                svga_recalctimings(svga);
                break;

            case 0x12e8:
            case 0x12e9:
                /*In preparation to switch from VGA to 8514/A mode*/
                WRITE8(port, dev->v_total_reg, val);
                dev->v_total_reg &= 0x1fff;
                dev->v_total = dev->v_total_reg + 1;
                if (dev->interlace)
                    dev->v_total >>= 1;

                ibm8514_log("IBM 8514/A compatible: (0x%04x): vtotal=0x%02x.\n", port, val);
                svga_recalctimings(svga);
                break;

            case 0x16e8:
            case 0x16e9:
                /*In preparation to switch from VGA to 8514/A mode*/
                WRITE8(port, dev->v_disp, val);
                dev->v_disp &= 0x1fff;
                dev->vdisp = (dev->v_disp + 1) >> 1;
                ibm8514_log("IBM 8514/A: V_DISP write 16E8 = %d\n", dev->v_disp);
                ibm8514_log("IBM 8514/A: (0x%04x): vdisp=0x%02x.\n", port, val);
                svga_recalctimings(svga);
                break;

            case 0x1ae8:
            case 0x1ae9:
                /*In preparation to switch from VGA to 8514/A mode*/
                WRITE8(port, dev->v_sync_start, val);
                dev->v_sync_start &= 0x1fff;
                dev->v_syncstart = dev->v_sync_start + 1;
                if (dev->interlace)
                    dev->v_syncstart >>= 1;

                ibm8514_log("IBM 8514/A compatible: V_SYNCSTART write 1AE8 = %d\n", dev->v_syncstart);
                ibm8514_log("IBM 8514/A compatible: (0x%04x): vsyncstart=0x%02x.\n", port, val);
                svga_recalctimings(svga);
                break;

            case 0x1ee8:
            case 0x1ee9:
                ibm8514_log("IBM 8514/A compatible: V_SYNC_WID write 1EE8 = %02x\n", val);
                ibm8514_log("IBM 8514/A compatible: (0x%04x): vsyncwidth=0x%02x.\n", port, val);
                svga_recalctimings(svga);
                break;

            case 0x22e8:
                dev->disp_cntl = val;
                dev->interlace = !!(dev->disp_cntl & 0x10);
                ibm8514_log("IBM 8514/A compatible: DISP_CNTL write %04x=%02x, interlace=%d.\n", port, dev->disp_cntl, dev->interlace);
                svga_recalctimings(svga);
                break;

            case 0x42e8:
                if (val & 0x01)
                    dev->subsys_stat &= ~0x01;
                if (val & 0x02)
                    dev->subsys_stat &= ~0x02;
                if (val & 0x04)
                    dev->subsys_stat &= ~0x04;
                if (val & 0x08)
                    dev->subsys_stat &= ~0x08;
                break;
            case 0x42e9:
                dev->subsys_cntl = val;
                if (val & 0x01)
                    dev->subsys_stat |= 0x01;
                if (val & 0x02)
                    dev->subsys_stat |= 0x02;
                if (val & 0x04)
                    dev->subsys_stat |= 0x04;
                if (val & 0x08)
                    dev->subsys_stat |= 0x08;

                if ((val & 0xc0) == 0xc0) {
                    dev->fifo_idx = 0;
                    dev->force_busy = 0;
                    dev->force_busy2 = 0;
                }
                break;

            case 0x4ae8:
                WRITE8(port, dev->accel.advfunc_cntl, val);
                dev->on = dev->accel.advfunc_cntl & 0x01;
                ibm8514_log("[%04X:%08X]: IBM 8514/A: (0x%04x): ON=%d, shadow crt=%x, hdisp=%d, vdisp=%d.\n", CS, cpu_state.pc, port, dev->on, dev->accel.advfunc_cntl & 0x04, dev->hdisp, dev->vdisp);
                ibm8514_log("IBM mode set %s resolution.\n", (dev->accel.advfunc_cntl & 0x04) ? "2: 1024x768" : "1: 640x480");
                svga_recalctimings(svga);
                break;

            default:
                break;
        }
    }
}

static void
ibm8514_accel_outb(uint16_t port, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    ibm8514_accel_out(port, val, svga, 1);
}

static void
ibm8514_accel_outw(uint16_t port, uint16_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    ibm8514_accel_out(port, val, svga, 2);
}

uint16_t
ibm8514_accel_in_fifo(svga_t *svga, uint16_t port, int len)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    uint16_t temp = 0;
    int      cmd  = 0;

    switch (port) {
        case 0x82e8:
        case 0xc2e8:
            if (len == 2)
                temp = dev->accel.cur_y;
            break;

        case 0x86e8:
        case 0xc6e8:
            if (len == 2)
                temp = dev->accel.cur_x;
            break;

        case 0x92e8:
            if (len == 2)
                temp = dev->test;
            break;

        case 0x96e8:
            if (len == 2)
                temp = dev->accel.maj_axis_pcnt;
            break;

        case 0x9ae8:
        case 0xdae8:
            if ((dev->fifo_idx >= 1) && (dev->fifo_idx <= 8)) {
                temp |= (1 << (dev->fifo_idx - 1));
                switch (dev->accel.cmd >> 13) {
                    case 2:
                    case 3:
                    case 4:
                    case 6:
                        if (dev->accel.sy < 0)
                            dev->fifo_idx = 0;
                        break;
                    default:
                        if (!dev->accel.sy)
                            dev->fifo_idx = 0;
                        break;
                }
            }
            if (len == 2) {
                if (dev->force_busy)
                    temp |= 0x200; /*Hardware busy*/
                dev->force_busy = 0;
                if (dev->data_available) {
                    temp |= 0x100; /*Read Data available*/
                    dev->data_available = 0;
                }
            }
            break;
        case 0x9ae9:
        case 0xdae9:
            if (len == 1) {
                if (dev->force_busy2)
                    temp |= 0x02; /*Hardware busy*/
                dev->force_busy2 = 0;
                if (dev->data_available2) {
                    temp |= 0x01; /*Read Data available*/
                    dev->data_available2 = 0;
                }
            }
            break;

        case 0xe2e8:
        case 0xe6e8:
            if (ibm8514_cpu_dest(svga)) {
                if (len == 2) {
                    cmd = (dev->accel.cmd >> 13);
                    READ_PIXTRANS_WORD(dev->accel.cx, 0);
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
            break;

        default:
            break;
    }
    return temp;
}

uint8_t
ibm8514_accel_in(uint16_t port, svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    uint8_t   temp = 0;
    int16_t clip_t = dev->accel.clip_top;
    int16_t clip_l = dev->accel.clip_left;
    uint16_t clip_b_ibm = dev->accel.clip_bottom;
    uint16_t clip_r_ibm = dev->accel.clip_right;
    int cmd = dev->accel.cmd >> 13;

    switch (port) {
        case 0x2e8:
            if (dev->vc == dev->v_syncstart)
                temp |= 0x02;

            ibm8514_log("0x2E8 read: Display Status=%02x.\n", temp);
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
            if (dev->vc == dev->v_syncstart)
                dev->subsys_stat |= 0x01;

            if (cmd == 6) {
                if ((dev->accel.dx >= clip_l) &&
                    (dev->accel.dx <= clip_r_ibm) &&
                    (dev->accel.dy >= clip_t) &&
                    (dev->accel.dy <= clip_b_ibm))
                    dev->subsys_stat |= 0x02;
            } else {
                if ((dev->accel.cx >= clip_l) &&
                    (dev->accel.cx <= clip_r_ibm) &&
                    (dev->accel.cy >= clip_t) &&
                    (dev->accel.cy <= clip_b_ibm))
                    dev->subsys_stat |= 0x02;
            }

            if (!dev->fifo_idx) {
                if (!dev->force_busy && !dev->force_busy2)
                    temp |= 0x08;
            }

            if (port & 1)
                temp = dev->vram_512k_8514 ? 0x00 : 0x80;
            else {
                temp |= (dev->subsys_stat | (dev->vram_512k_8514 ? 0x00 : 0x80));
                temp |= 0x20;
            }
            break;

        default:
            break;
    }
    return temp;
}

static uint8_t
ibm8514_accel_inb(uint16_t port, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    uint8_t temp;

    if (port & 0x8000)
        temp = ibm8514_accel_in_fifo(svga, port, 1);
    else
        temp = ibm8514_accel_in(port, svga);

    return temp;
}

static uint16_t
ibm8514_accel_inw(uint16_t port, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    uint16_t temp;

    if (port & 0x8000)
        temp = ibm8514_accel_in_fifo(svga, port, 2);
    else {
        temp = ibm8514_accel_in(port, svga);
        temp |= (ibm8514_accel_in(port + 1, svga) << 8);
    }
    return temp;
}

void
ibm8514_short_stroke_start(int count, int cpu_input, uint32_t mix_dat, uint32_t cpu_dat, svga_t *svga, uint8_t ssv, int len)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if (!cpu_input) {
        dev->accel.ssv_len  = ssv & 0x0f;
        dev->accel.ssv_dir  = ssv & 0xe0;
        dev->accel.ssv_draw = ssv & 0x10;
        dev->accel.ssv_len_back = dev->accel.ssv_len;

        if (ibm8514_cpu_src(svga)) {
            dev->data_available  = 0;
            dev->data_available2 = 0;
            return; /*Wait for data from CPU*/
        }
    }

    ibm8514_accel_start(count, cpu_input, mix_dat, cpu_dat, svga, len);
}

void
ibm8514_accel_start(int count, int cpu_input, uint32_t mix_dat, uint32_t cpu_dat, svga_t *svga, UNUSED(int len))
{
    ibm8514_t *dev     = (ibm8514_t *) svga->dev8514;
    uint16_t  *vram_w  = (uint16_t *) dev->vram;
    uint16_t   src_dat = 0;
    uint16_t   dest_dat;
    uint16_t   old_dest_dat;
    int        frgd_mix;
    int        bkgd_mix;
    int16_t    clip_t          = dev->accel.clip_top;
    int16_t    clip_l          = dev->accel.clip_left;
    uint16_t   clip_b          = dev->accel.clip_bottom;
    uint16_t   clip_r          = dev->accel.clip_right;
    int        pixcntl         = (dev->accel.multifunc[0x0a] >> 6) & 3;
    uint16_t   mix_mask        = dev->bpp ? 0x8000 : 0x80;
    uint16_t   compare         = dev->accel.color_cmp;
    int        compare_mode    = dev->accel.multifunc[0x0a] & 0x38;
    int        cmd             = dev->accel.cmd >> 13;
    uint16_t   wrt_mask        = dev->accel.wrt_mask;
    uint16_t   rd_mask         = dev->accel.rd_mask;
    uint16_t   rd_mask_polygon = dev->accel.rd_mask;
    uint16_t   frgd_color      = dev->accel.frgd_color;
    uint16_t   bkgd_color      = dev->accel.bkgd_color;
    uint32_t   old_mix_dat;
    int        and3            = dev->accel.cur_x & 3;
    int        poly_src;

    if (!dev->bpp) {
        compare &= 0xff;
        frgd_color &= 0xff;
        bkgd_color &= 0xff;
        rd_mask = ((dev->accel.rd_mask & 0x01) << 7) | ((dev->accel.rd_mask & 0xfe) >> 1);
        rd_mask &= 0xff;
        rd_mask_polygon &= 0xff;
    }

    if (dev->accel.cmd & 0x100) {
        dev->force_busy  = 1;
        dev->force_busy2 = 1;
    }

    frgd_mix = (dev->accel.frgd_mix >> 5) & 3;
    bkgd_mix = (dev->accel.bkgd_mix >> 5) & 3;

    if (cpu_input) {
        if ((dev->accel.cmd & 0x02) || (pixcntl == 2)) {
            if ((frgd_mix == 2) || (bkgd_mix == 2))
                count >>= 3;
            else if (pixcntl == 2) {
                if (dev->accel.cmd & 0x02)
                    count >>= 1;
                else
                    count >>= 3;
            }
        } else
            count >>= 3;

        if (dev->bpp) {
            if ((dev->accel.cmd & 0x200) && (count == 2))
                count >>= 1;
        }
    }

    if (pixcntl == 1) {
        mix_dat = 0;
        if (and3 == 3) {
            if (dev->accel.multifunc[8] & 0x02)
                mix_dat |= 0x08;
            if (dev->accel.multifunc[8] & 0x04)
                mix_dat |= 0x10;
            if (dev->accel.multifunc[8] & 0x08)
                mix_dat |= 0x20;
            if (dev->accel.multifunc[8] & 0x10)
                mix_dat |= 0x40;
            if (dev->accel.multifunc[9] & 0x02)
                mix_dat |= 0x80;
            if (dev->accel.multifunc[9] & 0x04)
                mix_dat |= 0x01;
            if (dev->accel.multifunc[9] & 0x08)
                mix_dat |= 0x02;
            if (dev->accel.multifunc[9] & 0x10)
                mix_dat |= 0x04;
        }
        if (and3 == 2) {
            if (dev->accel.multifunc[8] & 0x02)
                mix_dat |= 0x04;
            if (dev->accel.multifunc[8] & 0x04)
                mix_dat |= 0x08;
            if (dev->accel.multifunc[8] & 0x08)
                mix_dat |= 0x10;
            if (dev->accel.multifunc[8] & 0x10)
                mix_dat |= 0x20;
            if (dev->accel.multifunc[9] & 0x02)
                mix_dat |= 0x40;
            if (dev->accel.multifunc[9] & 0x04)
                mix_dat |= 0x80;
            if (dev->accel.multifunc[9] & 0x08)
                mix_dat |= 0x01;
            if (dev->accel.multifunc[9] & 0x10)
                mix_dat |= 0x02;
        }
        if (and3 == 1) {
            if (dev->accel.multifunc[8] & 0x02)
                mix_dat |= 0x02;
            if (dev->accel.multifunc[8] & 0x04)
                mix_dat |= 0x04;
            if (dev->accel.multifunc[8] & 0x08)
                mix_dat |= 0x08;
            if (dev->accel.multifunc[8] & 0x10)
                mix_dat |= 0x10;
            if (dev->accel.multifunc[9] & 0x02)
                mix_dat |= 0x20;
            if (dev->accel.multifunc[9] & 0x04)
                mix_dat |= 0x40;
            if (dev->accel.multifunc[9] & 0x08)
                mix_dat |= 0x80;
            if (dev->accel.multifunc[9] & 0x10)
                mix_dat |= 0x01;
        }
        if (and3 == 0) {
            if (dev->accel.multifunc[8] & 0x02)
                mix_dat |= 0x01;
            if (dev->accel.multifunc[8] & 0x04)
                mix_dat |= 0x02;
            if (dev->accel.multifunc[8] & 0x08)
                mix_dat |= 0x04;
            if (dev->accel.multifunc[8] & 0x10)
                mix_dat |= 0x08;
            if (dev->accel.multifunc[9] & 0x02)
                mix_dat |= 0x10;
            if (dev->accel.multifunc[9] & 0x04)
                mix_dat |= 0x20;
            if (dev->accel.multifunc[9] & 0x08)
                mix_dat |= 0x40;
            if (dev->accel.multifunc[9] & 0x10)
                mix_dat |= 0x80;
        }
    }

    old_mix_dat = mix_dat;

    if (cmd == 5 || cmd == 1 || (cmd == 2 && (dev->accel.multifunc[0x0a] & 0x06)))
        ibm8514_log("CMD=%d, full=%04x, pixcntl=%d, filling=%02x.\n", cmd, dev->accel.cmd, pixcntl, dev->accel.multifunc[0x0a] & 0x06);

    /*Bit 4 of the Command register is the draw yes bit, which enables writing to memory/reading from memory when enabled.
      When this bit is disabled, no writing to memory/reading from memory is allowed. (This bit is almost meaningless on
      the NOP command)*/
    switch (cmd) {
        case 0: /*NOP (Short Stroke Vectors)*/
            if (dev->accel.ssv_state == 0)
                break;

            if (dev->accel.cmd & 0x08) {
                while (count-- && dev->accel.ssv_len >= 0) {
                    if ((dev->accel.cx >= clip_l) &&
                        (dev->accel.cx <= clip_r) &&
                        (dev->accel.cy >= clip_t) &&
                        (dev->accel.cy <= clip_b)) {
                        switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
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

                            default:
                                break;
                        }

                        READ((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);

                        if ((compare_mode == 0) ||
                            ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                            ((compare_mode == 0x18) && (dest_dat < compare)) ||
                            ((compare_mode == 0x20) && (dest_dat != compare)) ||
                            ((compare_mode == 0x28) && (dest_dat == compare)) ||
                            ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                            ((compare_mode == 0x38) && (dest_dat > compare))) {
                            old_dest_dat = dest_dat;
                            MIX(mix_dat & mix_mask, dest_dat, src_dat);
                            dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);

                            if (dev->accel.ssv_draw) {
                                if ((dev->accel.cmd & 0x04) && dev->accel.ssv_len) {
                                    WRITE((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);
                                } else if (!(dev->accel.cmd & 0x04)) {
                                    WRITE((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);
                                }
                            }
                        }
                    }

                    mix_dat <<= 1;
                    mix_dat |= 1;
                    if (dev->bpp)
                        cpu_dat >>= 16;
                    else
                        cpu_dat >>= 8;

                    if (!dev->accel.ssv_len) {
                        dev->accel.cmd_back = 1;
                        break;
                    }

                    switch (dev->accel.ssv_dir & 0xe0) {
                        case 0x00:
                            dev->accel.cx++;
                            break;
                        case 0x20:
                            dev->accel.cx++;
                            dev->accel.cy--;
                            break;
                        case 0x40:
                            dev->accel.cy--;
                            break;
                        case 0x60:
                            dev->accel.cx--;
                            dev->accel.cy--;
                            break;
                        case 0x80:
                            dev->accel.cx--;
                            break;
                        case 0xa0:
                            dev->accel.cx--;
                            dev->accel.cy++;
                            break;
                        case 0xc0:
                            dev->accel.cy++;
                            break;
                        case 0xe0:
                            dev->accel.cx++;
                            dev->accel.cy++;
                            break;

                        default:
                            break;
                    }

                    dev->accel.ssv_len--;
                }
            } else {
                while (count-- && (dev->accel.ssv_len >= 0)) {
                    if ((dev->accel.cx >= clip_l) &&
                        (dev->accel.cx <= clip_r) &&
                        (dev->accel.cy >= clip_t) &&
                        (dev->accel.cy <= clip_b)) {
                        switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
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

                            default:
                                break;
                        }

                        READ((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);

                        if ((compare_mode == 0) ||
                            ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                            ((compare_mode == 0x18) && (dest_dat < compare)) ||
                            ((compare_mode == 0x20) && (dest_dat != compare)) ||
                            ((compare_mode == 0x28) && (dest_dat == compare)) ||
                            ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                            ((compare_mode == 0x38) && (dest_dat > compare))) {
                            old_dest_dat = dest_dat;
                            MIX(mix_dat & mix_mask, dest_dat, src_dat);
                            dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);

                            if (dev->accel.ssv_draw) {
                                if ((dev->accel.cmd & 0x04) && dev->accel.ssv_len) {
                                    WRITE((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);
                                } else if (!(dev->accel.cmd & 0x04)) {
                                    WRITE((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);
                                }
                            }
                        }
                    }

                    mix_dat <<= 1;
                    mix_dat |= 1;
                    if (dev->bpp)
                        cpu_dat >>= 16;
                    else
                        cpu_dat >>= 8;

                    if (!dev->accel.ssv_len) {
                        dev->accel.cmd_back = 1;
                        break;
                    }

                    if (dev->accel.cmd & 0x40) {
                        if (dev->accel.cmd & 0x80)
                            dev->accel.cy++;
                        else
                            dev->accel.cy--;

                        if (dev->accel.err_term >= dev->accel.ssv_len_back) {
                            dev->accel.err_term += dev->accel.destx_distp;
                            if (dev->accel.cmd & 0x20)
                                dev->accel.cx++;
                            else
                                dev->accel.cx--;
                        } else
                            dev->accel.err_term += dev->accel.desty_axstp;
                    } else {
                        if (dev->accel.cmd & 0x20)
                            dev->accel.cx++;
                        else
                            dev->accel.cx--;

                        if (dev->accel.err_term >= dev->accel.ssv_len_back) {
                            dev->accel.err_term += dev->accel.destx_distp;
                            if (dev->accel.cmd & 0x80)
                                dev->accel.cy++;
                            else
                                dev->accel.cy--;
                        } else
                            dev->accel.err_term += dev->accel.desty_axstp;
                    }

                    dev->accel.ssv_len--;
                }
            }
            dev->accel.cur_x = dev->accel.cx;
            dev->accel.cur_y = dev->accel.cy;
            break;

        case 1: /*Draw line*/
            if (!cpu_input) {
                dev->accel.output = 0;
                dev->accel.x_count = 0;

                dev->accel.cx = dev->accel.cur_x;
                if (dev->accel.cur_x >= 0x600)
                    dev->accel.cx |= ~0x5ff;

                dev->accel.cy = dev->accel.cur_y;
                if (dev->accel.cur_y >= 0x600)
                    dev->accel.cy |= ~0x5ff;

                dev->accel.sy = dev->accel.maj_axis_pcnt;

                ibm8514_log("Line Draw 8514/A CMD=%04x, frgdmix=%d, bkgdmix=%d, c(%d,%d), pixcntl=%d, sy=%d, polyfill=%x, selfrmix=%02x, selbkmix=%02x, bkgdcol=%02x, frgdcol=%02x, clipt=%d, clipb=%d.\n", dev->accel.cmd, frgd_mix, bkgd_mix, dev->accel.cx, dev->accel.cy, pixcntl, dev->accel.sy, dev->accel.multifunc[0x0a] & 6, dev->accel.frgd_mix & 0x1f, dev->accel.bkgd_mix & 0x1f, bkgd_color, frgd_color, dev->accel.clip_top, clip_b);
                if (ibm8514_cpu_src(svga)) {
                    if (dev->accel.cmd & 0x02) {
                        if (!(dev->accel.cmd & 0x1000)) {
                            if (dev->accel.cmd & 0x08)
                                dev->accel.output = 1;
                        }
                    }
                    dev->data_available  = 0;
                    dev->data_available2 = 0;
                    return; /*Wait for data from CPU*/
                } else if (ibm8514_cpu_dest(svga)) {
                    dev->data_available  = 1;
                    dev->data_available2 = 1;
                    return;
                }
            }

            if (dev->accel.cmd & 0x08) { /*Vector Line*/
                if (ibm8514_cpu_dest(svga) && cpu_input && (dev->accel.cmd & 0x02))
                    count >>= 1;

                if (dev->accel.cmd & 0x02)
                    ibm8514_log("Line Draw Vector Single pixtrans=%04x, count=%d.\n", mix_dat, count);

                while (count-- && (dev->accel.sy >= 0)) {
                    if ((dev->accel.cx >= clip_l) &&
                        (dev->accel.cx <= clip_r) &&
                        (dev->accel.cy >= clip_t) &&
                        (dev->accel.cy <= clip_b)) {
                        if (ibm8514_cpu_dest(svga) && (pixcntl == 0)) {
                            mix_dat = mix_mask; /* Mix data = forced to foreground register. */
                        } else if (ibm8514_cpu_dest(svga) && (pixcntl == 3)) {
                            /* Mix data = current video memory value. */
                            READ((dev->accel.cy * dev->pitch) + dev->accel.cx, mix_dat);
                            mix_dat = ((mix_dat & rd_mask) == rd_mask);
                            mix_dat = mix_dat ? mix_mask : 0;
                        }

                        if (ibm8514_cpu_dest(svga)) {
                            READ((dev->accel.cy * dev->pitch) + dev->accel.cx, src_dat);
                            if (pixcntl == 3)
                                src_dat = ((src_dat & rd_mask) == rd_mask);
                        } else {
                            if (dev->accel.output) {
                                switch ((mix_dat & 0x01) ? frgd_mix : bkgd_mix) {
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

                                    default:
                                        break;
                                }
                            } else {
                                switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
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

                                    default:
                                        break;
                                }
                            }
                        }

                        READ((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);

                        if ((compare_mode == 0) ||
                            ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                            ((compare_mode == 0x18) && (dest_dat < compare)) ||
                            ((compare_mode == 0x20) && (dest_dat != compare)) ||
                            ((compare_mode == 0x28) && (dest_dat == compare)) ||
                            ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                            ((compare_mode == 0x38) && (dest_dat > compare))) {
                            old_dest_dat = dest_dat;
                            if (dev->accel.output) {
                                MIX(mix_dat & 0x01, dest_dat, src_dat);
                            } else {
                                MIX(mix_dat & mix_mask, dest_dat, src_dat);
                            }
                            dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                            if ((dev->accel.cmd & 0x02) && ibm8514_cpu_src(svga)) {
                                if ((dev->accel.cmd & 0x04) && dev->accel.sy) {
                                    WRITE((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);
                                } else if (!(dev->accel.cmd & 0x04)) {
                                    WRITE((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);
                                }
                            } else {
                                if (ibm8514_cpu_src(svga) || !cpu_input) {
                                    if ((dev->accel.cmd & 0x04) && dev->accel.sy) {
                                        WRITE((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);
                                    } else if (!(dev->accel.cmd & 0x04)) {
                                        WRITE((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);
                                    }
                                }
                            }
                        }
                    }

                    if (!dev->accel.sy) {
                        dev->accel.cmd_back = 1;
                        if (!cpu_input) {
                            dev->accel.cur_x = dev->accel.cx;
                            dev->accel.cur_y = dev->accel.cy;
                        }
                        break;
                    }

                    if (dev->accel.output)
                        mix_dat >>= 1;
                    else {
                        mix_dat <<= 1;
                        mix_dat |= 1;
                    }

                    if (dev->bpp)
                        cpu_dat >>= 16;
                    else
                        cpu_dat >>= 8;

                    switch (dev->accel.cmd & 0xe0) {
                        case 0x00:
                            dev->accel.cx++;
                            break;
                        case 0x20:
                            dev->accel.cx++;
                            dev->accel.cy--;
                            break;
                        case 0x40:
                            dev->accel.cy--;
                            break;
                        case 0x60:
                            dev->accel.cx--;
                            dev->accel.cy--;
                            break;
                        case 0x80:
                            dev->accel.cx--;
                            break;
                        case 0xa0:
                            dev->accel.cx--;
                            dev->accel.cy++;
                            break;
                        case 0xc0:
                            dev->accel.cy++;
                            break;
                        case 0xe0:
                            dev->accel.cx++;
                            dev->accel.cy++;
                            break;

                        default:
                            break;
                    }

                    dev->accel.sy--;
                }
                dev->accel.x_count = 0;
                dev->accel.output = 0;
            } else { /*Bresenham Line*/
                if (pixcntl == 1) {
                    dev->accel.temp_cnt = 8;
                    while (count-- && (dev->accel.sy >= 0)) {
                        if (!dev->accel.temp_cnt) {
                            dev->accel.temp_cnt = 8;
                            mix_dat = old_mix_dat;
                        }
                        if ((dev->accel.cx >= clip_l) &&
                            (dev->accel.cx <= clip_r) &&
                            (dev->accel.cy >= clip_t) &&
                            (dev->accel.cy <= clip_b)) {
                            if (ibm8514_cpu_dest(svga)) {
                                READ((dev->accel.cy * dev->pitch) + dev->accel.cx, src_dat);
                            } else
                                switch ((mix_dat & 0x01) ? frgd_mix : bkgd_mix) {
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

                                    default:
                                        break;
                                }

                            READ((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);

                            if ((compare_mode == 0) ||
                                ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                                ((compare_mode == 0x18) && (dest_dat < compare)) ||
                                ((compare_mode == 0x20) && (dest_dat != compare)) ||
                                ((compare_mode == 0x28) && (dest_dat == compare)) ||
                                ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                                ((compare_mode == 0x38) && (dest_dat > compare))) {
                                old_dest_dat = dest_dat;
                                MIX(mix_dat & 0x01, dest_dat, src_dat);
                                dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                if ((dev->accel.cmd & 0x04) && dev->accel.sy) {
                                    WRITE((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);
                                } else if (!(dev->accel.cmd & 0x04)) {
                                    WRITE((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);
                                }
                            }
                        }

                        dev->accel.temp_cnt--;
                        mix_dat >>= 1;
                        if (dev->bpp)
                            cpu_dat >>= 16;
                        else
                            cpu_dat >>= 8;

                        if (!dev->accel.sy) {
                            dev->accel.cmd_back = 1;
                            break;
                        }

                        if (dev->accel.cmd & 0x40) {
                            if (dev->accel.cmd & 0x80)
                                dev->accel.cy++;
                            else
                                dev->accel.cy--;

                            if (dev->accel.err_term >= dev->accel.maj_axis_pcnt) {
                                dev->accel.err_term += dev->accel.destx_distp;
                                if (dev->accel.cmd & 0x20)
                                    dev->accel.cx++;
                                else
                                    dev->accel.cx--;
                            } else
                                dev->accel.err_term += dev->accel.desty_axstp;
                        } else {
                            if (dev->accel.cmd & 0x20)
                                dev->accel.cx++;
                            else
                                dev->accel.cx--;

                            if (dev->accel.err_term >= dev->accel.maj_axis_pcnt) {
                                dev->accel.err_term += dev->accel.destx_distp;
                                if (dev->accel.cmd & 0x80)
                                    dev->accel.cy++;
                                else
                                    dev->accel.cy--;
                            } else
                                dev->accel.err_term += dev->accel.desty_axstp;
                        }

                        dev->accel.sy--;
                    }
                } else {
                    while (count-- && (dev->accel.sy >= 0)) {
                        if ((dev->accel.cx >= clip_l) &&
                            (dev->accel.cx <= clip_r) &&
                            (dev->accel.cy >= clip_t) &&
                            (dev->accel.cy <= clip_b)) {
                            if (ibm8514_cpu_dest(svga) && (pixcntl == 0)) {
                                mix_dat = mix_mask; /* Mix data = forced to foreground register. */
                            } else if (ibm8514_cpu_dest(svga) && (pixcntl == 3)) {
                                /* Mix data = current video memory value. */
                                READ((dev->accel.cy * dev->pitch) + dev->accel.cx, mix_dat);
                                mix_dat = ((mix_dat & rd_mask) == rd_mask);
                                mix_dat = mix_dat ? mix_mask : 0;
                            }

                            if (ibm8514_cpu_dest(svga)) {
                                READ((dev->accel.cy * dev->pitch) + dev->accel.cx, src_dat);
                                if (pixcntl == 3)
                                    src_dat = ((src_dat & rd_mask) == rd_mask);
                            } else
                                switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
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

                                    default:
                                        break;
                                }

                            READ((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);

                            if ((compare_mode == 0) ||
                                ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                                ((compare_mode == 0x18) && (dest_dat < compare)) ||
                                ((compare_mode == 0x20) && (dest_dat != compare)) ||
                                ((compare_mode == 0x28) && (dest_dat == compare)) ||
                                ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                                ((compare_mode == 0x38) && (dest_dat > compare))) {
                                old_dest_dat = dest_dat;
                                MIX(mix_dat & mix_mask, dest_dat, src_dat);
                                dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                if ((dev->accel.cmd & 0x04) && dev->accel.sy) {
                                    WRITE((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);
                                } else if (!(dev->accel.cmd & 0x04)) {
                                    WRITE((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);
                                }
                            }
                        }

                        mix_dat <<= 1;
                        mix_dat |= 1;
                        if (dev->bpp)
                            cpu_dat >>= 16;
                        else
                            cpu_dat >>= 8;

                        if (!dev->accel.sy) {
                            dev->accel.cmd_back = 1;
                            if (!cpu_input) {
                                dev->accel.cur_x = dev->accel.cx;
                                dev->accel.cur_y = dev->accel.cy;
                            }
                            break;
                        }

                        if (dev->accel.cmd & 0x40) {
                            if (dev->accel.cmd & 0x80)
                                dev->accel.cy++;
                            else
                                dev->accel.cy--;

                            if (dev->accel.err_term >= dev->accel.maj_axis_pcnt) {
                                dev->accel.err_term += dev->accel.destx_distp;
                                if (dev->accel.cmd & 0x20)
                                    dev->accel.cx++;
                                else
                                    dev->accel.cx--;
                            } else
                                dev->accel.err_term += dev->accel.desty_axstp;
                        } else {
                            if (dev->accel.cmd & 0x20)
                                dev->accel.cx++;
                            else
                                dev->accel.cx--;

                            if (dev->accel.err_term >= dev->accel.maj_axis_pcnt) {
                                dev->accel.err_term += dev->accel.destx_distp;
                                if (dev->accel.cmd & 0x80)
                                    dev->accel.cy++;
                                else
                                    dev->accel.cy--;
                            } else
                                dev->accel.err_term += dev->accel.desty_axstp;
                        }

                        dev->accel.sy--;
                    }
                }
            }
            break;

        case 2: /*Rectangle fill (X direction)*/
        case 3: /*Rectangle fill (Y direction)*/
        case 4: /*Rectangle fill (Y direction using nibbles)*/
            if (!cpu_input) {
                dev->accel.x_count = 0;
                dev->accel.output = 0;
                dev->accel.input = 0;
                dev->accel.input2 = 0;
                dev->accel.odd_in = 0;

                dev->accel.cx = dev->accel.cur_x;
                if (dev->accel.cur_x >= 0x600)
                    dev->accel.cx |= ~0x5ff;

                dev->accel.cy = dev->accel.cur_y;
                if (dev->accel.cur_y >= 0x600)
                    dev->accel.cy |= ~0x5ff;

                dev->accel.sx = dev->accel.maj_axis_pcnt & 0x7ff;
                dev->accel.sy = dev->accel.multifunc[0] & 0x7ff;

                if ((dev->accel_bpp == 24) || (dev->accel_bpp <= 8))
                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                else if (dev->bpp)
                    dev->accel.dest = (dev->accel.ge_offset << 1) + (dev->accel.cy * dev->pitch);
                else
                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);

                if (cmd == 4)
                    dev->accel.cmd |= 0x02;
                else if (cmd == 3)
                    dev->accel.cmd &= ~0x02;

                if (ibm8514_cpu_src(svga)) {
                    if (dev->accel.cmd & 0x02) {
                        if (!(dev->accel.cmd & 0x1000)) {
                            if (dev->accel.cmd & 0x08) {
                                dev->accel.x_count = dev->accel.cx - (and3 + 3);
                                dev->accel.sx += (and3 + 3);
                            } else {
                                dev->accel.x_count = dev->accel.cx;
                                if (and3) {
                                    if (dev->accel.cmd & 0x20)
                                        dev->accel.x_count -= and3;
                                    else
                                        dev->accel.x_count += and3;

                                    dev->accel.sx += 8;
                                }
                            }
                        }
                    } else {
                        if (dev->accel.cmd & 0x1000) {
                            if (dev->accel.cmd & 0x200) {
                                if (dev->accel.cmd & 0x04) {
                                    dev->accel.output = 1;
                                    dev->accel.sx -= 2;
                                }
                            }
                        }
                    }
                    dev->data_available  = 0;
                    dev->data_available2 = 0;
                    return; /*Wait for data from CPU*/
                } else if (ibm8514_cpu_dest(svga)) {
                    if (!(dev->accel.cmd & 0x02)) {
                        if (dev->accel.cmd & 0x1000) {
                            if (dev->accel.cmd & 0x200) {
                                if (!(dev->accel.sx & 1) && !(dev->accel.cmd & 0x04)) {
                                    dev->accel.input = 1;
                                } else if (dev->accel.cmd & 0x04) {
                                    dev->accel.input2 = 1;
                                    dev->accel.sx -= 2;
                                }
                            }
                        }
                    }
                    ibm8514_log("INPUT=%d.\n", dev->accel.input);
                    dev->data_available  = 1;
                    dev->data_available2 = 1;
                    return; /*Wait for data from CPU*/
                }
            }

            ibm8514_log("Rectangle %d: full=%04x, odd=%d, c(%d,%d), frgdmix=%d, bkgdmix=%d, xcount=%d, and3=%d, len(%d,%d), CURX=%d, Width=%d, pixcntl=%d, mix_dat=%08x, count=%d, cpu_data=%08x, cpu_input=%d.\n", cmd, dev->accel.cmd, dev->accel.input, dev->accel.cx, dev->accel.cy, frgd_mix, bkgd_mix, dev->accel.x_count, and3, dev->accel.sx, dev->accel.sy, dev->accel.cur_x, dev->accel.maj_axis_pcnt, pixcntl, mix_dat, count, cpu_dat, cpu_input);

            if (dev->accel.cmd & 0x08) { /*Vectored Rectangle*/
                if (cpu_input) {
                    if (ibm8514_cpu_src(svga)) {
                        while (count-- && (dev->accel.sy >= 0)) {
                            if ((dev->accel.cx >= clip_l) &&
                                (dev->accel.cx <= clip_r) &&
                                (dev->accel.cy >= clip_t) &&
                                (dev->accel.cy <= clip_b)) {
                                switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
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

                                    default:
                                        break;
                                }

                                READ(dev->accel.dest + dev->accel.cx, dest_dat);

                                if ((compare_mode == 0) ||
                                    ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                                    ((compare_mode == 0x18) && (dest_dat < compare)) ||
                                    ((compare_mode == 0x20) && (dest_dat != compare)) ||
                                    ((compare_mode == 0x28) && (dest_dat == compare)) ||
                                    ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                                    ((compare_mode == 0x38) && (dest_dat > compare))) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix_dat & mix_mask, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                    if ((dev->accel.cmd & 0x02) && (dev->accel.x_count != dev->accel.cx))
                                        goto skip_vector_rect_write;

                                    if ((dev->accel.cmd & 0x04) && dev->accel.sx) {
                                        WRITE(dev->accel.dest + dev->accel.cx, dest_dat);
                                    } else if (!(dev->accel.cmd & 0x04)) {
                                        WRITE(dev->accel.dest + dev->accel.cx, dest_dat);
                                    }
                                }
                            }

                            switch (dev->accel.cmd & 0xe0) {
                                case 0x00:
                                    dev->accel.cx++;
                                    break;
                                case 0x20:
                                    dev->accel.cx++;
                                    break;
                                case 0x60:
                                    dev->accel.cx--;
                                    break;
                                case 0x80:
                                    dev->accel.cx--;
                                    break;
                                case 0xa0:
                                    dev->accel.cx--;
                                    break;
                                case 0xe0:
                                    dev->accel.cx++;
                                    break;

                                default:
                                    break;
                            }

skip_vector_rect_write:
                            switch (dev->accel.cmd & 0xe0) {
                                case 0x00:
                                    dev->accel.x_count++;
                                    break;
                                case 0x20:
                                    dev->accel.x_count++;
                                    break;
                                case 0x60:
                                    dev->accel.x_count--;
                                    break;
                                case 0x80:
                                    dev->accel.x_count--;
                                    break;
                                case 0xa0:
                                    dev->accel.x_count--;
                                    break;
                                case 0xe0:
                                    dev->accel.x_count++;
                                    break;

                                default:
                                    break;
                            }

                            if (dev->bpp)
                                cpu_dat >>= 16;
                            else
                                cpu_dat >>= 8;

                            mix_dat <<= 1;
                            mix_dat |= 1;

                            dev->accel.sx--;
                            if (dev->accel.sx < 0) {
                                dev->accel.sx = dev->accel.maj_axis_pcnt & 0x7ff;

                                if (dev->accel.cmd & 0x20)
                                    dev->accel.cx -= (dev->accel.sx + 1);
                                else
                                    dev->accel.cx += (dev->accel.sx + 1);

                                switch (dev->accel.cmd & 0xe0) {
                                    case 0x20:
                                        dev->accel.cy--;
                                        break;
                                    case 0x40:
                                        dev->accel.cy--;
                                        break;
                                    case 0x60:
                                        dev->accel.cy--;
                                        break;
                                    case 0xa0:
                                        dev->accel.cy++;
                                        break;
                                    case 0xc0:
                                        dev->accel.cy++;
                                        break;
                                    case 0xe0:
                                        dev->accel.cy++;
                                        break;

                                    default:
                                        break;
                                }

                                if ((dev->accel_bpp == 24) || (dev->accel_bpp <= 8))
                                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                                else if (dev->bpp)
                                    dev->accel.dest = (dev->accel.ge_offset << 1) + (dev->accel.cy * dev->pitch);
                                else
                                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);

                                dev->accel.sy--;
                                dev->accel.x_count = 0;

                                if (dev->accel.sy < 0)
                                    dev->accel.cmd_back = 1;
                                return;
                            }
                        }
                    } else
                        ibm8514_log("Vectored Rectangle with destination reads (TODO).\n");
                } else
                    ibm8514_log("Vectored Rectangle with normal processing (TODO).\n");
            } else { /*Normal Rectangle*/
                if (cpu_input) {
                    while (count-- && (dev->accel.sy >= 0)) {
                        if ((dev->accel.cx >= clip_l) &&
                            (dev->accel.cx <= clip_r) &&
                            (dev->accel.cy >= clip_t) &&
                            (dev->accel.cy <= clip_b)) {
                            if (ibm8514_cpu_dest(svga) && (pixcntl == 0)) {
                                mix_dat = mix_mask; /* Mix data = forced to foreground register. */
                            } else if (ibm8514_cpu_dest(svga) && (pixcntl == 3)) {
                                /* Mix data = current video memory value. */
                                READ(dev->accel.dest + dev->accel.cx, mix_dat);
                                mix_dat = ((mix_dat & rd_mask) == rd_mask);
                                mix_dat = mix_dat ? mix_mask : 0;
                            }

                            if (ibm8514_cpu_dest(svga)) {
                                READ(dev->accel.dest + dev->accel.cx, src_dat);
                                if (pixcntl == 3)
                                    src_dat = ((src_dat & rd_mask) == rd_mask);
                            } else {
                                if (dev->accel.cmd & 0x02) {
                                    switch ((mix_dat & 0x01) ? frgd_mix : bkgd_mix) {
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

                                        default:
                                            break;
                                    }
                                } else {
                                    switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
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

                                        default:
                                            break;
                                    }
                                }
                            }

                            READ(dev->accel.dest + dev->accel.cx, dest_dat);

                            if ((compare_mode == 0) ||
                                ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                                ((compare_mode == 0x18) && (dest_dat < compare)) ||
                                ((compare_mode == 0x20) && (dest_dat != compare)) ||
                                ((compare_mode == 0x28) && (dest_dat == compare)) ||
                                ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                                ((compare_mode == 0x38) && (dest_dat > compare))) {
                                old_dest_dat = dest_dat;
                                if (dev->accel.cmd & 0x02) {
                                    MIX(mix_dat & 0x01, dest_dat, src_dat);
                                    if ((dev->accel.x_count != dev->accel.cx) && !(dev->accel.cmd & 0x1000) && and3)
                                        goto skip_nibble_rect_write;

                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                    if ((dev->accel.cmd & 0x04) && dev->accel.sx) {
                                        WRITE(dev->accel.dest + dev->accel.cx, dest_dat);
                                    } else if (!(dev->accel.cmd & 0x04)) {
                                        WRITE(dev->accel.dest + dev->accel.cx, dest_dat);
                                    }
                                } else {
                                    if (ibm8514_cpu_dest(svga) && (cmd == 2)) {
                                        if (pixcntl == 3) {
                                            MIX(mix_dat & mix_mask, dest_dat, src_dat);
                                        }
                                    } else {
                                        MIX(mix_dat & mix_mask, dest_dat, src_dat);
                                    }
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                    if ((dev->accel.cmd & 0x04) && dev->accel.sx) {
                                        WRITE(dev->accel.dest + dev->accel.cx, dest_dat);
                                    } else if (!(dev->accel.cmd & 0x04)) {
                                        WRITE(dev->accel.dest + dev->accel.cx, dest_dat);
                                    }
                                }
                            }
                        }
                        if (dev->accel.cmd & 0x20)
                            dev->accel.cx++;
                        else
                            dev->accel.cx--;

skip_nibble_rect_write:
                        if (dev->accel.cmd & 0x20)
                            dev->accel.x_count++;
                        else
                            dev->accel.x_count--;

                        if (dev->accel.cmd & 0x02)
                            mix_dat >>= 1;
                        else {
                            mix_dat <<= 1;
                            mix_dat |= 1;
                        }

                        if (dev->bpp)
                            cpu_dat >>= 16;
                        else
                            cpu_dat >>= 8;

                        dev->accel.sx--;
                        if (dev->accel.sx < 0) {
                            dev->accel.sx = dev->accel.maj_axis_pcnt & 0x7ff;
                            if (dev->accel.input)
                                dev->accel.odd_in = 1;
                            if (dev->accel.output || dev->accel.input2)
                                dev->accel.sx -= 2;

                            if (dev->accel.cmd & 0x20)
                                dev->accel.cx -= (dev->accel.sx + 1);
                            else
                                dev->accel.cx += (dev->accel.sx + 1);

                            if (dev->accel.cmd & 0x80)
                                dev->accel.cy++;
                            else
                                dev->accel.cy--;

                            if ((dev->accel_bpp == 24) || (dev->accel_bpp <= 8))
                                dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                            else if (dev->bpp)
                                dev->accel.dest = (dev->accel.ge_offset << 1) + (dev->accel.cy * dev->pitch);
                            else
                                dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);

                            dev->accel.sy--;
                            dev->accel.x_count = 0;

                            if (dev->accel.sy < 0)
                                dev->accel.cmd_back = 1;
                            return;
                        }
                    }
                } else {
                    if (pixcntl == 1) {
                        if (dev->accel.cmd & 0x40) {
                            count = (dev->accel.maj_axis_pcnt & 0x7ff) + 1;
                            dev->accel.temp_cnt = 8;
                            while (count-- && dev->accel.sy >= 0) {
                                if (!dev->accel.temp_cnt) {
                                    mix_dat >>= 8;
                                    dev->accel.temp_cnt = 8;
                                }
                                if ((dev->accel.cx >= clip_l) &&
                                    (dev->accel.cx <= clip_r) &&
                                    (dev->accel.cy >= clip_t) &&
                                    (dev->accel.cy <= clip_b)) {
                                    switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
                                        case 0:
                                            src_dat = bkgd_color;
                                            break;
                                        case 1:
                                            src_dat = frgd_color;
                                            break;
                                        case 2:
                                        case 3:
                                            src_dat = 0;
                                            break;

                                        default:
                                            break;
                                    }

                                    READ(dev->accel.dest + dev->accel.cx, dest_dat);

                                    if ((compare_mode == 0) ||
                                        ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                                        ((compare_mode == 0x18) && (dest_dat < compare)) ||
                                        ((compare_mode == 0x20) && (dest_dat != compare)) ||
                                        ((compare_mode == 0x28) && (dest_dat == compare)) ||
                                        ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                                        ((compare_mode == 0x38) && (dest_dat > compare))) {
                                        old_dest_dat = dest_dat;
                                        MIX(mix_dat & mix_mask, dest_dat, src_dat);
                                        dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                        WRITE(dev->accel.dest + dev->accel.cx, dest_dat);
                                    }
                                }

                                if (dev->accel.temp_cnt > 0) {
                                    dev->accel.temp_cnt--;
                                    mix_dat <<= 1;
                                    mix_dat |= 1;
                                }

                                if (dev->accel.cmd & 0x20)
                                    dev->accel.cx++;
                                else
                                    dev->accel.cx--;

                                dev->accel.sx--;
                                if (dev->accel.sx < 0) {
                                    dev->accel.sx = dev->accel.maj_axis_pcnt & 0x7ff;

                                    if (dev->accel.cmd & 0x20) {
                                        dev->accel.cx -= (dev->accel.sx + 1);
                                    } else
                                        dev->accel.cx += (dev->accel.sx + 1);

                                    if (dev->accel.cmd & 0x80)
                                        dev->accel.cy++;
                                    else
                                        dev->accel.cy--;

                                    if ((dev->accel_bpp == 24) || (dev->accel_bpp <= 8))
                                        dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                                    else if (dev->bpp)
                                        dev->accel.dest = (dev->accel.ge_offset << 1) + (dev->accel.cy * dev->pitch);
                                    else
                                        dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);

                                    dev->accel.sy--;

                                    if (dev->accel.sy < 0)
                                        dev->accel.cmd_back = 1;
                                    return;
                                }
                            }
                        } else {
                            dev->accel.temp_cnt = 8;
                            while (count-- && (dev->accel.sy >= 0)) {
                                if (!dev->accel.temp_cnt) {
                                    dev->accel.temp_cnt = 8;
                                    mix_dat = old_mix_dat;
                                }
                                if ((dev->accel.cx >= clip_l) &&
                                    (dev->accel.cx <= clip_r) &&
                                    (dev->accel.cy >= clip_t) &&
                                    (dev->accel.cy <= clip_b)) {
                                    switch ((mix_dat & 0x01) ? frgd_mix : bkgd_mix) {
                                        case 0:
                                            src_dat = bkgd_color;
                                            break;
                                        case 1:
                                            src_dat = frgd_color;
                                            break;
                                        case 2:
                                        case 3:
                                            src_dat = 0;
                                            break;

                                        default:
                                            break;
                                    }

                                    READ(dev->accel.dest + dev->accel.cx, dest_dat);

                                    if ((compare_mode == 0) ||
                                        ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                                        ((compare_mode == 0x18) && (dest_dat < compare)) ||
                                        ((compare_mode == 0x20) && (dest_dat != compare)) ||
                                        ((compare_mode == 0x28) && (dest_dat == compare)) ||
                                        ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                                        ((compare_mode == 0x38) && (dest_dat > compare))) {
                                        old_dest_dat = dest_dat;
                                        MIX(mix_dat & 0x01, dest_dat, src_dat);
                                        dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                        WRITE(dev->accel.dest + dev->accel.cx, dest_dat);
                                    }
                                }

                                dev->accel.temp_cnt--;
                                mix_dat >>= 1;

                                if (dev->accel.cmd & 0x20)
                                    dev->accel.cx++;
                                else
                                    dev->accel.cx--;

                                dev->accel.sx--;
                                if (dev->accel.sx < 0) {
                                    dev->accel.sx = dev->accel.maj_axis_pcnt & 0x7ff;

                                    if (dev->accel.cmd & 0x20) {
                                        dev->accel.cx -= (dev->accel.sx + 1);
                                    } else
                                        dev->accel.cx += (dev->accel.sx + 1);

                                    if (dev->accel.cmd & 0x80)
                                        dev->accel.cy++;
                                    else
                                        dev->accel.cy--;

                                    if ((dev->accel_bpp == 24) || (dev->accel_bpp <= 8))
                                        dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                                    else if (dev->bpp)
                                        dev->accel.dest = (dev->accel.ge_offset << 1) + (dev->accel.cy * dev->pitch);
                                    else
                                        dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);

                                    dev->accel.sy--;

                                    if (dev->accel.sy < 0) {
                                        if (cmd != 4) {
                                            dev->accel.cur_x = dev->accel.cx;
                                            dev->accel.cur_y = dev->accel.cy;
                                        }
                                        dev->accel.cmd_back = 1;
                                        return;
                                    }
                                }
                            }
                        }
                    } else if ((dev->accel.multifunc[0x0a] & 0x06) == 0x04) { /*Polygon Draw Type A*/
                        ibm8514_log("Polygon Draw Type A: Clipping: L=%d, R=%d, T=%d, B=%d, C(%d,%d), sx=%d, sy=%d.\n", clip_l, clip_r, clip_t, clip_b, dev->accel.cx, dev->accel.cy, dev->accel.sx, dev->accel.sy);
                        while (count-- && (dev->accel.sy >= 0)) {
                            if ((dev->accel.cx >= clip_l) &&
                                (dev->accel.cx <= clip_r) &&
                                (dev->accel.cy >= clip_t) &&
                                (dev->accel.cy <= clip_b)) {
                                switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
                                    case 0:
                                        src_dat = bkgd_color;
                                        break;
                                    case 1:
                                        src_dat = frgd_color;
                                        break;
                                    case 2:
                                    case 3:
                                        src_dat = 0;
                                        break;

                                    default:
                                        break;
                                }

                                READ(dev->accel.dest + dev->accel.cx, poly_src);
                                if ((poly_src & rd_mask_polygon) == rd_mask_polygon)
                                    dev->accel.fill_state ^= 1;

                                READ(dev->accel.dest + dev->accel.cx, dest_dat);

                                old_dest_dat = dest_dat;
                                if (dev->accel.fill_state) {
                                    if (rd_mask_polygon & 0x01) {
                                        if (wrt_mask & 0x01) {
                                            dest_dat &= ~(rd_mask_polygon & wrt_mask); /*Fill State On, Write Mask 1, Read Mask 1.*/
                                            dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                        }
                                    } else {
                                        if (wrt_mask & 0x01) {
                                            MIX(mix_dat & mix_mask, dest_dat, src_dat);
                                            dest_dat &= ~rd_mask_polygon; /*Fill State On, Write Mask 1, Read Mask 0.*/
                                            dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                        }
                                    }
                                } else {
                                    if (rd_mask_polygon & 0x01) {
                                        if (wrt_mask & 0x01) {
                                            dest_dat &= ~(rd_mask_polygon & wrt_mask); /*Fill State Off, Write Mask 1, Read Mask 1.*/
                                            dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                        }
                                    } else {
                                        if (wrt_mask & 0x01) {
                                            dest_dat &= ~rd_mask_polygon; /*Fill State Off, Write Mask 1, Read Mask 0.*/
                                            dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                        }
                                    }
                                }

                                if ((compare_mode == 0) ||
                                    ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                                    ((compare_mode == 0x18) && (dest_dat < compare)) ||
                                    ((compare_mode == 0x20) && (dest_dat != compare)) ||
                                    ((compare_mode == 0x28) && (dest_dat == compare)) ||
                                    ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                                    ((compare_mode == 0x38) && (dest_dat > compare))) {
                                    ibm8514_log("Results c(%d,%d):rdmask=%02x, wrtmask=%02x, mix=%02x, destdat=%02x, nowrite=%d.\n", dev->accel.cx, dev->accel.cy, rd_mask_polygon, wrt_mask, mix_dat, dest_dat, dev->accel.cx_back);
                                    WRITE(dev->accel.dest + dev->accel.cx, dest_dat);
                                }
                            } else
                                ibm8514_log("Out of bounds DrawA C(%d,%d).\n", dev->accel.cx, dev->accel.cy);

                            mix_dat <<= 1;
                            mix_dat |= 1;

                            if (dev->accel.cmd & 0x20)
                                dev->accel.cx++;
                            else
                                dev->accel.cx--;

                            dev->accel.sx--;
                            if (dev->accel.sx < 0) {
                                dev->accel.fill_state = 0;
                                dev->accel.sx = dev->accel.maj_axis_pcnt & 0x7ff;

                                if (dev->accel.cmd & 0x20)
                                    dev->accel.cx -= (dev->accel.sx + 1);
                                else
                                    dev->accel.cx += (dev->accel.sx + 1);

                                if (dev->accel.cmd & 0x80)
                                    dev->accel.cy++;
                                else
                                    dev->accel.cy--;

                                if ((dev->accel_bpp == 24) || (dev->accel_bpp <= 8))
                                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                                else if (dev->bpp)
                                    dev->accel.dest = (dev->accel.ge_offset << 1) + (dev->accel.cy * dev->pitch);
                                else
                                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);

                                dev->accel.sy--;

                                if (dev->accel.sy < 0) {
                                    ibm8514_log(".\n");
                                    dev->accel.cmd_back = 1;
                                    dev->accel.cur_x = dev->accel.cx;
                                    dev->accel.cur_y = dev->accel.cy;
                                    return;
                                }
                            }
                        }
                    } else {
                        ibm8514_log("Polygon Draw Type=%02x, CX=%d, CY=%d, SY=%d, CL=%d, CR=%d.\n", dev->accel.multifunc[0x0a] & 0x06, dev->accel.cx, dev->accel.cy, dev->accel.sy, clip_l, clip_r);
                        while (count-- && (dev->accel.sy >= 0)) {
                            if ((dev->accel.cx >= clip_l) &&
                                (dev->accel.cx <= clip_r) &&
                                (dev->accel.cy >= clip_t) &&
                                (dev->accel.cy <= clip_b)) {
                                switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
                                    case 0:
                                        src_dat = bkgd_color;
                                        if (!bkgd_mix && (dev->accel.cmd & 0x40) && ((dev->accel.frgd_mix & 0x1f) == 7) && ((dev->accel.bkgd_mix & 0x1f) == 3) && !dev->bpp && (bkgd_color == 0x00)) /*For some reason, the September 1992 Mach8/32 drivers for Win3.x don't set the background colors properly.*/
                                            src_dat = frgd_color;
                                        break;
                                    case 1:
                                        src_dat = frgd_color;
                                        break;
                                    case 2:
                                    case 3:
                                        src_dat = 0;
                                        break;

                                    default:
                                        break;
                                }

                                READ(dev->accel.dest + dev->accel.cx, dest_dat);

                                if ((compare_mode == 0) ||
                                    ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                                    ((compare_mode == 0x18) && (dest_dat < compare)) ||
                                    ((compare_mode == 0x20) && (dest_dat != compare)) ||
                                    ((compare_mode == 0x28) && (dest_dat == compare)) ||
                                    ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                                    ((compare_mode == 0x38) && (dest_dat > compare))) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix_dat & mix_mask, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);

                                    if (dev->accel.cmd & 0x10) {
                                        WRITE(dev->accel.dest + dev->accel.cx, dest_dat);
                                    }
                                }
                            } else
                                ibm8514_log("Outside clipping.\n");

                            mix_dat <<= 1;
                            mix_dat |= 1;

                            if (dev->accel.cmd & 0x20)
                                dev->accel.cx++;
                            else
                                dev->accel.cx--;

                            dev->accel.sx--;
                            if (dev->accel.sx < 0) {
                                dev->accel.fill_state = 0;
                                dev->accel.sx = dev->accel.maj_axis_pcnt & 0x7ff;

                                if (dev->accel.cmd & 0x20)
                                    dev->accel.cx -= (dev->accel.sx + 1);
                                else
                                    dev->accel.cx += (dev->accel.sx + 1);

                                if (dev->accel.cmd & 0x80)
                                    dev->accel.cy++;
                                else
                                    dev->accel.cy--;

                                if ((dev->accel_bpp == 24) || (dev->accel_bpp <= 8))
                                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                                else if (dev->bpp)
                                    dev->accel.dest = (dev->accel.ge_offset << 1) + (dev->accel.cy * dev->pitch);
                                else
                                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);

                                dev->accel.sy--;
                                if (dev->accel.sy < 0) {
                                    if (cmd != 4) {
                                        dev->accel.cur_x = dev->accel.cx;
                                        dev->accel.cur_y = dev->accel.cy;
                                    }
                                    dev->accel.cmd_back = 1;
                                    return;
                                }
                            }
                        }
                    }
                }
            }
            break;

        case 5: /*Draw Polygon Boundary Line*/ {
            if (!cpu_input) {
                dev->accel.sy = dev->accel.maj_axis_pcnt_no_limit;

                dev->accel.cx = dev->accel.cur_x;
                if (dev->accel.cur_x >= 0x600)
                    dev->accel.cx |= ~0x5ff;

                dev->accel.cy = dev->accel.cur_y;
                if (dev->accel.cur_y >= 0x600)
                    dev->accel.cy |= ~0x5ff;

                if (dev->accel.cmd & 0x80)
                    dev->accel.oldcy = dev->accel.cy + 1;
                else
                    dev->accel.oldcy = dev->accel.cy - 1;

                ibm8514_log("Polygon Boundary activated=%04x, len=%d, cur(%d,%d), frgdmix=%02x, err=%d, clipping: l=%d, r=%d, t=%d, b=%d, pixcntl=%02x.\n", dev->accel.cmd, dev->accel.sy, dev->accel.cx, dev->accel.cy, dev->accel.frgd_mix & 0x1f, dev->accel.err_term, clip_l, clip_r, clip_t, clip_b, dev->accel.multifunc[0x0a]);

                if (ibm8514_cpu_src(svga)) {
                    dev->data_available  = 0;
                    dev->data_available2 = 0;
                    return; /*Wait for data from CPU*/
                } else if (ibm8514_cpu_dest(svga)) {
                    dev->data_available  = 1;
                    dev->data_available2 = 1;
                    return;
                }
            }

            if (dev->accel.cmd & 0x08) { /*Vectored Boundary Line*/
                while (count-- && (dev->accel.sy >= 0)) {
                    dev->accel.cx = CLAMP(dev->accel.cx, clip_l, clip_r);

                    if ((dev->accel.cx >= clip_l) &&
                        (dev->accel.cx <= clip_r) &&
                        (dev->accel.cy >= clip_t) &&
                        (dev->accel.cy <= clip_b)) {
                        switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
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

                            default:
                                break;
                        }


                        READ((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);

                        if ((compare_mode == 0) ||
                            ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                            ((compare_mode == 0x18) && (dest_dat < compare)) ||
                            ((compare_mode == 0x20) && (dest_dat != compare)) ||
                            ((compare_mode == 0x28) && (dest_dat == compare)) ||
                            ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                            ((compare_mode == 0x38) && (dest_dat > compare))) {
                            old_dest_dat = dest_dat;
                            MIX(mix_dat & mix_mask, dest_dat, src_dat);
                            dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                            if (dev->accel.cmd & 0x10) {
                                if (dev->accel.sy && (dev->accel.cmd & 0x04)) {
                                    if (dev->accel.oldcy != dev->accel.cy) {
                                        WRITE((dev->accel.cy * dev->pitch) + (dev->accel.cx), dest_dat);
                                    }
                                } else if (!(dev->accel.cmd & 0x04)) {
                                    if (dev->accel.oldcy != dev->accel.cy) {
                                        WRITE((dev->accel.cy * dev->pitch) + (dev->accel.cx), dest_dat);
                                    }
                                }
                            }
                        }
                    }

                    mix_dat <<= 1;
                    mix_dat |= 1;
                    if (dev->bpp)
                        cpu_dat >>= 16;
                    else
                        cpu_dat >>= 8;

                    if (!dev->accel.sy) {
                        dev->accel.cmd_back = 1;
                        break;
                    }

                    switch (dev->accel.cmd & 0xe0) {
                        case 0x00:
                            dev->accel.cx++;
                            break;
                        case 0x20:
                            dev->accel.cx++;
                            dev->accel.oldcy = dev->accel.cy;
                            dev->accel.cy--;
                            break;
                        case 0x40:
                            dev->accel.oldcy = dev->accel.cy;
                            dev->accel.cy--;
                            break;
                        case 0x60:
                            dev->accel.cx--;
                            dev->accel.oldcy = dev->accel.cy;
                            dev->accel.cy--;
                            break;
                        case 0x80:
                            dev->accel.cx--;
                            break;
                        case 0xa0:
                            dev->accel.cx--;
                            dev->accel.oldcy = dev->accel.cy;
                            dev->accel.cy++;
                            break;
                        case 0xc0:
                            dev->accel.oldcy = dev->accel.cy;
                            dev->accel.cy++;
                            break;
                        case 0xe0:
                            dev->accel.cx++;
                            dev->accel.oldcy = dev->accel.cy;
                            dev->accel.cy++;
                            break;

                        default:
                            break;
                    }

                    dev->accel.sy--;
                }
            } else { /*Vectored Bresenham*/
                while (count-- && (dev->accel.sy >= 0)) {
                    dev->accel.cx = CLAMP(dev->accel.cx, clip_l, clip_r);

                    if ((dev->accel.cx >= clip_l) &&
                        (dev->accel.cx < clip_r) &&
                        (dev->accel.cy >= clip_t) &&
                        (dev->accel.cy <= clip_b)) {
                        switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
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

                            default:
                                break;
                        }

                        READ((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);

                        if ((compare_mode == 0) ||
                            ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                            ((compare_mode == 0x18) && (dest_dat < compare)) ||
                            ((compare_mode == 0x20) && (dest_dat != compare)) ||
                            ((compare_mode == 0x28) && (dest_dat == compare)) ||
                            ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                            ((compare_mode == 0x38) && (dest_dat > compare))) {
                            old_dest_dat = dest_dat;
                            MIX(mix_dat & mix_mask, dest_dat, src_dat);
                            dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);

                            if ((dev->accel.cmd & 0x14) == 0x14) {
                                if (dev->accel.sy) {
                                    if (dev->accel.oldcy != dev->accel.cy) {
                                        WRITE((dev->accel.cy * dev->pitch) + dev->accel.cx, dest_dat);
                                    }
                                }
                            }
                        }
                    }

                    mix_dat <<= 1;
                    mix_dat |= 1;
                    if (dev->bpp)
                        cpu_dat >>= 16;
                    else
                        cpu_dat >>= 8;

                    if (!dev->accel.sy) {
                        dev->accel.cmd_back = 1;
                        break;
                    }

                    if (dev->accel.cmd & 0x40) {
                        dev->accel.oldcy = dev->accel.cy;
                        if (dev->accel.cmd & 0x80)
                            dev->accel.cy++;
                        else
                            dev->accel.cy--;

                        if (dev->accel.err_term >= dev->accel.maj_axis_pcnt_no_limit) {
                            dev->accel.err_term += dev->accel.destx_distp;
                            if (dev->accel.cmd & 0x20)
                                dev->accel.cx++;
                            else
                                dev->accel.cx--;
                        } else
                            dev->accel.err_term += dev->accel.desty_axstp;
                    } else {
                        if (dev->accel.cmd & 0x20)
                            dev->accel.cx++;
                        else
                            dev->accel.cx--;

                        dev->accel.oldcy = dev->accel.cy;
                        if (dev->accel.err_term >= dev->accel.maj_axis_pcnt_no_limit) {
                            dev->accel.err_term += dev->accel.destx_distp;
                            if (dev->accel.cmd & 0x80)
                                dev->accel.cy++;
                            else
                                dev->accel.cy--;
                        } else
                            dev->accel.err_term += dev->accel.desty_axstp;
                    }

                    dev->accel.sy--;
                }
            }
        }
        break;

        case 6: /*BitBlt*/
            if (!cpu_input) /*!cpu_input is trigger to start operation*/
            {
                dev->accel.x_count = 0;

                dev->accel.dx = dev->accel.destx;
                if (dev->accel.destx >= 0x600)
                    dev->accel.dx |= ~0x5ff;

                dev->accel.dy = dev->accel.desty;
                if (dev->accel.desty >= 0x600)
                    dev->accel.dy |= ~0x5ff;

                dev->accel.cx = dev->accel.cur_x;
                if (dev->accel.cur_x >= 0x600)
                    dev->accel.cx |= ~0x5ff;

                dev->accel.cy = dev->accel.cur_y;
                if (dev->accel.cur_y >= 0x600)
                    dev->accel.cy |= ~0x5ff;

                dev->accel.sx = dev->accel.maj_axis_pcnt & 0x7ff;
                dev->accel.sy = dev->accel.multifunc[0] & 0x7ff;

                if ((dev->accel_bpp == 24) || (dev->accel_bpp <= 8)) {
                    dev->accel.src  = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.dy * dev->pitch);
                } else if (dev->bpp) {
                    dev->accel.src  = (dev->accel.ge_offset << 1) + (dev->accel.cy * dev->pitch);
                    dev->accel.dest = (dev->accel.ge_offset << 1) + (dev->accel.dy * dev->pitch);
                } else {
                    dev->accel.src  = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.dy * dev->pitch);
                }
                dev->accel.fill_state = 0;

                if (ibm8514_cpu_src(svga)) {
                    if (dev->accel.cmd & 0x02) {
                        if (!(dev->accel.cmd & 0x1000)) {
                            dev->accel.x_count = dev->accel.cx;
                            if (and3) {
                                if (dev->accel.cmd & 0x20)
                                    dev->accel.x_count -= and3;
                                else
                                    dev->accel.x_count += and3;

                                dev->accel.sx += 8;
                            }
                        }
                    }
                    dev->data_available  = 0;
                    dev->data_available2 = 0;
                    return; /*Wait for data from CPU*/
                } else if (ibm8514_cpu_dest(svga)) {
                    dev->data_available  = 1;
                    dev->data_available2 = 1;
                    return; /*Wait for data from CPU*/
                }
            }

            ibm8514_log("BitBLT: full=%04x, odd=%d, c(%d,%d), d(%d,%d), xcount=%d, and3=%d, len(%d,%d), CURX=%d, Width=%d, pixcntl=%d, mix_dat=%08x, count=%d, cpu_data=%08x, cpu_input=%d.\n", dev->accel.cmd, dev->accel.input, dev->accel.cx, dev->accel.cy, dev->accel.dx, dev->accel.dy, dev->accel.x_count, and3, dev->accel.sx, dev->accel.sy, dev->accel.cur_x, dev->accel.maj_axis_pcnt, pixcntl, mix_dat, count, cpu_dat, cpu_input);
            if (cpu_input) {
                while (count-- && (dev->accel.sy >= 0)) {
                    if ((dev->accel.dx >= clip_l) &&
                        (dev->accel.dx <= clip_r) &&
                        (dev->accel.dy >= clip_t) &&
                        (dev->accel.dy <= clip_b)) {
                        if (pixcntl == 3) {
                            if (!(dev->accel.cmd & 0x10) && ((frgd_mix != 3) || (bkgd_mix != 3))) {
                                READ(dev->accel.src + dev->accel.cx, mix_dat);
                                mix_dat = ((mix_dat & rd_mask) == rd_mask);
                                if (dev->accel.cmd & 0x02)
                                    mix_dat = mix_dat ? 0x01 : 0x00;
                                else
                                    mix_dat = mix_dat ? mix_mask : 0x00;
                            } else if (dev->accel.cmd & 0x10) {
                                READ(dev->accel.src + dev->accel.cx, mix_dat);
                                mix_dat = ((mix_dat & rd_mask) == rd_mask);
                                if (dev->accel.cmd & 0x02)
                                    mix_dat = mix_dat ? 0x01 : 0x00;
                                else
                                    mix_dat = mix_dat ? mix_mask : 0x00;
                            }
                        }
                        if (dev->accel.cmd & 0x02) {
                            switch ((mix_dat & 0x01) ? frgd_mix : bkgd_mix) {
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
                                    if (pixcntl == 3) {
                                        if (dev->accel.cmd & 0x10)
                                            src_dat = ((src_dat & rd_mask) == rd_mask);
                                    }
                                    break;

                                default:
                                    break;
                            }
                        } else {
                            switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
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
                                    if (pixcntl == 3) {
                                        if (dev->accel.cmd & 0x10)
                                            src_dat = ((src_dat & rd_mask) == rd_mask);
                                    }
                                    break;

                                default:
                                    break;
                            }
                        }

                        READ(dev->accel.dest + dev->accel.dx, dest_dat);

                        if ((compare_mode == 0) ||
                            ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                            ((compare_mode == 0x18) && (dest_dat < compare)) ||
                            ((compare_mode == 0x20) && (dest_dat != compare)) ||
                            ((compare_mode == 0x28) && (dest_dat == compare)) ||
                            ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                            ((compare_mode == 0x38) && (dest_dat > compare))) {
                            old_dest_dat = dest_dat;
                            if (dev->accel.cmd & 0x02) {
                                MIX(mix_dat & 0x01, dest_dat, src_dat);
                                if ((dev->accel.x_count != dev->accel.cx) && !(dev->accel.cmd & 0x1000) && and3)
                                    goto skip_nibble_bitblt_write;

                                dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                WRITE(dev->accel.dest + dev->accel.dx, dest_dat);
                            } else {
                                MIX(mix_dat & mix_mask, dest_dat, src_dat);
                                dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                WRITE(dev->accel.dest + dev->accel.dx, dest_dat);
                            }
                        }
                    }

                    if (dev->accel.cmd & 0x20) {
                        dev->accel.dx++;
                        dev->accel.cx++;
                    } else {
                        dev->accel.dx--;
                        dev->accel.cx--;
                    }

skip_nibble_bitblt_write:
                    if (dev->accel.cmd & 0x20)
                        dev->accel.x_count++;
                    else
                        dev->accel.x_count--;

                    if (dev->accel.cmd & 0x02)
                        mix_dat >>= 1;
                    else {
                        mix_dat <<= 1;
                        mix_dat |= 1;
                    }

                    if (dev->bpp)
                        cpu_dat >>= 16;
                    else
                        cpu_dat >>= 8;

                    dev->accel.sx--;
                    if (dev->accel.sx < 0) {
                        dev->accel.sx = dev->accel.maj_axis_pcnt & 0x7ff;

                        if (dev->accel.cmd & 0x20) {
                            dev->accel.dx -= (dev->accel.sx + 1);
                            dev->accel.cx -= (dev->accel.sx + 1);
                        } else {
                            dev->accel.dx += (dev->accel.sx + 1);
                            dev->accel.cx += (dev->accel.sx + 1);
                        }

                        if (dev->accel.cmd & 0x80) {
                            dev->accel.dy++;
                            dev->accel.cy++;
                        } else {
                            dev->accel.dy--;
                            dev->accel.cy--;
                        }

                        if ((dev->accel_bpp == 24) || (dev->accel_bpp <= 8)) {
                            dev->accel.src  = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                            dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.dy * dev->pitch);
                        } else if (dev->bpp) {
                            dev->accel.src  = (dev->accel.ge_offset << 1) + (dev->accel.cy * dev->pitch);
                            dev->accel.dest = (dev->accel.ge_offset << 1) + (dev->accel.dy * dev->pitch);
                        } else {
                            dev->accel.src  = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                            dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.dy * dev->pitch);
                        }

                        dev->accel.sy--;
                        dev->accel.x_count = 0;

                        if (dev->accel.sy < 0)
                            dev->accel.cmd_back = 1;
                        return;
                    }
                }
            } else {
                if (pixcntl == 1) {
                    if (dev->accel.cmd & 0x40) {
                        count = (dev->accel.maj_axis_pcnt & 0x7ff) + 1;
                        dev->accel.temp_cnt = 8;
                        while (count-- && (dev->accel.sy >= 0)) {
                            if (!dev->accel.temp_cnt) {
                                mix_dat >>= 8;
                                dev->accel.temp_cnt = 8;
                            }
                            if ((dev->accel.dx >= clip_l) &&
                                (dev->accel.dx <= clip_r) &&
                                (dev->accel.dy >= clip_t) &&
                                (dev->accel.dy <= clip_b)) {
                                switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
                                    case 0:
                                        src_dat = bkgd_color;
                                        break;
                                    case 1:
                                        src_dat = frgd_color;
                                        break;
                                    case 2:
                                        src_dat = 0;
                                        break;
                                    case 3:
                                        READ(dev->accel.src + dev->accel.cx, src_dat);
                                        break;

                                    default:
                                        break;
                                }

                                READ(dev->accel.dest + dev->accel.dx, dest_dat);

                                if ((compare_mode == 0) ||
                                    ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                                    ((compare_mode == 0x18) && (dest_dat < compare)) ||
                                    ((compare_mode == 0x20) && (dest_dat != compare)) ||
                                    ((compare_mode == 0x28) && (dest_dat == compare)) ||
                                    ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                                    ((compare_mode == 0x38) && (dest_dat > compare))) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix_dat & mix_mask, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                    WRITE(dev->accel.dest + dev->accel.dx, dest_dat);
                                }
                            }

                            if (dev->accel.temp_cnt > 0) {
                                dev->accel.temp_cnt--;
                                mix_dat <<= 1;
                                mix_dat |= 1;
                            }

                            if (dev->accel.cmd & 0x20) {
                                dev->accel.dx++;
                                dev->accel.cx++;
                            } else {
                                dev->accel.dx--;
                                dev->accel.cx--;
                            }

                            dev->accel.sx--;
                            if (dev->accel.sx < 0) {
                                dev->accel.sx = dev->accel.maj_axis_pcnt & 0x7ff;

                                if (dev->accel.cmd & 0x20) {
                                    dev->accel.dx -= (dev->accel.sx + 1);
                                    dev->accel.cx -= (dev->accel.sx + 1);
                                } else {
                                    dev->accel.dx += (dev->accel.sx + 1);
                                    dev->accel.cx += (dev->accel.sx + 1);
                                }

                                if (dev->accel.cmd & 0x80) {
                                    dev->accel.dy++;
                                    dev->accel.cy++;
                                } else {
                                    dev->accel.dy--;
                                    dev->accel.cy--;
                                }

                                if ((dev->accel_bpp == 24) || (dev->accel_bpp <= 8)) {
                                    dev->accel.src  = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.dy * dev->pitch);
                                } else if (dev->bpp) {
                                    dev->accel.src  = (dev->accel.ge_offset << 1) + (dev->accel.cy * dev->pitch);
                                    dev->accel.dest = (dev->accel.ge_offset << 1) + (dev->accel.dy * dev->pitch);
                                } else {
                                    dev->accel.src  = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.dy * dev->pitch);
                                }

                                dev->accel.sy--;

                                if (dev->accel.sy < 0)
                                    dev->accel.cmd_back = 1;
                                return;
                            }
                        }
                    } else {
                        dev->accel.temp_cnt = 8;
                        while (count-- && (dev->accel.sy >= 0)) {
                            if (!dev->accel.temp_cnt) {
                                dev->accel.temp_cnt = 8;
                                mix_dat = old_mix_dat;
                            }
                            if ((dev->accel.dx >= clip_l) &&
                                (dev->accel.dx <= clip_r) &&
                                (dev->accel.dy >= clip_t) &&
                                (dev->accel.dy <= clip_b)) {
                                switch ((mix_dat & 0x01) ? frgd_mix : bkgd_mix) {
                                    case 0:
                                        src_dat = bkgd_color;
                                        break;
                                    case 1:
                                        src_dat = frgd_color;
                                        break;
                                    case 2:
                                        src_dat = 0;
                                        break;
                                    case 3:
                                        READ(dev->accel.src + dev->accel.cx, src_dat);
                                        break;

                                    default:
                                        break;
                                }

                                READ(dev->accel.dest + dev->accel.dx, dest_dat);

                                if ((compare_mode == 0) ||
                                    ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                                    ((compare_mode == 0x18) && (dest_dat < compare)) ||
                                    ((compare_mode == 0x20) && (dest_dat != compare)) ||
                                    ((compare_mode == 0x28) && (dest_dat == compare)) ||
                                    ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                                    ((compare_mode == 0x38) && (dest_dat > compare))) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix_dat & 0x01, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                    WRITE(dev->accel.dest + dev->accel.dx, dest_dat);
                                }
                            }

                            dev->accel.temp_cnt--;
                            mix_dat >>= 1;

                            if (dev->accel.cmd & 0x20) {
                                dev->accel.dx++;
                                dev->accel.cx++;
                            } else {
                                dev->accel.dx--;
                                dev->accel.cx--;
                            }

                            dev->accel.sx--;
                            if (dev->accel.sx < 0) {
                                dev->accel.sx = dev->accel.maj_axis_pcnt & 0x7ff;

                                if (dev->accel.cmd & 0x20) {
                                    dev->accel.dx -= (dev->accel.sx + 1);
                                    dev->accel.cx -= (dev->accel.sx + 1);
                                } else {
                                    dev->accel.dx += (dev->accel.sx + 1);
                                    dev->accel.cx += (dev->accel.sx + 1);
                                }

                                if (dev->accel.cmd & 0x80) {
                                    dev->accel.dy++;
                                    dev->accel.cy++;
                                } else {
                                    dev->accel.dy--;
                                    dev->accel.cy--;
                                }

                                if ((dev->accel_bpp == 24) || (dev->accel_bpp <= 8)) {
                                    dev->accel.src  = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.dy * dev->pitch);
                                } else if (dev->bpp) {
                                    dev->accel.src  = (dev->accel.ge_offset << 1) + (dev->accel.cy * dev->pitch);
                                    dev->accel.dest = (dev->accel.ge_offset << 1) + (dev->accel.dy * dev->pitch);
                                } else {
                                    dev->accel.src  = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.dy * dev->pitch);
                                }
                                dev->accel.sy--;

                                if (dev->accel.sy < 0) {
                                    dev->accel.destx = dev->accel.dx;
                                    dev->accel.desty = dev->accel.dy;
                                    dev->accel.cmd_back = 1;
                                    return;
                                }
                            }
                        }
                    }
                } else {
                    if ((dev->accel_bpp == 24) && (dev->accel.cmd == 0xc2b5)) {
                        int64_t cx;
                        int64_t dx;

                        cx = (int64_t) dev->accel.cx;
                        dx = (int64_t) dev->accel.dx;

                        while (1) {
                            if ((dx >= (((int64_t)clip_l) * 3)) &&
                                (dx <= (((uint64_t)clip_r) * 3)) &&
                                (dev->accel.dy >= (clip_t << 1)) &&
                                (dev->accel.dy <= (clip_b << 1))) {

                                READ(dev->accel.src + cx, src_dat);
                                READ(dev->accel.dest + dx, dest_dat);
                                dest_dat = (src_dat & wrt_mask) | (dest_dat & ~wrt_mask);
                                WRITE(dev->accel.dest + dx, dest_dat);
                            }

                            cx++;
                            dx++;

                            dev->accel.sx--;
                            if (dev->accel.sx < 0) {
                                dev->accel.cmd_back = 1;
                                return;
                            }
                        }
                    } else {
                        while (count-- && dev->accel.sy >= 0) {
                            if ((dev->accel.dx >= clip_l) &&
                                (dev->accel.dx <= clip_r) &&
                                (dev->accel.dy >= clip_t) &&
                                (dev->accel.dy <= clip_b)) {
                                if (pixcntl == 3) {
                                    if (!(dev->accel.cmd & 0x10) && ((frgd_mix != 3) || (bkgd_mix != 3))) {
                                        READ(dev->accel.src + dev->accel.cx, mix_dat);
                                        mix_dat = ((mix_dat & rd_mask) == rd_mask);
                                        mix_dat = mix_dat ? mix_mask : 0;
                                    } else if (dev->accel.cmd & 0x10) {
                                        READ(dev->accel.src + dev->accel.cx, mix_dat);
                                        mix_dat = ((mix_dat & rd_mask) == rd_mask);
                                        mix_dat = mix_dat ? mix_mask : 0;
                                    }
                                }
                                switch ((mix_dat & mix_mask) ? frgd_mix : bkgd_mix) {
                                    case 0:
                                        src_dat = bkgd_color;
                                        break;
                                    case 1:
                                        src_dat = frgd_color;
                                        break;
                                    case 2:
                                        src_dat = 0;
                                        break;
                                    case 3:
                                        READ(dev->accel.src + dev->accel.cx, src_dat);
                                        if (pixcntl == 3) {
                                            if ((dev->accel.cmd & 0x10) && !(dev->accel.cmd & 0x40))
                                                src_dat = ((src_dat & rd_mask) == rd_mask);
                                        }
                                        break;

                                    default:
                                        break;
                                }

                                READ(dev->accel.dest + dev->accel.dx, dest_dat);

                                if ((compare_mode == 0) ||
                                    ((compare_mode == 0x10) && (dest_dat >= compare)) ||
                                    ((compare_mode == 0x18) && (dest_dat < compare)) ||
                                    ((compare_mode == 0x20) && (dest_dat != compare)) ||
                                    ((compare_mode == 0x28) && (dest_dat == compare)) ||
                                    ((compare_mode == 0x30) && (dest_dat <= compare)) ||
                                    ((compare_mode == 0x38) && (dest_dat > compare))) {
                                    old_dest_dat = dest_dat;
                                    MIX(mix_dat & mix_mask, dest_dat, src_dat);
                                    dest_dat = (dest_dat & wrt_mask) | (old_dest_dat & ~wrt_mask);
                                    if (dev->accel.cmd & 0x04) {
                                        if (dev->accel.sx) {
                                            WRITE(dev->accel.dest + dev->accel.dx, dest_dat);
                                        }
                                    } else {
                                        WRITE(dev->accel.dest + dev->accel.dx, dest_dat);
                                    }
                                }
                            }

                            mix_dat <<= 1;
                            mix_dat |= 1;

                            if (dev->accel.cmd & 0x20) {
                                dev->accel.dx++;
                                dev->accel.cx++;
                            } else {
                                dev->accel.dx--;
                                dev->accel.cx--;
                            }

                            dev->accel.sx--;
                            if (dev->accel.sx < 0) {
                                dev->accel.fill_state = 0;
                                dev->accel.sx = dev->accel.maj_axis_pcnt & 0x7ff;

                                if (dev->accel.cmd & 0x20) {
                                    dev->accel.dx -= (dev->accel.sx + 1);
                                    dev->accel.cx -= (dev->accel.sx + 1);
                                } else {
                                    dev->accel.dx += (dev->accel.sx + 1);
                                    dev->accel.cx += (dev->accel.sx + 1);
                                }

                                if (dev->accel.cmd & 0x80) {
                                    dev->accel.dy++;
                                    dev->accel.cy++;
                                } else {
                                    dev->accel.dy--;
                                    dev->accel.cy--;
                                }

                                if ((dev->accel_bpp == 24) || (dev->accel_bpp <= 8)) {
                                    dev->accel.src  = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.dy * dev->pitch);
                                } else if (dev->bpp) {
                                    dev->accel.src  = (dev->accel.ge_offset << 1) + (dev->accel.cy * dev->pitch);
                                    dev->accel.dest = (dev->accel.ge_offset << 1) + (dev->accel.dy * dev->pitch);
                                } else {
                                    dev->accel.src  = (dev->accel.ge_offset << 2) + (dev->accel.cy * dev->pitch);
                                    dev->accel.dest = (dev->accel.ge_offset << 2) + (dev->accel.dy * dev->pitch);
                                }
                                dev->accel.sy--;

                                if (dev->accel.sy < 0) {
                                    dev->accel.destx = dev->accel.dx;
                                    dev->accel.desty = dev->accel.dy;
                                    dev->accel.cmd_back = 1;
                                    return;
                                }
                            }
                        }
                    }
                }
            }
            break;

        default:
            break;
    }
}

void
ibm8514_render_blank(svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    if ((dev->displine + svga->y_add) < 0)
        return;

    if (dev->firstline_draw == 2000)
        dev->firstline_draw = dev->displine;
    dev->lastline_draw = dev->displine;

    uint32_t *line_ptr   = &svga->monitor->target_buffer->line[dev->displine + svga->y_add][svga->x_add];
    uint32_t  line_width = (uint32_t)(dev->h_disp) * sizeof(uint32_t);

    if (dev->h_disp > 0)
        memset(line_ptr, 0, line_width);
}

void
ibm8514_render_8bpp(svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    uint32_t  *p;
    uint32_t   dat;

    if ((dev->displine + svga->y_add) < 0)
        return;

    if (dev->changedvram[dev->ma >> 12] || dev->changedvram[(dev->ma >> 12) + 1] || svga->fullchange) {
        p = &buffer32->line[dev->displine + svga->y_add][svga->x_add];

        if (dev->firstline_draw == 2000)
            dev->firstline_draw = dev->displine;
        dev->lastline_draw = dev->displine;

        for (int x = 0; x <= dev->h_disp; x += 8) {
            dat  = *(uint32_t *) (&dev->vram[dev->ma & dev->vram_mask]);
            p[0] = dev->pallook[dat & dev->dac_mask & 0xff];
            p[1] = dev->pallook[(dat >> 8) & dev->dac_mask & 0xff];
            p[2] = dev->pallook[(dat >> 16) & dev->dac_mask & 0xff];
            p[3] = dev->pallook[(dat >> 24) & dev->dac_mask & 0xff];

            dat  = *(uint32_t *) (&dev->vram[(dev->ma + 4) & dev->vram_mask]);
            p[4] = dev->pallook[dat & dev->dac_mask & 0xff];
            p[5] = dev->pallook[(dat >> 8) & dev->dac_mask & 0xff];
            p[6] = dev->pallook[(dat >> 16) & dev->dac_mask & 0xff];
            p[7] = dev->pallook[(dat >> 24) & dev->dac_mask & 0xff];

            dev->ma += 8;
            p += 8;
        }
        dev->ma &= dev->vram_mask;
    }
}

void
ibm8514_render_15bpp(svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    int        x;
    uint32_t  *p;
    uint32_t   dat;

    if ((dev->displine + svga->y_add) < 0)
        return;

    if (dev->changedvram[dev->ma >> 12] || dev->changedvram[(dev->ma >> 12) + 1] || svga->fullchange) {
        p = &buffer32->line[dev->displine + svga->y_add][svga->x_add];

        if (dev->firstline_draw == 2000)
            dev->firstline_draw = dev->displine;
        dev->lastline_draw = dev->displine;

        for (x = 0; x <= dev->h_disp; x += 8) {
            dat      = *(uint32_t *) (&dev->vram[(dev->ma + (x << 1)) & dev->vram_mask]);
            p[x]     = video_15to32[dat & 0xffff];
            p[x + 1] = video_15to32[dat >> 16];

            dat      = *(uint32_t *) (&dev->vram[(dev->ma + (x << 1) + 4) & dev->vram_mask]);
            p[x + 2] = video_15to32[dat & 0xffff];
            p[x + 3] = video_15to32[dat >> 16];

            dat      = *(uint32_t *) (&dev->vram[(dev->ma + (x << 1) + 8) & dev->vram_mask]);
            p[x + 4] = video_15to32[dat & 0xffff];
            p[x + 5] = video_15to32[dat >> 16];

            dat      = *(uint32_t *) (&dev->vram[(dev->ma + (x << 1) + 12) & dev->vram_mask]);
            p[x + 6] = video_15to32[dat & 0xffff];
            p[x + 7] = video_15to32[dat >> 16];
        }
        dev->ma += (x << 1);
        dev->ma &= dev->vram_mask;
    }
}

void
ibm8514_render_16bpp(svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    int        x;
    uint32_t  *p;
    uint32_t   dat;

    if ((dev->displine + svga->y_add) < 0)
        return;

    if (dev->changedvram[dev->ma >> 12] || dev->changedvram[(dev->ma >> 12) + 1] || svga->fullchange) {
        p = &buffer32->line[dev->displine + svga->y_add][svga->x_add];

        if (dev->firstline_draw == 2000)
            dev->firstline_draw = dev->displine;
        dev->lastline_draw = dev->displine;

        for (x = 0; x <= dev->h_disp; x += 8) {
            dat      = *(uint32_t *) (&dev->vram[(dev->ma + (x << 1)) & dev->vram_mask]);
            p[x]     = video_16to32[dat & 0xffff];
            p[x + 1] = video_16to32[dat >> 16];

            dat      = *(uint32_t *) (&dev->vram[(dev->ma + (x << 1) + 4) & dev->vram_mask]);
            p[x + 2] = video_16to32[dat & 0xffff];
            p[x + 3] = video_16to32[dat >> 16];

            dat      = *(uint32_t *) (&dev->vram[(dev->ma + (x << 1) + 8) & dev->vram_mask]);
            p[x + 4] = video_16to32[dat & 0xffff];
            p[x + 5] = video_16to32[dat >> 16];

            dat      = *(uint32_t *) (&dev->vram[(dev->ma + (x << 1) + 12) & dev->vram_mask]);
            p[x + 6] = video_16to32[dat & 0xffff];
            p[x + 7] = video_16to32[dat >> 16];
        }
        dev->ma += (x << 1);
        dev->ma &= dev->vram_mask;
    }
}

void
ibm8514_render_24bpp(svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    uint32_t *p;
    uint32_t  dat;

    if ((dev->displine + svga->y_add) < 0)
        return;

    if (dev->changedvram[dev->ma >> 12] || dev->changedvram[(dev->ma >> 12) + 1] || svga->fullchange) {
        p = &buffer32->line[dev->displine + svga->y_add][svga->x_add];

        if (dev->firstline_draw == 2000)
            dev->firstline_draw = dev->displine;
        dev->lastline_draw = dev->displine;

        for (int x = 0; x <= dev->h_disp; x += 4) {
            dat  = *(uint32_t *) (&dev->vram[dev->ma & dev->vram_mask]);
            p[x] = dat & 0xffffff;

            dat      = *(uint32_t *) (&dev->vram[(dev->ma + 3) & dev->vram_mask]);
            p[x + 1] = dat & 0xffffff;

            dat      = *(uint32_t *) (&dev->vram[(dev->ma + 6) & dev->vram_mask]);
            p[x + 2] = dat & 0xffffff;

            dat      = *(uint32_t *) (&dev->vram[(dev->ma + 9) & dev->vram_mask]);
            p[x + 3] = dat & 0xffffff;

            dev->ma += 12;
        }
        dev->ma &= dev->vram_mask;
    }
}

void
ibm8514_render_BGR(svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    uint32_t  *p;
    uint32_t   dat;

    if ((dev->displine + svga->y_add) < 0)
        return;

    if (dev->changedvram[dev->ma >> 12] || dev->changedvram[(dev->ma >> 12) + 1] || svga->fullchange) {
        p = &buffer32->line[dev->displine + svga->y_add][svga->x_add];

        if (dev->firstline_draw == 2000)
            dev->firstline_draw = dev->displine;
        dev->lastline_draw = dev->displine;

        for (int x = 0; x <= dev->h_disp; x += 4) {
            dat  = *(uint32_t *) (&dev->vram[dev->ma & dev->vram_mask]);
            p[x] = ((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16);

            dat      = *(uint32_t *) (&dev->vram[(dev->ma + 3) & dev->vram_mask]);
            p[x + 1] = ((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16);

            dat      = *(uint32_t *) (&dev->vram[(dev->ma + 6) & dev->vram_mask]);
            p[x + 2] = ((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16);

            dat      = *(uint32_t *) (&dev->vram[(dev->ma + 9) & dev->vram_mask]);
            p[x + 3] = ((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16);

            dev->ma += 12;
        }
        dev->ma &= dev->vram_mask;
    }
}

void
ibm8514_render_ABGR8888(svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    int        x;
    uint32_t  *p;
    uint32_t   dat;

    if ((dev->displine + svga->y_add) < 0)
        return;

    if (dev->changedvram[dev->ma >> 12] || dev->changedvram[(dev->ma >> 12) + 1] || svga->fullchange) {
        p = &buffer32->line[dev->displine + svga->y_add][svga->x_add];

        if (dev->firstline_draw == 2000)
            dev->firstline_draw = dev->displine;
        dev->lastline_draw = dev->displine;

        for (x = 0; x <= dev->h_disp; x++) {
            dat  = *(uint32_t *) (&dev->vram[(dev->ma + (x << 2)) & dev->vram_mask]);
            *p++ = ((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16);
        }
        dev->ma += (x * 4);
        dev->ma &= dev->vram_mask;
    }
}

void
ibm8514_render_32bpp(svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    int        x;
    uint32_t  *p;
    uint32_t   dat;

    if ((dev->displine + svga->y_add) < 0)
        return;

    if (dev->changedvram[dev->ma >> 12] || dev->changedvram[(dev->ma >> 12) + 1] || dev->changedvram[(dev->ma >> 12) + 2] || svga->fullchange) {
        p = &buffer32->line[dev->displine + svga->y_add][svga->x_add];

        if (dev->firstline_draw == 2000)
            dev->firstline_draw = dev->displine;
        dev->lastline_draw = dev->displine;

        for (x = 0; x <= dev->h_disp; x++) {
            dat  = *(uint32_t *) (&dev->vram[(dev->ma + (x << 2)) & dev->vram_mask]);
            p[x] = dat & 0xffffff;
        }
        dev->ma += (x * 4);
        dev->ma &= dev->vram_mask;
    }
}

static void
ibm8514_render_overscan_left(ibm8514_t *dev, svga_t *svga)
{
    if ((dev->displine + svga->y_add) < 0)
        return;

    if (svga->scrblank || (dev->h_disp == 0))
        return;

    for (int i = 0; i < svga->x_add; i++)
        buffer32->line[dev->displine + svga->y_add][i] = svga->overscan_color;
}

static void
ibm8514_render_overscan_right(ibm8514_t *dev, svga_t *svga)
{
    int right;

    if ((dev->displine + svga->y_add) < 0)
        return;

    if (svga->scrblank || (dev->h_disp == 0))
        return;

    right = (overscan_x >> 1);
    for (int i = 0; i < right; i++)
        buffer32->line[dev->displine + svga->y_add][svga->x_add + dev->h_disp + i] = svga->overscan_color;
}

void
ibm8514_set_poll(svga_t *svga)
{
    timer_set_callback(&svga->timer, ibm8514_poll);
}

void
ibm8514_poll(void *priv)
{
    svga_t *svga = (svga_t *)priv;
    ibm8514_t *dev = (ibm8514_t *)svga->dev8514;
    uint32_t x;
    int      wx;
    int      wy;

    ibm8514_log("IBM 8514/A poll=%x.\n", dev->on);
    if (dev->on) {
        ibm8514_log("ON!\n");
        if (!dev->linepos) {
            if ((dev->displine == ((dev->hwcursor_latch.y < 0) ? 0 : dev->hwcursor_latch.y)) && dev->hwcursor_latch.ena) {
                dev->hwcursor_on      = dev->hwcursor_latch.cur_ysize - dev->hwcursor_latch.yoff;
                dev->hwcursor_oddeven = 0;
            }

            if ((dev->displine == (((dev->hwcursor_latch.y < 0) ? 0 : dev->hwcursor_latch.y) + 1)) && dev->hwcursor_latch.ena && dev->interlace) {
                dev->hwcursor_on      = dev->hwcursor_latch.cur_ysize - (dev->hwcursor_latch.yoff + 1);
                dev->hwcursor_oddeven = 1;
            }

            timer_advance_u64(&svga->timer, dev->dispofftime);
            svga->cgastat |= 1;
            dev->linepos = 1;

            if (dev->dispon) {
                dev->hdisp_on = 1;

                dev->ma &= dev->vram_mask;

                if (dev->firstline == 2000) {
                    dev->firstline = dev->displine;
                    video_wait_for_buffer_monitor(svga->monitor_index);
                }

                if (dev->hwcursor_on)
                    dev->changedvram[dev->ma >> 12] = dev->changedvram[(dev->ma >> 12) + 1] = dev->interlace ? 3 : 2;

                svga->render8514(svga);

                svga->x_add = (overscan_x >> 1);
                ibm8514_render_overscan_left(dev, svga);
                ibm8514_render_overscan_right(dev, svga);
                svga->x_add = (overscan_x >> 1);

                if (dev->hwcursor_on) {
                    if (svga->hwcursor_draw)
                        svga->hwcursor_draw(svga, (dev->displine + svga->y_add + ((dev->hwcursor_latch.y >= 0) ? 0 : dev->hwcursor_latch.y)) & 2047);
                    dev->hwcursor_on--;
                    if (dev->hwcursor_on && dev->interlace)
                        dev->hwcursor_on--;
                }

                if (dev->lastline < dev->displine)
                    dev->lastline = dev->displine;
            }

            dev->displine++;
            if (dev->interlace)
                dev->displine++;
            if ((svga->cgastat & 8) && ((dev->displine & 0x0f) == (svga->crtc[0x11] & 0x0f)) && svga->vslines)
                svga->cgastat &= ~8;
            svga->vslines++;
            if (dev->displine > 1500)
                dev->displine = 0;
        } else {
            timer_advance_u64(&svga->timer, dev->dispontime);
            if (dev->dispon)
                svga->cgastat &= ~1;
            dev->hdisp_on = 0;

            dev->linepos = 0;
            if (dev->dispon) {
                if (dev->sc == dev->rowcount) {
                    dev->sc = 0;
                    dev->maback += (dev->rowoffset << 3);
                    if (dev->interlace)
                        dev->maback += (dev->rowoffset << 3);

                    dev->maback &= dev->vram_mask;
                    dev->ma = dev->maback;
                } else {
                    dev->sc++;
                    dev->sc &= 0x1f;
                    dev->ma = dev->maback;
                }
            }

            dev->vc++;
            dev->vc &= 0xfff;

            if (dev->vc == dev->dispend) {
                dev->dispon = 0;

                for (x = 0; x < ((dev->vram_mask + 1) >> 12); x++) {
                    if (dev->changedvram[x])
                        dev->changedvram[x]--;
                }

                if (svga->fullchange)
                    svga->fullchange--;
            }
            if (dev->vc == dev->v_syncstart) {
                dev->dispon = 0;
                svga->cgastat |= 8;
                x           = dev->h_disp;

                if (dev->interlace && !dev->oddeven)
                    dev->lastline++;
                if (dev->interlace && dev->oddeven)
                    dev->firstline--;

                wx = x;
                wy = dev->lastline - dev->firstline;
                svga_doblit(wx, wy, svga);

                dev->firstline = 2000;
                dev->lastline  = 0;

                dev->firstline_draw = 2000;
                dev->lastline_draw  = 0;

                dev->oddeven ^= 1;

                svga->monitor->mon_changeframecount = dev->interlace ? 3 : 2;
                svga->vslines    = 0;

                if (dev->interlace && dev->oddeven)
                    dev->ma = dev->maback = (dev->rowoffset << 1);
                else
                    dev->ma = dev->maback = 0;

                dev->ma     = (dev->ma << 2);
                dev->maback = (dev->maback << 2);
            }
            if (dev->vc == dev->v_total) {
                dev->vc       = 0;
                dev->sc       = 0;
                dev->dispon   = 1;
                dev->displine = (dev->interlace && dev->oddeven) ? 1 : 0;

                svga->x_add = (overscan_x >> 1);

                dev->hwcursor_on    = 0;
                dev->hwcursor_latch = dev->hwcursor;
            }
        }
    }
}

void
ibm8514_recalctimings(svga_t *svga)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;

    svga->render8514 = ibm8514_render_blank;
    if (dev->extensions) {
        if (svga->ext8514 != NULL)
            ati8514_recalctimings(svga);
    } else {
        if (dev->on) {
            dev->h_total = dev->htotal + 1;
            dev->rowcount = !!(dev->disp_cntl & 0x08);

            if (dev->accel.advfunc_cntl & 0x01) {
                if (dev->accel.advfunc_cntl & 0x04) {
                    dev->h_disp = dev->hdisp;
                    dev->dispend = dev->vdisp;
                } else {
                    dev->h_disp = 640;
                    dev->dispend = 480;
                }
            } else {
                dev->h_disp = dev->hdisp;
                dev->dispend = dev->vdisp;
            }

            if (dev->accel.advfunc_cntl & 0x04)
                svga->clock8514 = (cpuclock * (double) (1ULL << 32)) / 44900000.0;
            else
                svga->clock8514 = (cpuclock * (double) (1ULL << 32)) / 25175000.0;

            if (dev->dispend == 766)
                dev->dispend += 2;

            if (dev->dispend == 478)
                dev->dispend += 2;

            if (dev->interlace)
                dev->dispend >>= 1;

            dev->pitch = 1024;
            dev->rowoffset = 0x80;
            if (dev->vram_512k_8514) {
                if (dev->h_disp == 640)
                    dev->pitch = 640;
            }
            dev->accel_bpp = 8;
            svga->render8514 = ibm8514_render_8bpp;
            ibm8514_log("BPP=%d, Pitch = %d, rowoffset = %d, crtc13 = %02x, highres bit = %02x, has_vga? = %d.\n", dev->bpp, dev->pitch, dev->rowoffset, svga->crtc[0x13], dev->accel.advfunc_cntl & 4, !ibm8514_standalone_enabled);
        }
    }
    ibm8514_log("8514 enabled, hdisp=%d, vtotal=%d, htotal=%d, dispend=%d, rowoffset=%d, split=%d, vsyncstart=%d, split=%08x\n", dev->hdisp, dev->vtotal, dev->htotal, dev->dispend, dev->rowoffset, dev->split, dev->vsyncstart, dev->split);
}

static uint8_t
ibm8514_mca_read(int port, void *priv)
{
    const svga_t    *svga = (svga_t *) priv;
    const ibm8514_t *dev  = (ibm8514_t *) svga->dev8514;

    return (dev->pos_regs[port & 7]);
}

static void
ibm8514_mca_write(int port, uint8_t val, void *priv)
{
    svga_t    *svga = (svga_t *) priv;
    ibm8514_t *dev  = (ibm8514_t *) svga->dev8514;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102)
        return;

    /* Save the MCA register value. */
    dev->pos_regs[port & 7] = val;
}

static uint8_t
ibm8514_mca_feedb(void *priv)
{
    const svga_t    *svga = (svga_t *) priv;
    const ibm8514_t *dev  = (ibm8514_t *) svga->dev8514;

    return dev->pos_regs[2] & 1;
}

static void
ibm8514_mca_reset(void *priv)
{
    svga_t          *svga = (svga_t *) priv;
    ibm8514_t       *dev  = (ibm8514_t *) svga->dev8514;

    ibm8514_log("MCA reset.\n");
    dev->on = 0;
    if (dev->extensions)
        ati8514_mca_write(0x102, 0, svga);
    else
        ibm8514_mca_write(0x102, 0, svga);

    svga_set_poll(svga);
}

static void *
ibm8514_init(const device_t *info)
{
    uint32_t bios_addr = 0;
    uint16_t bios_rom_eeprom = 0x0000;

    if (svga_get_pri() == NULL)
        return NULL;

    svga_t    *svga  = svga_get_pri();
    ibm8514_t *dev   = (ibm8514_t *) calloc(1, sizeof(ibm8514_t));

    svga->dev8514    = dev;
    svga->ext8514    = NULL;

    dev->vram_amount = device_get_config_int("memory");
    dev->vram_512k_8514 = dev->vram_amount == 512;
    dev->vram_size   = dev->vram_amount << 10;
    dev->vram        = calloc(dev->vram_size, 1);
    dev->changedvram = calloc((dev->vram_size >> 12) + 1, 1);
    dev->vram_mask   = dev->vram_size - 1;
    dev->map8        = dev->pallook;
    dev->local       = 0;
    dev->accel_bpp   = 8;

    dev->type     = info->flags;
    dev->bpp      = 0;

    dev->extensions = device_get_config_int("extensions");
    bios_addr = device_get_config_hex20("bios_addr");
    if (dev->type & DEVICE_MCA)
        bios_addr = 0xc6000;

    switch (dev->extensions) {
        case 1:
            if (rom_present(BIOS_MACH8_ROM_PATH)) {
                mach_t * mach = (mach_t *) calloc(1, sizeof(mach_t));
                svga->ext8514 = mach;

                rom_init(&dev->bios_rom,
                         BIOS_MACH8_ROM_PATH,
                         bios_addr, 0x1000, 0xfff,
                         0, MEM_MAPPING_EXTERNAL);

                ati8514_init(svga, svga->ext8514, svga->dev8514);
                mach->accel.scratch0 = ((((bios_addr >> 7) - 0x1000) >> 4));
                bios_rom_eeprom = mach->accel.scratch0;
                if (dev->type & DEVICE_MCA) {
                    dev->pos_regs[0] = 0x88;
                    dev->pos_regs[1] = 0x80;
                    mach->eeprom.data[0] = 0x0000;
                    mach->eeprom.data[1] = bios_rom_eeprom | ((bios_rom_eeprom | 0x01) << 8);
                    mca_add(ati8514_mca_read, ati8514_mca_write, ibm8514_mca_feedb, ibm8514_mca_reset, svga);
                    ati_eeprom_load_mach8(&mach->eeprom, "ati8514_mca.nvr", 1);
                    mem_mapping_disable(&dev->bios_rom.mapping);
                } else
                    ati_eeprom_load_mach8(&mach->eeprom, "ati8514.nvr", 0);
                break;
            }

            fallthrough;
        default:
            ibm8514_io_set(svga);

            if (dev->type & DEVICE_MCA) {
                dev->pos_regs[0] = 0x7f;
                dev->pos_regs[1] = 0xef;
                mca_add(ibm8514_mca_read, ibm8514_mca_write, ibm8514_mca_feedb, ibm8514_mca_reset, svga);
            }
            break;
    }
    return svga;
}

static void
ibm8514_close(void *priv)
{
    svga_t    *svga = (svga_t *) priv;
    ibm8514_t *dev  = (ibm8514_t *) svga->dev8514;
    mach_t    *mach = (mach_t *) svga->ext8514;

    if (mach)
        free(mach);

    if (dev) {
        free(dev->vram);
        free(dev->changedvram);

        free(dev);
    }
}

static void
ibm8514_speed_changed(void *priv)
{
    svga_t *svga = (svga_t *) priv;

    svga_recalctimings(svga);
}

static void
ibm8514_force_redraw(void *priv)
{
    svga_t *svga = (svga_t *) priv;

    svga->fullchange = changeframecount;
}

// clang-format off
static const device_config_t isa_ext8514_config[] = {
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
    {
        .name           = "extensions",
        .description    = "Vendor",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IBM", .value = 0 },
            { .description = "ATI", .value = 1 },
            { .description = ""                }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr",
        .description    = "BIOS Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xc8000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "C800h", .value = 0xc8000 },
            { .description = "CA00h", .value = 0xca000 },
            { .description = "CC00h", .value = 0xcc000 },
            { .description = "CE00h", .value = 0xce000 },
            { .description = "D000h", .value = 0xd0000 },
            { .description = "D200h", .value = 0xd2000 },
            { .description = "D400h", .value = 0xd4000 },
            { .description = "D600h", .value = 0xd6000 },
            { .description = "D800h", .value = 0xd8000 },
            { .description = "DA00h", .value = 0xda000 },
            { .description = "DC00h", .value = 0xdc000 },
            { .description = "DE00h", .value = 0xde000 },
            { .description = ""                        }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

// clang-format off
static const device_config_t mca_ext8514_config[] = {
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
    {
        .name           = "extensions",
        .description    = "Vendor",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IBM", .value = 0 },
            { .description = "ATI", .value = 1 },
            { .description = ""                }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

// clang-format off
const device_t gen8514_isa_device = {
    .name          = "IBM 8514/A clone (ISA)",
    .internal_name = "8514_isa",
    .flags         = DEVICE_AT | DEVICE_ISA,
    .local         = 0,
    .init          = ibm8514_init,
    .close         = ibm8514_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = ibm8514_speed_changed,
    .force_redraw  = ibm8514_force_redraw,
    .config        = isa_ext8514_config
};

const device_t ibm8514_mca_device = {
    .name          = "IBM 8514/A (MCA)",
    .internal_name = "8514_mca",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = ibm8514_init,
    .close         = ibm8514_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = ibm8514_speed_changed,
    .force_redraw  = ibm8514_force_redraw,
    .config        = mca_ext8514_config
};

void
ibm8514_device_add(void)
{
    if (!ibm8514_standalone_enabled)
        return;

    if (machine_has_bus(machine, MACHINE_BUS_MCA))
        device_add(&ibm8514_mca_device);
    else
        device_add(&gen8514_isa_device);
}
