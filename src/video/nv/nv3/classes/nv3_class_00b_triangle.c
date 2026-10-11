/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x0B (Basic triangle)
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
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

/* per-channel primitive state (nv3_prim_state_t) */
#define nv3_tri_color  (nv3->pgraph.prim.tri_color)
#define nv3_tri_ccolor (nv3->pgraph.prim.tri_ccolor)
#define nv3_tri_x      (nv3->pgraph.prim.tri_x)
#define nv3_tri_y      (nv3->pgraph.prim.tri_y)
#define nv3_mesh_x     (nv3->pgraph.prim.mesh_x)
#define nv3_mesh_y     (nv3->pgraph.prim.mesh_y)
#define nv3_mesh_count (nv3->pgraph.prim.mesh_count)
#define nv3_tri32      (nv3->pgraph.prim.tri32)

static void
nv3_tri_mesh_add(int32_t x, int32_t y, uint32_t color, nv3_grobj_t grobj)
{
    if (nv3_mesh_count >= 2)
        nv3_render_triangle(nv3_mesh_x[0], nv3_mesh_y[0], nv3_mesh_x[1], nv3_mesh_y[1], x, y, nv3_render_expand_color(color, grobj), grobj);
    nv3_mesh_x[0] = nv3_mesh_x[1];
    nv3_mesh_y[0] = nv3_mesh_y[1];
    nv3_mesh_x[1] = x;
    nv3_mesh_y[1] = y;
    nv3_mesh_count++;
}

/* COLOR, TRIANGLE (0x310: three xy16), TRIANGLE32 (0x320: x0 y0 x1 y1 x2 y2),
   TRIMESH (0x400, xy16), TRIMESH32 (0x480, x y), CTRIANGLE (0x500: colour
   and three xy16) and CTRIMESH (0x580: colour, xy16). A mesh vertex makes a
   triangle with the two before it; a single triangle starts a new mesh. */
void nv3_class_00b_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    int32_t x = (int16_t) (param & 0xFFFF);
    int32_t y = (int16_t) (param >> 16);

    if (method_id == 0x0304)
        nv3_tri_color = param;
    else if (method_id >= 0x0310 && method_id <= 0x0318) {
        uint32_t v = (method_id - 0x0310) >> 2;
        nv3_tri_x[v] = x;
        nv3_tri_y[v] = y;
        nv3_mesh_count = 0;
        nv3_tri_mesh_add(x, y, nv3_tri_color, grobj);
        if (v == 2)
            nv3_render_triangle(nv3_tri_x[0], nv3_tri_y[0], nv3_tri_x[1], nv3_tri_y[1], x, y, nv3_render_expand_color(nv3_tri_color, grobj), grobj);
    } else if (method_id >= 0x0320 && method_id <= 0x0334) {
        uint32_t i = (method_id - 0x0320) >> 2;
        if (i < 5)
            nv3_tri32[i] = (int32_t) param;
        else
            nv3_render_triangle(nv3_tri32[0], nv3_tri32[1], nv3_tri32[2], nv3_tri32[3], nv3_tri32[4], (int32_t) param,
                                nv3_render_expand_color(nv3_tri_color, grobj), grobj);
    } else if (method_id >= 0x0400 && method_id < 0x0480)
        nv3_tri_mesh_add(x, y, nv3_tri_color, grobj);
    else if (method_id >= 0x0480 && method_id < 0x0500) {
        if (!(method_id & 4))
            nv3_tri32[0] = (int32_t) param;
        else
            nv3_tri_mesh_add(nv3_tri32[0], (int32_t) param, nv3_tri_color, grobj);
    } else if (method_id >= 0x0500 && method_id < 0x0580) {
        uint32_t i = (method_id >> 2) & 3;
        if (i == 0)
            nv3_tri_ccolor = param;
        else {
            nv3_tri_x[i - 1] = x;
            nv3_tri_y[i - 1] = y;
            if (i == 3)
                nv3_render_triangle(nv3_tri_x[0], nv3_tri_y[0], nv3_tri_x[1], nv3_tri_y[1], x, y, nv3_render_expand_color(nv3_tri_ccolor, grobj), grobj);
        }
    } else if (method_id >= 0x0580 && method_id < 0x0600) {
        if (!(method_id & 4))
            nv3_tri_ccolor = param;
        else
            nv3_tri_mesh_add(x, y, nv3_tri_ccolor, grobj);
    } else {
        nv_warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
        nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
    }
}
