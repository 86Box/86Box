/*
 * 86Box: WD90C33 drawing engine, command buffer and hardware cursor.
 *
 * Register definitions: Western Digital WD90C33 data sheet (Advanced
 * Information, 5/27/93), sections 11-13. Video-to-video operations and lines
 * complete synchronously; host transfers keep the engine busy until the last
 * pixel has been supplied, and buffered register writes made meanwhile wait
 * in the command buffer.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include "vid_wd90c33.h"

/* Register block 1. */
enum {
    DE_CONTROL1,
    DE_CONTROL2,
    DE_SRC_X,
    DE_SRC_Y,
    DE_DST_X,
    DE_DST_Y,
    DE_DIM_X,
    DE_DIM_Y,
    DE_ROP,
    DE_CLIP_LEFT,
    DE_CLIP_RIGHT,
    DE_CLIP_TOP,
    DE_CLIP_BOTTOM
};

/* Register block 3. */
enum {
    DE_MAP_BASE,
    DE_ROW_PITCH,
    DE_FG0,
    DE_FG1,
    DE_BG0,
    DE_BG1,
    DE_TRANS_COLOR0,
    DE_TRANS_COLOR1,
    DE_TRANS_MASK0,
    DE_TRANS_MASK1,
    DE_MASK0,
    DE_MASK1
};

enum {
    MODE_NOP,
    MODE_BITBLT,
    MODE_LINE_STRIP,
    MODE_TRAPEZOID,
    MODE_BRESENHAM
};

enum {
    FORMAT_COLOR,
    FORMAT_COMPARE,
    FORMAT_FIXED,
    FORMAT_HOST_MONO
};

#define CMD_BUFFER_SIZE 8

/* Command buffer and interrupt control register, port 23CEh. */
#define CMD_IRQ_STATUS 0x200
#define CMD_IRQ_ARM    0x100
#define CMD_DE_BUSY    0x080
#define CMD_OVERFLOW   0x040
#define CMD_ENABLE     0x020

typedef struct wd90c33_cmd_t {
    uint8_t  block; /* Register block, or 0 for the Bresenham ports. */
    uint8_t  index;
    uint16_t value;
} wd90c33_cmd_t;

struct wd90c33_t {
    svga_t       *svga;
    uint16_t      index;
    uint16_t      de[16], de2[16], cursor[16];
    uint16_t      active[16], active2[16];
    uint16_t      bresenham[3];
    uint16_t      cmd_control;
    wd90c33_cmd_t cmd[CMD_BUFFER_SIZE];
    int           cmd_count;
    uint16_t      read_latch[8];
    uint8_t       write_latch[8];

    /* Host transfer in progress. */
    int      busy;
    int      width, height;
    int      x, y;
    int      xdir, ydir;
    int      depth;
    uint32_t source;
    uint8_t  host_latch[4];
    uint32_t host_read_data;

    uint16_t cursor_origin;
    uint32_t cursor_address;
};

static const uint16_t de_masks[16] = {
    0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0x0fff,
    0x0f00, 0x0fff, 0x0fff, 0x0fff, 0x0fff, 0, 0, 0
};
static const uint16_t de2_masks[16] = {
    0x01ff, 0x0fff, 0x00ff, 0x00ff, 0x00ff, 0x00ff, 0x00ff, 0x00ff,
    0x00ff, 0x00ff, 0x00ff, 0x00ff, 0, 0, 0, 0
};
static const uint16_t cursor_masks[16] = {
    0x0ff0, 0x0ffc, 0x01ff, 0x00ff, 0x00ff, 0x00ff, 0x00ff, 0x00ff,
    0x00ff, 0x00ff, 0x00ff, 0x00ff, 0x0fff, 0x07ff, 0x03ff, 0
};

static void wd90c33_reg_write(wd90c33_t *wd, unsigned block, unsigned index, uint16_t value);

/* CRT and drawing engine share the byte/word/doubleword memory organization
   (tables 7-2 to 7-6). */
static uint32_t
wd90c33_vram_address(svga_t *svga, uint32_t address)
{
    uint32_t plane  = address & 3;
    uint32_t mapped = address & ~3u;

    if ((svga->crtc[0x14] & 0x40) && !svga->packed_chain4)
        mapped = ((mapped << 2) & 0x3fff0) | ((mapped >> 14) & 0x0c) | (mapped & ~0x3ffffu);
    else if (!(svga->crtc[0x14] & 0x40) && !(svga->crtc[0x17] & 0x40)) {
        unsigned shift = (svga->crtc[0x17] & 0x20) ? 15 : 13;
        mapped         = ((mapped << 1) & 0x1fff8) | ((mapped >> shift) & 4) | (mapped & ~0x1ffffu);
    }
    return (mapped | plane) & svga->vram_mask;
}

static int
wd90c33_sign14(uint16_t value)
{
    return (value & 0x2000) ? (int) value - 0x4000 : value;
}

static uint16_t
wd90c33_pixel_mask(wd90c33_t *wd)
{
    return (wd->depth == 2) ? 0xffff : (wd->depth ? 0xff : 0x0f);
}

/* Planar addresses count pixels, packed addresses count bytes (section 12.5). */
static uint32_t
wd90c33_pixel_address(wd90c33_t *wd, int x, int y)
{
    uint32_t offset = (uint32_t) (y & 0xfff) * wd->active2[DE_ROW_PITCH] + (x & 0xfff);

    if (!wd->depth)
        return offset + (wd->active2[DE_MAP_BASE] << 13);
    if (wd->depth == 2)
        offset <<= 1;
    return offset + (wd->active2[DE_MAP_BASE] << 12);
}

static uint16_t
wd90c33_pixel_read(wd90c33_t *wd, uint32_t address)
{
    svga_t  *svga  = wd->svga;
    uint16_t pixel = 0;

    if (wd->depth == 1)
        return svga->vram[wd90c33_vram_address(svga, address)];
    if (wd->depth == 2)
        return svga->vram[wd90c33_vram_address(svga, address)] | (svga->vram[wd90c33_vram_address(svga, address + 1)] << 8);

    for (unsigned plane = 0; plane < 4; plane++)
        pixel |= ((svga->vram[wd90c33_vram_address(svga, ((address >> 3) << 2) + plane)] >> (7 - (address & 7))) & 1) << plane;
    return pixel;
}

static void
wd90c33_byte_write(svga_t *svga, uint32_t address, uint8_t value, uint8_t mask)
{
    address                          = wd90c33_vram_address(svga, address);
    svga->vram[address]              = (svga->vram[address] & ~mask) | (value & mask);
    svga->changedvram[address >> 12] = svga->monitor->mon_changeframecount;
}

static void
wd90c33_pixel_write(wd90c33_t *wd, uint32_t address, uint16_t pixel)
{
    svga_t  *svga = wd->svga;
    uint16_t mask = wd->active2[DE_MASK0] | (wd->active2[DE_MASK1] << 8);

    if (wd->depth) {
        wd90c33_byte_write(svga, address, pixel, mask);
        if (wd->depth == 2)
            wd90c33_byte_write(svga, address + 1, pixel >> 8, mask >> 8);
    } else {
        uint8_t bit = 0x80 >> (address & 7);
        address     = (address >> 3) << 2;
        for (unsigned plane = 0; plane < 4; plane++) {
            if (mask & (1 << plane))
                wd90c33_byte_write(svga, address + plane, (pixel & (1 << plane)) ? bit : 0, bit);
        }
    }
    if (!(svga->gdcreg[6] & 1))
        svga->fullchange = svga->monitor->mon_changeframecount;
}

/* Truth-table bit order is 00, 01, 10, 11 from the most significant bit. */
static uint16_t
wd90c33_rop(unsigned rop, uint16_t source, uint16_t dest)
{
    return ((rop & 8) ? (~source & ~dest) : 0) | ((rop & 4) ? (~source & dest) : 0) | ((rop & 2) ? (source & ~dest) : 0) | ((rop & 1) ? (source & dest) : 0);
}

/* As on the WD90C31, set mask bits select the bits that are compared,
   although the data sheet describes them as "don't care" bits. */
static int
wd90c33_color_match(wd90c33_t *wd, uint16_t pixel)
{
    const uint16_t *r2    = wd->active2;
    uint16_t        color = r2[DE_TRANS_COLOR0] | (r2[DE_TRANS_COLOR1] << 8);
    uint16_t        mask  = r2[DE_TRANS_MASK0] | (r2[DE_TRANS_MASK1] << 8);

    return !((pixel ^ color) & mask & wd90c33_pixel_mask(wd));
}

/* Draws one pixel. mono is -1 for colour sources, otherwise the bit of a
   monochrome source that selects the foreground or background colour. */
static void
wd90c33_plot(wd90c33_t *wd, int x, int y, uint16_t source, int mono)
{
    const uint16_t *r  = wd->active;
    const uint16_t *r2 = wd->active2;
    uint32_t        address;
    uint16_t        dest;

    x &= 0xfff;
    y &= 0xfff;
    if (x < r[DE_CLIP_LEFT] || x > r[DE_CLIP_RIGHT] || y < r[DE_CLIP_TOP] || y > r[DE_CLIP_BOTTOM])
        return;
    if (mono >= 0) {
        if (!mono && (r[DE_CONTROL2] & 0x80))
            return;
        source = mono ? (r2[DE_FG0] | (r2[DE_FG1] << 8)) : (r2[DE_BG0] | (r2[DE_BG1] << 8));
    }
    address = wd90c33_pixel_address(wd, x, y);
    dest    = wd90c33_pixel_read(wd, address);
    if ((mono < 0) && (r[DE_CONTROL2] & 0x200) && (wd90c33_color_match(wd, dest) != !!(r[DE_CONTROL2] & 0x100)))
        return;
    wd90c33_pixel_write(wd, address, wd90c33_rop(r[DE_ROP] >> 8, source, dest) & wd90c33_pixel_mask(wd));
}

static void
wd90c33_update_irq(wd90c33_t *wd)
{
    if (wd->cmd_control & CMD_IRQ_STATUS)
        picint(1 << 9);
    else
        picintc(1 << 9);
}

static void
wd90c33_finish(wd90c33_t *wd)
{
    wd->busy = 0;
    if (wd->cmd_control & CMD_IRQ_ARM) {
        wd->cmd_control |= CMD_IRQ_STATUS;
        wd90c33_update_irq(wd);
    }

    /* Commands queued behind a host transfer start once it is done. */
    while (!wd->busy && wd->cmd_count) {
        wd90c33_cmd_t cmd = wd->cmd[0];
        memmove(&wd->cmd[0], &wd->cmd[1], --wd->cmd_count * sizeof(wd->cmd[0]));
        wd90c33_reg_write(wd, cmd.block, cmd.index, cmd.value);
    }
}

/* Advances to the next pixel of a host transfer; returns 0 at the end of a
   row, where the rest of the current host data is padding. */
static int
wd90c33_host_advance(wd90c33_t *wd)
{
    if (++wd->x < wd->width)
        return 1;
    wd->x = 0;
    if (++wd->y == wd->height) {
        wd->de[DE_DST_Y] = (wd->de[DE_DST_Y] + wd->ydir * wd->height) & 0xfff;
        wd90c33_finish(wd);
    }
    return 0;
}

static void
wd90c33_host_pixel(wd90c33_t *wd, uint16_t source, int mono)
{
    wd90c33_plot(wd, wd->active[DE_DST_X] + wd->xdir * wd->x, wd->active[DE_DST_Y] + wd->ydir * wd->y, source, mono);
}

static unsigned
wd90c33_pixels_per_dword(wd90c33_t *wd)
{
    return (wd->depth == 2) ? 2 : (wd->depth ? 4 : 8);
}

/* Image transfers: each doubleword holds pixels of one row only, starting at
   the pixel selected by the low bits of Source X (section 12.14.3). */
static void
wd90c33_host_image(wd90c33_t *wd)
{
    uint32_t data  = wd->host_latch[0] | (wd->host_latch[1] << 8) | (wd->host_latch[2] << 16) | ((uint32_t) wd->host_latch[3] << 24);
    int      count = wd90c33_pixels_per_dword(wd);
    int      i     = wd->x ? ((wd->xdir > 0) ? 0 : count - 1) : (wd->active[DE_SRC_X] & (count - 1));

    for (; wd->busy && (i >= 0) && (i < count); i += wd->xdir) {
        uint16_t pixel = 0;

        if (wd->depth == 2)
            pixel = data >> (i * 16);
        else if (wd->depth)
            pixel = (data >> (i * 8)) & 0xff;
        else {
            for (unsigned plane = 0; plane < 4; plane++)
                pixel |= ((data >> (plane * 8 + 7 - i)) & 1) << plane;
        }
        wd90c33_host_pixel(wd, pixel, -1);
        if (!wd90c33_host_advance(wd))
            break;
    }
}

/* Colour expansion: the most significant bit is the leftmost pixel and each
   write holds bits of one row only (section 12.14.4). */
static void
wd90c33_host_expand(wd90c33_t *wd, uint32_t bits, int count)
{
    int skip = wd->x ? 0 : (wd->active[DE_SRC_X] & (wd->depth ? (wd->depth == 2 ? 1 : 3) : 7));

    for (int i = skip; wd->busy && (i < count); i++) {
        wd90c33_host_pixel(wd, 0, (bits >> (31 - i)) & 1);
        if (!wd90c33_host_advance(wd))
            break;
    }
}

static int
wd90c33_host_source(wd90c33_t *wd)
{
    return wd->busy && (wd->active[DE_CONTROL1] & 0x20);
}

static void
wd90c33_host_write(wd90c33_t *wd, unsigned lane, uint32_t value, int size)
{
    if (!wd90c33_host_source(wd))
        return;

    /* Expanded bits per write (Control Register 2 bits 2:0): 2, 4 or 8 from
       the top of each byte, or 8/16 by write size. A 16-bit write has its
       leftmost pixel in bit 15, as WD's drivers build glyph rows as words. */
    if (((wd->active[DE_CONTROL1] >> 3) & 3) == FORMAT_HOST_MONO) {
        static const int expand_bits[8] = { 8, 8, 2, 4, 8, 8, 8, 8 };
        unsigned         select         = wd->active[DE_CONTROL2] & 7;

        if ((select == 5) && (size >= 2)) {
            for (int part = 0; part < size; part += 2)
                wd90c33_host_expand(wd, ((value >> (part * 8)) & 0xffff) << 16, 16);
        } else {
            for (int part = 0; part < size; part++)
                wd90c33_host_expand(wd, ((value >> (part * 8)) & 0xff) << 24, expand_bits[select]);
        }
        return;
    }

    int trigger = 0;
    for (int i = 0; i < size; i++) {
        unsigned l        = (lane + i) & 3;
        wd->host_latch[l] = value >> (i * 8);
        if (l == ((wd->xdir > 0) ? 3 : 0))
            trigger = 1;
    }
    if (trigger)
        wd90c33_host_image(wd);
}

/* Screen to host transfers return image data in the format of section
   12.14.3, each doubleword starting a row at its first pixel. */
static uint32_t
wd90c33_host_read(wd90c33_t *wd)
{
    uint32_t data  = 0;
    int      count = wd90c33_pixels_per_dword(wd);

    if (!wd->busy || !(wd->active[DE_CONTROL1] & 0x02))
        return 0xffffffff;
    for (int i = 0; wd->busy && (i < count); i++) {
        uint16_t pixel = wd90c33_pixel_read(wd, wd90c33_pixel_address(wd, wd->active[DE_SRC_X] + wd->xdir * wd->x,
                                                                      wd->active[DE_SRC_Y] + wd->ydir * wd->y));

        if (wd->depth == 2)
            data |= (uint32_t) pixel << (i * 16);
        else if (wd->depth)
            data |= (uint32_t) pixel << (i * 8);
        else {
            for (unsigned plane = 0; plane < 4; plane++)
                data |= (uint32_t) ((pixel >> plane) & 1) << (plane * 8 + 7 - i);
        }
        if (!wd90c33_host_advance(wd))
            break;
    }
    return data;
}

/* Source pixel for screen-sourced BITBLTs, including 8x8 patterns stored as
   64 consecutive pixels (section 12.9). */
static uint16_t
wd90c33_source_pixel(wd90c33_t *wd, int x, int y)
{
    const uint16_t *r = wd->active;

    if (r[DE_CONTROL1] & 0x04) {
        int      shift  = (wd->depth == 2) ? 1 : 0;
        uint32_t base   = wd->source & ~((64u << shift) - 1);
        unsigned start  = (wd->source >> shift) & 63;
        unsigned px     = ((start & 7) + wd->xdir * x) & 7;
        unsigned py     = ((start >> 3) + wd->ydir * y) & 7;

        return wd90c33_pixel_read(wd, base + (((py << 3) | px) << shift));
    }
    return wd90c33_pixel_read(wd, wd90c33_pixel_address(wd, r[DE_SRC_X] + wd->xdir * x, r[DE_SRC_Y] + wd->ydir * y));
}

static void
wd90c33_bitblt(wd90c33_t *wd)
{
    const uint16_t *r      = wd->active;
    unsigned        format = (r[DE_CONTROL1] >> 3) & 3;
    uint16_t        fg     = wd->active2[DE_FG0] | (wd->active2[DE_FG1] << 8);

    wd->width  = r[DE_DIM_X] + 1;
    wd->height = r[DE_DIM_Y] + 1;
    wd->source = wd90c33_pixel_address(wd, r[DE_SRC_X], r[DE_SRC_Y]);

    /* Host transfers wait for (or supply) data through 23C4h or memory. */
    if (r[DE_CONTROL1] & 0x22) {
        if ((r[DE_CONTROL1] & 0x20) && (format != FORMAT_COLOR) && (format != FORMAT_HOST_MONO))
            return;
        wd->busy = 1;
        return;
    }
    if (format == FORMAT_HOST_MONO)
        return;

    for (int y = 0; y < wd->height; y++) {
        for (int x = 0; x < wd->width; x++) {
            int dx = r[DE_DST_X] + wd->xdir * x;
            int dy = r[DE_DST_Y] + wd->ydir * y;

            if (format == FORMAT_FIXED)
                wd90c33_plot(wd, dx, dy, fg, -1);
            else {
                uint16_t source = wd90c33_source_pixel(wd, x, y);

                if (format == FORMAT_COMPARE)
                    wd90c33_plot(wd, dx, dy, 0, wd90c33_color_match(wd, source));
                else
                    wd90c33_plot(wd, dx, dy, source, -1);
            }
        }
    }
    wd->de[DE_DST_Y] = (wd->de[DE_DST_Y] + wd->ydir * wd->height) & 0xfff;
}

static void
wd90c33_strip(wd90c33_t *wd, int x, int y, int length, int ymajor)
{
    uint16_t fg = wd->active2[DE_FG0] | (wd->active2[DE_FG1] << 8);

    for (int i = 0; i < length; i++) {
        wd90c33_plot(wd, x, y, fg, -1);
        if (ymajor)
            y += wd->ydir;
        else
            x += wd->xdir;
    }
}

/* Line strips step along the major axis by the strip length and along the
   minor axis by one pixel after each strip (section 12.15.1). */
static void
wd90c33_line_strip(wd90c33_t *wd)
{
    const uint16_t *r      = wd->active;
    int             ymajor = !!(r[DE_CONTROL1] & 0x40);
    int             length = r[DE_DIM_X] + 1;
    int             x      = r[DE_DST_X];
    int             y      = r[DE_DST_Y];

    for (int strip = 0; strip <= r[DE_DIM_Y]; strip++) {
        wd90c33_strip(wd, x, y, length, ymajor);
        if (ymajor) {
            y += wd->ydir * length;
            x += wd->xdir;
        } else {
            x += wd->xdir * length;
            y += wd->ydir;
        }
    }
    wd->de[DE_DST_X] = x & 0xfff;
    wd->de[DE_DST_Y] = y & 0xfff;
}

/* Trapezoid fill strips are horizontal and only move down a row (12.15.2). */
static void
wd90c33_trapezoid(wd90c33_t *wd)
{
    const uint16_t *r = wd->active;

    wd90c33_strip(wd, r[DE_DST_X], r[DE_DST_Y], r[DE_DIM_X] + 1, 0);
    wd->de[DE_DST_Y] = (r[DE_DST_Y] + wd->ydir) & 0xfff;
}

static void
wd90c33_bresenham(wd90c33_t *wd)
{
    const uint16_t *r      = wd->active;
    int             ymajor = !!(r[DE_CONTROL1] & 0x40);
    int             count  = r[DE_DIM_X] + 1;
    int             k1     = wd90c33_sign14(wd->bresenham[0]);
    int             k2     = wd90c33_sign14(wd->bresenham[1]);
    int             error  = wd90c33_sign14(wd->bresenham[2]);
    int             x      = r[DE_DST_X];
    int             y      = r[DE_DST_Y];
    uint16_t        fg     = wd->active2[DE_FG0] | (wd->active2[DE_FG1] << 8);

    for (int i = 0; i < count; i++) {
        if ((i < count - 1) || !(r[DE_CONTROL1] & 0x01))
            wd90c33_plot(wd, x, y, fg, -1);
        if (error >= 0) {
            if (ymajor)
                x += wd->xdir;
            else
                y += wd->ydir;
            error += k2;
        } else
            error += k1;
        if (ymajor)
            y += wd->ydir;
        else
            x += wd->xdir;
    }
    wd->de[DE_DST_X]  = x & 0xfff;
    wd->de[DE_DST_Y]  = y & 0xfff;
    wd->bresenham[2]  = error & 0x3fff;
}

static void
wd90c33_start(wd90c33_t *wd, unsigned mode)
{
    memcpy(wd->active, wd->de, sizeof(wd->active));
    memcpy(wd->active2, wd->de2, sizeof(wd->active2));
    wd->depth = (wd->de[DE_CONTROL2] >> 10) & 3;
    wd->xdir  = (wd->de[DE_CONTROL1] & 0x100) ? -1 : 1;
    wd->ydir  = (wd->de[DE_CONTROL1] & 0x080) ? -1 : 1;
    wd->x = wd->y = 0;
    wd->busy      = 0;
    if (wd->depth == 3)
        return;

    switch (mode) {
        case MODE_BITBLT:
            wd90c33_bitblt(wd);
            break;
        case MODE_LINE_STRIP:
            wd90c33_line_strip(wd);
            break;
        case MODE_TRAPEZOID:
            wd90c33_trapezoid(wd);
            break;
        case MODE_BRESENHAM:
            wd90c33_bresenham(wd);
            break;
        default:
            return;
    }
    if (!wd->busy)
        wd90c33_finish(wd);
}

static void
wd90c33_cursor_update(wd90c33_t *wd)
{
    svga_t *svga = wd->svga;
    int     size = (((wd->cursor[0] >> 9) & 3) == 1) ? 64 : 32;
    int     x    = wd->cursor[0x0d] - (wd->cursor_origin & (size - 1));
    int     y    = wd->cursor[0x0e] - ((wd->cursor_origin >> 6) & (size - 1));

    svga->hwcursor.ena       = !!(wd->cursor[0] & 0x800);
    svga->hwcursor.cur_xsize = svga->hwcursor.cur_ysize = size;
    svga->hwcursor.x                                    = x;
    svga->hwcursor.y                                    = y < 0 ? 0 : y;
    svga->hwcursor.xoff                                 = 0;
    svga->hwcursor.yoff                                 = y < 0 ? -y : 0;
    svga->hwcursor.addr                                 = (wd->cursor_address << 2) + svga->hwcursor.yoff * (size >> 2);
    svga->hwcursor.pitch                                = size >> 2;
    svga->hwcursor.h_acc                                = wd->cursor[0];
    svga->fullchange                                    = svga->monitor->mon_changeframecount;
}

static void
wd90c33_reg_write(wd90c33_t *wd, unsigned block, unsigned index, uint16_t value)
{
    if (!block) {
        wd->bresenham[index] = value & 0x3fff;
        return;
    }
    if (index == 15) {
        wd->index = (wd->index & 0x1000) | (value & 0x0fff);
        return;
    }
    switch (block) {
        case 1:
            wd->de[index] = value & de_masks[index];
            if ((index == DE_CONTROL1) && (value & 0xe00))
                wd90c33_start(wd, (value >> 9) & 7);
            else if ((index == DE_DIM_X) && ((((wd->de[DE_CONTROL1] >> 9) & 7) == MODE_LINE_STRIP) || (((wd->de[DE_CONTROL1] >> 9) & 7) == MODE_TRAPEZOID)))
                wd90c33_start(wd, (wd->de[DE_CONTROL1] >> 9) & 7);
            break;
        case 2:
            wd->cursor[index] = value & cursor_masks[index];
            if (!index) {
                wd->cursor_origin  = wd->cursor[0x0c];
                wd->cursor_address = wd->cursor[1] | (wd->cursor[2] << 12);
            }
            wd90c33_cursor_update(wd);
            break;
        case 3:
            wd->de2[index] = value & de2_masks[index];
            break;

        default:
            break;
    }
}

/* Drawing engine writes wait in the command buffer while a host transfer
   keeps the engine busy (section 13.4). */
static void
wd90c33_buffered_write(wd90c33_t *wd, unsigned block, unsigned index, uint16_t value)
{
    if (wd->busy && (wd->cmd_control & CMD_ENABLE) && (index != 15)) {
        if (wd->cmd_count == CMD_BUFFER_SIZE) {
            wd->cmd_control |= CMD_OVERFLOW;
            return;
        }
        wd->cmd[wd->cmd_count].block   = block;
        wd->cmd[wd->cmd_count].index   = index;
        wd->cmd[wd->cmd_count++].value = value;
        return;
    }
    wd90c33_reg_write(wd, block, index, value);
}

static uint16_t
wd90c33_cmd_status(wd90c33_t *wd)
{
    uint16_t status = wd->cmd_control & (CMD_IRQ_STATUS | CMD_IRQ_ARM | CMD_OVERFLOW | CMD_ENABLE);

    if (wd->busy)
        status |= CMD_DE_BUSY;
    if (wd->cmd_control & CMD_ENABLE)
        status |= wd->cmd_count;
    else if (wd->busy)
        status |= CMD_BUFFER_SIZE;
    return status;
}

static void
wd90c33_cmd_control_write(wd90c33_t *wd, uint16_t value)
{
    if (value & CMD_DE_BUSY) {
        wd->busy      = 0;
        wd->cmd_count = 0;
    }
    if (!(value & CMD_ENABLE))
        wd->cmd_control &= ~CMD_OVERFLOW;
    if (!(value & CMD_IRQ_ARM))
        wd->cmd_control &= ~CMD_IRQ_STATUS;
    wd->cmd_control = (wd->cmd_control & (CMD_IRQ_STATUS | CMD_OVERFLOW)) | (value & (CMD_IRQ_ARM | CMD_ENABLE));
    wd90c33_update_irq(wd);
}

static uint16_t
wd90c33_inw(uint16_t port, void *priv)
{
    wd90c33_t *wd     = priv;
    unsigned   block  = wd->index & 0xff;
    unsigned   index  = (wd->index >> 8) & 15;
    uint16_t   result = index << 12;

    switch (port & 0x0e) {
        case 0x0:
            return wd->index | ((block > 3) ? 0x2000 : 0);
        case 0x2:
            if (!block && !index)
                result |= (wd->cmd_control & CMD_IRQ_STATUS) ? 5 : 0;
            else if (block == 1)
                result |= wd->de[index];
            else if (block == 2)
                result |= wd->cursor[index];
            else if (block == 3)
                result |= wd->de2[index];
            if (!(wd->index & 0x1000))
                wd->index = (wd->index & ~0x0f00) | (((index + 1) & 15) << 8);
            return result;
        case 0x4:
            wd->host_read_data = wd90c33_host_read(wd);
            return wd->host_read_data;
        case 0x6:
            return wd->host_read_data >> 16;
        case 0x8:
        case 0xa:
        case 0xc:
            return wd->bresenham[((port & 0x0e) - 8) >> 1];
        case 0xe:
            return wd90c33_cmd_status(wd);

        default:
            break;
    }
    return 0xffff;
}

static void
wd90c33_outw(uint16_t port, uint16_t value, void *priv)
{
    wd90c33_t *wd = priv;

    switch (port & 0x0e) {
        case 0x0:
            wd->index = value & 0x1fff;
            break;
        case 0x2:
            if ((value >> 12) == 15)
                wd90c33_reg_write(wd, 1, 15, value);
            else if ((wd->index & 0xff) == 2)
                wd90c33_reg_write(wd, 2, value >> 12, value);
            else if (((wd->index & 0xff) == 1) || ((wd->index & 0xff) == 3))
                wd90c33_buffered_write(wd, wd->index & 0xff, value >> 12, value);
            break;
        case 0x4:
        case 0x6:
            wd90c33_host_write(wd, port & 2, value, 2);
            break;
        case 0x8:
        case 0xa:
        case 0xc:
            wd90c33_buffered_write(wd, 0, ((port & 0x0e) - 8) >> 1, value);
            break;
        case 0xe:
            wd90c33_cmd_control_write(wd, value);
            break;

        default:
            break;
    }
}

static void
wd90c33_outl(uint16_t port, uint32_t value, void *priv)
{
    wd90c33_t *wd = priv;

    if ((port & 0x0e) == 0x4)
        wd90c33_host_write(wd, 0, value, 4);
    else {
        wd90c33_outw(port, value, wd);
        wd90c33_outw(port + 2, value >> 16, wd);
    }
}

static uint8_t
wd90c33_in(uint16_t port, void *priv)
{
    wd90c33_t *wd   = priv;
    unsigned   pair = (port & 0x0e) >> 1;

    if (!pair)
        return wd90c33_inw(port & ~1, wd) >> ((port & 1) * 8);
    /* Reading the first host data byte fetches the next doubleword. */
    if ((pair == 2) || (pair == 3)) {
        if (!(port & 3))
            wd->host_read_data = wd90c33_host_read(wd);
        return wd->host_read_data >> ((port & 3) * 8);
    }
    if (!(port & 1))
        wd->read_latch[pair] = wd90c33_inw(port, wd);
    return wd->read_latch[pair] >> ((port & 1) * 8);
}

static void
wd90c33_out(uint16_t port, uint8_t value, void *priv)
{
    wd90c33_t *wd   = priv;
    unsigned   pair = (port & 0x0e) >> 1;

    if (!pair) {
        unsigned shift = (port & 1) * 8;
        wd->index      = ((wd->index & ~(0xffu << shift)) | (value << shift)) & 0x1fff;
        return;
    }
    /* Host data ports take byte writes to individual latch bytes. */
    if ((pair == 2) || (pair == 3)) {
        wd90c33_host_write(wd, port & 3, value, 1);
        return;
    }
    if (!(port & 1))
        wd->write_latch[pair] = value;
    else
        wd90c33_outw(port & ~1, wd->write_latch[pair] | (value << 8), wd);
}

/* With Control Register 2 bit 3 set, display memory writes carry HBLT data
   while a host transfer runs; the address only selects the read-back-latch
   bytes (section 12.14.1). WD's drivers leave the bit set when they exit to
   DOS, so writes reach memory again once the transfer is done. */
int
wd90c33_mem_write(wd90c33_t *wd, uint32_t addr, uint16_t value, int size)
{
    if (!wd90c33_host_source(wd) || !(wd->active[DE_CONTROL2] & 0x08))
        return 0;
    wd90c33_host_write(wd, addr & 3, value, size);
    return 1;
}

void
wd90c33_hwcursor_draw(wd90c33_t *wd, int displine)
{
    svga_t     *svga    = wd->svga;
    hwcursor_t *cursor  = &svga->hwcursor_latch;
    unsigned    mode    = (cursor->h_acc >> 5) & 7;
    uint32_t    address = cursor->addr;
    uint32_t    colors[3];

    for (int i = 0; i < 3; i++)
        colors[i] = wd->cursor[3 + i * 3] | (wd->cursor[4 + i * 3] << 8) | (wd->cursor[5 + i * 3] << 16);
    if (svga->interlace && svga->hwcursor_oddeven)
        address += cursor->pitch;

    for (int x = 0; x < cursor->cur_xsize; x++) {
        int      screen_x = cursor->x + x;
        uint32_t a        = address + (x >> 3) * 2;
        unsigned bit      = 7 - (x & 7);
        unsigned code;
        uint32_t pixel;
        uint32_t color;

        if ((screen_x < 0) || (screen_x >= svga->hdisp) || (screen_x + svga->x_add >= 2048))
            continue;
        code = (((svga->vram[wd90c33_vram_address(svga, a)] >> bit) & 1) << 1) | ((svga->vram[wd90c33_vram_address(svga, a + 1)] >> bit) & 1);
        if (code == 2)
            continue;

        uint32_t *output = &svga->monitor->target_buffer->line[displine][screen_x + svga->x_add];
        pixel            = *output;
        if (svga->bpp <= 8) {
            /* The SVGA renderer has already converted palette indices to RGB. */
            for (unsigned i = 0; i < 256; i++) {
                if (svga->pallook[i] == pixel) {
                    pixel = i;
                    break;
                }
            }
        } else if (svga->bpp == 15)
            pixel = ((pixel >> 9) & 0x7c00) | ((pixel >> 6) & 0x3e0) | ((pixel >> 3) & 0x1f);
        else if (svga->bpp == 16)
            pixel = ((pixel >> 8) & 0xf800) | ((pixel >> 5) & 0x7e0) | ((pixel >> 3) & 0x1f);

        if (code == 0)
            color = mode ? colors[1] : 0;
        else if (code == 1)
            color = mode ? colors[0] : 0xffffff;
        else if (mode == 3)
            color = colors[2];
        else
            color = ~(pixel ^ ((mode == 2) ? colors[2] : 0));
        if ((cursor->h_acc & 0x100) && (svga->bpp <= 8) && ((mode == 0) || ((mode == 1) && (code == 3)))) {
            uint8_t mask = (svga->attrregs[0x10] & 0x80) ? 0xf0 : 0xc0;
            color        = (color & ~mask) | (colors[2] & mask);
        }

        if (svga->bpp <= 8)
            pixel = svga->pallook[color & svga->dac_mask & 0xff];
        else if (svga->bpp == 15)
            pixel = video_15to32[color & 0x7fff];
        else if (svga->bpp == 16)
            pixel = video_16to32[color & 0xffff];
        else
            pixel = color & 0xffffff;
        *output = pixel;
    }
    cursor->addr += cursor->pitch * (svga->interlace ? 2 : 1);
}

wd90c33_t *
wd90c33_init(svga_t *svga)
{
    wd90c33_t *wd = calloc(1, sizeof(*wd));
    wd->svga      = svga;
    io_sethandler(0x23c0, 16, wd90c33_in, wd90c33_inw, NULL,
                  wd90c33_out, wd90c33_outw, wd90c33_outl, wd);
    return wd;
}

void
wd90c33_close(wd90c33_t *wd)
{
    io_removehandler(0x23c0, 16, wd90c33_in, wd90c33_inw, NULL,
                     wd90c33_out, wd90c33_outw, wd90c33_outl, wd);
    if (wd->cmd_control & CMD_IRQ_STATUS)
        picintc(1 << 9);
    free(wd);
}
