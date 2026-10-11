/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x08 (Point)
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
#define nv3_point_color  (nv3->pgraph.prim.point_color)
#define nv3_point32_x    (nv3->pgraph.prim.point32_x)
#define nv3_cpoint_color (nv3->pgraph.prim.cpoint_color)

void nv3_class_008_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    /* COLOR, then POINT (0x400, xy16), POINT32 (0x480, x then y) or CPOINT (0x500, colour then xy16) */
    if (method_id == 0x0304)
        nv3_point_color = param;
    else if (method_id >= 0x0400 && method_id < 0x0480)
        nv3_render_pixel((int16_t) (param & 0xFFFF), (int16_t) (param >> 16), nv3_render_expand_color(nv3_point_color, grobj), grobj);
    else if (method_id >= 0x0480 && method_id < 0x0500) {
        if (!(method_id & 4))
            nv3_point32_x = (int32_t) param;
        else
            nv3_render_pixel(nv3_point32_x, (int32_t) param, nv3_render_expand_color(nv3_point_color, grobj), grobj);
    } else if (method_id >= 0x0500 && method_id < 0x0580) {
        if (!(method_id & 4))
            nv3_cpoint_color = param;
        else
            nv3_render_pixel((int16_t) (param & 0xFFFF), (int16_t) (param >> 16), nv3_render_expand_color(nv3_cpoint_color, grobj), grobj);
    } else {
        nv_warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
        nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
    }
}
