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
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>
#include <86box/nv/classes/vid_nv3_classes.h>


/* Render Core: Performs a ROP */
void nv3_perform_rop(uint32_t src, uint32_t dst, uint32_t pattern, uint32_t pen, nv3_render_operation_type rop)
{
    switch (rop)
    {
        case nv3_rop_blackness:
            return 0;
        case nv3_rop_srcand:

        case nv3_rop_srccopy:
            return src; 
        case nv3_rop_dstcopy:
            return dst; // do nothing
        case nv3_rop_dstinvert:
            return !dst;
        case nv3_rop_xor:
            return src ^ dst;
        case nv3_rop_whiteness:
            return 1; 
        
    }
}