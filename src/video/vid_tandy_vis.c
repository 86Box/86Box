/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Tandy VIS (Video Information System) video.
 *
 * Authors: Cacodemon345
 *          Carl
 *
 *          Copyright 2026 Cacodemon345.
 *          Copyright Carl
 */
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
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_8514a.h>
#include <86box/vid_xga.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include "cpu.h"

typedef struct tvis_vid_t {
    svga_t   svga;

    uint8_t rdac_ext_cnt;

    uint8_t m_extreg;
    uint8_t ext_dbl;
} tvis_vid_t;

static video_timings_t timing_vis = { .type = VIDEO_ISA, .write_b = 0, .write_w = 0, .write_l = 0, .read_b = 0, .read_w = 0, .read_l = 0 };

void
tvis_recalc_banks(tvis_vid_t* tvis_vid)
{
    if (!(tvis_vid->svga.seqregs[0x25] & 0x40)) {
        tvis_vid->svga.fb_only = 0;
        tvis_vid->svga.read_bank = 0;
        tvis_vid->svga.write_bank = 0;
        return;
    }
    tvis_vid->svga.fb_only = 1;
    tvis_vid->svga.read_bank = ((tvis_vid->svga.seqregs[0x1e] & 0xf) == 3) ? (tvis_vid->svga.seqregs[0x19] | (tvis_vid->svga.seqregs[0x18] << 8)) : (tvis_vid->svga.seqregs[0x1d] | (tvis_vid->svga.seqregs[0x1c] << 8));
    tvis_vid->svga.read_bank *= 64;

    tvis_vid->svga.write_bank = tvis_vid->svga.seqregs[0x19] | (tvis_vid->svga.seqregs[0x18] << 8);
    tvis_vid->svga.write_bank *= 64;
}

static uint32_t
tvis_yuv_to_rgb(int y, int u, int v)
{
    int r, g, b;

    u -= 128;
    v -= 128;
    r = y + ((v * 351) >> 8);
    g = y - ((u * 86 + v * 179) >> 8);
    b = y + ((u * 444) >> 8);
    r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
    g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
    b = (b < 0) ? 0 : ((b > 255) ? 255 : b);

    return (r << 16) | (g << 8) | b;
}

static const uint8_t tvis_yuv8_delta[32] = {
    0, 1, 2, 3, 4, 5, 6, 9, 12, 17, 22, 29, 38, 50, 66, 91, 128, 165, 190,
    206, 218, 227, 234, 239, 244, 247, 250, 251, 252, 253, 254, 255
};

static void tvis_decode_ext(svga_t *svga, const uint8_t *buf, uint32_t *p, int n);

/* The hidden RAMDAC register (written after four reads of 0x3C6) selects
   the direct colour modes, in which the RAMDAC takes the bytes the 256-colour
   shifter outputs and combines them into pixels instead of using the palette. */
static void
tvis_render_ext(svga_t *svga)
{
    uint8_t     buf[2048 + 64 + 8];
    uint32_t    line[2048 + 64 + 8];
    uint32_t   *p;
    int         skip;
    uint32_t    addr;
    uint32_t    changed_addr;
    int         n;
    int         x;
    int         dbl;

    if (((svga->displine + svga->y_add) < 0) ||
        (svga->monitor->target_buffer == NULL) ||
        (svga->monitor->target_buffer->line[svga->displine + svga->y_add] == NULL))
        return;

    changed_addr = svga->remap_func(svga, svga->memaddr);
    if (!(svga->changedvram[changed_addr >> 12] || svga->changedvram[(changed_addr >> 12) + 1] || svga->fullchange))
        return;

    p = &svga->monitor->target_buffer->line[svga->displine + svga->y_add][svga->x_add];

    if (svga->firstline_draw == 2000)
        svga->firstline_draw = svga->displine;
    svga->lastline_draw = svga->displine;

    /* The CRTC model starts the line hblank_sub characters late; decode from
       the real start of the line since the first bytes matter to the
       delta-encoded modes and to the alignment of the multi-byte pixels. */
    skip = svga->hblank_sub << 2;
    if ((skip < 0) || (skip > 64))
        skip = 0;
    dbl = ((tvis_vid_t *) svga->priv)->ext_dbl;
    n   = (svga->hdisp + svga->scrollcache) >> dbl;
    if (n > 2048)
        n = 2048;
    n = (n + skip + 3) & ~3;

    svga->memaddr = (svga->memaddr - skip) & svga->vram_display_mask;
    for (x = 0; x < n; x += 4) {
        addr = svga->remap_required ? svga->remap_func(svga, svga->memaddr) : svga->memaddr;
        *(uint32_t *) &buf[x] = *(uint32_t *) &svga->vram[addr & svga->vram_display_mask];
        svga->memaddr = (svga->memaddr + 4) & svga->vram_display_mask;
    }
    memset(&buf[n], 0, 8);

    tvis_decode_ext(svga, buf, line, n);
    if (dbl) {
        for (x = 0; x < (n - skip); x++)
            p[x << 1] = p[(x << 1) + 1] = line[skip + x];
    } else
        memcpy(p, &line[skip], (n - skip) * sizeof(uint32_t));
}

static void
tvis_decode_ext(svga_t *svga, const uint8_t *buf, uint32_t *p, int n)
{
    tvis_vid_t *vga = (tvis_vid_t *) svga->priv;
    int         x;

    if ((vga->m_extreg & 0xc0) == 0x80) {
        /* 15-bit RGB, two bytes per pixel. */
        for (x = 0; x < n; x += 2)
            p[x] = p[x + 1] = svga->conv_16to32(svga, buf[x] | (buf[x + 1] << 8), 15);
        return;
    }

    switch (vga->m_extreg & 0xc7) {
        case 0xc0:
        case 0xc1:
            /* 16-bit RGB, two bytes per pixel. */
            for (x = 0; x < n; x += 2)
                p[x] = p[x + 1] = svga->conv_16to32(svga, buf[x] | (buf[x + 1] << 8), 16);
            break;

        case 0xc3: {
            /* YUV 4:2:2, Y0 U Y1 V; each luma sample lasts two bytes. */
            int ua = 0, va = 0;

            for (x = 0; x < n; x += 4) {
                int ub = buf[x + 1];
                int vb = buf[x + 3];
                int u  = x ? ((ua + ub) >> 1) : ub;
                int v  = x ? ((va + vb) >> 1) : vb;

                p[x] = p[x + 1] = tvis_yuv_to_rgb(buf[x], u, v);
                p[x + 2] = p[x + 3] = tvis_yuv_to_rgb(buf[x + 2], ub, vb);
                ua = ub;
                va = vb;
            }
            break;
        }

        case 0xc4: {
            /* Delta-encoded YUV, four pixels per dword. Each byte holds a
               5-bit luma delta; the top bits of the four bytes hold two 5-bit
               chroma deltas and the position of the chroma transition. The
               first dword of a line holds absolute values instead. */
            uint8_t ydelta = 0, ua = 0, va = 0;

            for (x = 0; x < n; x += 4) {
                uint8_t a = buf[x], b = buf[x + 1], c = buf[x + 2], d = buf[x + 3];
                uint8_t y[4], ub, vb;
                int     trans;
                int     u, v;

                if (x) {
                    y[0] = tvis_yuv8_delta[a & 0x1f] + ydelta;
                    ub   = tvis_yuv8_delta[((a >> 5) & 3) | ((b >> 3) & 0x1c)] + ua;
                    vb   = tvis_yuv8_delta[((c >> 5) & 3) | ((d >> 3) & 0x1c)] + va;
                } else {
                    y[0] = (a & 0x1f) << 3;
                    ua = ub = ((a >> 2) & 0x18) | (b & 0xe0);
                    va = vb = ((c >> 2) & 0x18) | (d & 0xe0);
                }
                y[1] = tvis_yuv8_delta[b & 0x1f] + y[0];
                y[2] = tvis_yuv8_delta[c & 0x1f] + y[1];
                y[3] = tvis_yuv8_delta[d & 0x1f] + y[2];

                trans = (a >> 7) | ((c >> 6) & 2);
                u     = ua;
                v     = va;
                for (int i = 0; i < 4; i++) {
                    if (i == trans) {
                        u = (ua + ub) >> 1;
                        v = (va + vb) >> 1;
                    } else if (i == (trans + 1)) {
                        u = ub;
                        v = vb;
                    }
                    p[x + i] = tvis_yuv_to_rgb(y[i], u, v);
                }
                ua     = ub;
                va     = vb;
                ydelta = y[3];
            }
            break;
        }

        case 0xc5:
            /* 24-bit RGB, three bytes (B, G, R) per pixel. */
            for (x = 0; x < n; x += 3) {
                uint32_t col = buf[x] | (buf[x + 1] << 8) | (buf[x + 2] << 16);

                p[x] = p[x + 1] = p[x + 2] = col;
            }
            break;

        case 0xc6:
            /* RAMDAC output disabled. */
            memset(p, 0, n * sizeof(uint32_t));
            break;

        default:
            /* Border encoded 8-bit mode: treated as plain 256-colour. */
            for (x = 0; x < n; x++)
                p[x] = svga->map8[buf[x] & svga->dac_mask];
            break;
    }
}

/* Sequencer register 0x1F bit 4 selects the 256-colour mode: it forces
   dword addressing and the 256-colour shifter regardless of the CRTC and
   GDC settings. Each pixel is then one byte shifted out over two dots, so
   with the halved dot clock a line is half as many bytes as dots. */
static void
tvis_recalctimings(svga_t *svga)
{
    tvis_vid_t *vga = (tvis_vid_t *) svga->priv;
    int         tv_halved;

    /* CRTC register 0x30 selects TV scanning: the vertical registers count
       lines per field, and unless sequencer register 0x25 bit 5 is set the
       horizontal registers are programmed at twice the character rate, so
       each character clock carries half as many dots. With bit 4 set the
       fields are interlaced (odd field one row down).

       The VIS only drives a TV, and the firmware programs a 262-line total
       in every mode, so a non-interlaced mode is a TV field whose lines each
       cover two lines of the interlaced frame: show every line twice, or a
       200-line mode such as the firmware logo comes out at half height. */
    svga->interlace        = !!(svga->crtc[0x30] & 0x10);

    /* The video clocks come from the 14.31818 MHz crystal. Clock select 1 is
       twice that (8x the NTSC colour subcarrier); in TV scanning the encoder
       needs that clock, so it is used whatever the clock select bits say.
       The interlaced modes keep the VGA clock select 0 (miscellaneous output
       0xE3) and would otherwise run at 53 Hz instead of 60. */
    if (svga->crtc[0x30] || svga->vidclock)
        svga->clock = (cpuclock * (double) (1ULL << 32)) / 28636363.0;

    svga->vertical_linedbl = !svga->interlace && (svga->vtotal < 400);
    tv_halved              = svga->crtc[0x30] && !(svga->seqregs[0x25] & 0x20);
    if (tv_halved && (svga->render != svga_render_blank)) {
        svga->hdisp >>= 1;
        svga->dots_per_clock >>= 1;
    }

    svga->force_dword_mode = !!(svga->seqregs[0x1f] & 0x10);

    if (!svga->force_dword_mode || (svga->render == svga_render_blank) ||
        (!(svga->gdcreg[6] & 1) && !(svga->attrregs[0x10] & 1)))
        return;

    svga->map8 = svga->pallook;

    if (vga->m_extreg & 0x80) {
        /* The RAMDAC takes one byte per pixel clock, eight per character.
           In TV timing with the halved dot clock the line is still only as
           many pixels as bytes, so each byte covers two dots. */
        vga->ext_dbl = tv_halved && (svga->seqregs[1] & 8);
        if ((svga->seqregs[1] & 8) && !tv_halved) {
            svga->hdisp >>= 1;
            svga->dots_per_clock >>= 1;
        }
        svga->render = tvis_render_ext;
    } else if (svga->seqregs[1] & 8) {
        svga->hdisp >>= 1;
        svga->dots_per_clock >>= 1;
        svga->render = svga_render_8bpp_lowres;
    } else
        svga->render = svga_render_8bpp_highres;
}

void
tvis_out(uint16_t addr, uint8_t val, void *priv)
{
    tvis_vid_t  *vga  = (tvis_vid_t *) priv;
    svga_t *svga = &vga->svga;
    uint8_t old, o;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c5:
            if (svga->seqaddr > 0x3f)
                return;
            o                                  = svga->seqregs[svga->seqaddr & 0x3f];
            svga->seqregs[svga->seqaddr & 0x3f] = val;
            if (o != val && (svga->seqaddr & 0x3f) == 1) {
                svga_recalctimings(svga);
            }
            switch (svga->seqaddr & 0x3f) {
                case 1:
                    if (svga->scrblank && !(val & 0x20))
                        svga->fullchange = 3;
                    svga->scrblank = (svga->scrblank & ~0x20) | (val & 0x20);
                    svga_recalctimings(svga);
                    break;
                case 2:
                    svga->writemask = val & 0xf;
                    break;
                case 3:
                    svga->charsetb = (((val >> 2) & 3) * 0x10000) + 2;
                    svga->charseta = ((val & 3) * 0x10000) + 2;
                    if (val & 0x10)
                        svga->charseta += 0x8000;
                    if (val & 0x20)
                        svga->charsetb += 0x8000;
                    break;
                case 4:
                    svga->chain2_write = !(val & 4);
                    svga->chain4       = (svga->chain4 & ~8) | (val & 8);
                    svga->fast         = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1]) &&
                                        ((svga->chain4 && (svga->packed_chain4 || svga->force_old_addr)) || svga->fb_only) &&
                                        !(svga->adv_flags & FLAG_ADDR_BY8);
                    break;

                case 0x18 ... 0x1d:
                    tvis_recalc_banks(vga);
                    break;

                case 0x1f:
                    if (o != val)
                        svga_recalctimings(svga);
                    break;

                default:
                    break;
            }
            return;
        case 0x3C6:
            {
                if (vga->rdac_ext_cnt == 4) {
                    if((val & 0xc7) != 0xc7)
                        vga->m_extreg = val;
                    vga->rdac_ext_cnt = 0;
                    svga_recalctimings(svga);
                    return;
                }
                vga->rdac_ext_cnt = 0;
                svga_out(addr, val, svga);
                return;
            }
        case 0x3c7 ... 0x3c9:
            vga->rdac_ext_cnt = 0;
            break;
        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if (svga->crtcreg == 0x30) {
                old              = svga->crtc[0x30];
                svga->crtc[0x30] = val;
                if (old != val) {
                    svga->fullchange = changeframecount;
                    svga_recalctimings(svga);
                }
                return;
            }
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
                        svga->memaddr_latch   = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
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
tvis_in(uint16_t addr, void *priv)
{
    tvis_vid_t  *vga  = (tvis_vid_t *) priv;
    svga_t *svga = &vga->svga;
    uint8_t temp;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c5:
            return svga->seqregs[svga->seqaddr & 0x3f];
        case 0x3C6:
            {
                if (vga->rdac_ext_cnt == 4) {
                    vga->rdac_ext_cnt = 0;
                    return vga->m_extreg;
                }
                vga->rdac_ext_cnt++;
                temp = svga_in(addr, svga);
                break;
            }
        case 0x3c7 ... 0x3c9:
            vga->rdac_ext_cnt = 0;
            temp = svga_in(addr, svga);
            break;
        case 0x3D4:
            temp = svga->crtcreg;
            break;
        case 0x3D5:
            if (svga->crtcreg == 0x30)
                temp = svga->crtc[0x30];
            else if (svga->crtcreg & 0x20)
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


void *
tvis_vid_init(const device_t *info)
{
    tvis_vid_t *vga = calloc(1, sizeof(tvis_vid_t));

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_vis);
    video_update_timing();

    svga_init(info, &vga->svga, vga, 1 << 18, /*256kb*/
              tvis_recalctimings,
              tvis_in, tvis_out,
              NULL,
              NULL);

    vga->svga.bpp     = 8;
    vga->svga.miscout = 0;

    vga->svga.vga_enabled = 1;

    io_sethandler(0x03a0, 0x0040, tvis_in, NULL, NULL, tvis_out, NULL, NULL, vga);

    return vga;
}

void
tvis_vid_close(void *priv)
{
    tvis_vid_t *vga = (tvis_vid_t *) priv;

    svga_close(&vga->svga);

    free(vga);
}

void
tvis_vid_speed_changed(void *priv)
{
    tvis_vid_t *vga = (tvis_vid_t *) priv;

    svga_recalctimings(&vga->svga);
}

void
tvis_vid_force_redraw(void *priv)
{
    tvis_vid_t *vga = (tvis_vid_t *) priv;

    vga->svga.fullchange = changeframecount;
}

const device_t tvis_vid_device = {
    .name          = "Tandy VIS VGA",
    .internal_name = "tvis_vid",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = tvis_vid_init,
    .close         = tvis_vid_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = tvis_vid_speed_changed,
    .force_redraw  = tvis_vid_force_redraw,
    .config        = NULL
};
