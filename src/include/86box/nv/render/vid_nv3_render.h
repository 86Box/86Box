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
 *
 *          Copyright 2024-2025 Connor Hyde
 */

#pragma once

/* Core */
void nv3_render_write_pixel(nv3_position_16_t position, uint32_t color, nv3_grobj_t grobj);
uint8_t nv3_render_read_pixel_8(nv3_position_16_t position, nv3_grobj_t grobj);
uint16_t nv3_render_read_pixel_16(nv3_position_16_t position, nv3_grobj_t grobj);
uint32_t nv3_render_read_pixel_32(nv3_position_16_t position, nv3_grobj_t grobj);

uint32_t nv3_render_to_chroma(nv3_color_expanded_t expanded);
nv3_color_expanded_t nv3_render_expand_color(uint32_t color, nv3_grobj_t grobj);            // Convert a colour to full RGB10 format from the current working format.
uint32_t nv3_render_downconvert_color(nv3_grobj_t grobj, nv3_color_expanded_t color);       // Convert a colour from the current working format to RGB10 format.

/* Pattern */
uint32_t nv3_render_set_pattern_color(nv3_color_expanded_t pattern_colour, bool use_color1);

/* Primitives */
void nv3_render_rect(nv3_position_16_t position, nv3_size_16_t size, uint32_t color, nv3_grobj_t grobj);

/* Chroma */
bool nv3_render_chroma_test(uint32_t color, nv3_grobj_t grobj);

/* Blit */
void nv3_render_blit_image(uint32_t color, nv3_grobj_t grobj);
void nv3_render_blit_screen2screen(nv3_grobj_t grobj);

/* GDI */
void nv3_render_gdi_type_d(nv3_grobj_t grobj, uint32_t param);                              /* GDI Type-D: Clipped 1bpp text */
void nv3_render_gdi_type_e(nv3_grobj_t grobj, uint32_t param);                              /* GDI Type-E: Clipped 1bpp two-colour text */