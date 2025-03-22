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
void nv3_render_pixel(nv3_position_16_t position, uint32_t color, nv3_grobj_t grobj);
uint32_t nv3_render_to_chroma(nv3_color_expanded_t expanded);
nv3_color_expanded_t nv3_render_expand_color(nv3_grobj_t grobj, uint32_t color);

/* Primitives */
void nv3_render_rect(nv3_position_16_t position, nv3_size_16_t size, uint32_t color, nv3_grobj_t grobj);

void nv3_render_chroma_test(nv3_grobj_t grobj);