/*
* 86Box    A hypervisor and IBM PC system emulator that specializes in
*          running old operating systems and software designed for IBM
*          PC systems and compatibles from 1981 through fairly recent
*          system designs based on the PCI bus.
*
*          This file is part of the 86Box distribution.
*
*          NV3 primitives: rectangles, the Win95 GDI class's rectangles and
*          monochrome bitmaps, points, lines and triangles.
*
* Authors: Connor Hyde, <mario64crashed@gmail.com>
 *          Marcos Alves, <xeon4d@gmail.com>
*
*          Copyright 2024-2026 Connor Hyde
 *          Copyright 2026 Marcos Alves
*/

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>
#include <86box/utils/video_stdlib.h>

/* Fill [x0, x1) x [y0, y1) */
void
nv3_render_fill(int32_t x0, int32_t y0, int32_t x1, int32_t y1, nv3_color_expanded_t c, nv3_grobj_t grobj, bool own_clip)
{
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > 4096)
        x1 = 4096;
    if (y1 > 16384)
        y1 = 16384;

    for (int32_t y = y0; y < y1; y++) {
        for (int32_t x = x0; x < x1; x++)
            nv3_render_pixel_ex(x, y, c, grobj, own_clip);
    }
}

/* Rectangle (class 0x07): signed position, unsigned size */
void
nv3_render_rect(nv3_coord_16_t position, nv3_coord_16_t size, uint32_t color, nv3_grobj_t grobj)
{
    int32_t x = (int16_t) position.x;
    int32_t y = (int16_t) position.y;

    nv3_render_fill(x, y, x + size.x, y + size.y, nv3_render_expand_color(color, grobj), grobj, false);
}

/* GDI type A: unclipped rectangle (only the canvas clips) */
void
nv3_render_gdi_rect(nv3_coord_16_t position, nv3_coord_16_t size, uint32_t color, nv3_grobj_t grobj)
{
    int32_t x = (int16_t) position.x;
    int32_t y = (int16_t) position.y;

    nv3_render_fill(x, y, x + size.x, y + size.y, nv3_render_expand_color(color, grobj), grobj, true);
}

/* GDI type B: a rectangle given by its edges, clipped by CLIP_B; right and
   bottom are exclusive */
void
nv3_render_rect_clipped(nv3_clip_16_t rect, uint32_t color, nv3_grobj_t grobj)
{
    const nv3_clip_16_t *clip = &nv3->pgraph.win95_gdi_text.clip_b;
    int32_t              x0   = MAX((int16_t) rect.left, (int16_t) clip->left);
    int32_t              y0   = MAX((int16_t) rect.top, (int16_t) clip->top);
    int32_t              x1   = MIN((int16_t) rect.right, (int16_t) clip->right);
    int32_t              y1   = MIN((int16_t) rect.bottom, (int16_t) clip->bottom);

    nv3_render_fill(x0, y0, x1, y1, nv3_render_expand_color(color, grobj), grobj, true);
}

/* GDI types C, D and E: a monochrome bitmap streamed 32 pixels per method,
   rows SIZE_IN wide with no padding of their own. 1 bits draw COLOR1; for E
   0 bits draw COLOR0, for C and D they are transparent. D and E draw only
   the SIZE_OUT part. Everything is clipped by the type's clip rectangle. */
static void
nv3_render_gdi_mono(uint32_t data, nv3_grobj_t grobj, nv3_coord_16_t point, nv3_coord_16_t size_in, nv3_coord_16_t size_out,
                    const nv3_clip_16_t *clip, uint32_t color0, uint32_t color1, bool opaque)
{
    uint32_t             total = (uint32_t) size_in.x * size_in.y;
    uint32_t             mono  = nv3_render_expand_mono(data, grobj);
    nv3_color_expanded_t c0    = nv3_render_expand_color(color0, grobj);
    nv3_color_expanded_t c1    = nv3_render_expand_color(color1, grobj);

    if (!size_in.x)
        return;

    for (int i = 0; i < 32; i++) {
        uint32_t n = nv3->pgraph.win95_gdi_text_bit_count;
        if (n >= total)
            return;
        nv3->pgraph.win95_gdi_text_bit_count++;

        uint32_t rx = n % size_in.x;
        uint32_t ry = n / size_in.x;
        bool     on = (mono >> i) & 1;

        if (!on && !opaque)
            continue;
        if (rx >= size_out.x || ry >= size_out.y)
            continue;

        int32_t x = (int16_t) point.x + (int32_t) rx;
        int32_t y = (int16_t) point.y + (int32_t) ry;

        if (x < (int16_t) clip->left || y < (int16_t) clip->top || x >= (int16_t) clip->right || y >= (int16_t) clip->bottom)
            continue;

        nv3_render_pixel_ex(x, y, on ? c1 : c0, grobj, true);
    }
}

void
nv3_render_gdi_transparent_bitmap(bool clip, uint32_t color, uint32_t bitmap_data, nv3_grobj_t grobj)
{
    nv3_win95_text_t *t = &nv3->pgraph.win95_gdi_text;

    if (!clip)
        nv3_render_gdi_mono(bitmap_data, grobj, t->point_c, t->size_c, t->size_c, &t->clip_c, 0, color, false);
    else
        nv3_render_gdi_mono(bitmap_data, grobj, t->point_d, t->size_in_d, t->size_out_d, &t->clip_d, 0, color, false);
}

void
nv3_render_gdi_1bpp_bitmap(uint32_t color0, uint32_t color1, uint32_t bitmap_data, nv3_grobj_t grobj)
{
    nv3_win95_text_t *t = &nv3->pgraph.win95_gdi_text;

    nv3_render_gdi_mono(bitmap_data, grobj, t->point_e, t->size_in_e, t->size_out_e, &t->clip_e, color0, color1, true);
}

/* A line from (x0, y0) to (x1, y1). LIN (class 0x0A) leaves the last pixel
   out, LINE (0x09) draws it. */
void
nv3_render_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, nv3_color_expanded_t c, nv3_grobj_t grobj, bool last_pixel)
{
    int32_t dx  = abs(x1 - x0);
    int32_t dy  = -abs(y1 - y0);
    int32_t sx  = (x0 < x1) ? 1 : -1;
    int32_t sy  = (y0 < y1) ? 1 : -1;
    int32_t err = dx + dy;

    for (int guard = 0; guard < 0x10000; guard++) {
        bool at_end = (x0 == x1) && (y0 == y1);

        if (!at_end || last_pixel)
            nv3_render_pixel(x0, y0, c, grobj);
        if (at_end)
            break;

        int32_t e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* A filled triangle: pixel centres inside the edges, top-left fill rule */
void
nv3_render_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, nv3_color_expanded_t c, nv3_grobj_t grobj)
{
    int64_t area = (int64_t) (x1 - x0) * (y2 - y0) - (int64_t) (y1 - y0) * (x2 - x0);

    if (!area)
        return;
    /* make it counter-clockwise in screen space (y down): area > 0 */
    if (area < 0) {
        int32_t t = x1;
        x1        = x2;
        x2        = t;
        t         = y1;
        y1        = y2;
        y2        = t;
    }

    int32_t minx = MAX(MIN(x0, MIN(x1, x2)), 0);
    int32_t maxx = MIN(MAX(x0, MAX(x1, x2)), 4095);
    int32_t miny = MAX(MIN(y0, MIN(y1, y2)), 0);
    int32_t maxy = MIN(MAX(y0, MAX(y1, y2)), 16383);

    const int32_t ex[3][4] = {
        { x0, y0, x1, y1 },
        { x1, y1, x2, y2 },
        { x2, y2, x0, y0 },
    };

    for (int32_t y = miny; y <= maxy; y++) {
        for (int32_t x = minx; x <= maxx; x++) {
            bool inside = true;

            for (int e = 0; e < 3 && inside; e++) {
                int64_t ax = ex[e][0], ay = ex[e][1], bx = ex[e][2], by = ex[e][3];
                /* edge function at the pixel centre, doubled to stay integral */
                int64_t w = (bx - ax) * (2 * y + 1 - 2 * ay) - (by - ay) * (2 * x + 1 - 2 * ax);
                /* top or left edges own their pixels */
                bool top_left = (by == ay && bx < ax) || (by > ay);

                if (w < 0 || (w == 0 && !top_left))
                    inside = false;
            }
            if (inside)
                nv3_render_pixel(x, y, c, grobj);
        }
    }
}
