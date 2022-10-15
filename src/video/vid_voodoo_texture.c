/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		3DFX Voodoo emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2008-2020 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_voodoo_common.h>
#include <86box/vid_voodoo_dither.h>
#include <86box/vid_voodoo_regs.h>
#include <86box/vid_voodoo_render.h>
#include <86box/vid_voodoo_texture.h>

#ifdef ENABLE_VOODOO_TEXTURE_LOG
int voodoo_texture_do_log = ENABLE_VOODOO_TEXTURE_LOG;

static void
voodoo_texture_log(const char *fmt, ...)
{
    va_list ap;

    if (voodoo_texture_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define voodoo_texture_log(fmt, ...)
#endif

void
voodoo_recalc_tex(voodoo_t *voodoo, int tmu)
{
    int      aspect = (voodoo->params.tLOD[tmu] >> 21) & 3;
    int      width = 256, height = 256;
    int      shift = 8;
    int      lod;
    uint32_t base    = voodoo->params.texBaseAddr[tmu];
    uint32_t offset  = 0;
    int      tex_lod = 0;
    uint32_t offsets[LOD_MAX + 3];
    int      widths[LOD_MAX + 3], heights[LOD_MAX + 3], shifts[LOD_MAX + 3];

    if (voodoo->params.tLOD[tmu] & LOD_S_IS_WIDER)
        height >>= aspect;
    else {
        width >>= aspect;
        shift -= aspect;
    }

    for (lod = 0; lod <= LOD_MAX + 2; lod++) {
        offsets[lod] = offset;
        widths[lod]  = width >> lod;
        heights[lod] = height >> lod;
        shifts[lod]  = shift - lod;

        if (!widths[lod])
            widths[lod] = 1;
        if (!heights[lod])
            heights[lod] = 1;
        if (shifts[lod] < 0)
            shifts[lod] = 0;

        if (!(voodoo->params.tLOD[tmu] & LOD_SPLIT) || ((lod & 1) && (voodoo->params.tLOD[tmu] & LOD_ODD)) || (!(lod & 1) && !(voodoo->params.tLOD[tmu] & LOD_ODD))) {
            if (voodoo->params.tformat[tmu] & 8)
                offset += (width >> lod) * (height >> lod) * 2;
            else
                offset += (width >> lod) * (height >> lod);
        }
    }

    if ((voodoo->params.textureMode[tmu] & TEXTUREMODE_TRILINEAR) && (voodoo->params.tLOD[tmu] & LOD_ODD))
        tex_lod++; /*Skip LOD 0*/

    //        voodoo_texture_log("TMU %i:    %08x\n", tmu, voodoo->params.textureMode[tmu]);
    for (lod = 0; lod <= LOD_MAX + 1; lod++) {
        if (voodoo->params.tLOD[tmu] & LOD_TMULTIBASEADDR) {
            switch (tex_lod) {
                case 0:
                    base = voodoo->params.texBaseAddr[tmu];
                    break;
                case 1:
                    base = voodoo->params.texBaseAddr1[tmu];
                    break;
                case 2:
                    base = voodoo->params.texBaseAddr2[tmu];
                    break;
                default:
                    base = voodoo->params.texBaseAddr38[tmu];
                    break;
            }
        }

        voodoo->params.tex_base[tmu][lod] = base + offsets[tex_lod];
        if (voodoo->params.tformat[tmu] & 8)
            voodoo->params.tex_end[tmu][lod] = base + offsets[tex_lod] + (widths[tex_lod] * heights[tex_lod] * 2);
        else
            voodoo->params.tex_end[tmu][lod] = base + offsets[tex_lod] + (widths[tex_lod] * heights[tex_lod]);
        voodoo->params.tex_w_mask[tmu][lod]  = widths[tex_lod] - 1;
        voodoo->params.tex_w_nmask[tmu][lod] = ~(widths[tex_lod] - 1);
        voodoo->params.tex_h_mask[tmu][lod]  = heights[tex_lod] - 1;
        voodoo->params.tex_shift[tmu][lod]   = shifts[tex_lod];
        voodoo->params.tex_lod[tmu][lod]     = tex_lod;

        if (!(voodoo->params.textureMode[tmu] & TEXTUREMODE_TRILINEAR) || ((lod & 1) && (voodoo->params.tLOD[tmu] & LOD_ODD)) || (!(lod & 1) && !(voodoo->params.tLOD[tmu] & LOD_ODD))) {
            if (!(voodoo->params.tLOD[tmu] & LOD_ODD) || lod != 0) {
                if (voodoo->params.textureMode[tmu] & TEXTUREMODE_TRILINEAR)
                    tex_lod += 2;
                else
                    tex_lod++;
            }
        }
    }

    voodoo->params.tex_width[tmu] = width;
}

#define makergba(r, g, b, a) ((b) | ((g) << 8) | ((r) << 16) | ((a) << 24))

void
voodoo_use_texture(voodoo_t *voodoo, voodoo_params_t *params, int tmu)
{
    int      c, d;
    int      lod;
    int      lod_min, lod_max;
    uint32_t addr = 0, addr_end;
    uint32_t palette_checksum;

    lod_min = (params->tLOD[tmu] >> 2) & 15;
    lod_max = (params->tLOD[tmu] >> 8) & 15;

    if (params->tformat[tmu] == TEX_PAL8 || params->tformat[tmu] == TEX_APAL8 || params->tformat[tmu] == TEX_APAL88) {
        if (voodoo->palette_dirty[tmu]) {
            palette_checksum = 0;

            for (c = 0; c < 256; c++)
                palette_checksum ^= voodoo->palette[tmu][c].u;

            voodoo->palette_checksum[tmu] = palette_checksum;
            voodoo->palette_dirty[tmu]    = 0;
        } else
            palette_checksum = voodoo->palette_checksum[tmu];
    } else
        palette_checksum = 0;

    if ((voodoo->params.tLOD[tmu] & LOD_SPLIT) && (voodoo->params.tLOD[tmu] & LOD_ODD) && (voodoo->params.tLOD[tmu] & LOD_TMULTIBASEADDR))
        addr = params->texBaseAddr1[tmu];
    else
        addr = params->texBaseAddr[tmu];

    /*Try to find texture in cache*/
    for (c = 0; c < TEX_CACHE_MAX; c++) {
        if (voodoo->texture_cache[tmu][c].base == addr && voodoo->texture_cache[tmu][c].tLOD == (params->tLOD[tmu] & 0xf00fff) && voodoo->texture_cache[tmu][c].palette_checksum == palette_checksum) {
            params->tex_entry[tmu] = c;
            voodoo->texture_cache[tmu][c].refcount++;
            return;
        }
    }

    /*Texture not found, search for unused texture*/
    do {
        for (c = 0; c < TEX_CACHE_MAX; c++) {
            voodoo->texture_last_removed++;
            voodoo->texture_last_removed &= (TEX_CACHE_MAX - 1);
            if (voodoo->texture_cache[tmu][voodoo->texture_last_removed].refcount == voodoo->texture_cache[tmu][voodoo->texture_last_removed].refcount_r[0] && (voodoo->render_threads == 1 || voodoo->texture_cache[tmu][voodoo->texture_last_removed].refcount == voodoo->texture_cache[tmu][voodoo->texture_last_removed].refcount_r[1]))
                break;
        }
        if (c == TEX_CACHE_MAX)
            voodoo_wait_for_render_thread_idle(voodoo);
    } while (c == TEX_CACHE_MAX);
    if (c == TEX_CACHE_MAX)
        fatal("Texture cache full!\n");

    c = voodoo->texture_last_removed;

    if ((voodoo->params.tLOD[tmu] & LOD_SPLIT) && (voodoo->params.tLOD[tmu] & LOD_ODD) && (voodoo->params.tLOD[tmu] & LOD_TMULTIBASEADDR))
        voodoo->texture_cache[tmu][c].base = params->texBaseAddr1[tmu];
    else
        voodoo->texture_cache[tmu][c].base = params->texBaseAddr[tmu];
    voodoo->texture_cache[tmu][c].tLOD = params->tLOD[tmu] & 0xf00fff;

    lod_min = (params->tLOD[tmu] >> 2) & 15;
    lod_max = (params->tLOD[tmu] >> 8) & 15;
    //        voodoo_texture_log("  add new texture to %i tformat=%i %08x LOD=%i-%i tmu=%i\n", c, voodoo->params.tformat[tmu], params->texBaseAddr[tmu], lod_min, lod_max, tmu);
    lod_min = MIN(lod_min, 8);
    lod_max = MIN(lod_max, 8);
    for (lod = lod_min; lod <= lod_max; lod++) {
        uint32_t *base     = &voodoo->texture_cache[tmu][c].data[texture_offset[lod]];
        uint32_t  tex_addr = params->tex_base[tmu][lod] & voodoo->texture_mask;
        int       x, y;
        int       shift = 8 - params->tex_lod[tmu][lod];
        rgba_u   *pal;

        // voodoo_texture_log("  LOD %i : %08x - %08x %i %i,%i\n", lod, params->tex_base[tmu][lod] & voodoo->texture_mask, addr, voodoo->params.tformat[tmu], voodoo->params.tex_w_mask[tmu][lod],voodoo->params.tex_h_mask[tmu][lod]);

        switch (params->tformat[tmu]) {
            case TEX_RGB332:
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr + x) & voodoo->texture_mask];

                        base[x] = makergba(rgb332[dat].r, rgb332[dat].g, rgb332[dat].b, 0xff);
                    }
                    tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                    base += (1 << shift);
                }
                break;

            case TEX_Y4I2Q2:
                pal = voodoo->ncc_lookup[tmu][(voodoo->params.textureMode[tmu] & TEXTUREMODE_NCC_SEL) ? 1 : 0];
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr + x) & voodoo->texture_mask];

                        base[x] = makergba(pal[dat].rgba.r, pal[dat].rgba.g, pal[dat].rgba.b, 0xff);
                    }
                    tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                    base += (1 << shift);
                }
                break;

            case TEX_A8:
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr + x) & voodoo->texture_mask];

                        base[x] = makergba(dat, dat, dat, dat);
                    }
                    tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                    base += (1 << shift);
                }
                break;

            case TEX_I8:
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr + x) & voodoo->texture_mask];

                        base[x] = makergba(dat, dat, dat, 0xff);
                    }
                    tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                    base += (1 << shift);
                }
                break;

            case TEX_AI8:
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr + x) & voodoo->texture_mask];

                        base[x] = makergba((dat & 0x0f) | ((dat << 4) & 0xf0), (dat & 0x0f) | ((dat << 4) & 0xf0), (dat & 0x0f) | ((dat << 4) & 0xf0), (dat & 0xf0) | ((dat >> 4) & 0x0f));
                    }
                    tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                    base += (1 << shift);
                }
                break;

            case TEX_PAL8:
                pal = voodoo->palette[tmu];
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr + x) & voodoo->texture_mask];

                        base[x] = makergba(pal[dat].rgba.r, pal[dat].rgba.g, pal[dat].rgba.b, 0xff);
                    }
                    tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                    base += (1 << shift);
                }
                break;

            case TEX_APAL8:
                pal = voodoo->palette[tmu];
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr + x) & voodoo->texture_mask];

                        int r = ((pal[dat].rgba.r & 3) << 6) | ((pal[dat].rgba.g & 0xf0) >> 2) | (pal[dat].rgba.r & 3);
                        int g = ((pal[dat].rgba.g & 0xf) << 4) | ((pal[dat].rgba.b & 0xc0) >> 4) | ((pal[dat].rgba.g & 0xf) >> 2);
                        int b = ((pal[dat].rgba.b & 0x3f) << 2) | ((pal[dat].rgba.b & 0x30) >> 4);
                        int a = (pal[dat].rgba.r & 0xfc) | ((pal[dat].rgba.r & 0xc0) >> 6);

                        base[x] = makergba(r, g, b, a);
                    }
                    tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                    base += (1 << shift);
                }
                break;

            case TEX_ARGB8332:
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint16_t dat = *(uint16_t *) &voodoo->tex_mem[tmu][(tex_addr + x * 2) & voodoo->texture_mask];

                        base[x] = makergba(rgb332[dat & 0xff].r, rgb332[dat & 0xff].g, rgb332[dat & 0xff].b, dat >> 8);
                    }
                    tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod] + 1));
                    base += (1 << shift);
                }
                break;

            case TEX_A8Y4I2Q2:
                pal = voodoo->ncc_lookup[tmu][(voodoo->params.textureMode[tmu] & TEXTUREMODE_NCC_SEL) ? 1 : 0];
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint16_t dat = *(uint16_t *) &voodoo->tex_mem[tmu][(tex_addr + x * 2) & voodoo->texture_mask];

                        base[x] = makergba(pal[dat & 0xff].rgba.r, pal[dat & 0xff].rgba.g, pal[dat & 0xff].rgba.b, dat >> 8);
                    }
                    tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod] + 1));
                    base += (1 << shift);
                }
                break;

            case TEX_R5G6B5:
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint16_t dat = *(uint16_t *) &voodoo->tex_mem[tmu][(tex_addr + x * 2) & voodoo->texture_mask];

                        base[x] = makergba(rgb565[dat].r, rgb565[dat].g, rgb565[dat].b, 0xff);
                    }
                    tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod] + 1));
                    base += (1 << shift);
                }
                break;

            case TEX_ARGB1555:
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint16_t dat = *(uint16_t *) &voodoo->tex_mem[tmu][(tex_addr + x * 2) & voodoo->texture_mask];

                        base[x] = makergba(argb1555[dat].r, argb1555[dat].g, argb1555[dat].b, argb1555[dat].a);
                    }
                    tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod] + 1));
                    base += (1 << shift);
                }
                break;

            case TEX_ARGB4444:
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint16_t dat = *(uint16_t *) &voodoo->tex_mem[tmu][(tex_addr + x * 2) & voodoo->texture_mask];

                        base[x] = makergba(argb4444[dat].r, argb4444[dat].g, argb4444[dat].b, argb4444[dat].a);
                    }
                    tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod] + 1));
                    base += (1 << shift);
                }
                break;

            case TEX_A8I8:
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint16_t dat = *(uint16_t *) &voodoo->tex_mem[tmu][(tex_addr + x * 2) & voodoo->texture_mask];

                        base[x] = makergba(dat & 0xff, dat & 0xff, dat & 0xff, dat >> 8);
                    }
                    tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod] + 1));
                    base += (1 << shift);
                }
                break;

            case TEX_APAL88:
                pal = voodoo->palette[tmu];
                for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod] + 1; y++) {
                    for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod] + 1; x++) {
                        uint16_t dat = *(uint16_t *) &voodoo->tex_mem[tmu][(tex_addr + x * 2) & voodoo->texture_mask];

                        base[x] = makergba(pal[dat & 0xff].rgba.r, pal[dat & 0xff].rgba.g, pal[dat & 0xff].rgba.b, dat >> 8);
                    }
                    tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod] + 1));
                    base += (1 << shift);
                }
                break;

            default:
                fatal("Unknown texture format %i\n", params->tformat[tmu]);
        }
    }

    voodoo->texture_cache[tmu][c].is16 = voodoo->params.tformat[tmu] & 8;

    if (params->tformat[tmu] == TEX_PAL8 || params->tformat[tmu] == TEX_APAL8 || params->tformat[tmu] == TEX_APAL88)
        voodoo->texture_cache[tmu][c].palette_checksum = palette_checksum;
    else
        voodoo->texture_cache[tmu][c].palette_checksum = 0;

    if (lod_min == 0) {
        voodoo->texture_cache[tmu][c].addr_start[0] = voodoo->params.tex_base[tmu][0];
        voodoo->texture_cache[tmu][c].addr_end[0]   = voodoo->params.tex_end[tmu][0];
    } else
        voodoo->texture_cache[tmu][c].addr_start[0] = voodoo->texture_cache[tmu][c].addr_end[0] = 0;

    if (lod_min <= 1 && lod_max >= 1) {
        voodoo->texture_cache[tmu][c].addr_start[1] = voodoo->params.tex_base[tmu][1];
        voodoo->texture_cache[tmu][c].addr_end[1]   = voodoo->params.tex_end[tmu][1];
    } else
        voodoo->texture_cache[tmu][c].addr_start[1] = voodoo->texture_cache[tmu][c].addr_end[1] = 0;

    if (lod_min <= 2 && lod_max >= 2) {
        voodoo->texture_cache[tmu][c].addr_start[2] = voodoo->params.tex_base[tmu][2];
        voodoo->texture_cache[tmu][c].addr_end[2]   = voodoo->params.tex_end[tmu][2];
    } else
        voodoo->texture_cache[tmu][c].addr_start[2] = voodoo->texture_cache[tmu][c].addr_end[2] = 0;

    if (lod_max >= 3) {
        voodoo->texture_cache[tmu][c].addr_start[3] = voodoo->params.tex_base[tmu][(lod_min > 3) ? lod_min : 3];
        voodoo->texture_cache[tmu][c].addr_end[3]   = voodoo->params.tex_end[tmu][(lod_max < 8) ? lod_max : 8];
    } else
        voodoo->texture_cache[tmu][c].addr_start[3] = voodoo->texture_cache[tmu][c].addr_end[3] = 0;

    for (d = 0; d < 4; d++) {
        addr     = voodoo->texture_cache[tmu][c].addr_start[d];
        addr_end = voodoo->texture_cache[tmu][c].addr_end[d];

        if (addr_end != 0) {
            for (; addr <= addr_end; addr += (1 << TEX_DIRTY_SHIFT))
                voodoo->texture_present[tmu][(addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT] = 1;
        }
    }

    params->tex_entry[tmu] = c;
    voodoo->texture_cache[tmu][c].refcount++;
}

void
flush_texture_cache(voodoo_t *voodoo, uint32_t dirty_addr, int tmu)
{
    int wait_for_idle = 0;
    int c;

    memset(voodoo->texture_present[tmu], 0, sizeof(voodoo->texture_present[0]));
    //        voodoo_texture_log("Evict %08x %i\n", dirty_addr, sizeof(voodoo->texture_present));
    for (c = 0; c < TEX_CACHE_MAX; c++) {
        if (voodoo->texture_cache[tmu][c].base != -1) {
            int d;

            for (d = 0; d < 4; d++) {
                int addr_start = voodoo->texture_cache[tmu][c].addr_start[d];
                int addr_end   = voodoo->texture_cache[tmu][c].addr_end[d];

                if (addr_end != 0) {
                    int addr_start_masked = addr_start & voodoo->texture_mask & ~0x3ff;
                    int addr_end_masked   = ((addr_end & voodoo->texture_mask) + 0x3ff) & ~0x3ff;

                    if (addr_end_masked < addr_start_masked)
                        addr_end_masked = voodoo->texture_mask + 1;
                    if (dirty_addr >= addr_start_masked && dirty_addr < addr_end_masked) {
                        //                                voodoo_texture_log("  Evict texture %i %08x\n", c, voodoo->texture_cache[tmu][c].base);

                        if (voodoo->texture_cache[tmu][c].refcount != voodoo->texture_cache[tmu][c].refcount_r[0] || (voodoo->render_threads == 2 && voodoo->texture_cache[tmu][c].refcount != voodoo->texture_cache[tmu][c].refcount_r[1]))
                            wait_for_idle = 1;

                        voodoo->texture_cache[tmu][c].base = -1;
                    } else {
                        for (; addr_start <= addr_end; addr_start += (1 << TEX_DIRTY_SHIFT))
                            voodoo->texture_present[tmu][(addr_start & voodoo->texture_mask) >> TEX_DIRTY_SHIFT] = 1;
                    }
                }
            }
        }
    }
    if (wait_for_idle)
        voodoo_wait_for_render_thread_idle(voodoo);
}

void
voodoo_tex_writel(uint32_t addr, uint32_t val, void *p)
{
    int       lod, s, t;
    voodoo_t *voodoo = (voodoo_t *) p;
    int       tmu;

    if (addr & 0x400000)
        return; /*TREX != 0*/

    tmu = (addr & 0x200000) ? 1 : 0;

    if (tmu && !voodoo->dual_tmus)
        return;

    if (voodoo->type < VOODOO_BANSHEE) {
        if (!(voodoo->params.tformat[tmu] & 8) && voodoo->type >= VOODOO_BANSHEE) {
            lod = (addr >> 16) & 0xf;
            t   = (addr >> 8) & 0xff;
        } else {
            lod = (addr >> 17) & 0xf;
            t   = (addr >> 9) & 0xff;
        }
        if (voodoo->params.tformat[tmu] & 8)
            s = (addr >> 1) & 0xfe;
        else {
            if ((voodoo->params.textureMode[tmu] & (1 << 31)) || voodoo->type >= VOODOO_BANSHEE)
                s = addr & 0xfc;
            else
                s = (addr >> 1) & 0xfc;
        }
        if (lod > LOD_MAX)
            return;

        //                if (addr >= 0x200000)
        //                        return;

        if (voodoo->params.tformat[tmu] & 8)
            addr = voodoo->params.tex_base[tmu][lod] + s * 2 + (t << voodoo->params.tex_shift[tmu][lod]) * 2;
        else
            addr = voodoo->params.tex_base[tmu][lod] + s + (t << voodoo->params.tex_shift[tmu][lod]);
    } else
        addr = (addr & 0x1ffffc) + voodoo->params.tex_base[tmu][0];

    if (voodoo->texture_present[tmu][(addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT]) {
        //                voodoo_texture_log("texture_present at %08x %i\n", addr, (addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT);
        flush_texture_cache(voodoo, addr & voodoo->texture_mask, tmu);
    }
    if (voodoo->type == VOODOO_3 && voodoo->texture_present[tmu ^ 1][(addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT]) {
        //                voodoo_texture_log("texture_present at %08x %i\n", addr, (addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT);
        flush_texture_cache(voodoo, addr & voodoo->texture_mask, tmu ^ 1);
    }
    *(uint32_t *) (&voodoo->tex_mem[tmu][addr & voodoo->texture_mask]) = val;
}
