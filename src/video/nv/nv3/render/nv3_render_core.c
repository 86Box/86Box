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
#include <86box/utils/video_stdlib.h>

/* Expand a colour.
   NOTE: THE GPU INTERNALLY OPERATES ON RGB10!!!!!!!!!!!
*/
nv3_color_expanded_t nv3_render_expand_color(nv3_grobj_t grobj, uint32_t color)
{
    // grobj0 = seems to share the format of PGRAPH_CONTEXT_SWITCH register.

    uint8_t format = (grobj.grobj_0 & 0x07);
    bool alpha_enabled = (grobj.grobj_0 >> NV3_PGRAPH_CONTEXT_SWITCH_ALPHA) & 0x01;

    nv3_color_expanded_t color_final; 
    // set the pixel format
    color_final.pixel_format = format;

    #ifdef ENABLE_NV_LOG
    nv_log("Expanding Colour 0x%08x using pgraph_pixel_format 0x%x alpha enabled=%d", color, format, alpha_enabled);
    #endif
    
    // default to fully opaque in case alpha is disabled
    color_final.a = 0xFF;

    switch (format)
    {
        // ALL OF THESE TYPES ARE 32 BITS IN SIZE

        // 555
        case nv3_pgraph_pixel_format_r5g5b5:
            nv3_color_16_a1r5g5b5_t new_color = *(nv3_color_16_a1r5g5b5_t*)&color;
            // "stretch out" the colour
            color_final.r = new_color.r * 0x20;
            color_final.g = new_color.g * 0x20;
            color_final.b = new_color.b * 0x20;

            if (alpha_enabled)
                color_final.a = new_color.a;

            break;
        // 888 (standard colour + 8-bit alpha)
        case nv3_pgraph_pixel_format_r8g8b8:
            if (alpha_enabled)
                color_final.a = ((color >> 24) & 0xFF) * 4;

            color_final.r = ((color >> 16) & 0xFF) * 4;
            color_final.g = ((color >> 8) & 0xFF) * 4;
            color_final.b = (color & 0xFF) * 4;

            if (alpha_enabled)
                color_final.a = new_color.a;
            break;
        case nv3_pgraph_pixel_format_r10g10b10:
            nv3_color_x2a10g10b10_t new_color_rgb10 = *(nv3_color_x2a10g10b10_t*)&color;

            color_final.r = new_color_rgb10.r;
            color_final.g = new_color_rgb10.g;
            color_final.b = new_color_rgb10.b;

            if (alpha_enabled)
                color_final.a = new_color.a;

            break;
        case nv3_pgraph_pixel_format_y8:
            color_final.a = (color >> 8) & 0xFF;

            // yuv
            color_final.r = color_final.g = color_final.b = (color & 0xFF) * 4; // convert to rgb10
            break;
        case nv3_pgraph_pixel_format_y16:
            color_final.a = (color >> 16) & 0xFFFF;

            // yuv
            color_final.r = color_final.g = color_final.b = (color & 0xFFFF) * 4; // convert to rgb10
            break;
        default:
            warning("nv3_render_expand_color unknown format %d", format);
            break;
        
    }

    // i8 is a union under i16
    color_final.i16 = (color & 0xFFFF);

    return color_final;
}

/* Used for chroma test */
uint32_t nv3_render_downconvert(nv3_grobj_t grobj, nv3_color_expanded_t color)
{
    uint8_t format = (grobj.grobj_0 & 0x07);
    bool alpha_enabled = (grobj.grobj_0 >> NV3_PGRAPH_CONTEXT_SWITCH_ALPHA) & 0x01;

    #ifdef ENABLE_NV_LOG
    nv_log("Downconverting Colour 0x%08x using pgraph_pixel_format 0x%x alpha enabled=%d", color, format, alpha_enabled);
    #endif

    uint32_t packed_color = 0x00;

    switch (format)
    {
        case nv3_pgraph_pixel_format_r5g5b5:
            packed_color = (color.r / 0x20) << 10 | 
                           (color.g / 0x20) << 5 |
                           (color.b / 0x20);

            break;
        case nv3_pgraph_pixel_format_r8g8b8:
            break;
        case nv3_pgraph_pixel_format_r10g10b10:
            break;
        case nv3_pgraph_pixel_format_y8:
            break;
        case nv3_pgraph_pixel_format_y16:
            break;
        default:
            warning("nv3_render_downconvert_color unknown format %d", format);
            break;

    }

    return packed_color;
}

/* Runs the chroma key test */
void nv3_render_chroma_test(nv3_grobj_t grobj)
{

}

/* Convert expanded colour format to chroma key format */
uint32_t nv3_render_to_chroma(nv3_color_expanded_t expanded)
{
    // convert the alpha to 1 bit. then return packed rgb10
    return !!expanded.a | (expanded.r << 30) | (expanded.b << 20) | (expanded.a << 10);
}

/* Plots a pixel. */
void nv3_render_pixel(nv3_position_16_t position, uint32_t color, nv3_grobj_t grobj)
{
    uint8_t alpha = 0xFF;

    // PFB_0 is always set to hardcoded "NO_TILING" value of 0x1114.
    // It seems, you are meant to 

    /* put this here for debugging + it may be needed later. */
    #ifdef DEBUG
    uint8_t color_format_object = (grobj.grobj_0 >> NV3_PGRAPH_CONTEXT_SWITCH_COLOR_FORMAT) & 0x07;
    #endif
    bool alpha_enabled = (grobj.grobj_0 >> NV3_PGRAPH_CONTEXT_SWITCH_ALPHA) & 0x01;

    uint32_t framebuffer_bpp = nv3->nvbase.svga.bpp; // maybe y16 too?
    uint32_t current_buffer = (nv3->pgraph.context_switch >> NV3_PGRAPH_CONTEXT_SWITCH_SRC_BUFFER) & 0x03; 

    /* doesn't seem*/
    nv3_color_argb_t color_data = *(nv3_color_argb_t*)&color;

    if (framebuffer_bpp == 32)
        alpha = color_data.a;
    
    int32_t clip_end_x = nv3->pgraph.clip_start.x + nv3->pgraph.clip_size.x;
    int32_t clip_end_y = nv3->pgraph.clip_start.y + nv3->pgraph.clip_size.y;
    
    /* First, check our current buffer. */
    /* Then do the clip. */
    if (position.x < nv3->pgraph.clip_start.x
        || position.y < nv3->pgraph.clip_start.y
        || position.x > clip_end_x
        || position.y > clip_end_y)
        {
            // DO NOT DRAW THE PIXEL
            return;
        }

    /* TODO: Chroma Key, Pattern, Plane Mask...*/

    /* Combine the current buffer with the pitch to get the address in the framebuffer to draw from. */

    uint32_t vram_x = position.x;

    // we have to multiply the x position by the number of bytes per pixel
    switch (framebuffer_bpp)
    {
        case 8:
            break;
        case 16:
            vram_x = position.x << 1;
            break;
        case 32:
            vram_x = position.x << 2;
            break;
    }

    uint32_t pixel_addr_vram = vram_x + (nv3->pgraph.bpitch[current_buffer] * position.y) + nv3->pgraph.boffset[current_buffer];

    pixel_addr_vram &= nv3->nvbase.svga.vram_mask;

    /*  Go to vram and do the final ROP for a basic bitblit.
        It seems we can skip the downconversion step *for now*, since (framebuffer bits per pixel) == (object bits per pixel) 
        I'm not sure how games will react. But it depends on how the D3D drivers operate, we may need ro convert texture formats to the current bpp internally.

        TODO: MOVE TO BPIXEL DEPTH or GROBJ0 to determine this, once we figure out how to get the bpixel depth.
    */

    uint32_t src = 0, dst = 0;

    switch (framebuffer_bpp)
    {
        case 8:
            src = color & 0xFF;
            dst = nv3->nvbase.svga.vram[pixel_addr_vram];
            nv3->nvbase.svga.vram[pixel_addr_vram] = video_rop_gdi_ternary(nv3->pgraph.rop, src, dst, 0x00) & 0xFF;

            nv3->nvbase.svga.changedvram[pixel_addr_vram >> 12] = changeframecount;

            break;
        case 16:
            uint16_t* vram_16 = (uint16_t*)(nv3->nvbase.svga.vram);
            pixel_addr_vram >>= 1; 

            // mask off the alpha bit. Even though the drivers should send this. What!
            if (!alpha_enabled)
                src = ((color & (~0x8000)) & 0xFFFF);
            else 
                src = color & 0xFFFF;

            // convert to 16bpp
            // forcing it to render in 15bpp fixes it, 

            
            dst = vram_16[pixel_addr_vram];
            vram_16[pixel_addr_vram] = video_rop_gdi_ternary(nv3->pgraph.rop, src, dst, 0x00) & 0xFFFF;

            nv3->nvbase.svga.changedvram[pixel_addr_vram >> 11] = changeframecount;

            break;
        case 32:
            uint32_t* vram_32 = (uint32_t*)(nv3->nvbase.svga.vram);
            pixel_addr_vram >>= 2; 

            src = color;
            dst = vram_32[pixel_addr_vram];
            vram_32[pixel_addr_vram] = video_rop_gdi_ternary(nv3->pgraph.rop, src, dst, 0x00);

            nv3->nvbase.svga.changedvram[pixel_addr_vram >> 10] = changeframecount;

            break;
    }
}
