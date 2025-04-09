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

/* Render GDI-B clipped rectangle */
void nv3_render_rect_clipped(nv3_clip_16_t clip, uint32_t color, nv3_grobj_t grobj)
{
    nv3_position_16_t current_pos = {0};

    for (int32_t y = clip.top; y < clip.bottom; y++)
    {
        current_pos.y = y; 

        for (int32_t x = clip.left; x < clip.right; x++)
        {
            current_pos.x = x;

            /* compare against the global clip too */
            if (current_pos.x >= nv3->pgraph.win95_gdi_text.clip_b.left
            && current_pos.x <= nv3->pgraph.win95_gdi_text.clip_b.right
            && current_pos.y >= nv3->pgraph.win95_gdi_text.clip_b.top
            && current_pos.y <= nv3->pgraph.win95_gdi_text.clip_b.bottom)
            {
                nv3_render_write_pixel(current_pos, color, grobj);
            }
        }
    }
}

void nv3_render_gdi_transparent_bitmap_blit(bool bit, bool clip, uint32_t color, nv3_grobj_t grobj)
{
    /* If the bit is set, and cliping is enabled (Type D) tru and lcip */
    if (bit && clip)
    {
        /* Turn the bit off if we need to clip (Type D ) */
        if (nv3->pgraph.win95_gdi_text_current_position.x < nv3->pgraph.win95_gdi_text.clip_d.left
        || nv3->pgraph.win95_gdi_text_current_position.x > nv3->pgraph.win95_gdi_text.clip_d.right
        || nv3->pgraph.win95_gdi_text_current_position.y < nv3->pgraph.win95_gdi_text.clip_d.top
        || nv3->pgraph.win95_gdi_text_current_position.y > nv3->pgraph.win95_gdi_text.clip_d.bottom)
        {
            bit = false; 
        }   

        /* 
           Also clip if we are outside of the SIZE_OUT range 
           We only need to do this in one direction just to get rid of the crud sent by NV
        */
        uint32_t clip_x = nv3->pgraph.win95_gdi_text.point_d.x + (nv3->pgraph.win95_gdi_text.size_out_d.w);
        uint32_t clip_y = nv3->pgraph.win95_gdi_text.point_d.y + (nv3->pgraph.win95_gdi_text.size_out_d.h);

        if (nv3->pgraph.win95_gdi_text_current_position.x >= clip_x
        || nv3->pgraph.win95_gdi_text_current_position.y >= clip_y)
            bit = false; 
    }

    /* We don't need to and it, because it seems the Riva only uses non-packed bpp formats for this class */
    if (bit)
        nv3_render_write_pixel(nv3->pgraph.win95_gdi_text_current_position, color, grobj);

    /* 
       Check if we've reached the bottom
       Because we check the bits in reverse, we go forward (bits 7,6,5 were set for a 1x3 bitmap)
    */

    uint32_t end_x = (clip) ? nv3->pgraph.win95_gdi_text.point_d.x + nv3->pgraph.win95_gdi_text.size_in_d.w : nv3->pgraph.win95_gdi_text.point_c.x + nv3->pgraph.win95_gdi_text.size_c.w;

    nv3->pgraph.win95_gdi_text_current_position.x++;

    if (nv3->pgraph.win95_gdi_text_current_position.x >= end_x)
    {
        nv3->pgraph.win95_gdi_text_current_position.y++; 

        if (!clip)
            nv3->pgraph.win95_gdi_text_current_position.x = nv3->pgraph.win95_gdi_text.point_c.x;
        else 
            nv3->pgraph.win95_gdi_text_current_position.x = nv3->pgraph.win95_gdi_text.point_d.x;
    }

}

/* Originally written 23 March 2025, but then, redone, properly, on 30 March 2025 */
void nv3_render_gdi_transparent_bitmap(bool clip, uint32_t color, uint32_t bitmap_data, nv3_grobj_t grobj)
{
    /* 
        First, we need to figure out how many bits we have left.
        If we have less than 32, don't process all of the bits. 

        Bits are processed in the following order: [7-0] [15-8] [23-16] [31-24]
        TODO: Store this somewhere, so it doesn't need to be recalculated.

        We store a global bit count for this purpose.
    */

    uint32_t bitmap_size = (clip) ? nv3->pgraph.win95_gdi_text.size_in_d.w * nv3->pgraph.win95_gdi_text.size_in_d.h : nv3->pgraph.win95_gdi_text.size_c.w * nv3->pgraph.win95_gdi_text.size_c.h;
    uint32_t bits_remaining_in_bitmap = bitmap_size - nv3->pgraph.win95_gdi_text_bit_count;

    /* we have to interpret every bit in reverse bit order but in the right byte order */

    bool current_bit = false;

    /* Start by rendering bits 7 through 0 */
    for (int32_t bit = 7; bit >= 0; bit--)
    {
        current_bit = (bitmap_data >> bit) & 0x01;

        nv3_render_gdi_transparent_bitmap_blit(current_bit, clip, color, grobj);
        nv3->pgraph.win95_gdi_text_bit_count++;
        bits_remaining_in_bitmap--;

        if (!bits_remaining_in_bitmap)
            break;
    }

    /* IF we're done, let's return */
    if (!bits_remaining_in_bitmap)
        return;

    /* Now for 15 through 8 */
    for (int32_t bit = 15; bit >= 8; bit--)
    {
        current_bit = (bitmap_data >> bit) & 0x01;

        nv3_render_gdi_transparent_bitmap_blit(current_bit, clip, color, grobj);
        nv3->pgraph.win95_gdi_text_bit_count++;
        bits_remaining_in_bitmap--;

        if (!bits_remaining_in_bitmap)
            break;
    }

    /* IF we're done, let's return */
    if (!bits_remaining_in_bitmap)
        return;

    /* Now for 23 through 16 */
    for (int32_t bit = 23; bit >= 16; bit--)
    {
        current_bit = (bitmap_data >> bit) & 0x01;

        nv3_render_gdi_transparent_bitmap_blit(current_bit, clip, color, grobj);
        nv3->pgraph.win95_gdi_text_bit_count++;
        bits_remaining_in_bitmap--;

        if (!bits_remaining_in_bitmap)
            break;
    }

    /* IF we're done, let's return */
    if (!bits_remaining_in_bitmap)
        return;

    /* Now for 31 through 24 */
    for (int32_t bit = 31; bit >= 24; bit--)
    {
        current_bit = (bitmap_data >> bit) & 0x01;

        nv3_render_gdi_transparent_bitmap_blit(current_bit, clip, color, grobj);
        nv3->pgraph.win95_gdi_text_bit_count++;
        bits_remaining_in_bitmap--;

        if (!bits_remaining_in_bitmap)
            break;
    }

    /* IF we're done, let's return */
    if (!bits_remaining_in_bitmap)
        return;

}

void nv3_render_gdi_1bpp_bitmap_blit(bool bit, uint32_t color0, uint32_t color1, nv3_grobj_t grobj)
{
    /* We can't force the bit off because this is a 1bpp bitmap */
    bool skip = false; 

    /* For Type E, always clip */
        /* Turn the bit off if we need to clip (Type D ) */
    if (nv3->pgraph.win95_gdi_text_current_position.x < nv3->pgraph.win95_gdi_text.clip_e.left
    || nv3->pgraph.win95_gdi_text_current_position.x > nv3->pgraph.win95_gdi_text.clip_e.right
    || nv3->pgraph.win95_gdi_text_current_position.y < nv3->pgraph.win95_gdi_text.clip_e.top
    || nv3->pgraph.win95_gdi_text_current_position.y > nv3->pgraph.win95_gdi_text.clip_e.bottom)
    {
        skip = true; 
    }   

    /* 
        Also clip if we are outside of the SIZE_OUT range 
        We only need to do this in one direction just to get rid of the crud sent by NV
    */
    uint32_t clip_x = nv3->pgraph.win95_gdi_text.point_e.x + (nv3->pgraph.win95_gdi_text.size_out_e.w);
    uint32_t clip_y = nv3->pgraph.win95_gdi_text.point_e.y + (nv3->pgraph.win95_gdi_text.size_out_e.h);

    if (nv3->pgraph.win95_gdi_text_current_position.x >= clip_x
    || nv3->pgraph.win95_gdi_text_current_position.y >= clip_y)
        skip = true;

    /* We don't need to and it, because it seems the Riva only uses non-packed bpp formats for this class */
    if (!skip)
    {
        if (bit)
            nv3_render_write_pixel(nv3->pgraph.win95_gdi_text_current_position, nv3->pgraph.win95_gdi_text.color1_e, grobj);
        else 
            nv3_render_write_pixel(nv3->pgraph.win95_gdi_text_current_position, nv3->pgraph.win95_gdi_text.color0_e, grobj);
    }
       

    /* 
       Check if we've reached the bottom, if so, advance to the next horizontal lin
       Because we check the bits in reverse, we go forward (bits 7,6,5 were set for a 1x3 bitmap)
    */

    uint32_t end_x = nv3->pgraph.win95_gdi_text.point_e.x + nv3->pgraph.win95_gdi_text.size_in_e.w;

    nv3->pgraph.win95_gdi_text_current_position.x++;

    if (nv3->pgraph.win95_gdi_text_current_position.x >= end_x)
    {
        nv3->pgraph.win95_gdi_text_current_position.y++; 
        nv3->pgraph.win95_gdi_text_current_position.x = nv3->pgraph.win95_gdi_text.point_e.x;
    }
}


/* Originally written 23 March 2025, but then, redone, properly, on 30 March 2025 */
void nv3_render_gdi_1bpp_bitmap(uint32_t color0, uint32_t color1, uint32_t bitmap_data, nv3_grobj_t grobj)
{
    /* 
        First, we need to figure out how many bits we have left.
        If we have less than 32, don't process all of the bits. 

        Bits are processed in the following order: [7-0] [15-8] [23-16] [31-24]
        TODO: Store this somewhere, so it doesn't need to be recalculated.

        We store a global bit count for this purpose.
    */

    uint32_t bitmap_size = nv3->pgraph.win95_gdi_text.size_in_e.w * nv3->pgraph.win95_gdi_text.size_in_e.h;
    uint32_t bits_remaining_in_bitmap = bitmap_size - nv3->pgraph.win95_gdi_text_bit_count;

    /* we have to interpret every bit in reverse bit order but in the right byte order */

    bool current_bit = false;

    /* Start by rendering bits 7 through 0 */
    for (int32_t bit = 7; bit >= 0; bit--)
    {
        current_bit = (bitmap_data >> bit) & 0x01;

        nv3_render_gdi_1bpp_bitmap_blit(current_bit, color0, color1, grobj);
        nv3->pgraph.win95_gdi_text_bit_count++;
        bits_remaining_in_bitmap--;

        if (!bits_remaining_in_bitmap)
            break;
    }

    /* IF we're done, let's return */
    if (!bits_remaining_in_bitmap)
        return;

    /* Now for 15 through 8 */
    for (int32_t bit = 15; bit >= 8; bit--)
    {
        current_bit = (bitmap_data >> bit) & 0x01;

        nv3_render_gdi_1bpp_bitmap_blit(current_bit, color0, color1, grobj);
        nv3->pgraph.win95_gdi_text_bit_count++;
        bits_remaining_in_bitmap--;

        if (!bits_remaining_in_bitmap)
            break;
    }

    /* IF we're done, let's return */
    if (!bits_remaining_in_bitmap)
        return;

    /* Now for 23 through 16 */
    for (int32_t bit = 23; bit >= 16; bit--)
    {
        current_bit = (bitmap_data >> bit) & 0x01;

        nv3_render_gdi_1bpp_bitmap_blit(current_bit, color0, color1, grobj);
        nv3->pgraph.win95_gdi_text_bit_count++;
        bits_remaining_in_bitmap--;

        if (!bits_remaining_in_bitmap)
            break;
    }

    /* IF we're done, let's return */
    if (!bits_remaining_in_bitmap)
        return;

    /* Now for 31 through 24 */
    for (int32_t bit = 31; bit >= 24; bit--)
    {
        current_bit = (bitmap_data >> bit) & 0x01;

        nv3_render_gdi_1bpp_bitmap_blit(current_bit, color0, color1, grobj);
        nv3->pgraph.win95_gdi_text_bit_count++;
        bits_remaining_in_bitmap--;

        if (!bits_remaining_in_bitmap)
            break;
    }

    /* IF we're done, let's return */
    if (!bits_remaining_in_bitmap)
        return;
}