/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x0C (Windows 95 GDI text acceleration)
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

void nv3_class_00c_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    switch (method_id)
    {
        /* Type A: Unclipped Rectangle */

        /* NOTE: This method is used by the GDI driver as part of the notification engine. */
        case NV3_W95TXT_A_COLOR:
            nv3->pgraph.win95_gdi_text.color_a = param;
            nv_log("Method Execution: GDI-A Color 0x%08x\n", nv3->pgraph.win95_gdi_text.color_a);
            break;
        /* Type B: Clipped Rectangle */
        case NV3_W95TXT_B_COLOR:
            nv3->pgraph.win95_gdi_text.color_b = param;
            nv_log("Method Execution: GDI-B Color 0x%08x\n", nv3->pgraph.win95_gdi_text.color_b);
            break;
        case NV3_W95TXT_B_CLIP_TOPLEFT:
            nv3->pgraph.win95_gdi_text.clip_b.left = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.clip_b.top = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: GDI-B Clip Left,Top %04x,%04x", nv3->pgraph.win95_gdi_text.clip_b.left, nv3->pgraph.win95_gdi_text.clip_b.top);
            break;
        case NV3_W95TXT_B_CLIP_BOTTOMRIGHT:
            nv3->pgraph.win95_gdi_text.clip_b.bottom = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.clip_b.right = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: GDI-B Clip Bottom,Right %04x,%04x", nv3->pgraph.win95_gdi_text.clip_b.right, nv3->pgraph.win95_gdi_text.clip_b.bottom);
            break;
        /* Type C: Unclipped Bitmap */
        case NV3_W95TXT_C_CLIP_COLOR:
            nv3->pgraph.win95_gdi_text.color1_c = param;
            nv_log("Method Execution: GDI-C Color 0x%08x\n", nv3->pgraph.win95_gdi_text.color1_c);
            break;
        case NV3_W95TXT_C_CLIP_SIZE:
            nv3->pgraph.win95_gdi_text.size_c.x = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.size_c.y = ((param >> 16) & 0xFFFF);

            nv3->pgraph.win95_gdi_text_bit_count = 0;
            nv_log("Method Execution: GDI-C Size In %04x,%04x\n", nv3->pgraph.win95_gdi_text.size_c.x,  nv3->pgraph.win95_gdi_text.size_c.y);
            break;
        case NV3_W95TXT_C_CLIP_POSITION:
            nv3->pgraph.win95_gdi_text.point_c.x = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.point_c.y = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: GDI-C Point %04x,%04x\n", nv3->pgraph.win95_gdi_text.point_c.x, nv3->pgraph.win95_gdi_text.point_c.y);

            nv3->pgraph.win95_gdi_text_current_position.x = nv3->pgraph.win95_gdi_text.point_c.x  ;
            nv3->pgraph.win95_gdi_text_current_position.y = nv3->pgraph.win95_gdi_text.point_c.y;

            break;
        case NV3_W95TXT_C_CLIP_TOPLEFT: 
            nv3->pgraph.win95_gdi_text.clip_c.left = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.clip_c.top = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: GDI-C Clip Left,Top %04x,%04x\n", nv3->pgraph.win95_gdi_text.clip_c.left, nv3->pgraph.win95_gdi_text.clip_c.top);
            break; 
        case NV3_W95TXT_C_CLIP_BOTTOMRIGHT:
            nv3->pgraph.win95_gdi_text.clip_c.right = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.clip_c.bottom = ((param >> 16) & 0xFFFF);
            /* is it "only if we are out of the top left or the bottom right or is it "all of them"*/
            nv_log("Method Execution: GDI-C Clip Right,Bottom %04x,%04x\n", nv3->pgraph.win95_gdi_text.clip_c.left, nv3->pgraph.win95_gdi_text.clip_c.top);
            break;
        /* Type B and C not implemented YET, as they are not used by NT GDI driver */
        case NV3_W95TXT_D_CLIP_TOPLEFT: 
            nv3->pgraph.win95_gdi_text.clip_d.left = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.clip_d.top = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: GDI-D Clip Left,Top %04x,%04x\n", nv3->pgraph.win95_gdi_text.clip_d.left, nv3->pgraph.win95_gdi_text.clip_d.top);
            break; 
        case NV3_W95TXT_D_CLIP_BOTTOMRIGHT:
            nv3->pgraph.win95_gdi_text.clip_d.right = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.clip_d.bottom = ((param >> 16) & 0xFFFF);
            /* is it "only if we are out of the top left or the bottom right or is it "all of them"*/
            nv_log("Method Execution: GDI-D Clip Right,Bottom %04x,%04x\n", nv3->pgraph.win95_gdi_text.clip_d.left, nv3->pgraph.win95_gdi_text.clip_d.top);
            break;

        case NV3_W95TXT_D_CLIP_COLOR:
            nv3->pgraph.win95_gdi_text.color1_d = param;
            nv_log("Method Execution: GDI-D Color 0x%08x\n", nv3->pgraph.win95_gdi_text.color_a);
            break;
        case NV3_W95TXT_D_CLIP_SIZE_IN:
            nv3->pgraph.win95_gdi_text.size_in_d.x = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.size_in_d.y = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: GDI-D Size In %04x,%04x\n", nv3->pgraph.win95_gdi_text.size_in_d.x, nv3->pgraph.win95_gdi_text.size_in_d.y);
            break;
        case NV3_W95TXT_D_CLIP_SIZE_OUT:
            nv3->pgraph.win95_gdi_text.size_out_d.x = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.size_out_d.y = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: GDI-D Size Out %04x,%04x\n", nv3->pgraph.win95_gdi_text.size_out_d.x, nv3->pgraph.win95_gdi_text.size_out_d.y);
            break; 
        case NV3_W95TXT_D_CLIP_POSITION:
            nv3->pgraph.win95_gdi_text.point_d.x = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.point_d.y = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: GDI-D Point %04x,%04x\n", nv3->pgraph.win95_gdi_text.point_d.x, nv3->pgraph.win95_gdi_text.point_d.y);

            nv3->pgraph.win95_gdi_text_current_position.x = nv3->pgraph.win95_gdi_text.point_d.x;
            nv3->pgraph.win95_gdi_text_current_position.y = nv3->pgraph.win95_gdi_text.point_d.y;
            nv3->pgraph.win95_gdi_text_bit_count = 0;

            break;
        /* Type E: Two-colour 1bpp */
        case NV3_W95TXT_E_CLIP_TOPLEFT: 
            nv3->pgraph.win95_gdi_text.clip_e.left = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.clip_e.top = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: GDI-E Clip Left,Top 0x%08x\n", nv3->pgraph.win95_gdi_text.clip_e.left, nv3->pgraph.win95_gdi_text.clip_e.top);
            break; 
        case NV3_W95TXT_E_CLIP_BOTTOMRIGHT:
            nv3->pgraph.win95_gdi_text.clip_e.right = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.clip_e.bottom = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: GDI-E Clip Bottom,Right 0x%08x\n", nv3->pgraph.win95_gdi_text.clip_e.right, nv3->pgraph.win95_gdi_text.clip_e.bottom);
            /* is it "only if we are out of the top left or the bottom right or is it "all of them"*/
            break;
        case NV3_W95TXT_E_CLIP_COLOR_0:
            nv3->pgraph.win95_gdi_text.color0_e = param;
            nv_log("Method Execution: GDI-E Color0 0x%08x\n", nv3->pgraph.win95_gdi_text.color_a);
            break;
        case NV3_W95TXT_E_CLIP_COLOR_1:
            nv3->pgraph.win95_gdi_text.color1_e = param;
            nv_log("Method Execution: GDI-E Color1 0x%08x\n", nv3->pgraph.win95_gdi_text.color_a);
            break;
        case NV3_W95TXT_E_CLIP_SIZE_IN:
            nv3->pgraph.win95_gdi_text.size_in_e.x = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.size_in_e.y = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: GDI-E Size In %04x,%04x\n", nv3->pgraph.win95_gdi_text.size_in_e.x,  nv3->pgraph.win95_gdi_text.size_in_e.y);
            break;
        case NV3_W95TXT_E_CLIP_SIZE_OUT:
            nv3->pgraph.win95_gdi_text.size_out_e.x = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.size_out_e.y = ((param >> 16) & 0xFFFF);
            nv_log("Method Execution: GDI-E Size Out %04x,%04x\n", nv3->pgraph.win95_gdi_text.size_out_e.x,  nv3->pgraph.win95_gdi_text.size_out_e.y);
            break; 
        case NV3_W95TXT_E_CLIP_POSITION:
            nv3->pgraph.win95_gdi_text.point_e.x = (param & 0xFFFF);
            nv3->pgraph.win95_gdi_text.point_e.y = ((param >> 16) & 0xFFFF);
            
            nv3->pgraph.win95_gdi_text_current_position.x = nv3->pgraph.win95_gdi_text.point_e.x;
            nv3->pgraph.win95_gdi_text_current_position.y = nv3->pgraph.win95_gdi_text.point_e.y;
            nv3->pgraph.win95_gdi_text_bit_count = 0;

            nv_log("Method Execution: GDI-E Point %04x,%04x\n", nv3->pgraph.win95_gdi_text.point_e.x,  nv3->pgraph.win95_gdi_text.point_e.y);
            break;
        default:
            /* Type A submission: these are the same things as rectangles */
            if (method_id >= NV3_W95TXT_A_RECT_START && method_id <= NV3_W95TXT_A_RECT_END)
            {
                uint32_t index = (method_id - NV3_W95TXT_A_RECT_START) >> 3;

                // IN THIS ONE SPECIFIC PLACE, ****AND ONLY THIS ONE SPECIFIC PLACE****, X AND Y ARE SWAPPED???? */
                // If the size is submitted, render it.
                if (method_id & 0x04)
                {
                    nv3->pgraph.win95_gdi_text.rect_a_size[index].x = (param >> 16) & 0xFFFF;
                    nv3->pgraph.win95_gdi_text.rect_a_size[index].y = param & 0xFFFF;
                    
                    nv_log("Method Execution: Rect GDI-A%d Size=%d,%d", index, nv3->pgraph.win95_gdi_text.rect_a_size[index].x, 
                        nv3->pgraph.win95_gdi_text.rect_a_size[index].y);

                    nv3_render_rect(nv3->pgraph.win95_gdi_text.rect_a_position[index], 
                        nv3->pgraph.win95_gdi_text.rect_a_size[index], nv3->pgraph.win95_gdi_text.color_a, grobj);
                }
                else // position
                {
                    nv3->pgraph.win95_gdi_text.rect_a_position[index].x = (param >> 16) & 0xFFFF;
                    nv3->pgraph.win95_gdi_text.rect_a_position[index].y = param & 0xFFFF;
                    
                    nv_log("Method Execution: Rect GDI-A%d Position=%d,%d\n", index,
                         nv3->pgraph.win95_gdi_text.rect_a_position[index].x, nv3->pgraph.win95_gdi_text.rect_a_position[index].y);
                }
                
                return;
            }
            /* Type B: Clipped Rectangle */
            else if (method_id >= NV3_W95TXT_B_CLIP_CLIPRECT_START && method_id <= NV3_W95TXT_B_CLIP_CLIPRECT_END)
            {
                uint32_t index = (method_id - NV3_W95TXT_B_CLIP_CLIPRECT_START) >> 3;

                /* Works slightly differently, we define the bounds of the rectangle instead. */
                if (method_id & 0x04)
                {
                    nv3->pgraph.win95_gdi_text.clipped_rect[index].right = (param & 0xFFFF);
                    nv3->pgraph.win95_gdi_text.clipped_rect[index].bottom = ((param >> 16) & 0xFFFF);

                    nv_log("Method Execution: Rect GDI-B%d Right,Bottom=%d,%d\n", index, nv3->pgraph.win95_gdi_text.clipped_rect[index].right, 
                        nv3->pgraph.win95_gdi_text.clipped_rect[index].bottom);

                    nv3_render_rect_clipped(nv3->pgraph.win95_gdi_text.clipped_rect[index], 
                        nv3->pgraph.win95_gdi_text.color_b, grobj);
                }
                else // left,top
                {
                    nv3->pgraph.win95_gdi_text.clipped_rect[index].left = (param & 0xFFFF);
                    nv3->pgraph.win95_gdi_text.clipped_rect[index].top = ((param >> 16) & 0xFFFF);
                    
                    nv_log("Method Execution: Rect GDI-B%d Left,Top=%d,%d\n", index,
                        nv3->pgraph.win95_gdi_text.clipped_rect[index].left, nv3->pgraph.win95_gdi_text.clipped_rect[index].top);
                }

                return;
            }
            /* Type C */
            else if (method_id >= NV3_W95TXT_C_CLIP_CLIPRECT_START && method_id <= NV3_W95TXT_C_CLIP_CLIPRECT_END)
            {
                /* lol */
                uint32_t index = (method_id - NV3_W95TXT_C_CLIP_CLIPRECT_START) >> 3;

                nv3->pgraph.win95_gdi_text.bitmap_c[index] = param;

                /* Mammoth logger! */
                nv_log("Method Execution: Rect GDI-C%d Data=%08x Size%04x,%04x Point%04x,%04x Color=%08x Clip Left=0x%04x Right=0x%04x Top=0x%04x Bottom=0x%04x",
                index, param, nv3->pgraph.win95_gdi_text.size_c.x, nv3->pgraph.win95_gdi_text.size_c.y,
                nv3->pgraph.win95_gdi_text.point_c.x, nv3->pgraph.win95_gdi_text.point_c.y, 
                nv3->pgraph.win95_gdi_text.color1_c, 
                nv3->pgraph.win95_gdi_text.clip_c.left, nv3->pgraph.win95_gdi_text.clip_c.right, 
                nv3->pgraph.win95_gdi_text.clip_c.top, nv3->pgraph.win95_gdi_text.clip_c.bottom);
                
                nv3_render_gdi_transparent_bitmap(false, nv3->pgraph.win95_gdi_text.color1_c, nv3->pgraph.win95_gdi_text.bitmap_c[index], grobj);
                return;
            }  
            else if (method_id >= NV3_W95TXT_D_CLIP_CLIPRECT_START && method_id <= NV3_W95TXT_D_CLIP_CLIPRECT_END)
            {
                /* lol */
                uint32_t index = (method_id - NV3_W95TXT_D_CLIP_CLIPRECT_START) >> 3;

                nv3->pgraph.win95_gdi_text.bitmap_d[index] = param;

                /* Mammoth logger! */
                nv_log("Method Execution: Rect GDI-D%d Data=%08x SizeIn%04x,%04x SizeOut%04x,%04x Point%04x,%04x Color=%08x Clip Left=0x%04x Right=0x%04x Top=0x%04x Bottom=0x%04x",
                index, param, nv3->pgraph.win95_gdi_text.size_in_d.x, nv3->pgraph.win95_gdi_text.size_in_d.y,
                nv3->pgraph.win95_gdi_text.size_out_d.x, nv3->pgraph.win95_gdi_text.size_out_d.y,
                nv3->pgraph.win95_gdi_text.point_d.x, nv3->pgraph.win95_gdi_text.point_d.y, 
                nv3->pgraph.win95_gdi_text.color1_d, 
                nv3->pgraph.win95_gdi_text.clip_d.left, nv3->pgraph.win95_gdi_text.clip_d.right, 
                nv3->pgraph.win95_gdi_text.clip_d.top, nv3->pgraph.win95_gdi_text.clip_d.bottom);
                
                nv3_render_gdi_transparent_bitmap(true, nv3->pgraph.win95_gdi_text.color1_d, nv3->pgraph.win95_gdi_text.bitmap_d[index], grobj);
                return;
            }
            else if (method_id >= NV3_W95TXT_E_CLIP_CLIPRECT_START && method_id <= NV3_W95TXT_E_CLIP_CLIPRECT_END)
            {
                /* lol */
                uint32_t index = (method_id - NV3_W95TXT_E_CLIP_CLIPRECT_START) >> 3;

                nv3->pgraph.win95_gdi_text.bitmap_e[index] = param;

                /* Mammoth logger! */
                nv_log("Method Execution: Rect GDI-E%d Data=%08x SizeIn%04x,%04x SizeOut%04x,%04x Point%04x,%04x Color=%08x Clip Left=0x%04x Right=0x%04x Top=0x%04x Bottom=0x%04x",
                index, param, nv3->pgraph.win95_gdi_text.size_in_e.x, nv3->pgraph.win95_gdi_text.size_in_e.y,
                nv3->pgraph.win95_gdi_text.size_out_e.x, nv3->pgraph.win95_gdi_text.size_out_e.y,
                nv3->pgraph.win95_gdi_text.point_e.x, nv3->pgraph.win95_gdi_text.point_e.y, 
                nv3->pgraph.win95_gdi_text.color1_e, 
                nv3->pgraph.win95_gdi_text.clip_e.left, nv3->pgraph.win95_gdi_text.clip_e.right, nv3->pgraph.win95_gdi_text.clip_e.top, nv3->pgraph.win95_gdi_text.clip_e.bottom);
                
                nv3_render_gdi_1bpp_bitmap(nv3->pgraph.win95_gdi_text.color0_e, nv3->pgraph.win95_gdi_text.color1_e, nv3->pgraph.win95_gdi_text.bitmap_e[index], grobj);
                return;
            }

            warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            break;
    }
}