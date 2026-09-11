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

    if (pcjr->jx_profile) {
        uint32_t display_page = (pcjr->memctrl & 7) << 14;
        uint32_t cpu_page = (pcjr->memctrl & 0x38) << 11;
        if ((pcjr->memctrl & 0xc0) == 0xc0) {
            display_page &= ~0x4000;
            cpu_page &= ~0x4000;
        }
        pcjr->vram = display_page < pcjr->shared_size ? pcjr->shared_ram + display_page : NULL;
        pcjr->b8000 = cpu_page < pcjr->shared_size ? pcjr->shared_ram + cpu_page : NULL;
        return;
    }
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
    /* Unprogrammed or software-created invalid CRTC widths must still let
       emulated time advance, rather than spinning a zero-period timer. */
    if (pcjr->jx_profile) {
        if (_dispontime < 1)
            _dispontime = 1;
        if (_dispofftime < 1)
            _dispofftime = 1;
    }
    _dispontime *= CGACONST;
    _dispofftime *= CGACONST;
    pcjr->dispontime  = (uint64_t) (int64_t) (_dispontime);
    pcjr->dispofftime = (uint64_t) (int64_t) (_dispofftime);
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
            if (pcjr->jx_profile && pcjr->crtcreg >= 0x10)
                return; /* Light-pen address registers are read-only. */
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
                if ((pcjr->array_index & 0x1f) == 0x02)
                    update_cga16_color(pcjr->array[0], val & 0xf);
                else if (!(pcjr->array_index & 0x1f))
                    update_cga16_color(val, pcjr->array[2] & 0xf);
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
    int def; /* Reference sync-start-to-display delay from the PCjr BIOS. */
    int coef;
    int ret;

    switch ((pcjr->array[0] & 0x13) | ((pcjr->array[3] & 0x08) << 5)) {
        case 0x13: /*320x200x16*/
            def = 28;
            coef = 8;
            break;
        case 0x12: /*160x200x16*/
            def = 14;
            coef = 16;
            break;
        case 0x03: /*640x200x4*/
            def = 28;
            coef = 8;
            break;
        case 0x01: /*80 column text*/
            def = 24;
            coef = 8;
            break;
        case 0x00: /*40 column text*/
        default:
            def = 13;
            coef = 16;
            break;
        case 0x02: /*320x200x4*/
            def = 14;
            coef = 16;
            break;
        case 0x102: /*640x200x2*/
            def = 14;
            coef = 16;
            break;
    }

    if (pcjr->jx_profile) {
        /* JX uses sync end, with four low-bandwidth or ten high-bandwidth
         * character clocks between sync end and the next display line. */
        const int reference_back_porch = (pcjr->array[0] & 1) ? 10 : 4;

        ret = video_6845_get_hsync_delay(pcjr->crtc, pcjr->crtc[3] & 0x0f) -
              reference_back_porch;
    } else {
        /* Preserve PCjr sync-start alignment; raw R3 width is not a viewport offset. */
        ret = video_6845_get_hsync_delay(pcjr->crtc, 0) - def;
    }

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

/* Decode one CRTC character clock to output color codes. The scanout and JX
   diagnostic feedback share this pixel path, including the two-plane mode A. */
static void
vid_cell(pcjr_t *pcjr, uint16_t memaddr, int scanline, uint8_t pixels[16])
{
    uint16_t offset = 0;
    uint16_t mask = 0x1fff;
    uint8_t mode = pcjr->array[0];
    uint16_t dat;
    uint8_t lo, hi;
    int width = (mode & 1) ? 8 : 16;
    int graphics;
    int blink_bit = pcjr->jx_profile ? 2 : 4;

    if (pcjr->jx_profile)
        mode = (mode & ~2) | ((pcjr->pb & 4) ? 0 : 2);
    graphics = mode & 2;

    if (pcjr->jx_profile && (!(pcjr->array[0] & 8) ||
        (pcjr->array[4] & 3) || !(pcjr->jx_array[1] & 0x10) || !pcjr->vram)) {
        memset(pixels, pcjr->array[2] & 15, width);
        return;
    }

    switch (pcjr->addr_mode) {
        case 0:
            mask = 0x3fff;
            break;
        case 1:
            offset = (scanline & 1) * 0x2000;
            break;
        case 3:
            offset = (scanline & 3) * 0x2000;
            break;
        default:
            break;
    }
    offset += (memaddr << 1) & mask;
    lo = pcjr->vram[offset];
    hi = pcjr->vram[offset + 1];
    dat = (lo << 8) | hi;

    if (!graphics) {
        uint16_t cursoraddr = (pcjr->crtc[15] | (pcjr->crtc[14] << 8)) & 0x3fff;
        int cursor = memaddr == cursoraddr && pcjr->cursorvisible && pcjr->cursoron;
        uint8_t fg = hi & 15;
        uint8_t bg = hi >> 4;
        uint8_t glyph = pcjr->jx_profile ? pcjr->cg1[lo * 8 + (scanline & 7)] :
                                          fontdat[lo][scanline & 7];
        if (pcjr->array[3] & blink_bit) {
            bg &= 7;
            if ((pcjr->blink & 16) && (hi & 0x80) && !cursor)
                fg = bg;
        }
        fg = pcjr->array[16 + (fg & pcjr->array[1] & 15)] & 15;
        bg = pcjr->array[16 + (bg & pcjr->array[1] & 15)] & 15;
        if (scanline & 8)
            glyph = 0;
        for (int c = 0; c < width; c++) {
            int dot = c / (width / 8);
            pixels[c] = (glyph & (0x80 >> dot)) ? fg : bg;
            if (cursor)
                pixels[c] ^= 15;
        }
    } else {
        for (int c = 0; c < width; c++) {
            uint8_t index;
            switch ((mode & 0x13) | ((pcjr->array[3] & 8) << 5)) {
                case 0x13: /* 320x200x16, packed nibbles. */
                case 0x12: /* 160x200x16, packed nibbles. */
                    index = (dat >> (12 - (c / (width / 4)) * 4)) & 15;
                    break;
                case 0x03: /* 640x200x4, adjacent low/high bitplane bytes. */
                    index = ((lo >> (7 - c)) & 1) | (((hi >> (7 - c)) & 1) << 1);
                    break;
                case 0x02:
                    index = (dat >> (14 - (c / 2) * 2)) & 3;
                    break;
                case 0x102:
                    index = (dat >> (15 - c)) & 1;
                    break;
                default:
                    index = 0;
                    break;
            }
            if (pcjr->jx_profile || (mode & 0x11))
                index &= pcjr->array[1] & 15;
            if (pcjr->jx_profile && (pcjr->array[3] & 2))
                index = (index & 7) | ((pcjr->blink & 16) >> 1);
            pixels[c] = pcjr->array[16 + index] & 15;
        }
    }
}

static void
vid_render(pcjr_t *pcjr, int line, int ho_s, int ho_d)
{
    uint8_t pixels[16];
    int width = (pcjr->array[0] & 1) ? 8 : 16;

    hline(buffer32, 0, line, pcjr->crtc[1] * width + ho_s, (pcjr->array[2] & 15) + 16);
    for (int x = 0; x < pcjr->crtc[1]; x++) {
        vid_cell(pcjr, pcjr->memaddr++, pcjr->scanline, pixels);
        for (int c = 0; c < width; c++)
            buffer32->line[line][x * width + ho_d + c] = pixels[c] + 16;
    }
}

/* The active half-line ends at timer.ts. Reads sample that clock; they never
   move the beam. CRTC addresses remain character addresses for light pen. */
static unsigned
pcjx_raster_dot(pcjr_t *pcjr)
{
    unsigned width = pcjr->crtc[1] * ((pcjr->array[0] & 1) ? 8 : 16);
    uint128_t remaining = timer_get_remaining_u64(&pcjr->timer);
    if (!width || !pcjr->dispontime || remaining >= pcjr->dispontime)
        return 0;
    unsigned dot = ((uint128_t) (pcjr->dispontime - remaining) * width) / pcjr->dispontime;
    return dot < width ? dot : width - 1;
}

uint8_t
pcjx_vid_in(uint16_t port, uint8_t vp_mask, void *priv)
{
    pcjr_t *pcjr = (pcjr_t *) priv;
    if (port == 0x3da) {
        uint8_t ret = (pcjr->status & 0x0b) | 4; /* Disconnected light-pen switch. */
        uint8_t color = 0; /* Blanking drives all four diagnostic outputs low. */
        if (!vp_mask)
            return 0xff;
        if (pcjr->status & 1) {
            uint8_t pixels[16];
            unsigned width = (pcjr->array[0] & 1) ? 8 : 16;
            unsigned dot = pcjx_raster_dot(pcjr);
            int scanline = ((pcjr->crtc[8] & 3) == 3) ? (pcjr->scanline << 1) & 7 : pcjr->scanline;
            vid_cell(pcjr, pcjr->memaddr + dot / width, scanline, pixels);
            color = pixels[dot % width];
        }
        uint8_t feedback = 0x10;
        for (int bank = 0; bank < 2; bank++) {
            if (!(vp_mask & (1 << bank)))
                continue;
            if (bank)
                pcjr->jx_array_ff = 0;
            else
                pcjr->array_ff = 0;
            feedback &= ((color >> pcjr->dot_component[bank]) & 1) << 4;
        }
        return ret | feedback;
    }
    return vid_in(port, priv);
}

void
pcjx_vid_out(uint16_t port, uint8_t val, uint8_t vp_mask, void *priv)
{
    pcjr_t *pcjr = (pcjr_t *) priv;
    switch (port) {
        case 0x3d9:
            pcjr->pg2 = val;
            return;
        case 0x3df:
            pcjr->memctrl = val;
            pcjr->addr_mode = val >> 6;
            recalc_address(pcjr);
            return;
        case 0x3db:
            pcjr->status &= ~2;
            return;
        case 0x3dc:
            if (!(pcjr->status & 2)) {
                unsigned width = (pcjr->array[0] & 1) ? 8 : 16;
                uint16_t address = pcjr->memaddr;
                if (pcjr->status & 1)
                    address += pcjx_raster_dot(pcjr) / width;
                address &= 0x3fff;
                pcjr->crtc[0x10] = address >> 8;
                pcjr->crtc[0x11] = address;
                pcjr->status |= 2;
            }
            return;
        case 0x3da:
            for (int bank = 0; bank < 2; bank++) {
                int *phase = bank ? &pcjr->jx_array_ff : &pcjr->array_ff;
                int *index = bank ? &pcjr->jx_array_index : &pcjr->array_index;
                if (!(vp_mask & (1 << bank)))
                    continue;
                if (!*phase) {
                    *index = val & 31;
                    if (val < 4)
                        pcjr->dot_component[bank] = val;
                } else {
                    uint8_t *registers = pcjr->array;
                    if (*index == 0 || *index == 1 || *index == 3)
                        registers = bank ? pcjr->jx_array : pcjr->array;
                    /* Register 05 exists only in VP1; undefined registers are
                       retained bank-locally without assigning them devices. */
                    else if (*index != 2 && *index != 4 && *index != 6 && *index < 16)
                        registers = bank ? pcjr->jx_array : pcjr->array;
                    if (*index != 5 || !bank) {
                        if (*index == 4 && (val & 1) && !(registers[4] & 1)) {
                            memset(pcjr->shared_ram, 0, pcjr->shared_size);
                            memset(pcjr->dedicated_vram, 0, 0x8000);
                        }
                        registers[*index] = val;
                    }
                }
                *phase = !*phase;
            }
            update_cga16_color(pcjr->array[0], pcjr->array[2] & 15);
            pcjr_recalc_timings(pcjr);
            pcjr->fullchange = changeframecount;
            return;
        default:
            vid_out(port, val, priv);
            return;
    }
}

static void
vid_render_blank(pcjr_t *pcjr, int line, int ho_s)
{
    if (pcjr->jx_profile || (pcjr->array[3] & 4)) {
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
        if (!pcjr->jx_profile && pcjr->vc == pcjr->crtc[7] && !pcjr->scanline) {
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
                if (pcjr->jx_profile)
                    picintc(1 << 5);
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
                if (pcjr->jx_profile) {
                    pcjr->vsynctime = (pcjr->crtc[3] >> 4) ? (pcjr->crtc[3] >> 4) : 16;
                    pcjr->status |= 8;
                }
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
        if (pcjr->jx_profile)
            pcjr->status = (pcjr->status & ~1) | (pcjr->dispon ? 1 : 0);
    }
}


static void
vid_init_common(pcjr_t *pcjr)
{
    int     display_type;

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_dram);

    display_type    = device_get_config_int("display_type");
    pcjr->composite = (display_type == PCJR_COMPOSITE);
    pcjr->apply_hd  = device_get_config_int("apply_hd");
    overscan_x = 256;
    overscan_y = 32;

    timer_add(&pcjr->timer, vid_poll, pcjr, 1);

    if (&(cga_palette) != NULL) {
        if (pcjr->composite)
            cga_palette = 0;
        else
            cga_palette = (display_type << 1);
    }
    cgapal_rebuild();

    pcjr->double_type = device_get_config_int("double_type");
    cga_interpolate_init();

    monitors[monitor_index_global].mon_composite = !!pcjr->composite;
}

void
pcjr_vid_init(pcjr_t *pcjr)
{
    pcjr->memctrl = -1;
    if (mem_size < 128)
        pcjr->memctrl &= ~0x24;
    mem_mapping_add(&pcjr->mapping, 0xb8000, 0x08000,
                    vid_read, NULL, NULL, vid_write, NULL, NULL, NULL, 0, pcjr);
    io_sethandler(0x03d0, 16, vid_in, NULL, NULL, vid_out, NULL, NULL, pcjr);
    vid_init_common(pcjr);
}

void
pcjx_vid_init(pcjr_t *pcjr, uint8_t *shared_ram, uint32_t shared_size,
              uint8_t *dedicated_vram, const uint8_t *cg1)
{
    pcjr->jx_profile = 1;
    pcjr->shared_ram = shared_ram;
    pcjr->shared_size = shared_size > 0x20000 ? 0x20000 : shared_size;
    pcjr->dedicated_vram = dedicated_vram;
    pcjr->jx_array[3] = 0x10; /* Assumed cold-reset English memory ordering. */
    pcjr->status = 4;
    pcjr->firstline = 1000;
    if (!cg1) {
        video_load_font(FONT_IBM_MDA_437_PATH, FONT_FORMAT_MDA, LOAD_FONT_NO_OFFSET);
        cg1 = &fontdat[0][0];
    }
    /* No authentic JX CG1 dump is available: keep a private immutable copy
       of the same hardware-font asset used by the PCjr, never CPU ROM data. */
    memcpy(pcjr->cg1, cg1, sizeof(pcjr->cg1));
    recalc_address(pcjr);
    device_context(&pcjx_video_device);
    vid_init_common(pcjr);
    device_context_restore();
    pcjr_recalc_timings(pcjr);
}

static void
pcjx_vid_speed_changed(void *priv)
{
    pcjr_recalc_timings((pcjr_t *) priv);
}

static const device_config_t pcjx_video_config[] = {
    {
        .name = "display_type", .description = "Display type",
        .type = CONFIG_SELECTION, .default_int = PCJR_RGB,
        .selection = {
            { .description = "RGB", .value = PCJR_RGB },
            { .description = "Composite", .value = PCJR_COMPOSITE },
            { .description = "RGB (no brown)", .value = PCJR_RGB_NO_BROWN },
            { .description = "RGB (IBM 5153)", .value = PCJR_RGB_IBM_5153 },
            { .description = "" }
        }
    },
    {
        .name = "double_type", .description = "Line doubling type",
        .type = CONFIG_SELECTION, .default_int = DOUBLE_NONE,
        .selection = {
            { .description = "None", .value = DOUBLE_NONE },
            { .description = "Simple doubling", .value = DOUBLE_SIMPLE },
            { .description = "sRGB interpolation", .value = DOUBLE_INTERPOLATE_SRGB },
            { .description = "Linear interpolation", .value = DOUBLE_INTERPOLATE_LINEAR },
            { .description = "" }
        }
    },
    {
        .name = "apply_hd", .description = "Apply overscan deltas",
        .type = CONFIG_BINARY, .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t pcjx_video_device = {
    .name = "IBM PC JX (Video)",
    .internal_name = "pcjx_video",
    .speed_changed = pcjx_vid_speed_changed,
    .config = pcjx_video_config
};
