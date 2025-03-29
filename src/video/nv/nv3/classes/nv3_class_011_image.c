/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x11 (Color image)
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



void nv3_class_011_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    switch (method_id)
    {
        case NV3_IMAGE_START_POSITION:
            nv3->pgraph.image.point.x = (param & 0xFFFF);
            nv3->pgraph.image.point.y = (param >> 16);
            nv_log("Method Execution: Image Point=%d,%d\n", nv3->pgraph.image.point.x, nv3->pgraph.image.point.y);
            break; 
        /* Seems to allow scaling of the bitblt. */
        case NV3_IMAGE_SIZE:
            nv3->pgraph.image.size.w = (param & 0xFFFF);
            nv3->pgraph.image.size.h = (param >> 16);
            nv_log("Method Execution: Image Size (Clip)=%d,%d\n", nv3->pgraph.image.size.w, nv3->pgraph.image.size.h);
            break;
        case NV3_IMAGE_SIZE_IN:
            nv3->pgraph.image.size_in.w = (param & 0xFFFF);
            nv3->pgraph.image.size_in.h = (param >> 16);
            nv3->pgraph.image_current_position = nv3->pgraph.image.point;
            nv_log("Method Execution: Image SizeIn=%d,%d\n", nv3->pgraph.image.size_in.w, nv3->pgraph.image.size_in.h);
            break;
        default:
            if (method_id >= NV3_IMAGE_COLOR_START && method_id <= NV3_IMAGE_COLOR_END)
            {    
                uint32_t pixel_slot = (method_id - NV3_IMAGE_COLOR_START) >> 2;
                nv_log("Method Execution: Pixel%d Colour%08x Format%x\n", pixel_slot, param, (grobj.grobj_0) & 0x07);
                nv3_render_blit_image(param, grobj);
            }
            else
            {
                warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
                nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
                
            }
            return;
    }
}