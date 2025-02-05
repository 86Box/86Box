/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 User Submission Area (NV_USER, conceptually considered "Cache1 Pusher")
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *
 *          Copyright 2024-2025 starfrost
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86Box/86box.h>
#include <86Box/device.h>
#include <86Box/mem.h>
#include <86box/pci.h>
#include <86Box/rom.h> // DEPENDENT!!!
#include <86Box/video.h>
#include <86Box/nv/vid_nv.h>
#include <86Box/nv/vid_nv3.h>

// 128 channels conceptually supported - a hangover from nv1 where multiple windows all directly programming the gpu were supported? total lunacy. 
uint32_t nv3_user_read(uint32_t address)
{
    uint8_t method_offset = (address & 0x1FFC);

    nv_log("User Submission Area method_offset=0x%04x\n", method_offset);

    // 0x10 is free CACHE1 object
    // TODO: THERE ARE OTHER STUFF!
    switch (method_offset)
    {
        case NV3_GENERIC_METHOD_IS_PFIFO_FREE:
            return nv3_pfifo_cache1_is_free();
        
    }

    nv_log("IT'S NOT IMPLEMENTED!!!!\n", method_offset);


    return 0x00;
}; 

// Although NV3 doesn't have DMA mode unlike NV4 and later, it's conceptually similar to a "pusher" that pushes graphics commands that you write into CACHE1 that are then pulled out.
// So we send the writes here. This might do other stuff, so we keep this function
void nv3_user_write(uint32_t address, uint32_t value) 
{
    nv3_pfifo_cache1_push(address, value);
}