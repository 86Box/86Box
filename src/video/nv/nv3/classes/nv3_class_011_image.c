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

/* Check the line bounds */
void nv3_class_011_check_line_bounds()
{                
    uint32_t relative_x = nv3->pgraph.image_current_position.x - nv3->pgraph.image.point.x;
    //uint32_t relative_y = nv3->pgraph.image_current_position.y - nv3->pgraph.image.point.y;

    /* In theory, relative_y should never be exceeded...because it only submits enough pixels to render the image*/
    if (relative_x >= nv3->pgraph.image.size_in.w)
    {   
        nv3->pgraph.image_current_position.y++;
        nv3->pgraph.image_current_position.x = nv3->pgraph.image.point.x;
    }
}

void nv3_class_011_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    switch (method_id)
    {
        case NV3_IMAGE_START_POSITION:
            nv3->pgraph.image.point.x = (param & 0xFFFF);
            nv3->pgraph.image.point.y = (param >> 16);
            nv_log("Image Point=%d,%d", nv3->pgraph.image.point.x, nv3->pgraph.image.point.y);
            break; 
        /* Seems to allow scaling of the bitblt. */
        case NV3_IMAGE_SIZE:
            nv3->pgraph.image.size.w = (param & 0xFFFF);
            nv3->pgraph.image.size.h = (param >> 16);
            break;
        case NV3_IMAGE_SIZE_IN:
            nv3->pgraph.image.size_in.w = (param & 0xFFFF);
            nv3->pgraph.image.size_in.h = (param >> 16);
            nv3->pgraph.image_current_position = nv3->pgraph.image.point;
            break;
        default:
            if (method_id >= NV3_IMAGE_COLOR_START && method_id <= NV3_IMAGE_COLOR_END)
            {
                // shift left by 2 because it's 4 bits per si\e..
                uint32_t pixel_slot = (method_id - NV3_IMAGE_COLOR_START) >> 2;
                uint32_t current_buffer = (nv3->pgraph.context_switch >> NV3_PGRAPH_CONTEXT_SWITCH_SRC_BUFFER) & 0x03; 
                
                /* todo: a lot of stuff */

                uint32_t pixel0 = 0, pixel1 = 0, pixel2 = 0, pixel3 = 0;

                /* we need to unpack them - IF THIS IS USED SOMEWHERE ELSE, DO SOMETHING ELSE WITH IT */
                /* the reverse order is due to the endianness */
                switch (nv3->nvbase.svga.bpp)
                {
                    // 4pixels packed into one param
                    case 8:
                    
                        //pixel3
                        pixel3 = param & 0xFF;
                        nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel3, grobj);
                        nv3->pgraph.image_current_position.x++;
                        nv3_class_011_check_line_bounds();

                        pixel2 = (param >> 8) & 0xFF;
                        nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel2, grobj);
                        nv3->pgraph.image_current_position.x++;
                        nv3_class_011_check_line_bounds();
                        
                        pixel1 = (param >> 16) & 0xFF;
                        nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel1, grobj);
                        nv3->pgraph.image_current_position.x++;
                        nv3_class_011_check_line_bounds();

                        pixel0 = (param >> 24) & 0xFF;
                        nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel0, grobj);
                        nv3->pgraph.image_current_position.x++;
                        nv3_class_011_check_line_bounds();

                        break;
                    //2pixels packed into one param
                    case 16:
                        pixel1 = (param) & 0xFFFF;
                        nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel1, grobj);
                        nv3->pgraph.image_current_position.x++;
                        nv3_class_011_check_line_bounds();

                        pixel0 = (param >> 16) & 0xFFFF;
                        nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel0, grobj);
                        nv3->pgraph.image_current_position.x++;
                        nv3_class_011_check_line_bounds();
                            
                        break;
                    // just one pixel in 32bpp
                    case 32: 
                        pixel0 = param;
                        nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel0, grobj);
                        nv3->pgraph.image_current_position.x++;
                        nv3_class_011_check_line_bounds();

                        break;
                }
            }
            else
            {
                nv_log("%s: Invalid or Unimplemented method 0x%04x", nv3_class_names[context.class_id & 0x1F], method_id);
                nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
                
            }
            return;
    }
}