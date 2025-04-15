/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x10 (Blit something)
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *
 *          Copyright 2024-2025 Connor Hyde
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

void nv3_class_010_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    switch (method_id)
    {
        case NV3_BLIT_POSITION_IN:
            nv3->pgraph.blit.point_in.x = (param & 0xFFFF);
            nv3->pgraph.blit.point_in.y = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: S2SB POINT_IN %d,%d\n", nv3->pgraph.blit.point_in.x, nv3->pgraph.blit.point_in.y);
            break;
        case NV3_BLIT_POSITION_OUT:
            nv3->pgraph.blit.point_out.x = (param & 0xFFFF);
            nv3->pgraph.blit.point_out.y = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: S2SB POINT_OUT %d,%d\n", nv3->pgraph.blit.point_out.x, nv3->pgraph.blit.point_out.y);

            break; 
        case NV3_BLIT_SIZE:
            /* This is the last one*/
            nv3->pgraph.blit.size.w = (param & 0xFFFF);
            nv3->pgraph.blit.size.h = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: S2SB Size %d,%d grobj_0=0x%08x\n", nv3->pgraph.blit.size.w, nv3->pgraph.blit.size.h, grobj.grobj_0);

            /* Some of these seem to have sizes of 0, so skip */
            if (nv3->pgraph.blit.size.h == 0
            && nv3->pgraph.blit.size.w == 0)
                return;

            nv3_render_blit_screen2screen(grobj);

            break; 
        default:
            warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            return;
    }
}