/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x06 (Pattern)
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

void nv3_class_006_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    switch (method_id)
    {
        /* Valid software method, suppress logging */
        case NV3_PATTERN_FORMAT:
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            break; 
        case NV3_PATTERN_SHAPE:
            /* 
            Technically you are meant to do this: 

            if (param > NV3_PATTERN_SHAPE_LAST_VALID)
            {
                nv_warning("NV3 class 0x06 (Pattern) invalid shape %d (This is a bug)", param);
                nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_INVALID_DATA);
                return; 
            }
            
            But in practice, the drivers always submit 0x20 or 0x40, which are valid when param & 0x03,
            and appear to be deliberate behaviour in the drivers rather than bugs. What

            */
            nv3->pgraph.pattern_shape = param & 0x03;

            break;
        case NV3_PATTERN_COLOR0:
        {
            nv3_color_expanded_t expanded_colour0 = nv3_render_expand_color(param, grobj);
            nv3_render_set_pattern_color(expanded_colour0, false);
            break;
        }
        case NV3_PATTERN_COLOR1:
        {
            nv3_color_expanded_t expanded_colour1 = nv3_render_expand_color(param, grobj);
            nv3_render_set_pattern_color(expanded_colour1, true);
            break;
        }
        /* 0x318 is pattern bits 31:0, 0x31c bits 63:32, in the object's mono order */
        case NV3_PATTERN_BITMAP_HIGH:
            nv3->pgraph.pattern_mono_bitmap[0] = nv3_render_expand_mono(param, grobj);
            break;
        case NV3_PATTERN_BITMAP_LOW:
            nv3->pgraph.pattern_mono_bitmap[1] = nv3_render_expand_mono(param, grobj);
            break;
        default:
            nv_warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            break;
    }
}