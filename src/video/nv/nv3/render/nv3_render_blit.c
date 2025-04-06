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


#define NV3_MAX_HORIZONTAL_SIZE     1920
#define NV3_MAX_VERTICAL_SIZE       1200

/* 1920 for margin. Holds a buffer of the old screen we want to hold so we don't overwrite things we already overwtote
We only need to clear it once per blit, because the blits are always the same size, and then only for the size of our new blit 

Extremely not crazy about this...Surely a better way to do it without buffering the ENTIRE SCREEN. I only update the parts that are needed, but still...

This is LUDICROUSLY INEFFICIENT (2*O(n^2)) and COMPLETELY TERRIBLE code, but it's currently 2:48am so I can't think of a better approach... 
*/
uint32_t nv3_s2sb_line_buffer[NV3_MAX_HORIZONTAL_SIZE*NV3_MAX_VERTICAL_SIZE] = {0};

void nv3_render_blit_screen2screen(nv3_grobj_t grobj)
{
    if (nv3->pgraph.blit.size.w < NV3_MAX_HORIZONTAL_SIZE
    && nv3->pgraph.blit.size.h < NV3_MAX_VERTICAL_SIZE)
        memset(&nv3_s2sb_line_buffer, 0x00, (sizeof(uint32_t) * nv3->pgraph.blit.size.h) * (sizeof(uint32_t) * nv3->pgraph.blit.size.w));

    nv3_position_16_t old_position = nv3->pgraph.blit.point_in;
    nv3_position_16_t new_position = nv3->pgraph.blit.point_out;

    uint16_t end_x_in = (nv3->pgraph.blit.point_in.x + nv3->pgraph.blit.size.w); /* needed for bounds checking */
    uint16_t end_x_out = (nv3->pgraph.blit.point_out.x + nv3->pgraph.blit.size.w);
    uint16_t end_y = (nv3->pgraph.blit.point_out.y + nv3->pgraph.blit.size.h);

    uint32_t pixel_to_copy = 0x00;

    /* Prevents overwriting pixels we've already modified*/
    uint32_t xdiff = 0, ydiff = 0;

    /* Read the old pixel into the line buffer */
    for (int32_t y = 0; y < nv3->pgraph.blit.size.h; y++)
    {
        old_position.y = nv3->pgraph.blit.point_in.y + y;

        for (int32_t x = 0; x < nv3->pgraph.blit.size.w; x++)
        {
            old_position.x = nv3->pgraph.blit.point_in.x + x;

            switch (nv3->nvbase.svga.bpp)
            {
                case 8:
                    pixel_to_copy = nv3_render_read_pixel_8(old_position, grobj, false) & 0xFF;
                    break;
                case 15 ... 16: //15bpp and 16bpp modes are considered as identical
                    pixel_to_copy = nv3_render_read_pixel_16(old_position, grobj, false) & 0xFFFF;
                    break; 
                case 32: 
                    pixel_to_copy = nv3_render_read_pixel_32(old_position, grobj, false);
                    break;
            }

            uint32_t buf_position = (y * nv3->pgraph.blit.size.w) + x;
            nv3_s2sb_line_buffer[buf_position] = pixel_to_copy;
        }


    }
    /* simply write it all back to vram */
    for (int32_t y = 0; y < nv3->pgraph.blit.size.h; y++)
    {
        new_position.y = nv3->pgraph.blit.point_out.y + y;

        for (int32_t x = 0; x < nv3->pgraph.blit.size.w; x++)
        {
            new_position.x = nv3->pgraph.blit.point_out.x + x;
    
            uint32_t buf_position = (y * nv3->pgraph.blit.size.w) + x;
    
            nv3_render_write_pixel(new_position, nv3_s2sb_line_buffer[buf_position], grobj);
        }
    }
    

}