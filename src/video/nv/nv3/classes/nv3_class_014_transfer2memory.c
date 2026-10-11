/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x14 (Transfer to Memory)
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

/* POINT (0x308, xy16) and SIZE (0x30c, wh16) of a rectangle of the source
   surface, the PITCH (0x310) of the copy in memory, and its OFFSET (0x314)
   in the object's DMA object, which starts the transfer */
void nv3_class_014_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    nv3_image_to_memory_t *itm = &nv3->pgraph.transfer2memory;

    switch (method_id) {
        case 0x0308:
            itm->point.x = param & 0xFFFF;
            itm->point.y = param >> 16;
            break;
        case 0x030C:
            itm->size.x = param & 0xFFFF;
            itm->size.y = param >> 16;
            break;
        case 0x0310:
            itm->image_pitch = param;
            break;
        case 0x0314:
        {
            uint32_t src_buffer = (grobj.grobj_0 >> NV3_PGRAPH_CTX_SWITCH_SRC_BUFFER) & 0x03;
            uint32_t cpp        = nv3_render_cpp(nv3->pgraph.bpixel[src_buffer]);
            uint32_t inst       = nv3->pgraph.dma_settings & 0xFFFF;

            itm->image_start = param;
            for (uint32_t y = 0; y < itm->size.y; y++) {
                for (uint32_t x = 0; x < itm->size.x; x++) {
                    uint32_t pixel = nv3_render_read_surface(src_buffer, (int16_t) itm->point.x + (int32_t) x, (int16_t) itm->point.y + (int32_t) y, cpp);
                    uint32_t out   = param + y * itm->image_pitch + x * cpp;

                    for (uint32_t b = 0; b < cpp; b++)
                        nv3_dma_write8(inst, out + b, pixel >> (b * 8));
                }
            }
            break;
        }
        default:
            nv_warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            return;
    }
}
