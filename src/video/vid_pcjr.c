/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM PCjr video subsystem emulation
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Connor Hyde / starfrost <mario64crashed@gmail.com> 
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2025 starfrost
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/vid_cga_comp.h>
#include <86box/plat_unused.h>
#include "cpu.h"

#include <86box/m_pcjr.h>

static video_timings_t timing_dram = { VIDEO_BUS, 0, 0, 0, 0, 0, 0 }; /*No additional waitstates*/

static uint8_t crtcmask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void
recalc_address(pcjr_t *pcjr)
{
    uint8_t masked_memctrl = pcjr->memctrl;

    /* According to the Technical Reference, bits 2 and 5 are
       ignored if there is only 64k of RAM and there are only
       4 pages. */
    if (mem_size < 128)
        masked_memctrl &= ~0x24;

    if ((pcjr->memctrl & 0xc0) == 0xc0) {
        pcjr->vram  = &ram[(masked_memctrl & 0x06) << 14];
        pcjr->b8000 = &ram[(masked_memctrl & 0x30) << 11];
    } else {
        pcjr->vram  = &ram[(masked_memctrl & 0x07) << 14];
        pcjr->b8000 = &ram[(masked_memctrl & 0x38) << 11];
    }
}

void
pcjr_recalc_timings(pcjr_t *pcjr)
{
    double _dispontime;
    double _dispofftime;
    double disptime;

    if (pcjr->array[0] & 1) {
        disptime    = pcjr->crtc[0] + 1;
        _dispontime = pcjr->crtc[1];
    } else {
        disptime    = (pcjr->crtc[0] + 1) << 1;
        _dispontime = pcjr->crtc[1] << 1;
    }

    _dispofftime = disptime - _dispontime;
    _dispontime *= CGACONST;
    _dispofftime *= CGACONST;
    pcjr->dispontime  = (uint64_t) (_dispontime);
    pcjr->dispofftime = (uint64_t) (_dispofftime);
}

static int
vid_get_h_overscan_size(pcjr_t *pcjr)
{
    int ret;

    if (pcjr->array[0] & 1)
        ret = 128;
    else
        ret = 256;

    return ret;
}

static void
vid_out(uint16_t addr, uint8_t val, void *priv)
{
    pcjr_t *pcjr = (pcjr_t *) priv;
    uint8_t old;

    switch (addr) {
        case 0x3d0:
        case 0x3d2:
        case 0x3d4:
        case 0x3d6:
            pcjr->crtcreg = val & 0x1f;
            return;

        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
            old                       = pcjr->crtc[pcjr->crtcreg];
            pcjr->crtc[pcjr->crtcreg] = val & crtcmask[pcjr->crtcreg];
            if (pcjr->crtcreg == 2)
                overscan_x = vid_get_h_overscan_size(pcjr);
            if (old != val) {
                if (pcjr->crtcreg < 0xe || pcjr->crtcreg > 0x10) {
                    pcjr->fullchange = changeframecount;
                    pcjr_recalc_timings(pcjr);
                }
            }
            return;

        case 0x3da:
            if (!pcjr->array_ff)
                pcjr->array_index = val & 0x1f;
            else {
                if (pcjr->array_index & 0x10)
                    val &= 0x0f;
                pcjr->array[pcjr->array_index & 0x1f] = val;
                if (!(pcjr->array_index & 0x1f))
                    update_cga16_color(val);
            }
            pcjr->array_ff = !pcjr->array_ff;
            break;

        case 0x3df:
            pcjr->memctrl   = val;
            pcjr->pa        = val; /* The PCjr BIOS expects the value written to 3DF to
                                      then be readable from port 60, others it errors out
                                      with only 64k RAM set (but somehow, still works with
                                      128k or more RAM). */
            pcjr->addr_mode = val >> 6;
            recalc_address(pcjr);
            break;

        default:
            break;
    }
}

static uint8_t
vid_in(uint16_t addr, void *priv)
{
    pcjr_t *pcjr = (pcjr_t *) priv;
    uint8_t ret  = 0xff;

    switch (addr) {
        case 0x3d0:
        case 0x3d2:
        case 0x3d4:
        case 0x3d6:
            ret = pcjr->crtcreg;
            break;

        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
            ret = pcjr->crtc[pcjr->crtcreg];
            break;

        case 0x3da:
            pcjr->array_ff = 0;
            pcjr->status ^= 0x10;
            ret = pcjr->status;
            break;

        default:
            break;
    }

    return ret;
}

void
pcjr_waitstates(UNUSED(void *priv))
{
    int ws_array[16] = { 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5 };
    int ws;

    ws = ws_array[cycles & 0xf];
    cycles -= ws;
}

static void
vid_write(uint32_t addr, uint8_t val, void *priv)
{
    pcjr_t *pcjr = (pcjr_t *) priv;

    if (pcjr->memctrl == -1)
        return;

    pcjr_waitstates(NULL);

    pcjr->b8000[addr & 0x3fff] = val;
}

static uint8_t
vid_read(uint32_t addr, void *priv)
{
    const pcjr_t *pcjr = (pcjr_t *) priv;

    if (pcjr->memctrl == -1)
        return 0xff;

    pcjr_waitstates(NULL);

    return (pcjr->b8000[addr & 0x3fff]);
}

static int
vid_get_h_overscan_delta(pcjr_t *pcjr)
{
    int def;
    int coef;
    int ret;

    switch ((pcjr->array[0] & 0x13) | ((pcjr->array[3] & 0x08) << 5)) {
        case 0x13: /*320x200x16*/
            def = 0x56;
            coef = 8;
            break;
        case 0x12: /*160x200x16*/
            def = 0x2c; /* I'm going to assume a datasheet erratum here. */
            coef = 16;
            break;
        case 0x03: /*640x200x4*/
            def = 0x56;
            coef = 8;
            break;
        case 0x01: /*80 column text*/
            def = 0x5a;
            coef = 8;
            break;
        case 0x00: /*40 column text*/
        default:
            def = 0x2c;
            coef = 16;
            break;
        case 0x02: /*320x200x4*/
            def = 0x2b;
            coef = 16;
            break;
        case 0x102: /*640x200x2*/
            def = 0x2b;
            coef = 16;
            break;
    }

    ret = def - pcjr->crtc[0x02];

    if (ret < -8)
        ret = -8;

    if (ret > 8)
        ret = 8;

    return ret * coef;
}

static void
vid_blit_v_overscan(pcjr_t *pcjr)
{
    int      cols = (pcjr->array[2] & 0xf) + 16;
    int      y0   = pcjr->firstline;
    int      y    = pcjr->lastline + 8;
    int      h    = 8;
    int      ho_s = vid_get_h_overscan_size(pcjr);
    int      i;
    int      x;

    if (pcjr->double_type > DOUBLE_NONE) {
        y0 <<= 1;
        y <<= 1;

        h <<= 1;
    }

    if (pcjr->array[0] & 1)
        x = (pcjr->crtc[1] << 3) + ho_s;
    else
        x = (pcjr->crtc[1] << 4) + ho_s;

    for (i = 0; i < h; i++) {
        hline(buffer32, 0, y0 + i, x, cols);
        hline(buffer32, 0, y + i, x, cols);

        if (pcjr->composite) {
            Composite_Process(pcjr->array[0], 0, x >> 2, buffer32->line[y0 + i]);
            Composite_Process(pcjr->array[0], 0, x >> 2, buffer32->line[y + i]);
        } else {
            video_process_8(x, y0 + i);
            video_process_8(x, y + i);
        }
    }
}

static void
vid_render(pcjr_t *pcjr, int line, int ho_s, int ho_d)
{
    uint16_t cursoraddr   = (pcjr->crtc[15] | (pcjr->crtc[14] << 8)) & 0x3fff;
    int      drawcursor;
    uint8_t  chr;
    uint8_t  attr;
    uint16_t dat;
    int      cols[4];
    uint16_t offset       = 0;
    uint16_t mask         = 0x1fff;
    int      x;

    cols[0]        = (pcjr->array[2] & 0xf) + 16;

    if (pcjr->array[0] & 1)
        hline(buffer32, 0, line, (pcjr->crtc[1] << 3) + ho_s, cols[0]);
    else
        hline(buffer32, 0, line, (pcjr->crtc[1] << 4) + ho_s, cols[0]);

    switch (pcjr->addr_mode) {
        case 0: /*Alpha*/
            offset = 0;
            mask   = 0x3fff;
            break;
        case 1: /*Low resolution graphics*/
            offset = (pcjr->scanline & 1) * 0x2000;
            break;
        case 3: /*High resolution graphics*/
            offset = (pcjr->scanline & 3) * 0x2000;
            break;
        default:
            break;
    }
    switch ((pcjr->array[0] & 0x13) | ((pcjr->array[3] & 0x08) << 5)) {
        case 0x13: /*320x200x16*/
            for (x = 0; x < pcjr->crtc[1]; x++) {
                int ef_x = (x << 3) + ho_d;
                dat = (pcjr->vram[((pcjr->memaddr << 1) & mask) + offset] << 8) |
                      pcjr->vram[((pcjr->memaddr << 1) & mask) + offset + 1];
                pcjr->memaddr++;
                buffer32->line[line][ef_x] = buffer32->line[line][ef_x + 1] =
                    pcjr->array[((dat >> 12) & pcjr->array[1] & 0x0f) + 16] + 16;
                buffer32->line[line][ef_x + 2] = buffer32->line[line][ef_x + 3] =
                    pcjr->array[((dat >> 8) & pcjr->array[1] & 0x0f) + 16] + 16;
                buffer32->line[line][ef_x + 4] = buffer32->line[line][ef_x + 5] =
                    pcjr->array[((dat >> 4) & pcjr->array[1] & 0x0f) + 16] + 16;
                buffer32->line[line][ef_x + 6] = buffer32->line[line][ef_x + 7] =
                    pcjr->array[(dat & pcjr->array[1] & 0x0f) + 16] + 16;
            }
            break;
        case 0x12: /*160x200x16*/
            for (x = 0; x < pcjr->crtc[1]; x++) {
                int ef_x = (x << 4) + ho_d;
                dat = (pcjr->vram[((pcjr->memaddr << 1) & mask) + offset] << 8) |
                      pcjr->vram[((pcjr->memaddr << 1) & mask) + offset + 1];
                pcjr->memaddr++;
                buffer32->line[line][ef_x] = buffer32->line[line][ef_x + 1] =
                buffer32->line[line][ef_x + 2] = buffer32->line[line][ef_x + 3] =
                    pcjr->array[((dat >> 12) & pcjr->array[1] & 0x0f) + 16] + 16;
                buffer32->line[line][ef_x + 4] = buffer32->line[line][ef_x + 5] =
                    buffer32->line[line][ef_x + 6] = buffer32->line[line][ef_x + 7] =
                    pcjr->array[((dat >> 8) & pcjr->array[1] & 0x0f) + 16] + 16;
                buffer32->line[line][ef_x + 8] = buffer32->line[line][ef_x + 9] =
                buffer32->line[line][ef_x + 10] = buffer32->line[line][ef_x + 11] =
                    pcjr->array[((dat >> 4) & pcjr->array[1] & 0x0f) + 16] + 16;
                buffer32->line[line][ef_x + 12] = buffer32->line[line][ef_x + 13] =
                buffer32->line[line][ef_x + 14] = buffer32->line[line][ef_x + 15] =
                    pcjr->array[(dat & pcjr->array[1] & 0x0f) + 16] + 16;
            }
            break;
        case 0x03: /*640x200x4*/
            for (x = 0; x < pcjr->crtc[1]; x++) {
                int ef_x = (x << 3) + ho_d;
                dat = (pcjr->vram[((pcjr->memaddr << 1) & mask) + offset + 1] << 8) |
                      pcjr->vram[((pcjr->memaddr << 1) & mask) + offset];
                pcjr->memaddr++;
                for (uint8_t c = 0; c < 8; c++) {
                    chr = (dat >> 7) & 1;
                    chr |= ((dat >> 14) & 2);
                    buffer32->line[line][ef_x + c] = pcjr->array[(chr & pcjr->array[1] & 0x0f) + 16] + 16;
                    dat <<= 1;
                }
            }
            break;
        case 0x01: /*80 column text*/
            for (x = 0; x < pcjr->crtc[1]; x++) {
                int ef_x = (x << 3) + ho_d;
                chr        = pcjr->vram[((pcjr->memaddr << 1) & mask) + offset];
                attr       = pcjr->vram[((pcjr->memaddr << 1) & mask) + offset + 1];
                drawcursor = ((pcjr->memaddr == cursoraddr) && pcjr->cursorvisible && pcjr->cursoron);
                if (pcjr->array[3] & 4) {
                    cols[1] = pcjr->array[((attr & 15) & pcjr->array[1] & 0x0f) + 16] + 16;
                    cols[0] = pcjr->array[(((attr >> 4) & 7) & pcjr->array[1] & 0x0f) + 16] + 16;
                    if ((pcjr->blink & 16) && (attr & 0x80) && !drawcursor)
                        cols[1] = cols[0];
                } else {
                    cols[1] = pcjr->array[((attr & 15) & pcjr->array[1] & 0x0f) + 16] + 16;
                    cols[0] = pcjr->array[((attr >> 4) & pcjr->array[1] & 0x0f) + 16] + 16;
                }
                if (pcjr->scanline & 8)
                    for (uint8_t c = 0; c < 8; c++)
                        buffer32->line[line][ef_x + c] = cols[0];
                    else for (uint8_t c = 0; c < 8; c++)
                        buffer32->line[line][ef_x + c] = cols[(fontdat[chr][pcjr->scanline & 7] & (1 << (c ^ 7))) ? 1 : 0];
                if (drawcursor)  for (uint8_t c = 0; c < 8; c++)
                    buffer32->line[line][ef_x + c] ^= 15;
                pcjr->memaddr++;
            }
            break;
        case 0x00: /*40 column text*/
            for (x = 0; x < pcjr->crtc[1]; x++) {
                int ef_x = (x << 4) + ho_d;
                chr        = pcjr->vram[((pcjr->memaddr << 1) & mask) + offset];
                attr       = pcjr->vram[((pcjr->memaddr << 1) & mask) + offset + 1];
                drawcursor = ((pcjr->memaddr == cursoraddr) && pcjr->cursorvisible && pcjr->cursoron);
                if (pcjr->array[3] & 4) {
                    cols[1] = pcjr->array[((attr & 15) & pcjr->array[1] & 0x0f) + 16] + 16;
                    cols[0] = pcjr->array[(((attr >> 4) & 7) & pcjr->array[1] & 0x0f) + 16] + 16;
                    if ((pcjr->blink & 16) && (attr & 0x80) && !drawcursor)
                        cols[1] = cols[0];
                } else {
                    cols[1] = pcjr->array[((attr & 15) & pcjr->array[1] & 0x0f) + 16] + 16;
                    cols[0] = pcjr->array[((attr >> 4) & pcjr->array[1] & 0x0f) + 16] + 16;
                }
                pcjr->memaddr++;
                if (pcjr->scanline & 8)
                    for (uint8_t c = 0; c < 8; c++)
                        buffer32->line[line][ef_x + (c << 1)] =
                        buffer32->line[line][ef_x + (c << 1) + 1] = cols[0];
                else
                    for (uint8_t c = 0; c < 8; c++)
                        buffer32->line[line][ef_x + (c << 1)] =
                        buffer32->line[line][ef_x + (c << 1) + 1] = cols[(fontdat[chr][pcjr->scanline & 7] & (1 << (c ^ 7))) ? 1 : 0];
                if (drawcursor)  for (uint8_t c = 0; c < 16; c++)
                    buffer32->line[line][ef_x + c] ^= 15;
            }
            break;
        case 0x02: /*320x200x4*/
            cols[0] = pcjr->array[0 + 16] + 16;
            cols[1] = pcjr->array[1 + 16] + 16;
            cols[2] = pcjr->array[2 + 16] + 16;
            cols[3] = pcjr->array[3 + 16] + 16;
            for (x = 0; x < pcjr->crtc[1]; x++) {
                int ef_x = (x << 4) + ho_d;
                dat = (pcjr->vram[((pcjr->memaddr << 1) & mask) + offset] << 8) |
                      pcjr->vram[((pcjr->memaddr << 1) & mask) + offset + 1];
                pcjr->memaddr++;
                for (uint8_t c = 0; c < 8; c++) {
                    buffer32->line[line][ef_x + (c << 1)] = buffer32->line[line][ef_x + (c << 1) + 1] = cols[dat >> 14];
                    dat <<= 2;
                }
            }
            break;
        case 0x102: /*640x200x2*/
            cols[0] = pcjr->array[0 + 16] + 16;
            cols[1] = pcjr->array[1 + 16] + 16;
            for (x = 0; x < pcjr->crtc[1]; x++) {
                int ef_x = (x << 4) + ho_d;
                dat = (pcjr->vram[((pcjr->memaddr << 1) & mask) + offset] << 8) |
                      pcjr->vram[((pcjr->memaddr << 1) & mask) + offset + 1];
                pcjr->memaddr++;
                for (uint8_t c = 0; c < 16; c++) {
                    buffer32->line[line][ef_x + c] = cols[dat >> 15];
                    dat <<= 1;
                }
            }
            break;

        default:
            break;
    }
}

static void
vid_render_blank(pcjr_t *pcjr, int line, int ho_s)
{
    if (pcjr->array[3] & 4) {
        if (pcjr->array[0] & 1)
            hline(buffer32, 0, line, (pcjr->crtc[1] << 3) + ho_s, (pcjr->array[2] & 0xf) + 16);
        else
            hline(buffer32, 0, line, (pcjr->crtc[1] << 4) + ho_s, (pcjr->array[2] & 0xf) + 16);
    } else {
        if (pcjr->array[0] & 1)
            hline(buffer32, 0, line, (pcjr->crtc[1] << 3) + ho_s, pcjr->array[0 + 16] + 16);
        else
            hline(buffer32, 0, line, (pcjr->crtc[1] << 4) + ho_s, pcjr->array[0 + 16] + 16);
    }
}

static void
vid_render_process(pcjr_t *pcjr, int line, int ho_s)
{
    int x;

    if (pcjr->array[0] & 1)
        x = (pcjr->crtc[1] << 3) + ho_s;
    else
        x = (pcjr->crtc[1] << 4) + ho_s;

    if (pcjr->composite)
        Composite_Process(pcjr->array[0], 0, x >> 2, buffer32->line[line]);
    else
        video_process_8(x, line);
}

static void
vid_poll(void *priv)
{
    pcjr_t  *pcjr = (pcjr_t *) priv;
    int      x;
    int      xs_temp;
    int      ys_temp;
    int      oldvc;
    int      scanline_old;
    int      l = pcjr->displine + 8;
    int      ho_s = vid_get_h_overscan_size(pcjr);
    int      ho_d = vid_get_h_overscan_delta(pcjr) + (ho_s / 2);
    int      old_ma;

    if (!pcjr->linepos) {
        timer_advance_u64(&pcjr->timer, pcjr->dispofftime);
        pcjr->status &= ~1;
        pcjr->linepos = 1;
        scanline_old         = pcjr->scanline;
        if ((pcjr->crtc[8] & 3) == 3)
            pcjr->scanline = (pcjr->scanline << 1) & 7;
        if (pcjr->dispon) {
            if (pcjr->displine < pcjr->firstline) {
                pcjr->firstline = pcjr->displine;
                video_wait_for_buffer();
            }
            pcjr->lastline = pcjr->displine;
            switch (pcjr->double_type) {
                default:
                    vid_render(pcjr, l << 1, ho_s, ho_d);
                    vid_render_blank(pcjr, (l << 1) + 1, ho_s);
                    break;
                case DOUBLE_NONE:
                    vid_render(pcjr, l, ho_s, ho_d);
                    break;
                case DOUBLE_SIMPLE:
                    old_ma = pcjr->memaddr;
                    vid_render(pcjr, l << 1, ho_s, ho_d);
                    pcjr->memaddr = old_ma;
                    vid_render(pcjr, (l << 1) + 1, ho_s, ho_d);
                    break;
            }
        } else  switch (pcjr->double_type) {
            default:
                vid_render_blank(pcjr, l << 1, ho_s);
                break;
            case DOUBLE_NONE:
                vid_render_blank(pcjr, l, ho_s);
                break;
            case DOUBLE_SIMPLE:
                vid_render_blank(pcjr, l << 1, ho_s);
                vid_render_blank(pcjr, (l << 1) + 1, ho_s);
                break;
        }

        switch (pcjr->double_type) {
            default:
                vid_render_process(pcjr, l << 1, ho_s);
                vid_render_process(pcjr, (l << 1) + 1, ho_s);
                break;
            case DOUBLE_NONE:
                vid_render_process(pcjr, l, ho_s);
                break;
        }

        pcjr->scanline = scanline_old;
        if (pcjr->vc == pcjr->crtc[7] && !pcjr->scanline) {
            pcjr->status |= 8;
        }
        pcjr->displine++;
        if (pcjr->displine >= 360)
            pcjr->displine = 0;
    } else {
        timer_advance_u64(&pcjr->timer, pcjr->dispontime);
        if (pcjr->dispon)
            pcjr->status |= 1;
        pcjr->linepos = 0;
        if (pcjr->vsynctime) {
            pcjr->vsynctime--;
            if (!pcjr->vsynctime) {
                pcjr->status &= ~8;
            }
        }
        if (pcjr->scanline == (pcjr->crtc[11] & 31) || ((pcjr->crtc[8] & 3) == 3 && pcjr->scanline == ((pcjr->crtc[11] & 31) >> 1))) {
            pcjr->cursorvisible  = 0;
        }
        if (pcjr->vadj) {
            pcjr->scanline++;
            pcjr->scanline &= 31;
            pcjr->memaddr = pcjr->memaddr_backup;
            pcjr->vadj--;
            if (!pcjr->vadj) {
                pcjr->dispon = 1;
                pcjr->memaddr = pcjr->memaddr_backup = (pcjr->crtc[13] | (pcjr->crtc[12] << 8)) & 0x3fff;
                pcjr->scanline                = 0;
            }
        } else if (pcjr->scanline == pcjr->crtc[9] || ((pcjr->crtc[8] & 3) == 3 && pcjr->scanline == (pcjr->crtc[9] >> 1))) {
            pcjr->memaddr_backup = pcjr->memaddr;
            pcjr->scanline     = 0;
            oldvc        = pcjr->vc;
            pcjr->vc++;
            pcjr->vc &= 127;
            if (pcjr->vc == pcjr->crtc[6])
                pcjr->dispon = 0;
            if (oldvc == pcjr->crtc[4]) {
                pcjr->vc   = 0;
                pcjr->vadj = pcjr->crtc[5];
                if (!pcjr->vadj)
                    pcjr->dispon = 1;
                if (!pcjr->vadj)
                    pcjr->memaddr = pcjr->memaddr_backup = (pcjr->crtc[13] | (pcjr->crtc[12] << 8)) & 0x3fff;
                if ((pcjr->crtc[10] & 0x60) == 0x20)
                    pcjr->cursoron = 0;
                else
                    pcjr->cursoron = pcjr->blink & 16;
            }
            if (pcjr->vc == pcjr->crtc[7]) {
                pcjr->dispon    = 0;
                pcjr->displine  = 0;
                pcjr->vsynctime = 16;
                picint(1 << 5);
                if (pcjr->crtc[7]) {
                    if (pcjr->array[0] & 1)
                        x = (pcjr->crtc[1] << 3) + ho_s;
                    else
                        x = (pcjr->crtc[1] << 4) + ho_s;
                    pcjr->lastline++;

                    xs_temp = x;
                    ys_temp = (pcjr->lastline - pcjr->firstline) << 1;

                    if ((xs_temp > 0) && (ys_temp > 0)) {
                        int actual_ys = ys_temp;

                        if (xs_temp < 64)
                            xs_temp = 656;
                        if (ys_temp < 32)
                            ys_temp = 400;
                        if (!enable_overscan)
                            xs_temp -= ho_s;

                        if ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get()) {
                            xsize = xs_temp;
                            ysize = ys_temp;

                            set_screen_size(xsize, ysize + (enable_overscan ? 32 : 0));

                            if (video_force_resize_get())
                                video_force_resize_set(0);
                        }

                        vid_blit_v_overscan(pcjr);

                        if (pcjr->double_type > DOUBLE_NONE) {
                            if (enable_overscan) {
                                cga_blit_memtoscreen(0, pcjr->firstline << 1,
                                                     xsize, actual_ys + 32,
                                                     pcjr->double_type);
                            } else if (pcjr->apply_hd) {
                                cga_blit_memtoscreen(ho_s / 2, (pcjr->firstline << 1) + 16,
                                                     xsize, actual_ys,
                                                     pcjr->double_type);
                            } else {
                                cga_blit_memtoscreen(ho_d, (pcjr->firstline << 1) + 16,
                                                     xsize, actual_ys,
                                                     pcjr->double_type);
                            }
                        } else {
                            if (enable_overscan) {
                                video_blit_memtoscreen(0, pcjr->firstline,
                                                       xsize, (actual_ys >> 1) + 16);
                            } else if (pcjr->apply_hd) {
                                video_blit_memtoscreen(ho_s / 2, pcjr->firstline + 8,
                                                       xsize, actual_ys >> 1);
                            } else {
                                video_blit_memtoscreen(ho_d, pcjr->firstline + 8,
                                                       xsize, actual_ys >> 1);
                            }
                        }
                    }

                    frames++;
                    video_res_x = xsize;
                    video_res_y = ysize;
                }
                pcjr->firstline = 1000;
                pcjr->lastline  = 0;
                pcjr->blink++;
            }
        } else {
            pcjr->scanline++;
            pcjr->scanline &= 31;
            pcjr->memaddr = pcjr->memaddr_backup;
        }
        if (pcjr->scanline == (pcjr->crtc[10] & 31) || ((pcjr->crtc[8] & 3) == 3 && pcjr->scanline == ((pcjr->crtc[10] & 31) >> 1)))
            pcjr->cursorvisible = 1;
    }
}


void
pcjr_vid_init(pcjr_t *pcjr)
{
    int     display_type;

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_dram);

    pcjr->memctrl   = -1;
    if (mem_size < 128)
        pcjr->memctrl &= ~0x24;

    display_type    = device_get_config_int("display_type");
    pcjr->composite = (display_type == PCJR_COMPOSITE);
    pcjr->apply_hd  = device_get_config_int("apply_hd");
    overscan_x = 256;
    overscan_y = 32;

    mem_mapping_add(&pcjr->mapping, 0xb8000, 0x08000,
                    vid_read, NULL, NULL,
                    vid_write, NULL, NULL, NULL, 0, pcjr);
    io_sethandler(0x03d0, 16,
                  vid_in, NULL, NULL, vid_out, NULL, NULL, pcjr);
    timer_add(&pcjr->timer, vid_poll, pcjr, 1);

    if (pcjr->composite)
        cga_palette = 0;
    else
        cga_palette = (display_type << 1);
    cgapal_rebuild();

    pcjr->double_type = device_get_config_int("double_type");
    cga_interpolate_init();

    monitors[monitor_index_global].mon_composite = !!pcjr->composite;
}
