/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x0C (Windows 95 GDI text acceleration)
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

void nv3_class_00c_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    switch (method_id)
    {
        /* Type A: Unclipped Rectangle */

        /* NOTE: This method is used by the GDI driver as part of the notification engine. */
        case NV3_W95TXT_A_COLOR:
            nv3->pgraph.win95_gdi_text.color_a = param;
            break;
        default:
            /* These are the same things as rectangles */
            if (method_id >= NV3_W95TXT_A_RECT_START && method_id <= NV3_W95TXT_A_RECT_END)
            {
                uint32_t index = (method_id - NV3_RECTANGLE_START) >> 3;

                // If the size is submitted, render it.
                if (method_id & 0x04)
                {
                    nv3->pgraph.win95_gdi_text.rect_a_size[index].w = param & 0xFFFF;
                    nv3->pgraph.win95_gdi_text.rect_a_size[index].h = (param >> 16) & 0xFFFF;
                    
                    nv_log("Rect GDI-A%d Size=%d,%d Color=0x%08x\n", index, nv3->pgraph.win95_gdi_text.rect_a_size[index].w, 
                        nv3->pgraph.win95_gdi_text.rect_a_size[index].h, nv3->pgraph.win95_gdi_text.color_a);

                    nv3_render_rect(nv3->pgraph.win95_gdi_text.rect_a_position[index], 
                        nv3->pgraph.win95_gdi_text.rect_a_size[index], nv3->pgraph.win95_gdi_text.color_a, grobj);
                }
                else // position
                {
                    nv3->pgraph.win95_gdi_text.rect_a_position[index].x = param & 0xFFFF;
                    nv3->pgraph.win95_gdi_text.rect_a_position[index].y = (param >> 16) & 0xFFFF;
                    
                    nv_log("Rect GDI-A%d Position=%d,%d\n", index,
                         nv3->pgraph.win95_gdi_text.rect_a_position[index].x, nv3->pgraph.win95_gdi_text.rect_a_position[index].y);
                }
                return;
            }

            nv_log("%s: Invalid or Unimplemented method 0x%04x", nv3_class_names[context.class_id & 0x1F], method_id);
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            break;
    }
}