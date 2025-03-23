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