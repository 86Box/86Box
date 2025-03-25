/*
* 86Box    A hypervisor and IBM PC system emulator that specializes in
*          running old operating systems and software designed for IBM
*          PC systems and compatibles from 1981 through fairly recent
*          system designs based on the PCI bus.
*
*          This file is part of the 86Box distribution.
*
*          NV3 code to render basic objects.
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
#include <86box/utils/video_stdlib.h>

void nv3_render_rect(nv3_position_16_t position, nv3_size_16_t size, uint32_t color, nv3_grobj_t grobj)
{
    nv3_position_16_t current_pos = {0};

    for (int32_t y = position.y; y < (position.y + size.h); y++)
    {
        current_pos.y = y; 

        for (int32_t x = position.x; x < (position.x + size.w); x++)
        {
            current_pos.x = x;

            nv3_render_write_pixel(current_pos, color, grobj);
        }
    }
}

void nv3_render_text_1bpp(bool bit, nv3_grobj_t grobj)
{
    uint16_t clip_x = nv3->pgraph.win95_gdi_text.point_d.x + nv3->pgraph.win95_gdi_text.size_out_d.w;
    uint16_t clip_y = nv3->pgraph.win95_gdi_text.point_d.y + nv3->pgraph.win95_gdi_text.size_out_d.h;

    /* they send more data than they need */
    if (nv3->pgraph.win95_gdi_text_current_position.y >= clip_y)
        bit = false;

    uint32_t final_color;

    // if it's a 0 bit we don't need to do anything
    if (bit)
    {
        switch (nv3->nvbase.svga.bpp)
        {
            case 8:
                final_color = (nv3->pgraph.win95_gdi_text.color1_d & 0xFF); /* do we need to add anything? mul blend perhaps? */
                break;
            case 16:
                final_color = (nv3->pgraph.win95_gdi_text.color1_d & 0xFFFF); /* do we need to add anything? mul blend perhaps? */
                break;
            case 32:
                final_color = (nv3->pgraph.win95_gdi_text.color1_d); /* do we need to add anything? mul blend perhaps? */
                break;
        }
    }

    /* in type d colour0 is always transparent */
    if (bit)
        nv3_render_write_pixel(nv3->pgraph.win95_gdi_text_current_position, final_color, grobj);

    /* increment the position - the bitmap is stored horizontally backward */
    nv3->pgraph.win95_gdi_text_current_position.x--;

    /* check if we need to go down a line */
    if (nv3->pgraph.win95_gdi_text_current_position.x < nv3->pgraph.win95_gdi_text.point_d.x)
    {
        nv3->pgraph.win95_gdi_text_current_position.x = (nv3->pgraph.win95_gdi_text.point_d.x + nv3->pgraph.win95_gdi_text.size_in_d.w - 1);
        
        nv3->pgraph.win95_gdi_text_current_position.y++;
    }

    /* check if we are in the clipping rectangle */
    if (nv3->pgraph.win95_gdi_text_current_position.x < nv3->pgraph.win95_gdi_text.clip_d.left
    || nv3->pgraph.win95_gdi_text_current_position.x > nv3->pgraph.win95_gdi_text.clip_d.right
    || nv3->pgraph.win95_gdi_text_current_position.y < nv3->pgraph.win95_gdi_text.clip_d.top
    || nv3->pgraph.win95_gdi_text_current_position.y > nv3->pgraph.win95_gdi_text.clip_d.bottom)
    {
        return;
    }
}

void nv3_render_gdi_type_d(nv3_grobj_t grobj, uint32_t param)
{
    // reset when a position is submitted
    nv3_position_16_t start_position = nv3->pgraph.win95_gdi_text_current_position;

    /* Go through the bitmap that was sent, bit by bit. */
    for (int32_t bit_num = 0; bit_num <= 31; bit_num++)
    {
        bool bit = (param >> bit_num) & 0x01;

        nv3_render_text_1bpp(bit, grobj);
    }
}

/* 2-colour 1bpp color-expanded text from [7-0] */
void nv3_render_text_1bpp_2color(uint8_t byte, nv3_grobj_t grobj)
{
    for (int32_t bit_num = 0; bit_num <= 7; bit_num++)
    {
        bool bit = (byte >> bit_num) & 0x01;

        uint16_t clip_x = nv3->pgraph.win95_gdi_text.point_e.x + nv3->pgraph.win95_gdi_text.size_out_e.w;
        uint16_t clip_y = nv3->pgraph.win95_gdi_text.point_e.y + nv3->pgraph.win95_gdi_text.size_out_e.h;
    
        /* they send more data than they need */
        if (nv3->pgraph.win95_gdi_text_current_position.y >= clip_y)
            bit = false;
    
        // if it's a 0 bit we don't need to do anything
    
        uint32_t final_color;
    
        switch (nv3->nvbase.svga.bpp)
        {
            case 8:
                final_color = (bit) ? (nv3->pgraph.win95_gdi_text.color1_e & 0xFF) : (nv3->pgraph.win95_gdi_text.color0_e & 0xFF); /* do we need to add anything? mul blend perhaps? */
                break;
            case 16:
                final_color = (bit) ? (nv3->pgraph.win95_gdi_text.color1_e & 0xFFFF) : (nv3->pgraph.win95_gdi_text.color0_e & 0xFFFF); /* do we need to add anything? mul blend perhaps? */
                break;
            case 32:
                final_color = (bit) ? nv3->pgraph.win95_gdi_text.color1_e : nv3->pgraph.win95_gdi_text.color0_e;  /* do we need to add anything? mul blend perhaps? */
                break;
        }
    
        nv3_render_write_pixel(nv3->pgraph.win95_gdi_text_current_position, final_color, grobj);
    
        /* increment the position - the bitmap is stored horizontally backward */
        nv3->pgraph.win95_gdi_text_current_position.x--;
    
        /* see if we need to go to the next line */
        if (nv3->pgraph.win95_gdi_text_current_position.x < nv3->pgraph.win95_gdi_text.point_e.x)
        {
            nv3->pgraph.win95_gdi_text_current_position.x = nv3->pgraph.win95_gdi_text.point_e.x + (nv3->pgraph.win95_gdi_text.size_in_e.w - 1);
            nv3->pgraph.win95_gdi_text_current_position.y++;
        }
    
        /* check if we are in the clipping rectangle */
        if (nv3->pgraph.win95_gdi_text_current_position.x < nv3->pgraph.win95_gdi_text.clip_e.left
        || nv3->pgraph.win95_gdi_text_current_position.x > nv3->pgraph.win95_gdi_text.clip_e.right
        || nv3->pgraph.win95_gdi_text_current_position.y < nv3->pgraph.win95_gdi_text.clip_e.top
        || nv3->pgraph.win95_gdi_text_current_position.y > nv3->pgraph.win95_gdi_text.clip_e.bottom)
        {
            return;
        }
    }
    

}

void nv3_render_gdi_type_e(nv3_grobj_t grobj, uint32_t param)
{
    // reset when a position is submitted
    nv3_position_16_t start_position = nv3->pgraph.win95_gdi_text_current_position;

    /* we have to interpret every bit in reverse order but in the right bit order */
    uint8_t byte0 = ((param & 0xFF000000) >> 24);
    uint8_t byte1 = ((param & 0xFF0000) >> 16);
    uint8_t byte2 = ((param & 0xFF00) >> 8);
    uint8_t byte3 = (param & 0xFF);

    nv3_render_text_1bpp_2color(byte0, grobj);
    nv3_render_text_1bpp_2color(byte1, grobj);
    nv3_render_text_1bpp_2color(byte2, grobj);
    nv3_render_text_1bpp_2color(byte3, grobj);
}