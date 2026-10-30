/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3: Methods for class 0x0E (Get image from vram and scale it)
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2026 Connor Hyde
 *          Copyright 2026 Marcos Alves
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

static struct {
    int32_t  clip_x, clip_y;
    uint32_t clip_w, clip_h;
    int32_t  x, y;
    uint32_t w, h;
    uint32_t dudx, dvdy;
    uint32_t src_w, src_h;
    uint32_t pitch, offset;
} nv3_sifm;

static uint8_t
nv3_sifm_clamp(int32_t v)
{
    return (v < 0) ? 0 : ((v > 255) ? 255 : v);
}

/* A source pixel as a colour in the object's format; YUV becomes R8G8B8 */
static nv3_color_expanded_t
nv3_sifm_fetch(nv3_grobj_t grobj, uint32_t u, uint32_t v)
{
    uint32_t inst   = nv3->pgraph.dma_settings & 0xFFFF;
    uint32_t format = grobj.grobj_0 & 0x07;
    uint32_t row    = nv3_sifm.offset + v * nv3_sifm.pitch;
    uint32_t raw;

    switch (format) {
        case nv3_pgraph_pixel_format_y8:
            raw = nv3_dma_read8(inst, row + u);
            break;
        case nv3_pgraph_pixel_format_r5g5b5:
        case nv3_pgraph_pixel_format_y16:
            raw = nv3_dma_read8(inst, row + u * 2) | (nv3_dma_read8(inst, row + u * 2 + 1) << 8);
            break;
        case nv3_pgraph_pixel_format_v8y8u8y18:
        case nv3_pgraph_pixel_format_y18v8y8u8:
        {
            /* a 4-byte pair of pixels: YUY2 (Y0 U Y1 V) or UYVY (U Y0 V Y1) */
            uint32_t pair = row + (u & ~1) * 2;
            uint8_t  b[4];
            for (int i = 0; i < 4; i++)
                b[i] = nv3_dma_read8(inst, pair + i);

            int32_t y, cb, cr;
            if (format == nv3_pgraph_pixel_format_v8y8u8y18) {
                y  = b[(u & 1) ? 2 : 0];
                cb = b[1];
                cr = b[3];
            } else {
                y  = b[(u & 1) ? 3 : 1];
                cb = b[0];
                cr = b[2];
            }
            y -= 16;
            cb -= 128;
            cr -= 128;
            uint32_t r  = nv3_sifm_clamp((298 * y + 409 * cr + 128) >> 8);
            uint32_t g  = nv3_sifm_clamp((298 * y - 100 * cb - 208 * cr + 128) >> 8);
            uint32_t bl = nv3_sifm_clamp((298 * y + 516 * cb + 128) >> 8);

            nv3_grobj_t rgb = grobj;
            rgb.grobj_0     = (grobj.grobj_0 & ~0x0F) | nv3_pgraph_pixel_format_r8g8b8;
            return nv3_render_expand_color(0xFF000000 | (r << 16) | (g << 8) | bl, rgb);
        }
        default:
            raw = 0;
            for (int i = 0; i < 4; i++)
                raw |= nv3_dma_read8(inst, row + u * 4 + i) << (i * 8);
            break;
    }
    return nv3_render_expand_color(raw, grobj);
}

/* CLIP (0x308 corner, 0x30c size), the destination rectangle (0x310 xy16,
   0x314 wh16), DU/DX and DV/DY (0x318/0x31c, 12.20), and the source: SIZE
   (0x400), PITCH (0x404), OFFSET (0x408) in the object's DMA object, and the
   start position (0x40c, 12.4 u and v), which draws */
void nv3_class_00e_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj)
{
    switch (method_id) {
        case 0x0308:
            nv3_sifm.clip_x = (int16_t) (param & 0xFFFF);
            nv3_sifm.clip_y = (int16_t) (param >> 16);
            break;
        case 0x030C:
            nv3_sifm.clip_w = param & 0xFFFF;
            nv3_sifm.clip_h = param >> 16;
            break;
        case 0x0310:
            nv3_sifm.x = (int16_t) (param & 0xFFFF);
            nv3_sifm.y = (int16_t) (param >> 16);
            break;
        case 0x0314:
            nv3_sifm.w = param & 0xFFFF;
            nv3_sifm.h = param >> 16;
            break;
        case 0x0318:
            nv3_sifm.dudx = param;
            break;
        case 0x031C:
            nv3_sifm.dvdy = param;
            break;
        case 0x0400:
            nv3_sifm.src_w = param & 0xFFFF;
            nv3_sifm.src_h = param >> 16;
            break;
        case 0x0404:
            nv3_sifm.pitch = param & 0xFFFF;
            break;
        case 0x0408:
            nv3_sifm.offset = param;
            break;
        case 0x040C:
        {
            int64_t u0 = (int64_t) (int16_t) (param & 0xFFFF) << 16;
            int64_t v0 = (int64_t) (int16_t) (param >> 16) << 16;

            if (!nv3_sifm.src_w || !nv3_sifm.src_h)
                break;
            for (uint32_t dy = 0; dy < nv3_sifm.h; dy++) {
                int32_t y = nv3_sifm.y + (int32_t) dy;
                if (y < nv3_sifm.clip_y || y >= nv3_sifm.clip_y + (int32_t) nv3_sifm.clip_h)
                    continue;

                int64_t v  = (v0 + (int64_t) dy * nv3_sifm.dvdy) >> 20;
                v          = (v < 0) ? 0 : ((v >= nv3_sifm.src_h) ? nv3_sifm.src_h - 1 : v);
                for (uint32_t dx = 0; dx < nv3_sifm.w; dx++) {
                    int32_t x = nv3_sifm.x + (int32_t) dx;
                    if (x < nv3_sifm.clip_x || x >= nv3_sifm.clip_x + (int32_t) nv3_sifm.clip_w)
                        continue;

                    int64_t u = (u0 + (int64_t) dx * nv3_sifm.dudx) >> 20;
                    u         = (u < 0) ? 0 : ((u >= nv3_sifm.src_w) ? nv3_sifm.src_w - 1 : u);
                    nv3_render_pixel_ex(x, y, nv3_sifm_fetch(grobj, (uint32_t) u, (uint32_t) v), grobj, true);
                }
            }
            break;
        }
        default:
            nv_warning("%s: Invalid or unimplemented method 0x%04x\n", nv3_class_names[context.class_id & 0x1F], method_id);
            nv3_pgraph_interrupt_invalid(NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING);
            return;
    }
}
