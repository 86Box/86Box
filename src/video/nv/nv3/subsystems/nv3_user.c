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
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>


// PIO Method Submission
// 128 channels conceptually supported - a hangover from nv1 where multiple windows all directly programming the gpu were supported? total lunacy. 
uint32_t nv3_user_read(uint32_t address)
{
    // Get the address within the subchannel
    //todo: print out the subchannel 
    uint8_t method_offset = (address & 0x1FFC);

    uint8_t channel = (address - NV3_USER_START) / 0x10000;
    uint8_t subchannel = ((address - NV3_USER_START)) / 0x2000 % NV3_DMA_SUBCHANNELS_PER_CHANNEL;

    nv_log("User Submission Area PIO Channel %d.%d method_offset=0x%04x\n", channel, subchannel, method_offset);


    // 0x10 is free CACHE1 object
    // TODO: THERE ARE OTHER STUFF!
    switch (method_offset)
    {
        case NV3_SUBCHANNEL_PIO_IS_PFIFO_FREE:
            return nv3_pfifo_cache1_num_free_spaces();
        case NV3_SUBCHANNEL_PIO_ALWAYS_ZERO_START ... NV3_SUBCHANNEL_PIO_ALWAYS_ZERO_END:
            return 0x00;
        
    }

    nv_log("IT'S NOT IMPLEMENTED!!!! offset=0x%04x\n", method_offset);


    return 0x00;
}; 

// Although NV3 doesn't have DMA mode unlike NV4 and later, it's conceptually similar to a "pusher" that pushes graphics commands that you write into CACHE1 that are then pulled out.
// So we send the writes here. This might do other stuff, so we keep this function
void nv3_user_write(uint32_t address, uint32_t value) 
{
    nv3_pfifo_cache1_push(address, value);

    // This isn't ideal, but otherwise, the dynarec causes the GPU to write so many objects into CACHE1, it starts overwriting the old objects
    // This basically makes the fifo not a fifo, but oh well 
    nv3_pfifo_cache1_pull();
}