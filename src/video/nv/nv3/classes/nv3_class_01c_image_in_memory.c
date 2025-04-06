/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x1C (Image in memory)
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

void nv3_class_01c_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    /* We need this for a lot of methods, so may as well store it here. */
    uint32_t src_buffer_id = (grobj.grobj_0 >> NV3_PGRAPH_CONTEXT_SWITCH_SRC_BUFFER) & 0x03;

    switch (method_id)
    {
        /* Color format of the image */
        case NV3_IMAGE_IN_MEMORY_COLOR_FORMAT:
            // convert to how the bpixel registers represent surface 
            uint32_t real_format = 1; 

            /* TODO: THIS CODE MIGHT BE NONSENSE
                Convert between different internal representations of the pixel format, because Nvidia says: I WANT TO MAKE YOUR LIFE PAIN.
            */
            switch (param)
            {
                case nv3_image_in_memory_pixel_format_x8g8b8r8:
                    real_format = 3; //32bit
                    // no change
                    break; 
                case nv3_image_in_memory_pixel_format_x1r5g5b5_p2:
                    real_format = 2; 
                    break;
                case nv3_image_in_memory_pixel_format_le_y16_p2:
                    real_format = 0;
                    break;
            }

            /* Set the format */
            
            nv3->pgraph.bpixel[src_buffer_id] = (real_format | NV3_BPIXEL_FORMAT_IS_VALID);
            
            nv_log("Method Execution: Image in Memory BUF%d COLOR_FORMAT=0x%04x\n", src_buffer_id, param);

            break;
        /* DOn't log invalid */
        case NV3_IMAGE_IN_MEMORY_IN_MEMORY_DMA_CTX_TYPE:
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            break; 
        /* Pitch - length between scanlines */
        case NV3_IMAGE_IN_MEMORY_PITCH:
            nv3->pgraph.image_in_memory.pitch = param & 0x1FF0; 
            nv3->pgraph.bpitch[src_buffer_id] = param & 0x1FF0; // 12:0

            nv_log("Method Execution: Image in Memory BUF%d PITCH=0x%04x\n", src_buffer_id, nv3->pgraph.bpitch[src_buffer_id]);
            break;  
        /* Byte offset in GPU VRAM of top left pixel (22:0) */
        case NV3_IMAGE_IN_MEMORY_TOP_LEFT_OFFSET:
            if (nv3->nvbase.gpu_revision == NV3_PCI_CFG_REVISION_C00) // RIVA 128ZX
                nv3->pgraph.boffset[src_buffer_id] = param & 0x7FFFFF;
            else
                nv3->pgraph.boffset[src_buffer_id] = param & 0x3FFFFF;
                
            nv3->nvbase.last_buffer_address = nv3->pgraph.boffset[src_buffer_id];
            
            nv_log("Method Execution: Image in Memory BUF%d TOP_LEFT_OFFSET=0x%08x\n", src_buffer_id, nv3->pgraph.boffset[src_buffer_id]);
            break;
        default:
            warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            break;
    }
}