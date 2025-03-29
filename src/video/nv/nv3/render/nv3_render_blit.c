/*
* 86Box    A hypervisor and IBM PC system emulator that specializes in
*          running old operating systems and software designed for IBM
*          PC systems and compatibles from 1981 through fairly recent
*          system designs based on the PCI bus.
*
*          This file is part of the 86Box distribution.
*
*          NV3 Core rendering code (Software version)
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
#include <86box/plat.h>
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

/* Renders an image from cpu */
void nv3_render_blit_image(uint32_t color, nv3_grobj_t grobj)
{
    // shift left by 2 because it's 4 bits per size..
    uint32_t current_buffer = (nv3->pgraph.context_switch >> NV3_PGRAPH_CONTEXT_SWITCH_SRC_BUFFER) & 0x03; 

    /* todo: a lot of stuff */

    uint32_t pixel0 = 0, pixel1 = 0, pixel2 = 0, pixel3 = 0;

    /* Some extra data is sent as padding, we need to clip it off using size_out */

    uint16_t clip_x = nv3->pgraph.image.point.x + nv3->pgraph.image.size.w;
    /* we need to unpack them - IF THIS IS USED SOMEWHERE ELSE, DO SOMETHING ELSE WITH IT */
    /* the reverse order is due to the endianness */
    switch (nv3->nvbase.svga.bpp)
    {
        // 4pixels packed into one color
        case 8:
        
            //pixel3
            pixel3 = color & 0xFF;
            if (nv3->pgraph.image_current_position.x < clip_x) nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel3, grobj);
            nv3->pgraph.image_current_position.x++;
            nv3_class_011_check_line_bounds();

            pixel2 = (color >> 8) & 0xFF;
            if (nv3->pgraph.image_current_position.x < clip_x) nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel2, grobj);
            nv3->pgraph.image_current_position.x++;
            nv3_class_011_check_line_bounds();
            
            pixel1 = (color >> 16) & 0xFF;
            if (nv3->pgraph.image_current_position.x < clip_x) nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel1, grobj);
            nv3->pgraph.image_current_position.x++;
            nv3_class_011_check_line_bounds();

            pixel0 = (color >> 24) & 0xFF;
            if (nv3->pgraph.image_current_position.x < clip_x) nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel0, grobj);
            nv3->pgraph.image_current_position.x++;
            nv3_class_011_check_line_bounds();

            break;
        //2pixels packed into one color
        case 15:
        case 16:
            pixel1 = (color) & 0xFFFF;
            if (nv3->pgraph.image_current_position.x < (clip_x)) nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel1, grobj);
            nv3->pgraph.image_current_position.x++;
            nv3_class_011_check_line_bounds();

            pixel0 = (color >> 16) & 0xFFFF;
            if (nv3->pgraph.image_current_position.x < (clip_x)) nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel0, grobj);
            nv3->pgraph.image_current_position.x++;
            nv3_class_011_check_line_bounds();
                
            break;
        // just one pixel in 32bpp
        case 32: 
            pixel0 = color;
            if (nv3->pgraph.image_current_position.x < clip_x) nv3_render_write_pixel(nv3->pgraph.image_current_position, pixel0, grobj);
            nv3->pgraph.image_current_position.x++;
            nv3_class_011_check_line_bounds();

            break;
    }
                
}

void nv3_render_blit_screen2screen(nv3_grobj_t grobj)
{
    //nv3_position_16_t old_position = nv3->pgraph.blit.point_in + nv3->pgraph.blit.size.w;
    nv3_position_16_t old_position = {0};
    old_position.x = nv3->pgraph.blit.point_in.x + nv3->pgraph.blit.size.w;
    old_position.y = nv3->pgraph.blit.point_in.y + nv3->pgraph.blit.size.h;
    nv3_position_16_t new_position = nv3->pgraph.blit.point_out;

    uint16_t end_x = (nv3->pgraph.blit.point_out.x + nv3->pgraph.blit.size.w);
    uint16_t end_y = (nv3->pgraph.blit.point_out.y + nv3->pgraph.blit.size.h);

    /* Read the old pixel */
    switch (nv3->nvbase.svga.bpp)
    {
        case 8: //8bpp
            for (int32_t y = nv3->pgraph.blit.point_out.y; y < end_y; y++)
            {
                old_position.y++;
                new_position.y++;

                for (int32_t x = nv3->pgraph.blit.point_out.x; x < end_x; x++)
                {
                    old_position.x++;
                    new_position.x++;

                    uint32_t pixel_to_copy = nv3_render_read_pixel_8(old_position, grobj) & 0xFF;
                    nv3_render_write_pixel(new_position, pixel_to_copy, grobj);
                }
            }
            break;
        case 15:
        case 16: //16bpp
            for (int32_t y = nv3->pgraph.blit.point_out.y; y < end_y; y++)
            {
                old_position.y++;
                new_position.y++;

                for (int32_t x = nv3->pgraph.blit.point_out.x; x >= end_x; x++)
                {
                    old_position.x++;
                    new_position.x++;

                    uint32_t pixel_to_copy = nv3_render_read_pixel_16(old_position, grobj) & 0xFFFF;
                    nv3_render_write_pixel(new_position, pixel_to_copy, grobj);
                }
            }
            break;
        case 32: //32bpp
            for (int32_t y = nv3->pgraph.blit.point_out.y; y < end_y; y++)
            {
                old_position.y++;
                new_position.y++;

                for (int32_t x = nv3->pgraph.blit.point_out.x; x >= end_x; x++)
                {
                    old_position.x++;
                    new_position.x++;

                    uint32_t pixel_to_copy = nv3_render_read_pixel_32(old_position, grobj);
                    nv3_render_write_pixel(new_position, pixel_to_copy, grobj);
                }
            }
            break;
    }
}