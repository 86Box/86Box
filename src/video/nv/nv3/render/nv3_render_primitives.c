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

void nv3_render_gdi_type_d(nv3_grobj_t grobj, uint32_t param)
{
    // reset when a position is submitted
    nv3_position_16_t start_position = nv3->pgraph.win95_gdi_text_current_position;

    // is this clip or point?

    uint16_t end_x = nv3->pgraph.win95_gdi_text.point_d.x + nv3->pgraph.win95_gdi_text.size_in_d.w;
    uint16_t end_y = nv3->pgraph.win95_gdi_text.point_d.y + nv3->pgraph.win95_gdi_text.size_in_d.h;

    /* set up our packed pixels */
    uint32_t pixel0 = 0, pixel1 = 0, pixel2 = 0, pixel3 = 0;

    /* Go through the bitmap that was sent, bit by bit. */
    for (int32_t bit_num = 0; bit_num <= 31; bit_num++)
    {
        bool bit = (param >> bit_num) & 0x01;
        //bool bit = true; // debug test


        // if it's a 0 bit we don't need to do anything
        if (bit)
        {
            switch (nv3->nvbase.svga.bpp)
            {
                case 8:
                    uint32_t final_color8 = (nv3->pgraph.win95_gdi_text.color1_d & 0xFF); /* do we need to add anything? mul blend perhaps? */
                    nv3_render_write_pixel(nv3->pgraph.win95_gdi_text_current_position, final_color8, grobj);
                    break;
                case 16:
                    uint32_t final_color16 = (nv3->pgraph.win95_gdi_text.color1_d & 0xFFFF); /* do we need to add anything? mul blend perhaps? */
                    nv3_render_write_pixel(nv3->pgraph.win95_gdi_text_current_position, final_color16, grobj);
                    break;
                case 32:
                    uint32_t final_color32 = (nv3->pgraph.win95_gdi_text.color1_d); /* do we need to add anything? mul blend perhaps? */
                    nv3_render_write_pixel(nv3->pgraph.win95_gdi_text_current_position, final_color32, grobj);
                    break;
            }
        }

        /* increment the position - the bitmap is stored horizontally backward */
        nv3->pgraph.win95_gdi_text_current_position.x--;

        /* let's hope NV never overflow the y */
        if (nv3->pgraph.win95_gdi_text_current_position.x <= nv3->pgraph.win95_gdi_text.point_d.x)
        {
            nv3->pgraph.win95_gdi_text_current_position.x = nv3->pgraph.win95_gdi_text.point_d.x + nv3->pgraph.win95_gdi_text.size_in_d.w;
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
}