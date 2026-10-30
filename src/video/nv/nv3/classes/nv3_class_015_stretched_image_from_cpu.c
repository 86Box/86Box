/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x15 (stretched image from cpu to memory)
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

/* SIZE_IN (0x304), DX/DU (0x308) and DY/DV (0x30c) in 12.20, the clip
   rectangle's corner (0x310, xy16) and size (0x314), the destination point
   (0x318, 12.4 each) and the pixels from 0x400 */
void nv3_class_015_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    nv3_stretched_image_from_cpu_t *sifc = &nv3->pgraph.stretched_image_from_cpu;

    switch (method_id) {
        case NV3_STRETCH_SIZE_IN:
            sifc->size_in.x = param & 0xFFFF;
            sifc->size_in.y = param >> 16;
            break;
        case NV3_STRETCH_SIZE_DELTA_DX_DU:
            sifc->delta_dx_du = param;
            break;
        case NV3_STRETCH_SIZE_DELTA_DY_DV:
            sifc->delta_dy_dv = param;
            break;
        case NV3_STRETCH_SIZE_CLIP_0:
            sifc->clip_0.x = param & 0xFFFF;
            sifc->clip_0.y = param >> 16;
            break;
        case NV3_STRETCH_SIZE_CLIP_1:
            sifc->clip_1.x = param & 0xFFFF;
            sifc->clip_1.y = param >> 16;
            break;
        case NV3_STRETCH_SIZE_POINT12D4:
            sifc->point12d4 = param;
            nv3_render_sifc_start();
            break;
        default:
            if (method_id >= NV3_STRETCH_SIZE_COLOUR_START && method_id < NV3_STRETCH_SIZE_COLOUR_END) {
                nv3_render_sifc(param, grobj);
                break;
            }
            nv_warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            return;
    }
}
