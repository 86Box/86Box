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

void nv3_render_blit_screen2screen(nv3_grobj_t grobj)
{
    nv3_position_16_t old_position = nv3->pgraph.blit.point_in;
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

                for (int32_t x = nv3->pgraph.blit.point_out.x; x < end_x; x++)
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

                for (int32_t x = nv3->pgraph.blit.point_out.x; x < end_x; x++)
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