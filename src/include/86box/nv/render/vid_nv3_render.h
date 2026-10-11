/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 headers for rendering 
 *
 * 
 * 
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2026 Connor Hyde
 *          Copyright 2026 Marcos Alves
 */

#pragma once

/* Pixel pipeline */
nv3_color_expanded_t nv3_render_expand_color(uint32_t color, nv3_grobj_t grobj);            // A colour in the object's format, expanded to R10G10B10 + A8
nv3_color_expanded_t nv3_render_expand_surface(uint32_t fmt, uint32_t pixel);               // A surface pixel, expanded
uint32_t nv3_render_expand_mono(uint32_t mono, nv3_grobj_t grobj);                          // Mono data in the object's bit order (pixel n = bit n)
uint32_t nv3_render_to_chroma(nv3_color_expanded_t expanded);                               // A1R10G10B10 for the chroma key register
uint32_t nv3_render_get_palette_index(uint8_t index);                                       // A CLUT entry as RGB888 with 0xFF alpha
void     nv3_render_set_pattern_color(nv3_color_expanded_t pattern_colour, bool use_color1);
uint32_t nv3_render_surface_format(nv3_grobj_t grobj);                                      // BPIXEL of the first destination surface
uint32_t nv3_render_cpp(uint32_t fmt);
uint32_t nv3_render_read_surface(uint32_t buffer, int32_t x, int32_t y, uint32_t cpp);
bool     nv3_render_clip_pass(int32_t x, int32_t y, nv3_grobj_t grobj, bool own_clip);
uint8_t  nv3_render_translate_nvrop(nv3_grobj_t grobj, uint32_t rop);
void     nv3_render_pixel(int32_t x, int32_t y, nv3_color_expanded_t s, nv3_grobj_t grobj);
void     nv3_render_pixel_ex(int32_t x, int32_t y, nv3_color_expanded_t s, nv3_grobj_t grobj, bool own_clip);
void     nv3_render_write_pixel(nv3_coord_16_t position, uint32_t color, nv3_grobj_t grobj);

/* Primitives */
void nv3_render_fill(int32_t x0, int32_t y0, int32_t x1, int32_t y1, nv3_color_expanded_t c, nv3_grobj_t grobj, bool own_clip);
void nv3_render_rect(nv3_coord_16_t position, nv3_coord_16_t size, uint32_t color, nv3_grobj_t grobj);        // Class 0x07 rectangle
void nv3_render_gdi_rect(nv3_coord_16_t position, nv3_coord_16_t size, uint32_t color, nv3_grobj_t grobj);    // GDI type A
void nv3_render_rect_clipped(nv3_clip_16_t clip, uint32_t color, nv3_grobj_t grobj);                           // GDI type B
void nv3_render_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, nv3_color_expanded_t c, nv3_grobj_t grobj, bool last_pixel);
void nv3_render_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, nv3_color_expanded_t c, nv3_grobj_t grobj);

/* Images */
void nv3_render_ifc_start(void);
void nv3_render_blit_image(uint32_t color, nv3_grobj_t grobj);
void nv3_render_bitmap(uint32_t data, nv3_grobj_t grobj);
void nv3_render_blit_screen2screen(nv3_grobj_t grobj);
void nv3_render_sifc_start(void);
void nv3_render_sifc(uint32_t color, nv3_grobj_t grobj);
void nv3_render_dump_images(void);
void nv3_render_trace_image(uint32_t cls, uint32_t ctx, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);

/* GDI */
void nv3_render_gdi_transparent_bitmap(bool clip, uint32_t color, uint32_t bitmap_data, nv3_grobj_t grobj);
void nv3_render_gdi_1bpp_bitmap(uint32_t color0, uint32_t color1, uint32_t bitmap_data, nv3_grobj_t grobj); /* GDI Type-E: Clipped 1bpp colour-expanded bitmap */

/* DMA */
bool nv3_perform_dma_m2mf(nv3_grobj_t grobj);
