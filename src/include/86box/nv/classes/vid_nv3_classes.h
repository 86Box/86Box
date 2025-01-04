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
 *          Note: These uint32_ts are basically object methods that are being submitted
 *          They have different names so the user can use them more easily but different versions of the same class can be distinguished
 *          ALL of these structures HAVE(?) to be a size of exactly 0x2000 bytes because that's what the hashtable expects and they need to actually map 
 *          directly to the PHYSICAL PGRAPH REGISTERS while sitting in RAMHT!!!!.
 *
 *          Also, these class IDs don't relate to the internal architecture of the GPU.
 *          Effectively, the NVIDIA drivers are faking shit. There are only 16 classes but the drivers recognise many more. See nv3_object_classes_driver.txt for the list of  
 *          classes recognised by the driver.
 *          This is why the Class IDs you see here are not the same as you may see in other places.
 *
 *          Todo: Is reserved* actually needed?
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

// This is slower, but these need to map *****EXACTLY***** to the registers in PGRAPH,
// or everything FUCKS UP
//
// DO NOT REMOVE! DO NOT REMOVE! DO NOT REMOVE!
#pragma pack(push, 1)

// CLass names for debugging
extern const char* nv3_class_names[];

/* Class context switch method */
typedef struct nv3_class_ctx_switch_method_s
{
    union 
    {
        uint32_t data;

        uint16_t instance;
        uint8_t channel_id : 6;
        uint16_t reserved : 9;
        bool reset_if_volatile; // ????
    } set_notify_ctx_dma;           // Set notifier context for DMA (context switch)

} nv3_class_ctx_switch_method_t;

/* 32-bit BGRA format colour for 2D acceleration */
typedef struct nv3_color_32_s
{
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
} nv3_color_32_t; 

/* A4R4G4B4 */
typedef struct nv3_color_16_a4r4g4b4_s
{
    uint8_t a : 4;
    uint8_t r : 4;
    uint8_t g : 4;
    uint8_t b : 4;
} nv3_color_16_a4r4g4b4_t;

/* A1R5G5B5 format */
typedef struct nv3_color_16_a1r5g5b5_s
{
    uint8_t a : 1;
    uint8_t r : 5;
    uint8_t g : 5;
    uint8_t b : 5;
} nv3_color_16_a1r5g5b5_t;

/* 565 format - NV3Tweak */
typedef struct nv3_color_16_r5g6b5_s
{
    uint8_t r : 5;
    uint8_t g : 6;
    uint8_t b : 5;
} nv3_color_16_r5g6b5_t;

/* Generic 16-bit position*/
typedef struct nv3_position_16_s
{
    union 
    {
        uint32_t pos;

        struct vid_nv3_classes
        {
            uint16_t y;
            uint16_t x;
        };
        
    
    } position;
} nv3_position_16_t;

/* Generic 16-bit size */
typedef struct nv3_size_16_s
{
    union
    {
        uint32_t size;

        struct 
        {
            uint16_t h;
            uint16_t w;
        };
        

    } size;
} nv3_size_16_t;

/* Generic 32-bit colour + 16-bit position */
typedef struct nv3_color_and_position_16_s
{
    uint32_t color;
    nv3_position_16_t points;
} nv3_color_and_position_16_t;

/* Generic 16-bit clip region */
typedef struct nv3_clip_16_s
{
    // The bounds of the clipping area.
    uint16_t left;
    uint16_t top;
    uint16_t right;
    uint16_t bottom;
} nv3_clip_16_t; 

/* In case your positions weren't HIGH PRECISION enough */
typedef struct nv3_position_32_s
{
    uint32_t x;
    uint32_t y;
} nv3_position_32_t;

// COLOUR FORMATS

/* 
    Object Class 0x01 (real hardware, also 0x41) 
    0x12 (drivers)
    Beta factor
*/
typedef struct nv3_object_class_001
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;    // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];        // Required for NV_CLASS Core Functionality
    uint32_t set_notify;
    uint8_t reserved2[0x1F8];
    uint8_t set_beta_factor_1d31;   // 31:31 (?) value, 30:21 fraction
    uint8_t reserved3[0x1CFB];      // needs to be 0x2000 bytes 
    // Put the rest of it here
} nv3_beta_factor_t;

/* 
    Object class 0x02 (real hardware)
    0x14/0x43 (drivers)
    Also 0x42 in context IDs
    Render operation used for things like blending. Appears to be 8-bit i.e. a ROP3 with 256 possible operations.
*/
typedef struct nv3_object_class_002
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;    // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;            // Set notifier
    uint8_t reserved2[0x1F8];
    uint8_t rop;                    // ROP3 (ID = ????????)
    uint8_t reserved3[0x1CFB];      // needs to be 0x2000 bytes 
} nv3_render_operation_t;

/* 
    Object class 0x03 (real hardware)
    0x15 (drivers)
    Also 0x43 in context IDs
    A chroma/color key, like in video editing
*/
typedef struct nv3_object_class_003
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;    // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;            // Set notifier
    uint8_t reserved2[0x1F8];
    uint8_t color;                  // ROP3 (ID = ????????)
    uint8_t reserved3[0x1CFB];      // needs to be 0x2000 bytes     
} nv3_chroma_key_t;

/* 
    Object class 0x04 (real hardware)
    0x15 (drivers)
    Also 0x44 in context IDs
    Plane mask
*/
typedef struct nv3_object_class_004
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;    // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;            // Set notifier
    uint8_t reserved2[0x1F8];
    uint8_t color;                  // ROP3 (ID = ????????)
    uint8_t reserved3[0x1CFB];      // needs to be 0x2000 bytes     
} nv3_plane_mask_t;

/* 
    Object class 0x05 (real hardware)
    0x19/0x1E/0x47 (drivers)
    Also 0x45 in context IDs
    Clipping rectangle used for various blitting operations
*/
typedef struct nv3_object_class_005
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;    // Set notifier context for DMA (context switch)
    uint8_t reserved[0X100];
    uint32_t set_notify;            // Set notifier
    uint8_t reserved2[0x1F4];

    /* 16-bit precision */
    nv3_position_16_t position;
    nv3_size_16_t size;
    uint8_t reserved3[0x1CFB];      // needs to be 0x2000 bytes     

} nv3_clipping_rectangle_t;

/* 
    Object Class 0x06 (real hardware)
    0x?? (drivers)
    Also 0x46 in context IDs
    A pattern used for blits. Wahey!
*/
typedef struct nv3_object_class_006
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;    // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;            // Set notifier
    uint8_t reserved2[0x200];
    uint32_t shape;                 // 0 = 8x8, 1 = 64x1, 2 = 1x64
    uint32_t color0;                // Some 32-bit format (BGRA?)
    uint32_t color1;                // BGRA?  
    uint32_t pattern[2];            // BGRA?
    uint8_t reserved3[0x1CDF];      // needs to be 0x2000 bytes     
} nv3_pattern_t;

/* 
    Object Class 0x07 (real hardware)
    0x1E (drivers)
    Also 0x47 in context IDs
    A rectangle. Wahey!
*/
typedef struct nv3_object_class_007
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;    // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;            // Set notifier
    uint8_t reserved2[0x1FC];
    uint32_t color;                 // The colour
    uint8_t reserved3[0xF8];
    nv3_position_16_t position[16];
    nv3_size_16_t size[16];
    uint8_t reserved4[0x1B7F];
} nv3_rectangle_t;


/* In case your points weren't colourful enough */
typedef struct nv3_object_class_008_cpoint_s
{
    uint32_t color;
    nv3_position_16_t position;

} nv3_object_class_008_cpoint_t;

/* 
    Object Class 0x08 (real hardware)
    0x1A (drivers)
    Also 0x48 in context IDs
    A point: the revolutionary 3d graphics technique...
*/
typedef struct nv3_object_class_008
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;                    // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;                            // Set notifier
    uint8_t reserved2[0x1FC];
    uint32_t color;                                 // BGRA?
    nv3_position_16_t point[16];                    // Boring points 
    nv3_position_32_t point32[16];                  // Allows you to have points with full 32-bit precision 
    nv3_object_class_008_cpoint_t cpoint[16];       // Allows you to have c o l o r f u l points! 
    uint8_t reserved3[0x1A7B];
} nv3_point_t; 

/* Normal line... */
typedef struct nv3_object_class_009_line_s
{
    nv3_position_16_t start; // presumably unless it's in reverse order...TODO: check the order
    nv3_position_16_t end; 

} nv3_object_class_009_line_t;

/* THIRTY TWO BIT PRECISION line */
typedef struct nv3_object_class_009_line32_s
{
    uint32_t x0;
    uint32_t x1;
    uint32_t y0;
    uint32_t y1;
} nv3_object_class_009_line32_t;

/* nv3_object_class_009_polyline_t not implemented because it's just a duplicate of nv3_object_class_009_line */
/* nv3_object_class_009_polyline32_t not implemented because it's just a duplicate of nv3_object_class_009_line32 */


/* 
    Object Class 0x09 (real hardware)
    0x1B (drivers)
    Also 0x49 in context IDs
    It's a line, but also a polygon...
*/
typedef struct nv3_object_class_009
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;                    // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;                            // Set notifier
    uint8_t reserved2[0x1FC];
    uint32_t color;                                 // BGRA?
    nv3_object_class_009_line_t line[16];           // List of line points (...)
    nv3_object_class_009_line32_t line32[8];
    nv3_object_class_009_line_t polyline[32];
    nv3_object_class_009_line32_t polyline32[16];
    nv3_color_and_position_16_t cpolyline[16]; // List of line points and colours.

    uint8_t reserved3[0x197b];
} nv3_line_t;

/* 
    Object Class 0x0A (real hardware)
    0x1c (drivers)
    Also 0x4a in context IDs
    
    This one is where nvidia reinvents the line, but without the starting or ending pixel.
    Seriously.
*/
typedef struct nv3_object_class_00A
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;                    // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;                            // Set notifier
    uint8_t reserved2[0x1FC];
    uint32_t color;                                 // BGRA?
    nv3_object_class_009_line_t line[16];           // List of line points (...)
    nv3_object_class_009_line32_t line32[8];
    nv3_object_class_009_line_t polyline[32];
    nv3_object_class_009_line32_t polyline32[16];
    nv3_color_and_position_16_t cpolyline[16]; // List of line points and colours.

    uint8_t reserved3[0x197b];
} nv3_lin_t;

/* 
    Object Class 0x0B (real hardware)
    0x?? (drivers)
    Also 0x4b in context IDs.
    
    This is a triangle but seems to be obsolete. It's replaced with UD3D0Z / D3D5 Accelerated Triangle with Zeta Buffer. Does it even exist?
*/
typedef struct nv3_object_class_00B
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;                    // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;                            // Set notifier
    uint8_t reserved2[0x1FC];
    uint32_t color;                                 // BGRA?
    uint8_t reserved3[0x8];                     
    // The points of the triangle.
    nv3_position_16_t points[3];

    // Another way of filling out the points of the triangle
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;
    uint32_t y2;
    uint32_t x2; 

    nv3_position_16_t mesh[32];                     // Some kind of mesh format. I guess a list of vertex positions?
    nv3_position_32_t mesh32[16];               
    nv3_color_and_position_16_t ctriangle[3];       // Triangle with colour
    nv3_color_and_position_16_t ctrimesh[16];       // Some kind of mesh format. I guess a list of vertex positions? with colours
    uint8_t reserved4[0x19FB];
} nv3_triangle_t;

typedef struct nv3_object_class_00C_nclip_s
{
    nv3_position_16_t position;
    nv3_size_16_t size;
} nv3_object_class_00C_nclip_t;

/* 
    Object Class 0x0C (real hardware)
    0x0C (drivers)
    Also 0x4C in context IDs.
    
    GDI text acceleration for Windows 95.
    How the fuck does this even work?
*/
typedef struct nv3_object_class_00C
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;                    // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;                            // Set notifier
    uint8_t reserved2[0x2F4];                       
    uint32_t color_a;                               // Color for Clip A
    nv3_object_class_00C_nclip_t rect_nclip[64];    
    uint8_t reserved3[0x1F0];   
    nv3_clip_16_t clip_b;
    uint32_t color_b;                               // Color for Clip B
    nv3_clip_16_t rect_clip[64];
    uint8_t reserved4[0x1E8];
    nv3_clip_16_t clip_c;
    uint32_t color1_c;
    nv3_size_16_t size_c;
    nv3_position_16_t point_c;
    uint32_t color1_c_bitmap[128];
    uint8_t reserved5[0x368];
    nv3_clip_16_t clip_d;
    uint32_t color1_d;
    nv3_size_16_t size_in_d;
    nv3_size_16_t size_out_d;
    nv3_position_16_t point_d;
    uint32_t mono_color1_d[128];
    uint8_t reserved6[0x364];
    nv3_clip_16_t clip_e;
    uint32_t color0_e;
    uint32_t color1_e;
    nv3_size_16_t size_in_e;
    nv3_size_16_t size_out_e;
    nv3_position_16_t point_e;
    uint32_t mono_color1_e[128];
    uint8_t reserved7[0xB7F];
} nv3_win95_text_t;


/* 
    Object Class 0x0D (real hardware)
    0x?? (drivers)
    Also 0x4D in context IDs.
    
    Represents reformatting of an image in memory.
*/
typedef struct nv3_object_class_00D
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;                    // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;                            // Set notifier
    uint8_t reserved2[0x204];
    uint32_t offset_in;
    uint32_t offset_out;
    uint32_t pitch_in;
    uint32_t pitch_out;
    uint32_t line_length_in;                        // Stride?
    uint32_t line_count;
    uint8_t format_input_bits;                      // 1 2 or 4 to increment by bits
    uint8_t format_output_bits;                     // 1 2 to 4 to increment by bits
    uint8_t reserved3[2];
    uint32_t buffer_notify;                         // Notify the Buffedr
    uint8_t reserved4[0x1CD3];  
} nv3_memory_to_memory_format_t;

/* 
    Object Class 0x0E (real hardware)
    0x?? (drivers)
    Also 0x4E in context IDs.
    
    Represents a scaled image coming from memory.
*/
typedef struct nv3_object_class_00E
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;
    uint8_t reserved[0x100];
    uint32_t set_notify;
    uint8_t reserved2[0x200];
    nv3_position_16_t clip_0;
    nv3_size_16_t clip_1;
    nv3_position_16_t rectangle_out_0;
    nv3_size_16_t rectangle_out_1;
    // Calculus in a graphics card
    uint32_t delta_du_dx;
    uint32_t delta_dv_dy;
    uint8_t reserved3[0xE0];
    nv3_size_16_t size; // can be size_y if YUV420
    uint32_t pitch;
    uint32_t offset;
    uint32_t point;
    // YUV420 stuff
    uint32_t pitch_yuv420;
    uint32_t offset_y;
    uint32_t offset_u;
    uint32_t offset_v;
    uint32_t point_yuv420;
    uint8_t reserved4[0x1BE7];  // pad to 0x2000
} nv3_scaled_image_from_memory_t;

/* WHY IS THE FORMAT DIFFERENT TO THE REST OF THE GPU? 
   They are making it look like a bitfield but it's hex?

   THEY ARE ALL LITTLE ENDIAN
*/
typedef enum nv3_object_class_01C_pixel_format_e
{
    // Y8P4
    // 12-bits (Y8 - Planar YUV 8 bits (Y value only), 4 bits of indexed colour too?
    nv3_m2mt_pixel_format_le_y8_p4 = 0x1010000,

    // Y16P2
    // 16-bits (Y16) - Planar YUV 16 bits (Y value only), 2 bits of indexed colour too?
    nv3_m2mt_pixel_format_le_y16_p2 = 0x1010101,

    /* 1 unused bit, 555 15-bit format, p2(?)
    */
    nv3_m2mt_pixel_format_x1r5g5b5_p2 = 0x1000000,

    // X8G8B8R8, 24-bit colour (or 24-bit colour with alpha)
    nv3_m2mt_pixel_format_x8g8b8r8 = 0x1,
} nv3_object_class_01C_pixel_format; 

// TODO: PATCHCORDS!!!! TO LINK ALL OF THIS TOGETHER!!!
#pragma pack(pop) // return packing to whatever it was before this disaster