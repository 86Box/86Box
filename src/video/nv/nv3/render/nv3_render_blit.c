/*
* 86Box    A hypervisor and IBM PC system emulator that specializes in
*          running old operating systems and software designed for IBM
*          PC systems and compatibles from 1981 through fairly recent
*          system designs based on the PCI bus.
*
*          This file is part of the 86Box distribution.
*
*          NV3 image transfers: image from CPU (0x11), bitmap from CPU
*          (0x12), stretched image from CPU (0x15) and screen to screen
*          blits (0x10).
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
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

/* The last few image transfers, for the debug dump */
#define NV3_IMAGE_TRACE 16
static struct {
    uint32_t cls, ctx, a, b, c, d, e;
} nv3_image_trace[NV3_IMAGE_TRACE];
static uint32_t nv3_image_trace_pos;

void
nv3_render_trace_image(uint32_t cls, uint32_t ctx, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    uint32_t i = nv3_image_trace_pos++ % NV3_IMAGE_TRACE;

    nv3_image_trace[i].cls = cls;
    nv3_image_trace[i].ctx = ctx;
    nv3_image_trace[i].a   = a;
    nv3_image_trace[i].b   = b;
    nv3_image_trace[i].c   = c;
    nv3_image_trace[i].d   = d;
    nv3_image_trace[i].e   = e;
}

void
nv3_render_dump_images(void)
{
    for (uint32_t n = 0; n < NV3_IMAGE_TRACE; n++) {
        uint32_t i = (nv3_image_trace_pos + n) % NV3_IMAGE_TRACE;
        if (!nv3_image_trace[i].cls)
            continue;
        always_log("nv3: image cls %02x ctx %08x %08x %08x %08x %08x %08x\n", nv3_image_trace[i].cls, nv3_image_trace[i].ctx,
                   nv3_image_trace[i].a, nv3_image_trace[i].b, nv3_image_trace[i].c, nv3_image_trace[i].d, nv3_image_trace[i].e);
    }
}

/* Pixels the CPU sends per 32-bit method for a colour format, and the
   width of each */
static uint32_t
nv3_render_pixels_per_word(nv3_grobj_t grobj, uint32_t *bits)
{
    switch (grobj.grobj_0 & 0x07) {
        case nv3_pgraph_pixel_format_y8:
            *bits = 8;
            return 4;
        case nv3_pgraph_pixel_format_r5g5b5:
        case nv3_pgraph_pixel_format_y16:
            *bits = 16;
            return 2;
        default:
            *bits = 32;
            return 1;
    }
}

/* Image from CPU (class 0x11): SIZE_IN pixels per row are sent, the first
   SIZE_OUT of them are drawn (the rest is padding) */
void
nv3_render_ifc_start(void)
{
    nv3->pgraph.image_pixel_count = 0;
    nv3_render_trace_image(0x11, nv3->pgraph.context_switch, *(uint32_t *) &nv3->pgraph.image.point,
                           *(uint32_t *) &nv3->pgraph.image.size, *(uint32_t *) &nv3->pgraph.image.size_in, 0, 0);
}

void
nv3_render_blit_image(uint32_t color, nv3_grobj_t grobj)
{
    nv3_image_t *img = &nv3->pgraph.image;
    uint32_t     bits;
    uint32_t     count = nv3_render_pixels_per_word(grobj, &bits);
    uint32_t     total = (uint32_t) img->size_in.x * img->size_in.y;

    if (!img->size_in.x)
        return;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t n = nv3->pgraph.image_pixel_count;
        if (n >= total)
            return;
        nv3->pgraph.image_pixel_count++;

        uint32_t rx = n % img->size_in.x;
        uint32_t ry = n / img->size_in.x;
        if (rx >= img->size.x || ry >= img->size.y)
            continue;

        uint32_t pixel = (bits == 32) ? color : ((color >> (i * bits)) & ((1u << bits) - 1));
        nv3_render_pixel((int16_t) img->point.x + (int32_t) rx, (int16_t) img->point.y + (int32_t) ry,
                         nv3_render_expand_color(pixel, grobj), grobj);
    }
}

/* Bitmap from CPU (class 0x12): 32 monochrome pixels per method, COLOR0 for
   0 bits and COLOR1 for 1 bits (a colour with zero alpha draws nothing).
   The data comes as SIZE_IN images side by side, as many as it takes to
   cover SIZE_OUT's width; once they are all sent, the next word starts
   over at POINT. The Windows 3.x driver draws text a glyph at a time this
   way: SIZE_IN 8x16 set once, then POINT and SIZE_OUT per glyph, a glyph
   wider than 8 pixels sent as two 8-pixel strips. The bits left in the
   word that ends the bitmap are padding. */
void
nv3_render_bitmap(uint32_t data, nv3_grobj_t grobj)
{
    nv3_bitmap_t *bmp    = &nv3->pgraph.bitmap;
    uint32_t      strip  = (uint32_t) bmp->size_in.x * bmp->size_in.y;
    uint32_t      strips = 1;
    uint32_t      mono   = nv3_render_expand_mono(data, grobj);

    if (!strip)
        return;
    if (bmp->size.x > bmp->size_in.x)
        strips = (bmp->size.x + bmp->size_in.x - 1) / bmp->size_in.x;

    uint32_t total = strip * strips;

    if (nv3->pgraph.image_pixel_count >= total)
        nv3->pgraph.image_pixel_count = 0;

    for (int i = 0; i < 32; i++) {
        uint32_t n = nv3->pgraph.image_pixel_count;
        if (n >= total)
            return;
        nv3->pgraph.image_pixel_count++;

        uint32_t rx = (n / strip) * bmp->size_in.x + (n % strip) % bmp->size_in.x;
        uint32_t ry = (n % strip) / bmp->size_in.x;
        if (rx >= bmp->size.x || ry >= bmp->size.y)
            continue;

        nv3_render_pixel((int16_t) bmp->point.x + (int32_t) rx, (int16_t) bmp->point.y + (int32_t) ry,
                         ((mono >> i) & 1) ? bmp->color_1 : bmp->color_0, grobj);
    }
}

/* Screen to screen blit (class 0x10): from the object's source surface to
   its destination surfaces. The source rectangle is read first, so
   overlapping copies come out as if done in the right direction. */
void
nv3_render_blit_screen2screen(nv3_grobj_t grobj)
{
    nv3_blit_t *blit       = &nv3->pgraph.blit;
    int32_t     w          = blit->size.x;
    int32_t     h          = blit->size.y;
    int32_t     sx         = (int16_t) blit->point_in.x;
    int32_t     sy         = (int16_t) blit->point_in.y;
    int32_t     dx         = (int16_t) blit->point_out.x;
    int32_t     dy         = (int16_t) blit->point_out.y;
    uint32_t    src_buffer = (grobj.grobj_0 >> NV3_PGRAPH_CTX_SWITCH_SRC_BUFFER) & 0x03;
    uint32_t    fmt        = nv3_render_surface_format(grobj);
    uint32_t    cpp        = nv3_render_cpp(fmt);

    if (w <= 0 || h <= 0)
        return;

    uint32_t *src = malloc((size_t) w * h * sizeof(uint32_t));
    if (!src)
        return;

    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            int32_t px = sx + x;
            int32_t py = sy + y;
            src[y * w + x] = (px < 0 || py < 0) ? 0 : nv3_render_read_surface(src_buffer, px, py, cpp);
        }
    }

    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++)
            nv3_render_pixel(dx + x, dy + y, nv3_render_expand_surface(fmt, src[y * w + x]), grobj);
    }

    free(src);
}

/* Stretched image from CPU (class 0x15): SIZE_IN source pixels, each
   covering DX/DU by DY/DV destination pixels (12.20 fixed point) from POINT
   (12.4), clipped by the class's own clip rectangle */
void
nv3_render_sifc_start(void)
{
    nv3_stretched_image_from_cpu_t *sifc = &nv3->pgraph.stretched_image_from_cpu;

    nv3->pgraph.image_pixel_count = 0;
    nv3_render_trace_image(0x15, nv3->pgraph.context_switch, *(uint32_t *) &sifc->size_in, sifc->delta_dx_du, sifc->delta_dy_dv,
                           *(uint32_t *) &sifc->clip_0, sifc->point12d4);
}

static int32_t
nv3_render_sifc_first_pixel(int64_t edge)
{
    /* first pixel whose centre (p + 0.5) is at or past edge (20-bit fraction) */
    return (int32_t) ((edge - (1 << 19) + (1 << 20) - 1) >> 20);
}

void
nv3_render_sifc(uint32_t color, nv3_grobj_t grobj)
{
    nv3_stretched_image_from_cpu_t *sifc = &nv3->pgraph.stretched_image_from_cpu;
    uint32_t                        bits;
    uint32_t                        count = nv3_render_pixels_per_word(grobj, &bits);
    uint32_t                        total = (uint32_t) sifc->size_in.x * sifc->size_in.y;
    int64_t                         x0    = (int64_t) (int16_t) (sifc->point12d4 & 0xFFFF) << 16;
    int64_t                         y0    = (int64_t) (int16_t) (sifc->point12d4 >> 16) << 16;
    int32_t                         cx0   = (int16_t) sifc->clip_0.x;
    int32_t                         cy0   = (int16_t) sifc->clip_0.y;
    int32_t                         cx1   = cx0 + sifc->clip_1.x;
    int32_t                         cy1   = cy0 + sifc->clip_1.y;

    if (!sifc->size_in.x)
        return;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t n = nv3->pgraph.image_pixel_count;
        if (n >= total)
            return;
        nv3->pgraph.image_pixel_count++;

        uint32_t u     = n % sifc->size_in.x;
        uint32_t v     = n / sifc->size_in.x;
        uint32_t pixel = (bits == 32) ? color : ((color >> (i * bits)) & ((1u << bits) - 1));

        int32_t xa = nv3_render_sifc_first_pixel(x0 + (int64_t) u * sifc->delta_dx_du);
        int32_t xb = nv3_render_sifc_first_pixel(x0 + (int64_t) (u + 1) * sifc->delta_dx_du);
        int32_t ya = nv3_render_sifc_first_pixel(y0 + (int64_t) v * sifc->delta_dy_dv);
        int32_t yb = nv3_render_sifc_first_pixel(y0 + (int64_t) (v + 1) * sifc->delta_dy_dv);

        nv3_color_expanded_t c = nv3_render_expand_color(pixel, grobj);
        for (int32_t y = MAX(ya, cy0); y < MIN(yb, cy1); y++) {
            for (int32_t x = MAX(xa, cx0); x < MIN(xb, cx1); x++)
                nv3_render_pixel_ex(x, y, c, grobj, true);
        }
    }
}
