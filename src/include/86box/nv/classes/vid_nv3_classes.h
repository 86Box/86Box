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
 *          ALL of these structures HAVE(?) to be a size of exactly 0x2000 bytes because that's what the hashtable expects and they need to actually map into the vram address space
 *          (they are converted to pointers).
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
        uint8_t channel : 6;
        uint16_t reserved : 9;
        bool reset_if_volatile; // ????
    } set_notify_ctx_dma;           // Set notifier context for DMA (context switch)

} nv3_class_ctx_switch_method_t;

/* 32-bit BGRA format colour for 2D acceleration */
typedef struct nv3_color_bgra_32_s
{
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
} nv3_color_bgra_32_t; 

/* 32-bit ARGB format colour for internal D3D5 stuff */
typedef struct nv3_color_argb_32_s
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} nv3_color_argb_32_t;

/* 30-bit colour format for internal PGRAPH use */
typedef struct nv3_color_x3a10g10b10_s
{
    uint8_t reserved : 1;
    bool alpha_if_chroma_key_otherwise_reserved2 : 1; // 1-bit ALPHA if chroma key, OTHERWISE USELESS and IGNORE
    uint16_t r : 10;
    uint16_t g : 10;
    uint16_t b : 10;
} nv3_color_x3a10g10b10_t;

/* 16-bit A4R4G4B4 colour format */
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

        struct
        {
            uint16_t y;
            uint16_t x;
        };
    
    } position;
} nv3_position_16_t;

/* A big position format with 30:16 = y, 15:11 = nothing, 10:0 = x */
typedef struct nv3_position_16_bigy_s
{
    // WHOSE IDEA WAS THIS?
    uint16_t x : 11; 
    uint8_t reserved : 5;
    uint16_t y : 15;
    bool reserved2 : 1;
} nv3_position_16_bigy_t;

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
    nv3_color_argb_32_t color;
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
    uint32_t color0;                // Some 32-bit format (argb?)
    uint32_t color1;                // argb?  
    uint32_t pattern[2];            // argb?
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
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;       // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;                                    // Set notifier         
    uint8_t reserved2[0x1FC];
    nv3_color_argb_32_t color;                              // The colour
    uint8_t reserved3[0xF8];
    nv3_position_16_t position[16];
    nv3_size_16_t size[16];
    uint8_t reserved4[0x1B7F];
} nv3_rectangle_t;


/* In case your points weren't colourful enough */
typedef struct nv3_object_class_008_cpoint_s
{
    nv3_color_argb_32_t color;                              // BGRA-format 32-bit color
    nv3_position_16_t position;                             //
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
    nv3_color_argb_32_t color;                                 // argb?
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
    nv3_color_argb_32_t color;                                 // argb?
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
    nv3_color_argb_32_t color;                                 // argb?
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
    nv3_color_argb_32_t color;                                 // argb?
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

// (0x0F does not exist)

/* 
    Object Class 0x10 (real hardware)
    0x?? (drivers)
    Also 0x50 in context IDs.
    
    Represents a blit.
*/

typedef struct nv3_object_class_010
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;
    uint8_t reserved[0x100];
    uint32_t set_notify;
    uint8_t reserved2[0x1F8];
    nv3_position_16_t point_in;
    nv3_position_16_t point_out;
    nv3_size_16_t size;
    uint8_t reserved3[0x1CF3];
} nv3_blit_t;

/* 
    Object Class 0x11 (real hardware)
    0x?? (drivers)
    Also 0x51 in context IDs.
    
    Represents an image from the cpu (i guess from system memory)
*/
typedef struct nv3_object_class_011
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;
    uint8_t reserved[0x100];
    uint32_t set_notify;
    uint8_t reserved2[0x1FC];
    nv3_position_16_t point;
    nv3_size_16_t size;
    nv3_size_16_t size_in;
    uint8_t reserved3[0xF0];
    nv3_color_argb_32_t color[32];                           // The colour to use
    uint8_t reserved4[0x1B7F];
} nv3_image_t;

/* 
    Object Class 0x12 (real hardware)
    0x?? (drivers)
    Also 0x52 in context IDs.
    
    Bitmap from CPU.
    It seems the difference is that an image is colour but a 
*/
typedef struct nv3_object_class_012
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;
    uint8_t reserved[0x100];
    uint32_t set_notify;
    uint8_t reserved2[0x200];
    nv3_color_argb_32_t color_0;
    nv3_color_argb_32_t color_1;
    nv3_position_16_t point;                        // Top left(?) of the bitmap
    nv3_size_16_t size;
    nv3_size_16_t size_in;
    uint32_t monochrome_bitmap[32];
    uint8_t reserved4[0x1B7F];
} nv3_bitmap_t;

// 0x13 doesn't exist

/* 
    Object Class 0x14 (real hardware)
    0x?? (drivers)
    Also 0x54 in context IDs.
    
    Image Transfer to Memory
    It seems the difference is that an image is colour but a bitmap is b&w
*/
typedef struct nv3_object_class_014
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;
    uint8_t reserved[0x100];
    uint32_t set_notify;
    uint8_t reserved2[0x200];
    nv3_position_16_t point;
    nv3_size_16_t size;
    uint32_t image_pitch;               // bytes per row
    uint32_t image_start;
    uint8_t reserved3[0x1C37];
} nv3_image_to_memory_t;

/* 
    Object Class 0x15 (real hardware)
    0x?? (drivers)
    Also 0x55 in context IDs.
    
    Stretched Image from CPU
    Seems to be, by the very large color array, the main class used in 2d acceleration.
*/
typedef struct nv3_object_class_015
{    
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;
    uint8_t reserved[0x100];
    uint32_t set_notify;
    uint8_t reserved2[0x1FC];
    nv3_size_16_t size_in;
    uint32_t delta_dx_du;
    uint32_t delta_dy_dv;
    nv3_position_16_t clip_0;
    nv3_size_16_t clip_1;
    uint32_t point12d4; /* todo: fraction struct */
    uint8_t reserved3[0xE4];
    uint32_t color[1792];
    // no reserve needed
} nv3_stretched_image_from_cpu_t; 

// 0x16 invalid

/* 
    Object Class 0x17 (real hardware)
    0x?? (drivers)
    Also 0x57 in context IDs.
    
    Direct3D 5.0 accelerated triangle with zeta buffer (combined z buffer and stencil buffer)
    This is the final boss of this GPU. True horror stands below.
*/

//
// NV3 D3D5: TEXTURING & PIXEL FORMATS
//

typedef enum nv3_d3d5_texture_pixel_format_e
{
    /*
    16-Bit Pixel Format
    5 bits red, 5 bits green, 5 bits alpha, "boolean" alpha
    */
    nv3_d3d5_pixel_format_le_a1r5g5b5 = 0,

    /*
    15-Bit Pixel Format (represented as 16)
    1 bit irrelevant, 5 bits red, 5 bits green, 5 bits alpha
    */
    nv3_d3d5_pixel_format_le_x1r5g5b5 = 1,

    /*
    16-Bit Pixel Format
    4 bits alpha, 4 bits red, 4 bits green, 4 bits blue
    */
    nv3_d3d5_pixel_format_le_a4r4g4b4 = 2,

    /*
    16-Bit Pixel Format
    5 bits red, 6 bits green, 5 bits blue 
    (Required nv3tweak...)
    */
    nv3_d3d5_pixel_format_le_r5g6b5 = 3,

} nv3_d3d5_texture_pixel_format;

/* Output format

   The output pixel format...I
*/
typedef enum nv3_d3d5_output_pixel_format_e
{
    /*
    32-Bit Pixel Format
    8 bits irrelevant, 8 bits red, 8 bits green, 8 bits blue
    */
    nv3_d3d5_output_pixel_format_le_x8r8g8b8 = 0,

    /*
    32-Bit Pixel Format
    8 bits irrelevant, 8 bits red, 8 bits green, 8 bits blue
    Is this even used?? The riva can't even do 32 bit colour in 3d...
    */
    nv3_d3d5_output_pixel_format_le_a8r8g8b8 = 1,


} nv3_d3d5_output_pixel_format;


/* Texture size 

   NOTE: ONLY 256X256 OR LOWER ARE SUPPORTED! 2048X2048X16 TAKES UP ENTIRE VRAM OF RIVA 128 ZX...
   I ASSUME THESE ARE INTERNALLY SCALED DOWN
*/
typedef enum nv3_d3d5_texture_size_e
{
    nv3_d3d5_texture_size_1x1 = 0,

    nv3_d3d5_texture_size_2x2 = 1,

    nv3_d3d5_texture_size_4x4 = 2,

    nv3_d3d5_texture_size_8x8 = 3,

    nv3_d3d5_texture_size_16x16 = 4,

    nv3_d3d5_texture_size_32x32 = 5,

    nv3_d3d5_texture_size_64x64 = 6,

    nv3_d3d5_texture_size_128x128 = 7,

    // Highest size supported natively by hardware?
    nv3_d3d5_texture_size_256x256 = 8,

    nv3_d3d5_texture_size_512x512 = 9,

    nv3_d3d5_texture_size_1024x1024 = 10,

    nv3_d3d5_texture_size_2048x2048 = 11,

    
} nv3_d3d5_texture_size;

/* Texture Wrapping Mode for U/V Coordinate Overflow 

*/
typedef enum nv3_d3d5_texture_wrap_mode_e
{
    // Map textures in a cylindrical fashion.
    nv3_d3d5_texture_wrap_mode_cylindrical = 0,

    // Simply wrap back to the start.
    nv3_d3d5_texture_wrap_mode_wrap = 1,

    // Mirror the texture.
    nv3_d3d5_texture_wrap_mode_mirror = 2,

    // Clamp to the last border pixel.
    nv3_d3d5_texture_wrap_mode_clamp = 3,
} nv3_d3d5_texture_wrap_mode; 


/* This is blending but isn't really considered to be it in the GPU for some reason
    What do we do with out input texel BEFORE processing it?
 */
typedef enum nv3_d3d5_dest_color_interpretation_e
{
    // Do nothing
    nv3_d3d5_source_color_normal = 0,

    // Invert Colour
    nv3_d3d5_source_color_inverse = 1,

    // Invert Alpha before Processing
    nv3_d3d5_source_color_alpha_inverse = 2,

    // Take Alpha as 1
    nv3_d3d5_source_color_alpha_one = 3,

} nv3_d3d5_dest_color_interpretation;

// The full texture format structure
typedef struct nv3_d3d5_texture_format_s
{
    uint16_t color_key_color_mask;
    bool color_key_enabled : 1;
    nv3_d3d5_texture_pixel_format color_format : 2;
    nv3_d3d5_texture_size size_min : 4;
    nv3_d3d5_texture_size size_max : 4;
} nv3_d3d5_texture_format_t;

//
// NV3 D3D5: INTERPOLATION
//

/* 
    ??????
    Interpolating between mip levels? (or for texture blending)
*/
typedef enum nv3_d3d5_interpolator_algorithm_e
{
    // Zero-order hold interpolation? 
    nv3_d3d5_interpolator_zoh = 0,

    // Zero-order hold (microsoft variant)?
    nv3_d3d5_interpolator_zoh_ms = 1,

    // Full-order hold interpolation?   
    nv3_d3d5_interpolator_foh = 2,

} nv3_d3d5_interpolator_algorithm;

//
// NV3 D3D5: Z-BUFFER
//

/* Probably the sorting algorithm */
typedef enum nv3_d3d5_zbuffer_type_e
{
    // Sort based on linear distance
    nv3_d3d5_zbuffer_linear = 0,

    // Sort based on distance from view frustum
    nv3_d3d5_zbuffer_screen = 1,

} nv3_d3d5_zbuffer_type;

// NV3 D3D5: WRITE CONTROL (SHARED BETWEEN ZETA BUFFER AND ALPHA)
typedef enum nv3_d3d5_buffer_write_control_e
{
    // Never write.
    nv3_d3d5_buffer_write_control_never = 0,

    // Only write the alpha.
    nv3_d3d5_buffer_write_control_alpha = 1,

    // Write both alpha and the zeta-buffer.
    nv3_d3d5_buffer_write_control_alpha_zeta = 2,

    // Write only the zeta-buffer
    nv3_d3d5_buffer_write_control_zeta = 3,

    // Write everything (alpha+z+zeta?)
    nv3_d3d5_buffer_write_control_always = 4,

    
} nv3_d3d5_buffer_write_control; 

//
// NV3 D3D5: BUFFER COMPARISON (SHARED BETWEEN ZETA BUFFER AND ALPHA CONTROL)
//

// Todo: Which direction? (i.e. is less than p1 < p2 or p2 < p1!)
typedef enum nv3_d3d5_buffer_comparison_type_e
{
    // !!!ILLEGAL COMPARISON TYPE!!!!
    nv3_d3d5_buffer_comparison_illegal = 0,

    // The (depth?) test always fails.
    nv3_d3d5_buffer_comparison_always_false = 1,
    
    // True if less than fail.
    nv3_d3d5_buffer_comparison_less_than = 2,

    // The test succeeds if equal.
    nv3_d3d5_buffer_comparison_equal = 3,
    
    // The test succeeds if less or equal.
    nv3_d3d5_buffer_comparison_less_or_equal = 4,
    
    // The test succeeds if greater.
    nv3_d3d5_buffer_comparison_greater = 5,
    
    // The test succeeds if the two elements are equal.
    nv3_d3d5_buffer_comparison_not_equal = 6,
    
    // The test succeeds if greater or equal
    nv3_d3d5_buffer_comparison_greater_or_equal = 7,
    
    // The test always succeeds.
    nv3_d3d5_buffer_comparison_always_true = 8,
        
} nv3_d3d5_buffer_comparison_type;

//
// NV3 D3D5: BLENDING
//

/* Render Operation for Blending */
typedef enum nv3_d3d5_blend_render_operation_e
{
    nv3_d3d5_blend_render_operation_and = 0,

    nv3_d3d5_blend_add_with_saturation = 1,

} nv3_d3d5_blend_render_operation;

typedef enum nv3_d3d5_blend_beta_factor_e
{
    nv3_d3d5_blend_beta_factor_srcalpha = 0,

    nv3_d3d5_blend_beta_factor_zero = 1,

} nv3_d3d5_blend_beta_factor;

typedef enum nv3_d3d5_blend_input0_e
{
    nv3_d3d5_blend_input0_srcalpha = 0,

    nv3_d3d5_blend_input0_zero = 1,

} nv3_d3d5_blend_input0;

typedef enum nv3_d3d5_blend_input1_e
{
    nv3_d3d5_blend_input1_destalpha = 0,

    nv3_d3d5_blend_input1_zero = 1,

} nv3_d3d5_blend_input1;

//
// NV3 D3D5: CULLING
//

typedef enum nv3_d3d5_culling_algorithm_e
{
    // Don't cull
    nv3_d3d5_culling_algorithm_none = 0,

    // Cull Clockwise around view frustum?
    nv3_d3d5_culling_algorithm_clockwise = 1,

    // Cull counterclockwise around view frustum?
    nv3_d3d5_culling_algorithm_counterclockwise = 2,

} nv3_d3d5_culling_algorithm;

/* Specular reflection parameters */
typedef struct nv3_d3d5_specular_s
{
    uint8_t i0 : 4;
    uint8_t i1 : 4;
    uint8_t i2 : 4;
    uint8_t i3 : 4;
    uint8_t i4 : 4;
    uint8_t i5 : 4;
    uint8_t fog; //table fog emulation?
} nv3_d3d5_specular_t;

//
// NV3 D3D5: MISC
//
typedef struct nv3_d3d5_texture_filter_s
{
    uint8_t spread_x;
    uint8_t spread_y;
    uint8_t mipmap;
    uint8_t turbo;
} nv3_d3d5_texture_filter_t;

//
// NV3 D3D5: OUTPUT CONTROL STRUCTURE
//

/* Output Control for D3D5 Triangles */
typedef struct nv3_d3d5_control_out_s
{
    nv3_d3d5_interpolator_algorithm ctrl_out_interpolator : 2;
    uint8_t reserved : 2;
    nv3_d3d5_texture_wrap_mode wrap_u : 2;                              // Controls wrapping mode of U texture coordinate
    nv3_d3d5_texture_wrap_mode wrap_v : 2;                              // Controls wrapping move of V texture coordinate
    nv3_d3d5_output_pixel_format output_pixel_format : 1;
    bool reserved2 : 1;
    nv3_d3d5_dest_color_interpretation dest_color_interpretation : 2;
    nv3_d3d5_culling_algorithm culling_algorithm : 2;
    bool reserved3 : 1;
    nv3_d3d5_zbuffer_type zbuffer_type : 1;
    nv3_d3d5_buffer_comparison_type zeta_buffer_compare : 4;
    nv3_d3d5_buffer_write_control zeta_write : 3;
    bool reserved4 : 1;
    nv3_d3d5_buffer_write_control color_write : 3;
    bool reserved5 : 1;
    nv3_d3d5_blend_render_operation blend_rop : 1;
    nv3_d3d5_blend_input0 blend_input0 : 1;
    nv3_d3d5_blend_input1 blend_input1 : 1;
} nv3_d3d5_control_out_t;

typedef struct nv3_d3d5_alpha_control_s
{
    uint8_t alpha_key;
    nv3_d3d5_buffer_comparison_type zeta_buffer_compare : 4;
    uint32_t reserved : 20;
} nv3_d3d5_alpha_control_t;

//
// NV3 D3D5: Triangle Coordinates
//
typedef struct nv3_d3d5_coordinate_s
{
    nv3_d3d5_specular_t specular_reflection_parameters;     
    nv3_color_bgra_32_t color;                              // YOU HAVE TO FLIP THE ENDIANNESS. NVIDIA??? WHAT???

    // Seems more plausible for these specifically to be floats.
    // Also makes my life easier...
    float x;                     // X coordinate in 3d space of the triangle
    float y;                     // Y coordinate in 3d space of the triangle
    float z;                     // Z coordinate in 3d space of the triangle
    float m;                     // "Measurement dimension" apparently, allows for more precise measurements. And curves
    float u;                     // U coordinate within texture for the (top left?) of the triangle where sampling starts.
    float v;                     // V coordinate within texture for the (top left?) of the triangle where sampling starts.
} nv3_d3d5_coordinate_t;

typedef struct nv3_object_class_017
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;       // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;                                    // Set notifier         
    uint8_t reserved2[0x1FC];
    uint32_t texture_offset;
    nv3_d3d5_texture_format_t texture_format;
    nv3_d3d5_texture_filter_t texture_filter;
    nv3_color_argb_32_t fog_color;                          // Alpha is ignored here!
    nv3_d3d5_control_out_t control_out;
    nv3_d3d5_alpha_control_t alpha_control;

    uint8_t reserved3[0xCE4];
    nv3_d3d5_coordinate_t coordinate_points[128];           // The points wer are rendering.
    /* No placeholder needed, it really is that long. */
} nv3_d3d5_accelerated_triangle_with_zeta_buffer_t;

/* 0x19, 0x1A, 0x1B don't exist */

typedef struct nv3_object_class_018
{

} nv3_point_with_zeta_buffer_t;

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

typedef struct nv3_object_class_01C
{
    nv3_class_ctx_switch_method_t set_notify_ctx_dma;   // Set notifier context for DMA (context switch)
    uint8_t reserved[0x100];
    uint32_t set_notify;                                // Set notifier
    uint8_t reserved2[0x1F8];
    nv3_object_class_01C_pixel_format format;           // Completely different from everything else
    uint32_t pitch;                                     // 16-bit
    uint32_t linear_address;                            // 22-bit: Linear address in vram.
    uint8_t reserved3[0x1C3F];
} nv3_image_in_memory_t;

// See envytools. This is where we finally end up after this mess, it allows parameters to be passed to the methods.
typedef struct nv3_grobj_s
{
    uint32_t grobj_0;
    uint32_t grobj_1;
    uint32_t grobj_2;
    uint32_t grobj_3;
} nv3_grobj_t;
// TODO: PATCHCORDS!!!! TO LINK ALL OF THIS TOGETHER!!!
#pragma pack(pop) // return packing to whatever it was before this disaster

// Method IDs
#define NV3_GENERIC_METHOD_IS_PFIFO_FREE                0x0010

// Class methods
void nv3_generic_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_001_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_002_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_003_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_004_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_005_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_006_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_007_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_008_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_008_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_009_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_00a_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_00b_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_00c_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_00d_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_00e_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_010_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_011_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_012_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_014_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_015_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_017_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_018_method(uint32_t method_id, nv3_grobj_t grobj);
void nv3_class_01c_method(uint32_t method_id, nv3_grobj_t grobj);

// This area is used for holding universal representations of the U* registers...
extern struct nv3_object_class_001 nv3_beta_factor;
extern struct nv3_object_class_002 nv3_rop;
extern struct nv3_object_class_003 nv3_chroma_key;
extern struct nv3_object_class_004 nv3_plane_mask;
extern struct nv3_object_class_005 nv3_clipping_rectangle;
extern struct nv3_object_class_006 nv3_pattern;
extern struct nv3_object_class_007 nv3_rectangle;
extern struct nv3_object_class_008 nv3_point;
extern struct nv3_object_class_009 nv3_line;
extern struct nv3_object_class_00A nv3_lin;
extern struct nv3_object_class_00B nv3_triangle;
extern struct nv3_object_class_00C nv3_win95_gdi_text;
extern struct nv3_object_class_00D nv3_m2mf;
extern struct nv3_object_class_00E nv3_scaled_image_from_memory;
extern struct nv3_object_class_010 nv3_blit;
extern struct nv3_object_class_011 nv3_image;
extern struct nv3_object_class_012 nv3_bitmap;
extern struct nv3_object_class_014 nv3_transfer2memory;
extern struct nv3_object_class_015 nv3_stretched_image_from_cpu;
extern struct nv3_object_class_017 nv3_d3d5_tri;
extern struct nv3_object_class_018 nv3_point_zeta_buffer;
extern struct nv3_object_class_01C nv3_image_in_memory;