/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Defines graphics objects for Nvidia NV3 architecture-based GPU (RIVA 128/RIVA 128 ZX),
 *          as well as for later GPUs if they use the same objects. 
 *
 *
 *
 * Authors: Connor Hyde <mario64crashed@gmail.com>
 *
 *          Copyright 2024-2025 Connor Hyde
 */

#pragma once 
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* 
    Note: These uint32_ts are basically object methods that are being submitted
    They have different names so the user can use them more easily but different versions of the same class can be distinguished
    
    ALL of these structures HAVE to be a size of exactly 0x2000 bytes because that's what the hashtable expects.

    Also, these class IDs don't relate to the internal architecture of the GPU.
    Effectively, the NVIDIA drivers are faking shit. There are only 16 classes but the drivers recognise many more. See nv3_object_classes_driver.txt for the list of  
    classes recognised by the driver.

    The 3-bit DMA SUBCHANNEL is combined with a 4-bit CLASS ID to get the REAL CLASS ID. There are 32 CLASSES per subchannel and 8 SUBCHANNELS. 

    This is why the Class IDs you see here are not the same as you may see in other places.
*/

extern const char* nv3_class_names[];

/* 
Object Class 0x07 (real hardware)
             0x1E (drivers)
Also 0x47 in context IDs
A rectangle. Wahey!
*/
typedef struct nv_object_class_007
{
    uint8_t reserved[0xFF];         // Required for NV_CLASS Core Functionality
    uint32_t set_notify_ctx_dma;    // Set notifier context for DMA
    uint32_t set_notify;            // Set notifier
    uint32_t set_image_output;      // Set the image output type
    uint8_t reserved2[0xF5];        // up to 0x200
    uint32_t set_zeta_output;       // Zeta buffer input
    uint32_t set_zeta_input;        // Zeta buffer input
    uint32_t set_color_format;      // Color format: 0x100000=15bpp.
    uint8_t reserved3[0xF5];        // up to 0x300

    /* THESE ARE ALL THE SAME METHOD */
    uint32_t color_zeta32;          // 32-bit zeta buffer color (?)
    uint32_t point;                 // Draw a point i guess
    uint8_t reserved4[0x4F3];       // up to 0x7fc   
    uint32_t control_out;           // 7fd-7ff
    uint8_t reserved5[0x1800];      // up to 0x2000
} nv3_rectangle_t;
