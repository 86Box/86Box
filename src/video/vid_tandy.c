/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tandy 1000 video emulation
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Connor Hyde / starfrost, <mario64crashed@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2025 starfrost
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/keyboard.h>
#include <86box/sound.h>
#include <86box/snd_sn76489.h>
#include <86box/video.h>
#include <86box/vid_cga_comp.h>
#include <86box/m_tandy.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>

static uint8_t crtcmask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static uint8_t crtcmask_sl[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xff, 0xff,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

enum {
    TANDY_RGB = 0,
    TANDY_COMPOSITE
};

static video_timings_t timing_dram = { VIDEO_BUS, 0, 0, 0, 0, 0, 0 }; /*No additional waitstates*/

static void
recalc_mapping(tandy_t *dev)
{
    t1kvid_t *vid = dev->vid;

    mem_mapping_disable(&vid->mapping);
    io_removehandler(0x03d0, 16,
                     tandy_vid_in, NULL, NULL, tandy_vid_out, NULL, NULL, dev);

    if (vid->planar_ctrl & 4) {
        mem_mapping_enable(&vid->mapping);
        if (vid->array[5] & 1)
            mem_mapping_set_addr(&vid->mapping, 0xa0000, 0x10000);
        else
            mem_mapping_set_addr(&vid->mapping, 0xb8000, 0x8000);
        io_sethandler(0x03d0, 16, tandy_vid_in, NULL, NULL, tandy_vid_out, NULL, NULL, dev);
    }
}

static void
recalc_timings(tandy_t *dev)
{
    t1kvid_t *vid = dev->vid;

    double _dispontime;
    double _dispofftime;
    double disptime;

    if (vid->mode & 1) {
        disptime    = vid->crtc[0] + 1;
        _dispontime = vid->crtc[1];
    } else {
        disptime    = (vid->crtc[0] + 1) << 1;
        _dispontime = vid->crtc[1] << 1;
    }

    _dispofftime = disptime - _dispontime;
    _dispontime *= CGACONST;
    _dispofftime *= CGACONST;
    vid->dispontime  = (uint64_t) (_dispontime);
    vid->dispofftime = (uint64_t) (_dispofftime);
}

static void
recalc_address(tandy_t *dev)
{
    t1kvid_t *vid = dev->vid;

    if ((vid->memctrl & 0xc0) == 0xc0) {
        vid->vram       = &ram[((vid->memctrl & 0x06) << 14) + dev->base];
        vid->b8000      = &ram[((vid->memctrl & 0x30) << 11) + dev->base];
        vid->b8000_mask = 0x7fff;
    } else {
        vid->vram       = &ram[((vid->memctrl & 0x07) << 14) + dev->base];
        vid->b8000      = &ram[((vid->memctrl & 0x38) << 11) + dev->base];
        vid->b8000_mask = 0x3fff;
    }
}

void
tandy_recalc_address_sl(tandy_t *dev)
{
    t1kvid_t *vid = dev->vid;

    vid->b8000_limit = 0x8000;

    if (vid->array[5] & 1) {
        vid->vram  = &ram[((vid->memctrl & 0x04) << 14) + dev->base];
        vid->b8000 = &ram[((vid->memctrl & 0x20) << 11) + dev->base];
    } else if ((vid->memctrl & 0xc0) == 0xc0) {
        vid->vram  = &ram[((vid->memctrl & 0x06) << 14) + dev->base];
        vid->b8000 = &ram[((vid->memctrl & 0x30) << 11) + dev->base];
    } else {
        vid->vram  = &ram[((vid->memctrl & 0x07) << 14) + dev->base];
        vid->b8000 = &ram[((vid->memctrl & 0x38) << 11) + dev->base];
        if ((vid->memctrl & 0x38) == 0x38)
            vid->b8000_limit = 0x4000;
    }
}

static void
vid_update_latch(t1kvid_t *vid)
{
    uint32_t lp_latch = vid->displine * vid->crtc[1];

    vid->crtc[0x10] = (lp_latch >> 8) & 0x3f;
    vid->crtc[0x11] = lp_latch & 0xff;
}

void
tandy_vid_out(uint16_t addr, uint8_t val, void *priv)
{
    tandy_t  *dev = (tandy_t *) priv;
    t1kvid_t *vid = dev->vid;
    uint8_t   old;

    if ((addr >= 0x3d0) && (addr <= 0x3d7))
        addr = (addr & 0xff9) | 0x004;

    switch (addr) {
        case 0x03d4:
            vid->crtcreg = val & 0x1f;
            break;

        case 0x03d5:
            old = vid->crtc[vid->crtcreg];
            if (dev->is_sl2)
                vid->crtc[vid->crtcreg] = val & crtcmask_sl[vid->crtcreg];
            else
                vid->crtc[vid->crtcreg] = val & crtcmask[vid->crtcreg];
            if (old != val) {
                if (vid->crtcreg < 0xe || vid->crtcreg > 0x10) {
                    vid->fullchange = changeframecount;
                    recalc_timings(dev);
                }
            }
            break;

        case 0x03d8:
            old = vid->mode;
            vid->mode = val;
            if ((old ^ val) & 0x01)
                recalc_timings(dev);
            if (!dev->is_sl2)
                update_cga16_color(vid->mode);
            break;

        case 0x03d9:
            vid->col = val;
            break;

        case 0x03da:
            vid->array_index = val & 0x1f;
            break;

        case 0x3db:
            if (!dev->is_sl2 && (vid->lp_strobe == 1))
                vid->lp_strobe = 0;
            break;

        case 0x3dc:
            if (!dev->is_sl2 && (vid->lp_strobe == 0)) {
                vid->lp_strobe = 1;
                vid_update_latch(vid);
            }
            break;

        case 0x03de:
            if (vid->array_index & 16)
                val &= 0xf;
            vid->array[vid->array_index & 0x1f] = val;
            if (dev->is_sl2) {
                if ((vid->array_index & 0x1f) == 5) {
                    recalc_mapping(dev);
                    tandy_recalc_address_sl(dev);
                }
            }
            break;

        case 0x03df:
            vid->memctrl = val;
            if (dev->is_sl2)
                tandy_recalc_address_sl(dev);
            else
                recalc_address(dev);
            break;

        case 0x0065:
            if (val == 8)
                return; /*Hack*/
            vid->planar_ctrl = val;
            recalc_mapping(dev);
            break;

        default:
            break;
    }
}

uint8_t
tandy_vid_in(uint16_t addr, void *priv)
{
    const tandy_t  *dev = (tandy_t *) priv;
    t1kvid_t       *vid = dev->vid;
    uint8_t         ret = 0xff;

    if ((addr >= 0x3d0) && (addr <= 0x3d7))
        addr = (addr & 0xff9) | 0x004;

    switch (addr) {
        case 0x03d4:
            ret = vid->crtcreg;
            break;

        case 0x03d5:
            ret = vid->crtc[vid->crtcreg];
            break;

        case 0x03da:
            ret = vid->status;
            break;

        case 0x3db:
            if (!dev->is_sl2 && (vid->lp_strobe == 1))
                vid->lp_strobe = 0;
            break;

        case 0x3dc:
            if (!dev->is_sl2 && (vid->lp_strobe == 0)) {
                vid->lp_strobe = 1;
                vid_update_latch(vid);
            }
            break;

        default:
            break;
    }

    return ret;
}

static void
vid_write(uint32_t addr, uint8_t val, void *priv)
{
    tandy_t  *dev = (tandy_t *) priv;
    t1kvid_t *vid = dev->vid;

    if (vid->memctrl == -1)
        return;

    if (dev->is_sl2) {
        if (vid->array[5] & 1)
            vid->b8000[addr & 0xffff] = val;
        else {
            if ((addr & 0x7fff) < vid->b8000_limit)
                vid->b8000[addr & 0x7fff] = val;
        }
    } else {
        vid->b8000[addr & vid->b8000_mask] = val;
    }
}

static uint8_t
vid_read(uint32_t addr, void *priv)
{
    const tandy_t  *dev = (tandy_t *) priv;
    const t1kvid_t *vid = dev->vid;

    if (vid->memctrl == -1)
        return 0xff;

    if (dev->is_sl2) {
        if (vid->array[5] & 1)
            return (vid->b8000[addr & 0xffff]);
        if ((addr & 0x7fff) < vid->b8000_limit)
            return (vid->b8000[addr & 0x7fff]);
        else
            return 0xff;
    } else {
        return (vid->b8000[addr & vid->b8000_mask]);
    }
}

static void
vid_poll(void *priv)
{
    tandy_t  *dev = (tandy_t *) priv;
    t1kvid_t *vid = dev->vid;
    uint16_t  cursoraddr  = (vid->crtc[15] | (vid->crtc[14] << 8)) & 0x3fff;
    int       drawcursor;
    int       x;
    int       c;
    int       xs_temp;
    int       ys_temp;
    int       oldvc;
    uint8_t   chr;
    uint8_t   attr;
    uint16_t  dat;
    int       cols[4];
    int       col;
    int       scanline_old;

    if (!vid->linepos) {
        timer_advance_u64(&vid->timer, vid->dispofftime);
        vid->status |= 1;
        vid->linepos = 1;
        scanline_old        = vid->scanline;
        if ((vid->crtc[8] & 3) == 3)
            vid->scanline = (vid->scanline << 1) & 7;
        if (vid->dispon) {
            if (vid->displine < vid->firstline) {
                vid->firstline = vid->displine;
                video_wait_for_buffer();
            }
            vid->lastline = vid->displine;
            cols[0]       = (vid->array[2] & 0xf) + 16;
            for (c = 0; c < 8; c++) {
                if (vid->array[3] & 4) {
                    buffer32->line[vid->displine << 1][c] = buffer32->line[(vid->displine << 1) + 1][c] = cols[0];
                    if (vid->mode & 1) {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 3) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 3) + 8] = cols[0];
                    } else {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 4) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 4) + 8] = cols[0];
                    }
                } else if ((vid->mode & 0x12) == 0x12) {
                    buffer32->line[vid->displine << 1][c] = buffer32->line[(vid->displine << 1) + 1][c] = 0;
                    if (vid->mode & 1) {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 3) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 3) + 8] = 0;
                    } else {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 4) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 4) + 8] = 0;
                    }
                } else {
                    buffer32->line[vid->displine << 1][c] = buffer32->line[(vid->displine << 1) + 1][c] = (vid->col & 15) + 16;
                    if (vid->mode & 1) {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 3) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 3) + 8] = (vid->col & 15) + 16;
                    } else {
                        buffer32->line[vid->displine << 1][c + (vid->crtc[1] << 4) + 8] = buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 4) + 8] = (vid->col & 15) + 16;
                    }
                }
            }
            if (dev->is_sl2 && (vid->array[5] & 1)) { /*640x200x16*/
                for (x = 0; x < vid->crtc[1] * 2; x++) {
                    dat = (vid->vram[(vid->memaddr << 1) & 0xffff] << 8) | vid->vram[((vid->memaddr << 1) + 1) & 0xffff];
                    vid->memaddr++;
                    buffer32->line[vid->displine << 1][(x << 2) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 2) + 8] = vid->array[((dat >> 12) & 0xf) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 2) + 9] = buffer32->line[(vid->displine << 1) + 1][(x << 2) + 9] = vid->array[((dat >> 8) & 0xf) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 2) + 10] = buffer32->line[(vid->displine << 1) + 1][(x << 2) + 10] = vid->array[((dat >> 4) & 0xf) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 2) + 11] = buffer32->line[(vid->displine << 1) + 1][(x << 2) + 11] = vid->array[(dat & 0xf) + 16] + 16;
                }
            } else if ((vid->array[3] & 0x10) && (vid->mode & 1)) { /*320x200x16*/
                for (x = 0; x < vid->crtc[1]; x++) {
                    dat = (vid->vram[((vid->memaddr << 1) & 0x1fff) + ((vid->scanline & 3) * 0x2000)] << 8) | vid->vram[((vid->memaddr << 1) & 0x1fff) + ((vid->scanline & 3) * 0x2000) + 1];
                    vid->memaddr++;
                    buffer32->line[vid->displine << 1][(x << 3) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 8] = buffer32->line[vid->displine << 1][(x << 3) + 9] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 9] = vid->array[((dat >> 12) & vid->array[1] & 0x0f) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 3) + 10] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 10] = buffer32->line[vid->displine << 1][(x << 3) + 11] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 11] = vid->array[((dat >> 8) & vid->array[1] & 0x0f) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 3) + 12] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 12] = buffer32->line[vid->displine << 1][(x << 3) + 13] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 13] = vid->array[((dat >> 4) & vid->array[1] & 0x0f) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 3) + 14] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 14] = buffer32->line[vid->displine << 1][(x << 3) + 15] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 15] = vid->array[(dat & vid->array[1] & 0x0f) + 16] + 16;
                }
            } else if (vid->array[3] & 0x10) { /*160x200x16*/
                for (x = 0; x < vid->crtc[1]; x++) {
                    if (dev->is_sl2) {
                        dat = (vid->vram[((vid->memaddr << 1) & 0x1fff) + ((vid->scanline & 1) * 0x2000)] << 8) | vid->vram[((vid->memaddr << 1) & 0x1fff) + ((vid->scanline & 1) * 0x2000) + 1];
                    } else {
                        dat = (vid->vram[((vid->memaddr << 1) & 0x1fff) + ((vid->scanline & 3) * 0x2000)] << 8) | vid->vram[((vid->memaddr << 1) & 0x1fff) + ((vid->scanline & 3) * 0x2000) + 1];
                    }
                    vid->memaddr++;
                    buffer32->line[vid->displine << 1][(x << 4) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 8] = buffer32->line[vid->displine << 1][(x << 4) + 9] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 9] = buffer32->line[vid->displine << 1][(x << 4) + 10] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 10] = buffer32->line[vid->displine << 1][(x << 4) + 11] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 11] = vid->array[((dat >> 12) & vid->array[1] & 0x0f) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 4) + 12] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 12] = buffer32->line[vid->displine << 1][(x << 4) + 13] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 13] = buffer32->line[vid->displine << 1][(x << 4) + 14] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 14] = buffer32->line[vid->displine << 1][(x << 4) + 15] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 15] = vid->array[((dat >> 8) & vid->array[1] & 0x0f) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 4) + 16] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 16] = buffer32->line[vid->displine << 1][(x << 4) + 17] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 17] = buffer32->line[vid->displine << 1][(x << 4) + 18] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 18] = buffer32->line[vid->displine << 1][(x << 4) + 19] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 19] = vid->array[((dat >> 4) & vid->array[1] & 0x0f) + 16] + 16;
                    buffer32->line[vid->displine << 1][(x << 4) + 20] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 20] = buffer32->line[vid->displine << 1][(x << 4) + 21] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 21] = buffer32->line[vid->displine << 1][(x << 4) + 22] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 22] = buffer32->line[vid->displine << 1][(x << 4) + 23] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 23] = vid->array[(dat & vid->array[1] & 0x0f) + 16] + 16;
                }
            } else if (vid->array[3] & 0x08) { /*640x200x4 - this implementation is a complete guess!*/
                for (x = 0; x < vid->crtc[1]; x++) {
                    dat = (vid->vram[((vid->memaddr << 1) & 0x1fff) + ((vid->scanline & 3) * 0x2000)] << 8) | vid->vram[((vid->memaddr << 1) & 0x1fff) + ((vid->scanline & 3) * 0x2000) + 1];
                    vid->memaddr++;
                    for (c = 0; c < 8; c++) {
                        chr = (dat >> 6) & 2;
                        chr |= ((dat >> 15) & 1);
                        buffer32->line[vid->displine << 1][(x << 3) + 8 + c] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 8 + c] = vid->array[(chr & vid->array[1]) + 16] + 16;
                        dat <<= 1;
                    }
                }
            } else if (vid->mode & 1) {
                for (x = 0; x < vid->crtc[1]; x++) {
                    chr        = vid->vram[(vid->memaddr << 1) & 0x3fff];
                    attr       = vid->vram[((vid->memaddr << 1) + 1) & 0x3fff];
                    drawcursor = ((vid->memaddr == cursoraddr) && vid->cursorvisible && vid->cursoron);
                    if (vid->mode & 0x20) {
                        cols[1] = vid->array[((attr & 15) & vid->array[1]) + 16] + 16;
                        cols[0] = vid->array[(((attr >> 4) & 7) & vid->array[1]) + 16] + 16;
                        if ((vid->blink & 16) && (attr & 0x80) && !drawcursor)
                            cols[1] = cols[0];
                    } else {
                        cols[1] = vid->array[((attr & 15) & vid->array[1]) + 16] + 16;
                        cols[0] = vid->array[((attr >> 4) & vid->array[1]) + 16] + 16;
                    }
                    if (vid->scanline & 8) {
                        for (c = 0; c < 8; c++) {
                            buffer32->line[vid->displine << 1][(x << 3) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] = cols[0];
                        }
                    } else {
                        for (c = 0; c < 8; c++) {
                            if (vid->scanline == 8) {
                                buffer32->line[vid->displine << 1][(x << 3) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] = cols[(fontdat[chr][7] & (1 << (c ^ 7))) ? 1 : 0];
                            } else {
                                buffer32->line[vid->displine << 1][(x << 3) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] = cols[(fontdat[chr][vid->scanline & 7] & (1 << (c ^ 7))) ? 1 : 0];
                            }
                        }
                    }
                    if (drawcursor) {
                        for (c = 0; c < 8; c++) {
                            buffer32->line[vid->displine << 1][(x << 3) + c + 8] ^= 15;
                            buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] ^= 15;
                        }
                    }
                    vid->memaddr++;
                }
            } else if (!(vid->mode & 2)) {
                for (x = 0; x < vid->crtc[1]; x++) {
                    chr        = vid->vram[(vid->memaddr << 1) & 0x3fff];
                    attr       = vid->vram[((vid->memaddr << 1) + 1) & 0x3fff];
                    drawcursor = ((vid->memaddr == cursoraddr) && vid->cursorvisible && vid->cursoron);
                    if (vid->mode & 0x20) {
                        cols[1] = vid->array[((attr & 15) & vid->array[1]) + 16] + 16;
                        cols[0] = vid->array[(((attr >> 4) & 7) & vid->array[1]) + 16] + 16;
                        if ((vid->blink & 16) && (attr & 0x80) && !drawcursor)
                            cols[1] = cols[0];
                    } else {
                        cols[1] = vid->array[((attr & 15) & vid->array[1]) + 16] + 16;
                        cols[0] = vid->array[((attr >> 4) & vid->array[1]) + 16] + 16;
                    }
                    vid->memaddr++;
                    if (vid->scanline & 8) {
                        for (c = 0; c < 8; c++)
                            buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] = buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 1 + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] = cols[0];
                    } else {
                        for (c = 0; c < 8; c++) {
                            if (vid->scanline == 8) {
                                buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] = buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 1 + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][7] & (1 << (c ^ 7))) ? 1 : 0];
                            } else {
                                buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] = buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 1 + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][vid->scanline & 7] & (1 << (c ^ 7))) ? 1 : 0];
                            }
                        }
                    }
                    if (drawcursor) {
                        for (c = 0; c < 16; c++) {
                            buffer32->line[vid->displine << 1][(x << 4) + c + 8] ^= 15;
                            buffer32->line[(vid->displine << 1) + 1][(x << 4) + c + 8] ^= 15;
                        }
                    }
                }
            } else if (!(vid->mode & 16)) {
                cols[0] = (vid->col & 15);
                col     = (vid->col & 16) ? 8 : 0;
                if (vid->mode & 4) {
                    cols[1] = col | 3;
                    cols[2] = col | 4;
                    cols[3] = col | 7;
                } else if (vid->col & 32) {
                    cols[1] = col | 3;
                    cols[2] = col | 5;
                    cols[3] = col | 7;
                } else {
                    cols[1] = col | 2;
                    cols[2] = col | 4;
                    cols[3] = col | 6;
                }
                cols[0] = vid->array[(cols[0] & vid->array[1]) + 16] + 16;
                cols[1] = vid->array[(cols[1] & vid->array[1]) + 16] + 16;
                cols[2] = vid->array[(cols[2] & vid->array[1]) + 16] + 16;
                cols[3] = vid->array[(cols[3] & vid->array[1]) + 16] + 16;
                for (x = 0; x < vid->crtc[1]; x++) {
                    dat = (vid->vram[((vid->memaddr << 1) & 0x1fff) + ((vid->scanline & 1) * 0x2000)] << 8) | vid->vram[((vid->memaddr << 1) & 0x1fff) + ((vid->scanline & 1) * 0x2000) + 1];
                    vid->memaddr++;
                    for (c = 0; c < 8; c++) {
                        buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] = buffer32->line[vid->displine << 1][(x << 4) + (c << 1) + 1 + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
                        dat <<= 2;
                    }
                }
            } else {
                cols[0] = 0;
                cols[1] = vid->array[(vid->col & vid->array[1]) + 16] + 16;
                for (x = 0; x < vid->crtc[1]; x++) {
                    dat = (vid->vram[((vid->memaddr << 1) & 0x1fff) + ((vid->scanline & 1) * 0x2000)] << 8) | vid->vram[((vid->memaddr << 1) & 0x1fff) + ((vid->scanline & 1) * 0x2000) + 1];
                    vid->memaddr++;
                    for (c = 0; c < 16; c++) {
                        buffer32->line[vid->displine << 1][(x << 4) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + c + 8] = cols[dat >> 15];
                        dat <<= 1;
                    }
                }
            }
        } else {
            if (vid->array[3] & 4) {
                if (vid->mode & 1) {
                    hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 3) + 16, (vid->array[2] & 0xf) + 16);
                    hline(buffer32, 0, (vid->displine << 1) + 1, (vid->crtc[1] << 3) + 16, (vid->array[2] & 0xf) + 16);
                } else {
                    hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 4) + 16, (vid->array[2] & 0xf) + 16);
                    hline(buffer32, 0, (vid->displine << 1) + 1, (vid->crtc[1] << 4) + 16, (vid->array[2] & 0xf) + 16);
                }
            } else {
                cols[0] = ((vid->mode & 0x12) == 0x12) ? 0 : (vid->col & 0xf) + 16;
                if (vid->mode & 1) {
                    hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 3) + 16, cols[0]);
                    hline(buffer32, 0, (vid->displine << 1) + 1, (vid->crtc[1] << 3) + 16, cols[0]);
                } else {
                    hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 4) + 16, cols[0]);
                    hline(buffer32, 0, (vid->displine << 1) + 1, (vid->crtc[1] << 4) + 16, cols[0]);
                }
            }
        }

        if (vid->mode & 1)
            x = (vid->crtc[1] << 3) + 16;
        else
            x = (vid->crtc[1] << 4) + 16;
        if (!dev->is_sl2 && vid->composite) {
            Composite_Process(vid->mode, 0, x >> 2, buffer32->line[vid->displine << 1]);
            Composite_Process(vid->mode, 0, x >> 2, buffer32->line[(vid->displine << 1) + 1]);
        } else {
            video_process_8(x, vid->displine << 1);
            video_process_8(x, (vid->displine << 1) + 1);
        }
        vid->scanline = scanline_old;
        if (vid->vc == vid->crtc[7] && !vid->scanline)
            vid->status |= 8;
        vid->displine++;
        if (vid->displine >= 360)
            vid->displine = 0;
    } else {
        timer_advance_u64(&vid->timer, vid->dispontime);
        if (vid->dispon)
            vid->status &= ~1;
        vid->linepos = 0;
        if (vid->vsynctime) {
            vid->vsynctime--;
            if (!vid->vsynctime)
                vid->status &= ~8;
        }
        if (vid->scanline == (vid->crtc[11] & 31) || ((vid->crtc[8] & 3) == 3 && vid->scanline == ((vid->crtc[11] & 31) >> 1))) {
            vid->cursorvisible  = 0;
        }
        if (vid->vadj) {
            vid->scanline++;
            vid->scanline &= 31;
            vid->memaddr = vid->memaddr_backup;
            vid->vadj--;
            if (!vid->vadj) {
                vid->dispon = 1;
                if (dev->is_sl2 && (vid->array[5] & 1))
                    vid->memaddr = vid->memaddr_backup = vid->crtc[13] | (vid->crtc[12] << 8);
                else
                    vid->memaddr = vid->memaddr_backup = (vid->crtc[13] | (vid->crtc[12] << 8)) & 0x3fff;
                vid->scanline = 0;
            }
        } else if (vid->scanline == vid->crtc[9] || ((vid->crtc[8] & 3) == 3 && vid->scanline == (vid->crtc[9] >> 1))) {
            vid->memaddr_backup = vid->memaddr;
            vid->scanline     = 0;
            oldvc       = vid->vc;
            vid->vc++;
            if (dev->is_sl2)
                vid->vc &= 255;
            else
                vid->vc &= 127;
            if (vid->vc == vid->crtc[6])
                vid->dispon = 0;
            if (oldvc == vid->crtc[4]) {
                vid->vc   = 0;
                vid->vadj = vid->crtc[5];
                if (!vid->vadj)
                    vid->dispon = 1;
                if (!vid->vadj) {
                    if (dev->is_sl2 && (vid->array[5] & 1))
                        vid->memaddr = vid->memaddr_backup = vid->crtc[13] | (vid->crtc[12] << 8);
                    else
                        vid->memaddr = vid->memaddr_backup = (vid->crtc[13] | (vid->crtc[12] << 8)) & 0x3fff;
                }
                if ((vid->crtc[10] & 0x60) == 0x20)
                    vid->cursoron = 0;
                else
                    vid->cursoron = vid->blink & 16;
            }
            if (vid->vc == vid->crtc[7]) {
                vid->dispon    = 0;
                vid->displine  = 0;
                vid->vsynctime = 16;
                picint(1 << 5);
                if (vid->crtc[7]) {
                    if (vid->mode & 1)
                        x = (vid->crtc[1] << 3) + 16;
                    else
                        x = (vid->crtc[1] << 4) + 16;
                    vid->lastline++;

                    xs_temp = x;
                    ys_temp = (vid->lastline - vid->firstline) << 1;

                    if ((xs_temp > 0) && (ys_temp > 0)) {
                        if (xs_temp < 64)
                            xs_temp = 656;
                        if (ys_temp < 32)
                            ys_temp = 400;
                        if (!enable_overscan)
                            xs_temp -= 16;

                        if ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get()) {
                            xsize = xs_temp;
                            ysize = ys_temp;
                            set_screen_size(xsize, ysize + (enable_overscan ? 16 : 0));

                            if (video_force_resize_get())
                                video_force_resize_set(0);
                        }

                        if (enable_overscan) {
                            video_blit_memtoscreen(0, (vid->firstline - 4) << 1,
                                                   xsize, ((vid->lastline - vid->firstline) + 8) << 1);
                        } else {
                            video_blit_memtoscreen(8, vid->firstline << 1,
                                                   xsize, (vid->lastline - vid->firstline) << 1);
                        }
                    }

                    frames++;

                    video_res_x = xsize;
                    video_res_y = ysize;
                    if ((vid->array[3] & 0x10) && (vid->mode & 1)) { /*320x200x16*/
                        video_res_x /= 2;
                        video_bpp = 4;
                    } else if (vid->array[3] & 0x10) { /*160x200x16*/
                        video_res_x /= 4;
                        video_bpp = 4;
                    } else if (vid->array[3] & 0x08) { /*640x200x4 - this implementation is a complete guess!*/
                        video_bpp = 2;
                    } else if (vid->mode & 1) {
                        video_res_x /= 8;
                        video_res_y /= vid->crtc[9] + 1;
                        video_bpp = 0;
                    } else if (!(vid->mode & 2)) {
                        video_res_x /= 16;
                        video_res_y /= vid->crtc[9] + 1;
                        video_bpp = 0;
                    } else if (!(vid->mode & 16)) {
                        video_res_x /= 2;
                        video_bpp = 2;
                    } else {
                        video_bpp = 1;
                    }
                }
                vid->firstline = 1000;
                vid->lastline  = 0;
                vid->blink++;
            }
        } else {
            vid->scanline++;
            vid->scanline &= 31;
            vid->memaddr = vid->memaddr_backup;
        }
        if (vid->scanline == (vid->crtc[10] & 31) || ((vid->crtc[8] & 3) == 3 && vid->scanline == ((vid->crtc[10] & 31) >> 1)))
            vid->cursorvisible = 1;
    }
}

void
tandy_vid_speed_changed(void *priv)
{
    tandy_t *dev = (tandy_t *) priv;

    recalc_timings(dev);
}

void
tandy_vid_close(void *priv)
{
    tandy_t *dev = (tandy_t *) priv;

    free(dev->vid);
    dev->vid = NULL;
}

void
tandy_vid_init(tandy_t *dev)
{
    int       display_type;
    t1kvid_t *vid;

    vid = calloc(1, sizeof(t1kvid_t));
    vid->memctrl = -1;

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_dram);

    display_type   = device_get_config_int("display_type");
    vid->composite = (display_type != TANDY_RGB);

    cga_comp_init(1);

    if (dev->is_sl2) {
        vid->b8000_limit = 0x8000;
        vid->planar_ctrl = 4;
        overscan_x = overscan_y = 16;

        io_sethandler(0x0065, 1, tandy_vid_in, NULL, NULL, tandy_vid_out, NULL, NULL, dev);
    } else
        vid->b8000_mask = 0x3fff;

    timer_add(&vid->timer, vid_poll, dev, 1);
    mem_mapping_add(&vid->mapping, 0xb8000, 0x08000,
                    vid_read, NULL, NULL, vid_write, NULL, NULL, NULL, 0, dev);
    io_sethandler(0x03d0, 16,
                  tandy_vid_in, NULL, NULL, tandy_vid_out, NULL, NULL, dev);

    dev->vid     = vid;
}

const device_config_t vid_config[] = {
  // clang-format off
    {
        .name = "display_type",
        .description = "Display type",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = TANDY_RGB,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "RGB",       .value = TANDY_RGB       },
            { .description = "Composite", .value = TANDY_COMPOSITE },
            { .description = ""                                    }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t tandy_1000_video_device = {
    .name          = "Tandy 1000",
    .internal_name = "tandy1000_video",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = tandy_vid_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = tandy_vid_speed_changed,
    .force_redraw  = NULL,
    .config        = vid_config
};

const device_t tandy_1000hx_video_device = {
    .name          = "Tandy 1000 HX",
    .internal_name = "tandy1000_hx_video",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = tandy_vid_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = tandy_vid_speed_changed,
    .force_redraw  = NULL,
    .config        = vid_config
};

const device_t tandy_1000sl_video_device = {
    .name          = "Tandy 1000SL2",
    .internal_name = "tandy1000_sl_video",
    .flags         = 0,
    .local         = 1,
    .init          = NULL,
    .close         = tandy_vid_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = tandy_vid_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
