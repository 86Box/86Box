/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x09 (Line)
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

/* Line drawing state, one per class: [0] LINE (0x09), [1] LIN (0x0A) */
/* per-channel primitive state (nv3_prim_state_t): [0] LINE, [1] LIN */
#define nv3_line_state (nv3->pgraph.prim.line)

static void
nv3_line_to(nv3_line_state_t *st, int32_t x, int32_t y, uint32_t color, nv3_grobj_t grobj, bool last_pixel)
{
    nv3_render_line(st->lx, st->ly, x, y, nv3_render_expand_color(color, grobj), grobj, last_pixel);
    st->lx = x;
    st->ly = y;
}

/* LINE (0x400, pairs of xy16), LINE32 (0x480, x0 y0 x1 y1), POLYLINE
   (0x500, xy16), POLYLINE32 (0x580, x y) and CPOLYLINE (0x600, colour xy16).
   LIN leaves each line's last pixel out. */
void
nv3_line_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj, bool lin)
{
    nv3_line_state_t *st = &nv3_line_state[lin];
    int32_t           x  = (int16_t) (param & 0xFFFF);
    int32_t           y  = (int16_t) (param >> 16);

    if (method_id == 0x0304)
        st->color = param;
    else if (method_id >= 0x0400 && method_id < 0x0480) {
        if (!(method_id & 4)) {
            st->lx = x;
            st->ly = y;
        } else
            nv3_line_to(st, x, y, st->color, grobj, !lin);
    } else if (method_id >= 0x0480 && method_id < 0x0500) {
        uint32_t idx = (method_id >> 2) & 3;
        if (idx < 3)
            st->x32[idx] = (int32_t) param;
        else {
            st->lx = st->x32[0];
            st->ly = st->x32[1];
            nv3_line_to(st, st->x32[2], (int32_t) param, st->color, grobj, !lin);
        }
    } else if (method_id >= 0x0500 && method_id < 0x0580)
        nv3_line_to(st, x, y, st->color, grobj, !lin);
    else if (method_id >= 0x0580 && method_id < 0x0600) {
        if (!(method_id & 4))
            st->x32[0] = (int32_t) param;
        else
            nv3_line_to(st, st->x32[0], (int32_t) param, st->color, grobj, !lin);
    } else if (method_id >= 0x0600 && method_id < 0x0680) {
        if (!(method_id & 4))
            st->cpoly_color = param;
        else
            nv3_line_to(st, x, y, st->cpoly_color, grobj, !lin);
    } else {
        nv_warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
        nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
    }
}

void nv3_class_009_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    nv3_line_method(param, method_id, context, grobj, false);
}
