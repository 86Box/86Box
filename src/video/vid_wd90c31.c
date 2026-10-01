/*
 * 86Box: WD90C31 BitBLT engine and hardware cursor.
 *
 * Register definitions: Western Digital WD90C31 data sheet (11/25/91),
 * sections 8-10. The engine completes video-to-video operations synchronously;
 * host transfers remain busy until the last data word has been transferred.
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
#include "vid_wd90c31.h"

enum {
    BLT_CONTROL1,
    BLT_CONTROL2,
    BLT_SRC_LO,
    BLT_SRC_HI,
    BLT_DST_LO,
    BLT_DST_HI,
    BLT_WIDTH,
    BLT_HEIGHT,
    BLT_PITCH,
    BLT_ROP,
    BLT_FG,
    BLT_BG,
    BLT_TRANS_COLOR,
    BLT_TRANS_MASK,
    BLT_MASK
};

struct wd90c31_t {
    svga_t  *svga;
    uint16_t index;
    uint16_t blt[16], cursor[16], active[16];
    uint16_t read_latch[3];
    uint8_t  write_latch[3];
    uint32_t source, destination;
    uint32_t host_data;
    unsigned x, y;
    unsigned host_words;
    uint16_t cursor_origin;
    uint32_t cursor_address;
    int      busy, irq;
};

static const uint16_t blt_masks[16] = {
    0x0fff, 0x04fd, 0x0fff, 0x01ff, 0x0fff, 0x01ff, 0x0fff, 0x0fff,
    0x0fff, 0x0f00, 0x00ff, 0x00ff, 0x00ff, 0x00ff, 0x00ff, 0
};
/* Bit n set: a planar row whose starting pixel phase is the index and whose
   X dimension is n modulo 8 is drawn 8 pixels short. */
static const uint8_t planar_short_rows[8] = { 0, 0, 0, 0, 0x40, 0, 0x70, 0xb8 };
static const uint16_t cursor_masks[16] = {
    0x0fe0, 0x0fff, 0x01ff, 0x00ff, 0x00ff, 0x0fff, 0x07ff, 0x03ff,
    0x00ff, 0, 0, 0, 0, 0, 0, 0
};

/* CRT and BitBLT share byte/word/doubleword memory organization (table 7-9).
   Do not use the scanline-dependent CGA address substitutions for BitBLT. */
static uint32_t
wd90c31_vram_address(svga_t *svga, uint32_t address)
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

static uint8_t
wd90c31_pixel_read(wd90c31_t *wd, uint32_t address)
{
    svga_t *svga  = wd->svga;
    uint8_t pixel = 0;

    if (wd->active[BLT_CONTROL1] & 0x100)
        return svga->vram[wd90c31_vram_address(svga, address)];

    for (unsigned plane = 0; plane < 4; plane++)
        pixel |= ((svga->vram[wd90c31_vram_address(svga, ((address >> 3) << 2) + plane)] >> (7 - (address & 7))) & 1) << plane;
    return pixel;
}

static void
wd90c31_pixel_write(wd90c31_t *wd, uint32_t address, uint8_t pixel)
{
    svga_t *svga = wd->svga;
    uint8_t mask = wd->active[BLT_MASK];

    if (wd->active[BLT_CONTROL1] & 0x100) {
        address                          = wd90c31_vram_address(svga, address);
        svga->vram[address]              = (svga->vram[address] & ~mask) | (pixel & mask);
        svga->changedvram[address >> 12] = svga->monitor->mon_changeframecount;
    } else {
        uint8_t bit = 0x80 >> (address & 7);
        address     = (address >> 3) << 2;
        for (unsigned plane = 0; plane < 4; plane++) {
            if (mask & (1 << plane)) {
                uint32_t a                 = wd90c31_vram_address(svga, address + plane);
                svga->vram[a]              = (svga->vram[a] & ~bit) | ((pixel & (1 << plane)) ? bit : 0);
                svga->changedvram[a >> 12] = svga->monitor->mon_changeframecount;
            }
        }
    }
    if (!(svga->gdcreg[6] & 1))
        svga->fullchange = svga->monitor->mon_changeframecount;
}

static void
wd90c31_blt_finish(wd90c31_t *wd)
{
    const uint16_t *r = wd->active;

    if (r[BLT_CONTROL2] & 0x40) {
        int delta = r[BLT_WIDTH];
        if (r[BLT_CONTROL1] & 0x80)
            delta *= r[BLT_HEIGHT];
        if (r[BLT_CONTROL1] & 0x400)
            delta = -delta;
        uint32_t dst        = (wd->destination + delta) & 0x1fffff;
        wd->blt[BLT_DST_LO] = dst & 0xfff;
        wd->blt[BLT_DST_HI] = dst >> 12;
    }
    wd->busy = 0;
    wd->blt[BLT_CONTROL1] &= ~0x800;
    if (r[BLT_CONTROL2] & 0x400) {
        wd->irq = 1;
        picint(1 << 9);
    }
}

/* Truth-table bit order is 00, 01, 10, 11 from the most significant bit. */
static uint8_t
wd90c31_rop(unsigned rop, uint8_t source, uint8_t dest)
{
    return ((rop & 8) ? (~source & ~dest) : 0) | ((rop & 4) ? (~source & dest) : 0) | ((rop & 2) ? (source & ~dest) : 0) | ((rop & 1) ? (source & dest) : 0);
}

static uint32_t
wd90c31_blt_address(wd90c31_t *wd, int source)
{
    const uint16_t *r         = wd->active;
    int             direction = (r[BLT_CONTROL1] & 0x400) ? -1 : 1;
    uint32_t        base      = source ? wd->source : wd->destination;

    if (source && ((r[BLT_CONTROL2] & 0x30) == 0x10)) {
        unsigned x = ((base & 7) + direction * wd->x) & 7;
        unsigned y = (((base >> 3) & 7) + direction * wd->y) & 7;
        return (base & ~63u) | (y << 3) | x;
    }
    unsigned pitch = (r[BLT_CONTROL1] & (source ? 0x40 : 0x80)) ? r[BLT_WIDTH] : r[BLT_PITCH];
    return (base + direction * (wd->y * pitch + wd->x)) & 0x1fffff;
}

static uint8_t
wd90c31_blt_pixel(wd90c31_t *wd, uint8_t source)
{
    const uint16_t *r            = wd->active;
    unsigned        format       = (r[BLT_CONTROL1] >> 2) & 3;
    uint32_t        dst          = wd90c31_blt_address(wd, 0);
    uint8_t         dest         = (r[BLT_CONTROL1] & 0x20) ? 0 : wd90c31_pixel_read(wd, dst);
    /* Contrary to the data sheet, set mask bits select the bits that are
       compared: WD's drivers extract glyph plane n with a mask of 1 << n. */
    uint8_t         compare_mask = r[BLT_TRANS_MASK];
    int             write        = 1;

    if (!(r[BLT_CONTROL1] & 0x100))
        compare_mask &= 0x0f;
    if (format == 2)
        source = r[BLT_FG];
    else if (format == 1 || format == 3) {
        int mono = (format == 3) ? !!source : !((source ^ r[BLT_TRANS_COLOR]) & compare_mask);
        if (!mono && (r[BLT_CONTROL2] & 8))
            write = 0;
        source = r[mono ? BLT_FG : BLT_BG];
    }
    if ((r[BLT_CONTROL2] & 1) && format != 1 && format != 3) {
        int match = !((dest ^ r[BLT_TRANS_COLOR]) & compare_mask);
        if (match != !!(r[BLT_CONTROL2] & 4))
            write = 0;
    }
    uint8_t pixel = write ? wd90c31_rop(r[BLT_ROP] >> 8, source, dest) : dest;
    if (write && !(r[BLT_CONTROL1] & 0x20))
        wd90c31_pixel_write(wd, dst, pixel);

    if (++wd->x == r[BLT_WIDTH]) {
        wd->x = 0;
        if (++wd->y == r[BLT_HEIGHT] && !(r[BLT_CONTROL1] & 0x20))
            wd90c31_blt_finish(wd);
    }
    return pixel;
}

static void
wd90c31_blt_start(wd90c31_t *wd)
{
    memcpy(wd->active, wd->blt, sizeof(wd->active));
    wd->source      = wd->blt[BLT_SRC_LO] | (wd->blt[BLT_SRC_HI] << 12);
    wd->destination = wd->blt[BLT_DST_LO] | (wd->blt[BLT_DST_HI] << 12);
    wd->x = wd->y = wd->host_words = 0;
    wd->host_data                  = 0;
    wd->busy                       = 1;
    wd->blt[BLT_CONTROL1] |= 0x800;

    /* Zero/oversized dimensions and reserved addressing modes are not commands. */
    if (!wd->blt[BLT_WIDTH] || wd->blt[BLT_WIDTH] > 2048 || !wd->blt[BLT_HEIGHT] || wd->blt[BLT_HEIGHT] > 2048 || (wd->blt[BLT_CONTROL1] & 0x211) || ((wd->blt[BLT_CONTROL1] & 0x22) == 0x22)) {
        wd->busy = 0;
        wd->blt[BLT_CONTROL1] &= ~0x800;
        return;
    }
    /* Planar rows starting at some pixel phases come out one byte short for
       some widths. WD's 16-colour drivers (WD800_4.DRV, WD1024_4.DRV) add 8
       to the X dimension in exactly these cases, but only supply host data
       for the width they want drawn. The phase is counted from the starting
       corner, so it is mirrored for right-to-left operations. */
    if (!(wd->blt[BLT_CONTROL1] & 0x120) && (wd->blt[BLT_WIDTH] > 8)) {
        unsigned phase = wd->destination & 7;

        if (wd->blt[BLT_CONTROL1] & 0x400)
            phase ^= 7;
        if ((planar_short_rows[phase] >> (wd->blt[BLT_WIDTH] & 7)) & 1)
            wd->active[BLT_WIDTH] -= 8;
    }
    if (wd->blt[BLT_CONTROL1] & 0x22)
        return;

    while (wd->busy) {
        uint8_t source = wd90c31_pixel_read(wd, wd90c31_blt_address(wd, 1));
        wd90c31_blt_pixel(wd, source);
    }
}

static void
wd90c31_host_write(wd90c31_t *wd, uint16_t value)
{
    const uint16_t *r         = wd->active;
    int             packed    = !!(r[BLT_CONTROL1] & 0x100);
    int             mono      = ((r[BLT_CONTROL1] & 0x0c) == 0x0c);
    unsigned        count     = packed ? 4 : 8;
    unsigned        row       = wd->y;
    int             direction = (r[BLT_CONTROL1] & 0x400) ? -1 : 1;
    int             first     = wd->x ? (direction > 0 ? 0 : count - 1) : (wd->source & (count - 1));

    if (!wd->busy || !(r[BLT_CONTROL1] & 2) || (r[BLT_CONTROL1] & 0x20))
        return;
    if (mono)
        wd->host_data = value;
    else {
        wd->host_data |= (uint32_t) value << (16 * wd->host_words++);
        if (wd->host_words < 2)
            return;
    }
    for (int i = first; i >= 0 && i < (int) count && wd->busy && wd->y == row; i += direction) {
        uint8_t pixel = 0;
        if (mono)
            pixel = (wd->host_data >> (count - 1 - i)) & 1;
        else if (packed)
            pixel = wd->host_data >> (i * 8);
        else {
            for (unsigned plane = 0; plane < 4; plane++)
                pixel |= ((wd->host_data >> (plane * 8 + 7 - i)) & 1) << plane;
        }
        wd90c31_blt_pixel(wd, pixel);
    }
    wd->host_words = 0;
    wd->host_data  = 0;
}

static uint16_t
wd90c31_host_read(wd90c31_t *wd)
{
    int      packed = !!(wd->active[BLT_CONTROL1] & 0x100);
    unsigned row    = wd->y;

    if (wd->host_words) {
        wd->host_words = 0;
        if (wd->busy && wd->y == wd->active[BLT_HEIGHT])
            wd90c31_blt_finish(wd);
        return wd->host_data >> 16;
    }
    if (!wd->busy || !(wd->active[BLT_CONTROL1] & 0x20))
        return 0xffff;
    wd->host_data = 0;
    for (unsigned i = 0; i < (packed ? 4u : 8u) && wd->busy && wd->y == row; i++) {
        uint8_t source = wd90c31_pixel_read(wd, wd90c31_blt_address(wd, 1));
        uint8_t pixel  = wd90c31_blt_pixel(wd, source);
        if (packed)
            wd->host_data |= (uint32_t) pixel << (i * 8);
        else {
            for (unsigned plane = 0; plane < 4; plane++)
                wd->host_data |= (uint32_t) ((pixel >> plane) & 1) << (plane * 8 + 7 - i);
        }
    }
    wd->host_words = 1;
    return wd->host_data;
}

static void
wd90c31_cursor_update(wd90c31_t *wd)
{
    svga_t *svga = wd->svga;
    int     size = (wd->cursor[0] & 0x200) ? 64 : 32;
    int     x    = wd->cursor[6] - (wd->cursor_origin & (size - 1));
    int     y    = wd->cursor[7] - ((wd->cursor_origin >> 6) & (size - 1));

    svga->hwcursor.ena       = !!(wd->cursor[0] & 0x800);
    svga->hwcursor.cur_xsize = svga->hwcursor.cur_ysize = size;
    svga->hwcursor.x                                    = x;
    svga->hwcursor.y                                    = y < 0 ? 0 : y;
    svga->hwcursor.xoff                                 = 0;
    svga->hwcursor.yoff                                 = y < 0 ? -y : 0;
    svga->hwcursor.addr                                 = (wd->cursor_address << 2) + svga->hwcursor.yoff * (size >> 2);
    svga->hwcursor.pitch                                = size >> 2;
    /* These fields are copied with the cursor at vertical retrace. */
    svga->hwcursor.h_acc = wd->cursor[0];
    svga->hwcursor.v_acc = wd->cursor[3] | (wd->cursor[4] << 8) | (wd->cursor[8] << 16);
    svga->fullchange     = svga->monitor->mon_changeframecount;
}

static uint16_t
wd90c31_inw(uint16_t port, void *priv)
{
    wd90c31_t *wd     = priv;
    unsigned   block  = wd->index & 0xff;
    unsigned   index  = (wd->index >> 8) & 15;
    uint16_t   result = index << 12;

    switch (port & 6) {
        case 0:
            return wd->index | (block > 2 ? 0x2000 : 0);
        case 2:
            if (!block && !index)
                result |= wd->irq ? 5 : 0;
            else if (block == 1)
                result |= wd->blt[index];
            else if (block == 2)
                result |= wd->cursor[index];
            if (!(wd->index & 0x1000))
                wd->index = (wd->index & ~0x0f00) | (((index + 1) & 15) << 8);
            return result;
        case 4:
            return wd90c31_host_read(wd);
        default:
            return 0xffff;
    }
}

static uint8_t
wd90c31_in(uint16_t port, void *priv)
{
    wd90c31_t *wd   = priv;
    unsigned   pair = (port & 6) >> 1;

    if (pair == 3)
        return 0xff;
    if (!pair)
        return wd90c31_inw(port & ~1, wd) >> ((port & 1) * 8);
    if (!(port & 1))
        wd->read_latch[pair] = wd90c31_inw(port, wd);
    return wd->read_latch[pair] >> ((port & 1) * 8);
}

static void
wd90c31_outw(uint16_t port, uint16_t value, void *priv)
{
    wd90c31_t *wd    = priv;
    unsigned   block = wd->index & 0xff;
    unsigned   index = value >> 12;

    switch (port & 6) {
        case 0:
            wd->index = value & 0x1fff;
            break;
        case 2:
            if (block == 1) {
                wd->blt[index] = value & blt_masks[index];
                if (index == BLT_CONTROL2 && !(value & 0x400) && wd->irq) {
                    wd->irq = 0;
                    picintc(1 << 9);
                }
                if ((index == BLT_CONTROL1 && (value & 0x800)) || ((wd->blt[BLT_CONTROL2] & 0x80) && index == ((wd->blt[BLT_CONTROL2] & 0x40) ? BLT_SRC_LO : BLT_DST_LO)))
                    wd90c31_blt_start(wd);
            } else if (block == 2) {
                wd->cursor[index] = value & cursor_masks[index];
                if (!index) {
                    wd->cursor_origin  = wd->cursor[5];
                    wd->cursor_address = wd->cursor[1] | (wd->cursor[2] << 12);
                }
                wd90c31_cursor_update(wd);
            }
            break;
        case 4:
            wd90c31_host_write(wd, value);
            break;
        default:
            break;
    }
}

static void
wd90c31_out(uint16_t port, uint8_t value, void *priv)
{
    wd90c31_t *wd   = priv;
    unsigned   pair = (port & 6) >> 1;

    if (pair == 3)
        return;
    if (!pair) {
        unsigned shift = (port & 1) * 8;
        wd->index      = ((wd->index & ~(0xffu << shift)) | (value << shift)) & 0x1fff;
        return;
    }
    /* Monochrome host data only uses the low byte of the BitBLT I/O port
       (section 9.14), so a write to the even port is a complete transfer
       and the odd port is ignored. WD's planar drivers use OUTSB here. */
    if ((pair == 2) && ((wd->active[BLT_CONTROL1] & 0x0c) == 0x0c)) {
        if (!(port & 1))
            wd90c31_host_write(wd, value);
        return;
    }
    if (!(port & 1))
        wd->write_latch[pair] = value;
    else
        wd90c31_outw(port & ~1, wd->write_latch[pair] | (value << 8), wd);
}

void
wd90c31_hwcursor_draw(wd90c31_t *wd, int displine)
{
    svga_t     *svga      = wd->svga;
    hwcursor_t *cursor    = &svga->hwcursor_latch;
    unsigned    mode      = (cursor->h_acc >> 5) & 7;
    uint8_t     primary   = cursor->v_acc;
    uint8_t     secondary = cursor->v_acc >> 8;
    uint8_t     auxiliary = cursor->v_acc >> 16;
    unsigned    bytes     = (svga->bpp + 7) / 8;
    uint32_t    address   = cursor->addr;

    if (svga->interlace && svga->hwcursor_oddeven)
        address += cursor->pitch;
    for (int x = 0; x < cursor->cur_xsize; x += bytes) {
        int screen_x = (cursor->x + x) / (int) bytes;
        if (cursor->x + x < 0 || screen_x >= svga->hdisp || screen_x + svga->x_add < 0 || screen_x + svga->x_add >= 2048)
            continue;
        uint32_t *output = &svga->monitor->target_buffer->line[displine][screen_x + svga->x_add];
        uint32_t  pixel  = *output;
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
        int changed = 0;
        for (unsigned byte = 0; byte < bytes && x + byte < (unsigned) cursor->cur_xsize; byte++) {
            unsigned column = x + byte;
            uint32_t a      = address + (column >> 3) * 2;
            unsigned bit    = 7 - (column & 7);
            unsigned code   = (((svga->vram[wd90c31_vram_address(svga, a)] >> bit) & 1) << 1) | ((svga->vram[wd90c31_vram_address(svga, a + 1)] >> bit) & 1);
            uint8_t  color  = pixel >> (byte * 8);
            if (code == 2)
                continue;
            if (code == 0)
                color = mode ? secondary : 0;
            else if (code == 1)
                color = mode ? primary : 0xff;
            else if (mode == 3)
                color = auxiliary;
            else
                color = ~(color ^ ((mode == 2) ? auxiliary : 0));
            if ((cursor->h_acc & 0x100) && (mode == 0 || (mode == 1 && code == 3))) {
                uint8_t mask = (svga->attrregs[0x10] & 0x80) ? 0xf0 : 0xc0;
                color        = (color & ~mask) | (auxiliary & mask);
            }
            pixel   = (pixel & ~(0xffu << (byte * 8))) | ((uint32_t) color << (byte * 8));
            changed = 1;
        }
        if (changed) {
            if (svga->bpp <= 8)
                pixel = svga->pallook[pixel & svga->dac_mask];
            else if (svga->bpp == 15)
                pixel = video_15to32[pixel & 0xffff];
            else if (svga->bpp == 16)
                pixel = video_16to32[pixel & 0xffff];
            *output = pixel;
        }
    }
    cursor->addr += cursor->pitch * (svga->interlace ? 2 : 1);
}

wd90c31_t *
wd90c31_init(svga_t *svga)
{
    wd90c31_t *wd = calloc(1, sizeof(*wd));
    wd->svga      = svga;
    io_sethandler(0x23c0, 8, wd90c31_in, wd90c31_inw, NULL,
                  wd90c31_out, wd90c31_outw, NULL, wd);
    return wd;
}

void
wd90c31_close(wd90c31_t *wd)
{
    io_removehandler(0x23c0, 8, wd90c31_in, wd90c31_inw, NULL,
                     wd90c31_out, wd90c31_outw, NULL, wd);
    if (wd->irq)
        picintc(1 << 9);
    free(wd);
}
