/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM PC JX video subsystem emulation, derived from the PCjr renderer
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
#include <stdint.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/vid_cga_comp.h>
#include "cpu.h"

#include <86box/vid_pcjx.h>

static video_timings_t timing_dram = { VIDEO_BUS, 0, 0, 0, 0, 0, 0 }; /*No additional waitstates*/

static uint8_t crtcmask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Values are retained for existing machine profiles. */
enum {
    PCJX_RGB = 0,
    PCJX_COMPOSITE = 1,
    PCJX_RGB_NO_BROWN = 4,
    PCJX_RGB_IBM_5153 = 5
};

enum {
    PCJX_DOUBLE_NONE = 0,
    PCJX_DOUBLE_SIMPLE = 1,
    PCJX_DOUBLE_INTERPOLATE_SRGB = 2,
    PCJX_DOUBLE_INTERPOLATE_LINEAR = 3
};

static void
recalc_address(pcjx_video_t *video)
{
    uint32_t display_page = (video->memctrl & 7) << 14;

    if ((video->memctrl & 0xc0) == 0xc0)
        display_page &= ~0x4000;
    video->vram = display_page < video->shared_size ? video->shared_ram + display_page : NULL;
}

/* Both onboard processors use the common 14 MHz raster. Selecting a register
   bank for CPU I/O does not select (or disable) a display source. */
static uint8_t
pcjx_raster_mode(const pcjx_video_t *video)
{
    if (video->cg2 && (video->jx_array[0] & 8) && (video->jx_array[1] & 0x20))
        return video->jx_array[0];
    return video->array[0];
}

uint8_t
pcjx_vid_font_read(const pcjx_video_t *video, uint32_t offset)
{
    offset &= 0x3ffff;
    if (!video->cg2)
        return 0xff;
    /* This asset is a CPU-aperture capture, not four physical CG2 chips.
       The 2 KiB SRAM is mirrored throughout 88000h-8FFFFh. */
    if ((offset & 0x38000) == 0x8000)
        return video->gaiji ? video->gaiji[offset & (PCJX_GAIJI_SIZE - 1)] : 0xff;
    return offset < PCJX_CG2_IMAGE_SIZE ? video->cg2[offset] : 0xff;
}

void
pcjx_vid_font_write(pcjx_video_t *video, uint32_t offset, uint8_t value)
{
    offset &= 0x3ffff;
    if (video->cg2 && video->gaiji && (offset & 0x38000) == 0x8000)
        video->gaiji[offset & (PCJX_GAIJI_SIZE - 1)] = value;
}

static void
pcjx_recalc_timings(pcjx_video_t *video)
{
    double _dispontime;
    double _dispofftime;
    double disptime;

    if (pcjx_raster_mode(video) & 1) {
        disptime    = video->crtc[0] + 1;
        _dispontime = video->crtc[1];
    } else {
        disptime    = (video->crtc[0] + 1) << 1;
        _dispontime = video->crtc[1] << 1;
    }

    _dispofftime = disptime - _dispontime;
    /* Unprogrammed or software-created invalid CRTC widths must still let
       emulated time advance, rather than spinning a zero-period timer. */
    if (_dispontime < 1)
        _dispontime = 1;
    if (_dispofftime < 1)
        _dispofftime = 1;
    _dispontime *= CGACONST;
    _dispofftime *= CGACONST;
    video->dispontime  = (uint64_t) (int64_t) (_dispontime);
    video->dispofftime = (uint64_t) (int64_t) (_dispofftime);
}

static int
vid_get_h_overscan_size(pcjx_video_t *video)
{
    int ret;

    if (pcjx_raster_mode(video) & 1)
        ret = 128;
    else
        ret = 256;

    return ret;
}

void
pcjx_vid_waitstates(void)
{
    static const uint8_t ws_array[16] = { 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5 };

    cycles -= ws_array[cycles & 0xf];
}

static int
vid_get_h_overscan_delta(pcjx_video_t *video)
{
    int coef;
    int ret;

    switch ((pcjx_raster_mode(video) & 0x13) |
            (((video->cg2 && (video->jx_array[0] & 8) && (video->jx_array[1] & 0x20)
               ? video->jx_array[3] : video->array[3]) & 0x08) << 5)) {
        case 0x13: /* 320x200x16 */
        case 0x03: /* 640x200x4 */
        case 0x01: /* 80-column text */
            coef = 8;
            break;
        default:
            coef = 16;
            break;
    }

    /* JX uses sync end, with four low-bandwidth or ten high-bandwidth
     * character clocks between sync end and the next display line. */
    const int reference_back_porch = (pcjx_raster_mode(video) & 1) ? 10 : 4;

    ret = video_6845_get_hsync_delay(video->crtc, video->crtc[3] & 0x0f) -
          reference_back_porch;

    if (ret < -8)
        ret = -8;

    if (ret > 8)
        ret = 8;

    return ret * coef;
}

static void
vid_blit_v_overscan(pcjx_video_t *video)
{
    int      cols = (video->array[2] & 0xf) + 16;
    int      y0   = video->firstline;
    int      y    = video->lastline + 8;
    int      h    = 8;
    int      ho_s = vid_get_h_overscan_size(video);
    int      i;
    int      x;

    if (video->double_type > PCJX_DOUBLE_NONE) {
        y0 <<= 1;
        y <<= 1;

        h <<= 1;
    }

    if (pcjx_raster_mode(video) & 1)
        x = (video->crtc[1] << 3) + ho_s;
    else
        x = (video->crtc[1] << 4) + ho_s;

    for (i = 0; i < h; i++) {
        hline(buffer32, 0, y0 + i, x, cols);
        hline(buffer32, 0, y + i, x, cols);

        if (video->composite) {
            Composite_Process(pcjx_raster_mode(video), 0, x >> 2, buffer32->line[y0 + i]);
            Composite_Process(pcjx_raster_mode(video), 0, x >> 2, buffer32->line[y + i]);
        } else {
            video_process_8(x, y0 + i);
            video_process_8(x, y + i);
        }
    }
}

/* Decode one CRTC character clock to output color codes. The scanout and JX
   diagnostic feedback share this pixel path, including the two-plane mode A. */
static void
vid_english_cell(pcjx_video_t *video, uint16_t memaddr, int scanline, uint8_t pixels[16])
{
    uint16_t offset = 0;
    uint16_t mask = 0x1fff;
    uint8_t mode = video->array[0];
    uint16_t dat;
    uint8_t lo, hi;
    int width = (mode & 1) ? 8 : 16;
    int graphics;

    mode = (mode & ~2) | ((video->pb & 4) ? 0 : 2);
    graphics = mode & 2;

    if (!(video->array[0] & 8) ||
        (video->array[4] & 3) || !(video->jx_array[1] & 0x10) || !video->vram) {
        memset(pixels, video->array[2] & 15, width);
        return;
    }

    switch (video->addr_mode) {
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
    lo = video->vram[offset];
    hi = video->vram[offset + 1];
    dat = (lo << 8) | hi;

    if (!graphics) {
        uint16_t cursoraddr = (video->crtc[15] | (video->crtc[14] << 8)) & 0x3fff;
        int cursor = memaddr == cursoraddr && video->cursorvisible && video->cursoron;
        uint8_t fg = hi & 15;
        uint8_t bg = hi >> 4;
        uint8_t glyph = video->cg1[lo * 8 + (scanline & 7)];
        if (video->array[3] & 2) {
            bg &= 7;
            if ((video->blink & 16) && (hi & 0x80) && !cursor)
                fg = bg;
        }
        fg = video->array[16 + (fg & video->array[1] & 15)] & 15;
        bg = video->array[16 + (bg & video->array[1] & 15)] & 15;
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
            switch ((mode & 0x13) | ((video->array[3] & 8) << 5)) {
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
            index &= video->array[1] & 15;
            if (video->array[3] & 2)
                index = (index & 7) | ((video->blink & 16) >> 1);
            pixels[c] = video->array[16 + index] & 15;
        }
    }
}

/* Native processors supply palette indices to a common mixer, not separately
   palette-mapped RGBI colors. Graphics row banking is independent of the CRTC
   character height, allowing graphics behind 18-line Japanese text boxes. */
static int
vid_native_cell(pcjx_video_t *video, int bank, uint16_t memaddr, int scanline,
                int width, uint8_t pixels[16])
{
    const uint8_t *registers = bank ? video->jx_array : video->array;
    uint8_t mode = registers[0];
    uint8_t *ram = bank ? video->dedicated_vram : video->shared_ram;
    uint32_t size = bank ? 0x8000 : video->shared_size;
    uint32_t page = bank ? (video->pg2 & 3) << 14 : (video->memctrl & 7) << 14;
    uint32_t offset;
    int graphics = mode & 2;
    int banks = (mode & 1) ? 4 : 2;

    if (!(mode & 8) || !(video->jx_array[1] & (0x10 << bank)) || !ram)
        return 0;
    if (graphics) {
        uint16_t start = (video->crtc[13] | (video->crtc[12] << 8)) & 0x3fff;
        unsigned column = (memaddr - start - video->vc * video->crtc[1]) & 0x3fff;
        unsigned y = video->vc * (video->crtc[9] + 1) + scanline;
        unsigned address = start + (y / banks) * video->crtc[1] + column;
        page &= ~((banks == 4) ? 0x7fff : 0x3fff);
        offset = (y % banks) * 0x2000 + ((address << 1) & 0x1fff);
    } else
        offset = (memaddr << 1) & 0x3fff;
    if (page + offset + 1 >= size)
        return 0;

    uint8_t lo = ram[page + offset];
    uint8_t hi = ram[page + offset + 1];
    if (!graphics) {
        uint16_t cursoraddr = (video->crtc[15] | (video->crtc[14] << 8)) & 0x3fff;
        int cursor = (memaddr & 0x3fff) == cursoraddr && video->cursorvisible && video->cursoron;
        uint8_t fg = hi & 15;
        uint8_t bg = hi >> 4;
        uint8_t glyph = 0;
        if (bank && (mode & 0x40)) {
            uint32_t font_offset = (uint32_t) lo << 5;
            int right = 0;
            fg &= 7;
            bg &= 7;
            if (hi & 0x80) {
                right = !!(hi & 8);
                uint32_t adjacent = (offset + (right ? -2 : 2)) & 0x3fff;
                uint16_t code = right ? (ram[page + adjacent] << 8) | lo
                                      : (lo << 8) | ram[page + adjacent];
                /* Stored codes are already BIOS-translated internal codes:
                   e.g. external gaiji F041h is stored as 8441h. */
                font_offset = (code << 5) & 0x3ffe0;
            }
            if (scanline < 16) {
                glyph = pcjx_vid_font_read(video, font_offset + scanline * 2 + right);
                if ((hi & 0x80) && !right)
                    glyph &= 0x7f; /* CG2's left bit 7 is a control bit. */
            }
        } else {
            if (scanline < 8)
                glyph = bank ? pcjx_vid_font_read(video, (lo << 5) + scanline * 2 + 1)
                             : video->cg1[lo * 8 + scanline];
            if (registers[3] & 2) {
                bg &= 7;
                if ((video->blink & 16) && (hi & 0x80) && !cursor)
                    fg = bg;
            }
        }
        if (cursor)
            glyph = 0xff;
        for (int c = 0; c < width; c++) {
            uint8_t index = (glyph & (0x80 >> (c * 8 / width))) ? fg : bg;
            pixels[c] = index & registers[1] & 15;
        }
    } else {
        uint16_t dat = (lo << 8) | hi;
        for (int c = 0; c < width; c++) {
            uint8_t index;
            if (mode & 0x10)
                index = (dat >> (12 - (c * 4 / width) * 4)) & 15;
            else if (registers[3] & 8)
                index = (dat >> (15 - c * 16 / width)) & 1;
            else if (mode & 1) {
                int bit = 7 - c * 8 / width;
                index = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            } else
                index = (dat >> (14 - (c * 8 / width) * 2)) & 3;
            index &= registers[1] & 15;
            if (registers[3] & 2)
                index = (index & 7) | ((video->blink & 16) >> 1);
            pixels[c] = index;
        }
    }
    return 1;
}

static void
vid_cell(pcjx_video_t *video, uint16_t memaddr, int scanline, uint8_t pixels[16])
{
    if (!video->cg2) {
        vid_english_cell(video, memaddr, scanline, pixels);
        return;
    }
    int width = (pcjx_raster_mode(video) & 1) ? 8 : 16;
    if (video->array[4] & 3) {
        memset(pixels, video->array[2] & 15, width);
        return;
    }
    uint8_t vp1[16], vp2[16];
    int enabled1 = vid_native_cell(video, 0, memaddr, scanline, width, vp1);
    int enabled2 = vid_native_cell(video, 1, memaddr, scanline, width, vp2);
    unsigned mixer = video->array[6] & 15;
    if ((!enabled1 && !enabled2) || (mixer == 0 && !enabled1) || (mixer == 1 && !enabled2)) {
        memset(pixels, video->array[2] & 15, width);
        return;
    }
    if (!enabled1)
        memset(vp1, 0, width);
    if (!enabled2)
        memset(vp2, 0, width);
    for (int c = 0; c < width; c++) {
        uint8_t index;
        if (video->jx_array[0] & 0x80)
            index = ((vp1[c] & 3) | ((vp2[c] & 3) << 2)) ^ 0x0a;
        else {
            switch (mixer) {
                case 0:
                    index = vp1[c];
                    break;
                case 1:
                    index = vp2[c];
                    break;
                case 2:
                    index = vp1[c] == (video->array[5] & 15) ? vp2[c] : vp1[c];
                    break;
                case 3:
                    /* VP1 alone has a transparent-index comparator. Reversed
                       priority preserves that background in the foreground. */
                    index = vp1[c] == (video->array[5] & 15) ? vp1[c] : vp2[c];
                    break;
                default:
                    switch (video->array[6] & 12) {
                        case 4:
                            index = vp1[c] ^ vp2[c];
                            break;
                        case 8:
                            index = vp1[c] & vp2[c];
                            break;
                        default:
                            index = vp1[c] | vp2[c];
                            break;
                    }
                    break;
            }
        }
        pixels[c] = video->array[16 + index] & 15;
    }
}

static void
vid_render(pcjx_video_t *video, int line, int ho_s, int ho_d)
{
    uint8_t pixels[16];
    int width = (pcjx_raster_mode(video) & 1) ? 8 : 16;

    hline(buffer32, 0, line, video->crtc[1] * width + ho_s, (video->array[2] & 15) + 16);
    for (int x = 0; x < video->crtc[1]; x++) {
        vid_cell(video, video->memaddr++, video->scanline, pixels);
        for (int c = 0; c < width; c++)
            buffer32->line[line][x * width + ho_d + c] = pixels[c] + 16;
    }
}

/* The active half-line ends at timer.ts. Reads sample that clock; they never
   move the beam. CRTC addresses remain character addresses for light pen. */
static unsigned
pcjx_raster_dot(pcjx_video_t *video)
{
    unsigned width = video->crtc[1] * ((pcjx_raster_mode(video) & 1) ? 8 : 16);
    uint128_t remaining = timer_get_remaining_u64(&video->timer);
    if (!width || !video->dispontime || remaining >= video->dispontime)
        return 0;
    unsigned dot = ((uint128_t) (video->dispontime - remaining) * width) / video->dispontime;
    return dot < width ? dot : width - 1;
}

uint8_t
pcjx_vid_in(uint16_t port, uint8_t vp_mask, void *priv)
{
    pcjx_video_t *video = (pcjx_video_t *) priv;
    if (port == 0x3da) {
        uint8_t ret = (video->status & 0x0b) | 4; /* Disconnected light-pen switch. */
        uint8_t color = 0; /* Blanking drives all four diagnostic outputs low. */
        if (!vp_mask)
            return 0xff;
        if (video->status & 1) {
            uint8_t pixels[16];
            unsigned width = (pcjx_raster_mode(video) & 1) ? 8 : 16;
            unsigned dot = pcjx_raster_dot(video);
            int scanline = ((video->crtc[8] & 3) == 3)
                               ? (video->scanline << 1) & (video->cg2 ? 31 : 7) : video->scanline;
            vid_cell(video, video->memaddr + dot / width, scanline, pixels);
            color = pixels[dot % width];
        }
        uint8_t feedback = 0x10;
        for (int bank = 0; bank < 2; bank++) {
            if (!(vp_mask & (1 << bank)))
                continue;
            if (bank)
                video->jx_array_ff = 0;
            else
                video->array_ff = 0;
            feedback &= ((color >> video->dot_component[bank]) & 1) << 4;
        }
        return ret | feedback;
    }
    switch (port) {
        case 0x3d0:
        case 0x3d2:
        case 0x3d4:
        case 0x3d6:
            return video->crtcreg;
        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
            return video->crtc[video->crtcreg];
        default:
            return 0xff;
    }
}

void
pcjx_vid_out(uint16_t port, uint8_t val, uint8_t vp_mask, void *priv)
{
    pcjx_video_t *video = (pcjx_video_t *) priv;
    uint8_t old;

    switch (port) {
        case 0x3d0:
        case 0x3d2:
        case 0x3d4:
        case 0x3d6:
            video->crtcreg = val & 0x1f;
            return;

        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
            if (video->crtcreg >= 0x10)
                return; /* Light-pen address registers are read-only. */
            old = video->crtc[video->crtcreg];
            video->crtc[video->crtcreg] = val & crtcmask[video->crtcreg];
            if (video->crtcreg == 2)
                overscan_x = vid_get_h_overscan_size(video);
            if (old != val) {
                if (video->crtcreg < 0xe || video->crtcreg > 0x10) {
                    pcjx_recalc_timings(video);
                }
            }
            return;

        case 0x3d9:
            video->pg2 = val;
            return;
        case 0x3df:
            video->memctrl = val;
            video->addr_mode = val >> 6;
            recalc_address(video);
            return;
        case 0x3db:
            video->status &= ~2;
            return;
        case 0x3dc:
            if (!(video->status & 2)) {
                unsigned width = (pcjx_raster_mode(video) & 1) ? 8 : 16;
                uint16_t address = video->memaddr;
                if (video->status & 1)
                    address += pcjx_raster_dot(video) / width;
                address &= 0x3fff;
                video->crtc[0x10] = address >> 8;
                video->crtc[0x11] = address;
                video->status |= 2;
            }
            return;
        case 0x3da:
            for (int bank = 0; bank < 2; bank++) {
                int *phase = bank ? &video->jx_array_ff : &video->array_ff;
                int *index = bank ? &video->jx_array_index : &video->array_index;
                if (!(vp_mask & (1 << bank)))
                    continue;
                if (!*phase) {
                    *index = val & 31;
                    if (val < 4)
                        video->dot_component[bank] = val;
                } else {
                    uint8_t *registers = video->array;
                    if (*index == 0 || *index == 1 || *index == 3)
                        registers = bank ? video->jx_array : video->array;
                    /* Register 05 exists only in VP1; undefined registers are
                       retained bank-locally without assigning them devices. */
                    else if (*index != 2 && *index != 4 && *index != 6 && *index < 16)
                        registers = bank ? video->jx_array : video->array;
                    if (*index != 5 || !bank) {
                        if (*index == 4 && (val & 1) && !(registers[4] & 1)) {
                            memset(video->shared_ram, 0, video->shared_size);
                            memset(video->dedicated_vram, 0, 0x8000);
                        }
                        registers[*index] = val;
                    }
                }
                *phase = !*phase;
            }
            update_cga16_color(pcjx_raster_mode(video), video->array[2] & 15);
            pcjx_recalc_timings(video);
            return;
        default:
            return;
    }
}

static void
vid_render_blank(pcjx_video_t *video, int line, int ho_s)
{
    if (pcjx_raster_mode(video) & 1)
        hline(buffer32, 0, line, (video->crtc[1] << 3) + ho_s, (video->array[2] & 0xf) + 16);
    else
        hline(buffer32, 0, line, (video->crtc[1] << 4) + ho_s, (video->array[2] & 0xf) + 16);
}

static void
vid_render_process(pcjx_video_t *video, int line, int ho_s)
{
    int x;

    if (pcjx_raster_mode(video) & 1)
        x = (video->crtc[1] << 3) + ho_s;
    else
        x = (video->crtc[1] << 4) + ho_s;

    if (video->composite)
        Composite_Process(pcjx_raster_mode(video), 0, x >> 2, buffer32->line[line]);
    else
        video_process_8(x, line);
}

static void
vid_poll(void *priv)
{
    pcjx_video_t  *video = (pcjx_video_t *) priv;
    int      x;
    int      xs_temp;
    int      ys_temp;
    int      oldvc;
    int      scanline_old;
    int      l = video->displine + 8;
    int      ho_s = vid_get_h_overscan_size(video);
    int      ho_d = vid_get_h_overscan_delta(video) + (ho_s / 2);
    int      old_ma;

    if (!video->linepos) {
        timer_advance_u64(&video->timer, video->dispofftime);
        video->status &= ~1;
        video->linepos = 1;
        scanline_old         = video->scanline;
        if ((video->crtc[8] & 3) == 3)
            video->scanline = (video->scanline << 1) & (video->cg2 ? 31 : 7);
        if (video->dispon) {
            if (video->displine < video->firstline) {
                video->firstline = video->displine;
                video_wait_for_buffer();
            }
            video->lastline = video->displine;
            switch (video->double_type) {
                default:
                    vid_render(video, l << 1, ho_s, ho_d);
                    vid_render_blank(video, (l << 1) + 1, ho_s);
                    break;
                case PCJX_DOUBLE_NONE:
                    vid_render(video, l, ho_s, ho_d);
                    break;
                case PCJX_DOUBLE_SIMPLE:
                    old_ma = video->memaddr;
                    vid_render(video, l << 1, ho_s, ho_d);
                    video->memaddr = old_ma;
                    vid_render(video, (l << 1) + 1, ho_s, ho_d);
                    break;
            }
        } else  switch (video->double_type) {
            default:
                vid_render_blank(video, l << 1, ho_s);
                break;
            case PCJX_DOUBLE_NONE:
                vid_render_blank(video, l, ho_s);
                break;
            case PCJX_DOUBLE_SIMPLE:
                vid_render_blank(video, l << 1, ho_s);
                vid_render_blank(video, (l << 1) + 1, ho_s);
                break;
        }

        switch (video->double_type) {
            default:
                vid_render_process(video, l << 1, ho_s);
                vid_render_process(video, (l << 1) + 1, ho_s);
                break;
            case PCJX_DOUBLE_NONE:
                vid_render_process(video, l, ho_s);
                break;
        }

        video->scanline = scanline_old;
        video->displine++;
        if (video->displine >= 360)
            video->displine = 0;
    } else {
        timer_advance_u64(&video->timer, video->dispontime);
        if (video->dispon)
            video->status |= 1;
        video->linepos = 0;
        if (video->vsynctime) {
            video->vsynctime--;
            if (!video->vsynctime) {
                video->status &= ~8;
                picintc(1 << 5);
            }
        }
        if (video->scanline == (video->crtc[11] & 31) || ((video->crtc[8] & 3) == 3 && video->scanline == ((video->crtc[11] & 31) >> 1))) {
            video->cursorvisible  = 0;
        }
        if (video->vadj) {
            video->scanline++;
            video->scanline &= 31;
            video->memaddr = video->memaddr_backup;
            video->vadj--;
            if (!video->vadj) {
                video->dispon = 1;
                video->memaddr = video->memaddr_backup = (video->crtc[13] | (video->crtc[12] << 8)) & 0x3fff;
                video->scanline                = 0;
            }
        } else if (video->scanline == video->crtc[9] || ((video->crtc[8] & 3) == 3 && video->scanline == (video->crtc[9] >> 1))) {
            video->memaddr_backup = video->memaddr;
            video->scanline     = 0;
            oldvc        = video->vc;
            video->vc++;
            video->vc &= 127;
            if (video->vc == video->crtc[6])
                video->dispon = 0;
            if (oldvc == video->crtc[4]) {
                video->vc   = 0;
                video->vadj = video->crtc[5];
                if (!video->vadj)
                    video->dispon = 1;
                if (!video->vadj)
                    video->memaddr = video->memaddr_backup = (video->crtc[13] | (video->crtc[12] << 8)) & 0x3fff;
                if ((video->crtc[10] & 0x60) == 0x20)
                    video->cursoron = 0;
                else if (video->cg2 && !(video->crtc[10] & 0x60))
                    video->cursoron = 1;
                else
                    video->cursoron = video->blink & ((video->cg2 && (video->crtc[10] & 0x60) == 0x60) ? 32 : 16);
            }
            if (video->vc == video->crtc[7]) {
                video->dispon    = 0;
                video->displine  = 0;
                video->vsynctime = (video->crtc[3] >> 4) ? (video->crtc[3] >> 4) : 16;
                video->status |= 8;
                picint(1 << 5);
                if (video->crtc[7]) {
                    if (pcjx_raster_mode(video) & 1)
                        x = (video->crtc[1] << 3) + ho_s;
                    else
                        x = (video->crtc[1] << 4) + ho_s;
                    video->lastline++;

                    xs_temp = x;
                    ys_temp = (video->lastline - video->firstline) << 1;

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

                        vid_blit_v_overscan(video);

                        if (video->double_type > PCJX_DOUBLE_NONE) {
                            if (enable_overscan) {
                                cga_blit_memtoscreen(0, video->firstline << 1,
                                                     xsize, actual_ys + 32,
                                                     video->double_type);
                            } else if (video->apply_hd) {
                                cga_blit_memtoscreen(ho_s / 2, (video->firstline << 1) + 16,
                                                     xsize, actual_ys,
                                                     video->double_type);
                            } else {
                                cga_blit_memtoscreen(ho_d, (video->firstline << 1) + 16,
                                                     xsize, actual_ys,
                                                     video->double_type);
                            }
                        } else {
                            if (enable_overscan) {
                                video_blit_memtoscreen(0, video->firstline,
                                                       xsize, (actual_ys >> 1) + 16);
                            } else if (video->apply_hd) {
                                video_blit_memtoscreen(ho_s / 2, video->firstline + 8,
                                                       xsize, actual_ys >> 1);
                            } else {
                                video_blit_memtoscreen(ho_d, video->firstline + 8,
                                                       xsize, actual_ys >> 1);
                            }
                        }
                    }

                    frames++;
                    video_res_x = xsize;
                    video_res_y = ysize;
                }
                video->firstline = 1000;
                video->lastline  = 0;
                video->blink++;
            }
        } else {
            video->scanline++;
            video->scanline &= 31;
            video->memaddr = video->memaddr_backup;
        }
        if (video->scanline == (video->crtc[10] & 31) || ((video->crtc[8] & 3) == 3 && video->scanline == ((video->crtc[10] & 31) >> 1)))
            video->cursorvisible = 1;
        video->status = (video->status & ~1) | (video->dispon ? 1 : 0);
    }
}


static void
vid_init(pcjx_video_t *video)
{
    int     display_type;

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_dram);

    display_type    = device_get_config_int("display_type");
    video->composite = (display_type == PCJX_COMPOSITE);
    video->apply_hd  = device_get_config_int("apply_hd");
    overscan_x = 256;
    overscan_y = 32;

    timer_add(&video->timer, vid_poll, video, 1);

    if (video->composite)
        cga_palette = 0;
    else
        cga_palette = (display_type << 1);
    cgapal_rebuild();

    video->double_type = device_get_config_int("double_type");
    cga_interpolate_init();

    monitors[monitor_index_global].mon_composite = !!video->composite;
}

void
pcjx_vid_init(pcjx_video_t *video, uint8_t *shared_ram, uint32_t shared_size,
              uint8_t *dedicated_vram, const uint8_t *cg1,
              const uint8_t *cg2, uint8_t *gaiji)
{
    video->shared_ram = shared_ram;
    video->shared_size = shared_size > 0x20000 ? 0x20000 : shared_size;
    video->dedicated_vram = dedicated_vram;
    video->cg2 = cg2;
    video->gaiji = gaiji;
    video->jx_array[3] = 0x10; /* Assumed cold-reset English memory ordering. */
    video->status = 4;
    video->firstline = 1000;
    if (!cg1) {
        video_load_font(FONT_IBM_MDA_437_PATH, FONT_FORMAT_MDA, LOAD_FONT_NO_OFFSET);
        cg1 = &fontdat[0][0];
    }
    /* No authentic JX CG1 dump is available: keep a private immutable copy
       of the same hardware-font asset used by the PCjr, never CPU ROM data. */
    memcpy(video->cg1, cg1, sizeof(video->cg1));
    recalc_address(video);
    device_context(&pcjx_video_device);
    vid_init(video);
    device_context_restore();
    pcjx_recalc_timings(video);
}

static void
pcjx_vid_speed_changed(void *priv)
{
    pcjx_recalc_timings((pcjx_video_t *) priv);
}

static const device_config_t pcjx_video_config[] = {
    {
        .name = "display_type", .description = "Display type",
        .type = CONFIG_SELECTION, .default_int = PCJX_RGB,
        .selection = {
            { .description = "RGB", .value = PCJX_RGB },
            { .description = "Composite", .value = PCJX_COMPOSITE },
            { .description = "RGB (no brown)", .value = PCJX_RGB_NO_BROWN },
            { .description = "RGB (IBM 5153)", .value = PCJX_RGB_IBM_5153 },
            { .description = "" }
        }
    },
    {
        .name = "double_type", .description = "Line doubling type",
        .type = CONFIG_SELECTION, .default_int = PCJX_DOUBLE_NONE,
        .selection = {
            { .description = "None", .value = PCJX_DOUBLE_NONE },
            { .description = "Simple doubling", .value = PCJX_DOUBLE_SIMPLE },
            { .description = "sRGB interpolation", .value = PCJX_DOUBLE_INTERPOLATE_SRGB },
            { .description = "Linear interpolation", .value = PCJX_DOUBLE_INTERPOLATE_LINEAR },
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
