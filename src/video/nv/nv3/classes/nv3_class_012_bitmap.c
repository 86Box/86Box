/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x12 (Monochrome bitmap)
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

/* COLOR0 (0x308), COLOR1 (0x30c), POINT (0x310, xy16), SIZE_OUT (0x314),
   SIZE_IN (0x318), then the monochrome data from 0x400 */
void nv3_class_012_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    nv3_bitmap_t *bmp = &nv3->pgraph.bitmap;

    switch (method_id) {
        case 0x0308:
            bmp->color_0 = nv3_render_expand_color(param, grobj);
            break;
        case 0x030C:
            bmp->color_1 = nv3_render_expand_color(param, grobj);
            break;
        case 0x0310:
            bmp->point.x = param & 0xFFFF;
            bmp->point.y = param >> 16;
            /* a new point starts a new image */
            nv3->pgraph.image_pixel_count = 0;
            break;
        case 0x0314:
            bmp->size.x = param & 0xFFFF;
            bmp->size.y = param >> 16;
            break;
        case 0x0318:
            bmp->size_in.x = param & 0xFFFF;
            bmp->size_in.y = param >> 16;
            nv3->pgraph.image_pixel_count = 0;
            break;
        default:
            if (method_id >= 0x0400 && method_id < 0x0480) {
                nv3_render_bitmap(param, grobj);
                break;
            }
            nv_warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            return;
    }
}
