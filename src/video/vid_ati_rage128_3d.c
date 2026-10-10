/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- 3D engine.
 *
 *          The CCE 3D state registers and the 3D type-3 packets: the
 *          register latch, the vertex walks of the draw packets, triangle
 *          and line setup with exact integer edge functions, and the
 *          per-pixel pipeline (texture sampling, texture combine, specular,
 *          fog, alpha test, Z and stencil, alpha blend, dither). It also
 *          decides, per draw, which texture levels and which Z and color
 *          surfaces are copied into host-side staging arenas before the
 *          rasterizer runs.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 Software Development Guide",
 *              SDK-G04000 Rev 0.01, August 1999. Cited as "SDK: ...".
 *
 *          [3] ATI Technologies, "RAGE 128 Register Reference Supplement:
 *              Registers for CCE 3D Packets", 1999. Cited as "CCE 3D
 *              supplement" with the register name; it has no page numbers.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_ati_rage128.h>
#include <86box/vid_ati_rage128_regs.h>

void
rage128_3d_reset(rage128_t *dev)
{
    /* The memset leaves PM4_VC_FPU_SETUP at 0, the reset value the CCE 3D
       supplement gives every one of its fields (CCE 3D supplement,
       PM4_VC_FPU_SETUP): both face functions at 0 (cull), solid shading.
       A client that draws through the vertex walker without writing the
       register has both windings culled, as on the chip; a driver
       programs the register before its first draw (Mesa r128
       r128_state.c r128DDInitState). */
    memset(&dev->t3d, 0, sizeof(dev->t3d));
    dev->vc_bundle.pending = 0;
    /* All planes enabled, so a driver that never writes PLANE_3D_MASK_C
       (the Windows 98 Direct3D driver, DOS programs) is not masked. The
       guide's reset value is 0 (RRG: PLANE_3D_MASK_C, p. 3-260 /
       PDF 278). */
    dev->t3d.plane_3d_mask = 0xffffffff;
}

int
rage128_3d_reg_read(rage128_t *dev, uint32_t off, uint32_t *val)
{
    if (off >= RAGE128_PRIM_TEX_OFFSET_C(0) && off <= RAGE128_PRIM_TEX_OFFSET_C(10)) {
        *val = dev->t3d.prim_tex_offset[(off - RAGE128_PRIM_TEX_OFFSET_C(0)) >> 2];
        return 1;
    }
    if (off >= RAGE128_SEC_TEX_OFFSET_C(0) && off <= RAGE128_SEC_TEX_OFFSET_C(10)) {
        *val = dev->t3d.sec_tex_offset[(off - RAGE128_SEC_TEX_OFFSET_C(0)) >> 2];
        return 1;
    }

    /* clang-format off */
    switch (off) {
        case RAGE128_SCALE_3D_CNTL:        *val = dev->t3d.scale_3d_cntl; return 1;
        case RAGE128_SCALE_3D_DATATYPE:    *val = dev->t3d.scale_3d_datatype; return 1;
        case RAGE128_COMPOSITE_SHADOW_ID:  *val = dev->t3d.composite_shadow_id; return 1;
        case RAGE128_CLR_CMP_CLR_3D:       *val = dev->t3d.clr_cmp_clr_3d; return 1;
        case RAGE128_CLR_CMP_MSK_3D:       *val = dev->t3d.clr_cmp_msk_3d; return 1;
        case RAGE128_SETUP_CNTL:           *val = dev->t3d.setup_cntl; return 1;
        case RAGE128_WINDOW_XY_OFFSET:     *val = dev->t3d.window_xy_offset; return 1;
        case RAGE128_SETUP_CNTL_PM4:       *val = dev->t3d.setup_cntl_pm4; return 1;
        case RAGE128_Z_OFFSET_C:           *val = dev->t3d.z_offset; return 1;
        case RAGE128_Z_PITCH_C:            *val = dev->t3d.z_pitch; return 1;
        case RAGE128_Z_STEN_CNTL_C:        *val = dev->t3d.z_sten_cntl; return 1;
        case RAGE128_TEX_CNTL_C:           *val = dev->t3d.tex_cntl; return 1;
        case RAGE128_MISC_3D_STATE_CNTL:   *val = dev->t3d.misc_3d_state_cntl; return 1;
        case RAGE128_TEX_CLR_CMP_CLR_C:    *val = dev->t3d.tex_clr_cmp_clr; return 1;
        case RAGE128_TEX_CLR_CMP_MSK_C:    *val = dev->t3d.tex_clr_cmp_msk; return 1;
        case RAGE128_FOG_COLOR_C:          *val = dev->t3d.fog_color; return 1;
        case RAGE128_PRIM_TEX_CNTL_C:      *val = dev->t3d.prim_tex_cntl; return 1;
        case RAGE128_PRIM_TEX_COMBINE_CNTL_C: *val = dev->t3d.prim_tex_combine_cntl; return 1;
        case RAGE128_TEX_SIZE_PITCH_C:     *val = dev->t3d.tex_size_pitch; return 1;
        case RAGE128_SEC_TEX_CNTL_C:       *val = dev->t3d.sec_tex_cntl; return 1;
        case RAGE128_SEC_TEX_COMBINE_CNTL_C: *val = dev->t3d.sec_tex_combine_cntl; return 1;
        case RAGE128_CONSTANT_COLOR_C:     *val = dev->t3d.constant_color; return 1;
        case RAGE128_PRIM_TEX_BORDER_COLOR_C: *val = dev->t3d.prim_tex_border_color; return 1;
        case RAGE128_SEC_TEX_BORDER_COLOR_C:  *val = dev->t3d.sec_tex_border_color; return 1;
        case RAGE128_STEN_REF_MASK_C:      *val = dev->t3d.sten_ref_mask; return 1;
        case RAGE128_STEN_REF_MASK_LEGACY:
            *val = dev->t3d.sten_ref_mask;
            return 1;
        case RAGE128_PLANE_3D_MASK_C:      *val = dev->t3d.plane_3d_mask; return 1;

        /* The _C copies of 2D datapath registers in the CCE 3D context
           block are aliases of the 2D registers (RRG: DST_PITCH_OFFSET_C,
           p. 3-145 / PDF 163; RRG: DP_GUI_MASTER_CNTL_C, p. 3-177 /
           PDF 195; RRG: SC_TOP_LEFT_C, p. 3-162 / PDF 180; RRG:
           SC_BOTTOM_RIGHT_C, p. 3-163 / PDF 181), so they read back the
           2D state. */
        case RAGE128_DST_PITCH_OFFSET_C:
        case RAGE128_DP_GUI_MASTER_CNTL_C:
        case RAGE128_SC_TOP_LEFT_C:
        case RAGE128_SC_BOTTOM_RIGHT_C:
            return rage128_2d_reg_read(dev,
                (off == RAGE128_DP_GUI_MASTER_CNTL_C) ? RAGE128_DP_GUI_MASTER_CNTL
              : (off == RAGE128_SC_TOP_LEFT_C)        ? RAGE128_SC_TOP_LEFT
              : (off == RAGE128_SC_BOTTOM_RIGHT_C)    ? RAGE128_SC_BOTTOM_RIGHT
              : RAGE128_DST_PITCH_OFFSET, val);

        default:
            break;
    }
    /* clang-format on */
    return 0;
}

int
rage128_3d_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask)
{
#define MERGE(field) ((field) = ((field) & ~mask) | (val & mask))
    if (off >= RAGE128_PRIM_TEX_OFFSET_C(0) && off <= RAGE128_PRIM_TEX_OFFSET_C(10)) {
        MERGE(dev->t3d.prim_tex_offset[(off - RAGE128_PRIM_TEX_OFFSET_C(0)) >> 2]);
        return 1;
    }
    if (off >= RAGE128_SEC_TEX_OFFSET_C(0) && off <= RAGE128_SEC_TEX_OFFSET_C(10)) {
        MERGE(dev->t3d.sec_tex_offset[(off - RAGE128_SEC_TEX_OFFSET_C(0)) >> 2]);
        return 1;
    }

    /* clang-format off */
    switch (off) {
        case RAGE128_SCALE_3D_CNTL:        MERGE(dev->t3d.scale_3d_cntl); return 1;
        case RAGE128_SCALE_3D_DATATYPE:
            /* Latched only. The guides give this register no field list;
               the CCE 3D supplement names parts of it as aliases of the
               datatype and palette fields of PRIM_TEX_CNTL_C and
               SEC_TEX_CNTL_C, and the 3D path reads those registers. */
            MERGE(dev->t3d.scale_3d_datatype);
            return 1;
        case RAGE128_COMPOSITE_SHADOW_ID:
            MERGE(dev->t3d.composite_shadow_id);
            dev->t3d.composite_shadow_id &= RAGE128_SHADOW_ID_WMASK;
            return 1;
        case RAGE128_CLR_CMP_CLR_3D:       MERGE(dev->t3d.clr_cmp_clr_3d); return 1;
        case RAGE128_CLR_CMP_MSK_3D:       MERGE(dev->t3d.clr_cmp_msk_3d); return 1;
        case RAGE128_SETUP_CNTL:           MERGE(dev->t3d.setup_cntl); return 1;
        case RAGE128_WINDOW_XY_OFFSET:     MERGE(dev->t3d.window_xy_offset); return 1;
        case RAGE128_DRAW_LINE_POINT:
            /* Draws a point or a line from the setup engine's vertex
               registers (RRG: DRAW_LINE_POINT, p. 3-250 / PDF 268). Setup
               driven by CPU register writes is not modeled: every driver
               seen here submits 3D work through CCE packets. */
            return 1;
        case RAGE128_SETUP_CNTL_PM4:       MERGE(dev->t3d.setup_cntl_pm4); return 1;
        case RAGE128_Z_OFFSET_C:           MERGE(dev->t3d.z_offset); return 1;
        case RAGE128_Z_PITCH_C:            MERGE(dev->t3d.z_pitch); return 1;
        case RAGE128_Z_STEN_CNTL_C:        MERGE(dev->t3d.z_sten_cntl); return 1;
        case RAGE128_TEX_CNTL_C:           MERGE(dev->t3d.tex_cntl); return 1;
        case RAGE128_MISC_3D_STATE_CNTL:   MERGE(dev->t3d.misc_3d_state_cntl); return 1;
        case RAGE128_TEX_CLR_CMP_CLR_C:    MERGE(dev->t3d.tex_clr_cmp_clr); return 1;
        case RAGE128_TEX_CLR_CMP_MSK_C:    MERGE(dev->t3d.tex_clr_cmp_msk); return 1;
        case RAGE128_FOG_COLOR_C:          MERGE(dev->t3d.fog_color); return 1;
        case RAGE128_PRIM_TEX_CNTL_C:      MERGE(dev->t3d.prim_tex_cntl); return 1;
        case RAGE128_PRIM_TEX_COMBINE_CNTL_C: MERGE(dev->t3d.prim_tex_combine_cntl); return 1;
        case RAGE128_TEX_SIZE_PITCH_C:     MERGE(dev->t3d.tex_size_pitch); return 1;
        case RAGE128_SEC_TEX_CNTL_C:       MERGE(dev->t3d.sec_tex_cntl); return 1;
        case RAGE128_SEC_TEX_COMBINE_CNTL_C: MERGE(dev->t3d.sec_tex_combine_cntl); return 1;
        /* The older SECONDARY_TEXTURE_COMBINE_CNTL (0x1a34), which the RRG
           names only in its revision history, in a list of deleted
           registers (RRG: Revision History, p. B-7 / PDF 321). In captured
           Windows 98 Direct3D traffic (3DMark99) the driver programs the
           second stage's combine here, for example 0x04182043 (modulate
           the texel with the previous stage's color), while
           SEC_TEX_COMBINE_CNTL_C (0x1d04) keeps 0x04181040 (disabled).
           Both offsets therefore write the same state, and the last write
           wins. */
        case RAGE128_SECONDARY_TEXTURE_COMBINE_CNTL: MERGE(dev->t3d.sec_tex_combine_cntl); return 1;
        case RAGE128_CONSTANT_COLOR_C:     MERGE(dev->t3d.constant_color); return 1;
        case RAGE128_PRIM_TEX_BORDER_COLOR_C: MERGE(dev->t3d.prim_tex_border_color); return 1;
        case RAGE128_SEC_TEX_BORDER_COLOR_C:  MERGE(dev->t3d.sec_tex_border_color); return 1;
        case RAGE128_STEN_REF_MASK_C:      MERGE(dev->t3d.sten_ref_mask); return 1;
            /* clang-format on */
        case RAGE128_STEN_REF_MASK_LEGACY:
            /* 0x1ad0 is in no public document. The Windows 98 and Windows
               2000 Direct3D drivers write the stencil reference and masks
               only here in captured traffic; STEN_REF_MASK_C (0x1d40) gets
               only their context-initialization writes. It is modeled as
               the same register, with the STEN_REF_MSK_C field layout. */
            MERGE(dev->t3d.sten_ref_mask);
            return 1;
        case RAGE128_PLANE_3D_MASK_C:
            /* Kept apart from DP_WRITE_MASK. The guide says the value
               "would be written into DP_WRITE_MASK" (RRG: PLANE_3D_MASK_C,
               p. 3-260 / PDF 278), but the open-source drivers treat the
               two as separate registers: linux r128 DRM r128_state.c
               r128_emit_masks writes both, and Mesa r128 r128_state.c
               r128UpdateMasks packs glColorMask in framebuffer format into
               this register while r128DDInitState leaves DP_WRITE_MASK at
               0xffffffff. Copying one into the other would let a packed
               16 bpp 3D mask cut planes out of 2D drawing. */
            MERGE(dev->t3d.plane_3d_mask);
            return 1;

        /* The _C aliases of 2D datapath registers. DST_PITCH_OFFSET_C,
           the first register of the context block that linux r128 DRM
           r128_state.c r128_emit_context writes, carries the
           DST_PITCH_OFFSET packing (RRG: DST_PITCH_OFFSET_C, p. 3-145 /
           PDF 163). */
        case RAGE128_DST_PITCH_OFFSET_C:
            rage128_2d_set_pitch_offset(dev, 1, val);
            return 1;
        /* Texture palette upload for the pseudocolor texture datatypes.
           INDEX sets the write pointer [7:0]; each DATA write stores one
           0x00RRGGBB entry, made opaque, and advances the pointer, so the
           driver streams 256 entries in a row. The RRG has no page for
           these registers; the offsets come from captured Windows 98
           Direct3D traffic (see vid_ati_rage128_regs.h). */
        case RAGE128_TEX_PALETTE_INDEX:
            dev->tex_pal_wr_index = val & 0xff;
            return 1;
        case RAGE128_TEX_PALETTE_DATA:
            dev->t3d.tex_palette[dev->tex_pal_wr_index & 0xff] = 0xff000000u | (val & 0x00ffffffu);
            dev->tex_pal_wr_index++;
            dev->tex_pal_gen++;
            return 1;

        /* Fog table upload, the same INDEX/DATA shape: write the starting
           index, then the 8-bit entries, and the index advances after each
           write (SDK: Setting 3D Render States, p. 6-52 / PDF 164). The
           Windows 98 Direct3D driver writes all 256 entries after an
           index of 0. */
        case RAGE128_FOG_TABLE_INDEX:
            dev->fog_table_wr_index = val & 0xff;
            return 1;
        case RAGE128_FOG_TABLE_DATA:
            dev->t3d.fog_table[dev->fog_table_wr_index & 0xff] = val & 0xff;
            dev->fog_table_wr_index++;
            return 1;

        case RAGE128_DP_GUI_MASTER_CNTL_C:
            return rage128_2d_reg_write(dev, RAGE128_DP_GUI_MASTER_CNTL, val, mask);
        case RAGE128_SC_TOP_LEFT_C:
            return rage128_2d_reg_write(dev, RAGE128_SC_TOP_LEFT, val, mask);
        case RAGE128_SC_BOTTOM_RIGHT_C:
            return rage128_2d_reg_write(dev, RAGE128_SC_BOTTOM_RIGHT, val, mask);

        default:
            break;
    }
#undef MERGE
    return 0;
}

/* ------------------------------------------------------------------ */
/* Rasterizer. Register fields are decoded from the CCE 3D supplement  */
/* and the SDK's 3D chapter; where the two disagree or are silent, the */
/* comment at the decode says which reading the model takes.           */
/* ------------------------------------------------------------------ */

/* TEX_CNTL_C enable bits (CCE 3D supplement, TEX_CNTL_C): Z_EN, Z_MASK
   (Z write), STENCIL_EN, TEX_EN, SECONDARY_TEX_EN, FOG_EN, ALPHA_EN
   (alpha blend), ALPHA_TST_EN and SPECULAR_LIGHT_EN. */
#define R3D_TC_Z_EN     (1 << 0)
#define R3D_TC_Z_WR     (1 << 1)
#define R3D_TC_STEN_EN  (1 << 3)
#define R3D_TC_TEX_EN   (1 << 4)
#define R3D_TC_SEC_EN   (1 << 5)
#define R3D_TC_FOG_EN   (1 << 7)
#define R3D_TC_ALPHA_EN (1 << 9)
#define R3D_TC_ATEST_EN (1 << 10)
#define R3D_TC_SPEC_EN  (1 << 11)

/* The surface address in a 3D offset register: TEX_n_OFFSET [25:0] of
   PRIM_TEX_n_OFFSET_C and SEC_TEX_n_OFFSET_C ([29:26] reserved, TEX_n_TILE
   [31:30]) and Z_OFFSET [25:0] of Z_OFFSET_C ([31:26] reserved) (CCE 3D
   supplement, PRIM_TEX_0_OFFSET_C, SEC_TEX_0_OFFSET_C, Z_OFFSET_C). The
   64 MB card space is the local 32 MB plus the AGP half, bit 25, so a
   reserved bit a guest writes is not part of the address. */
#define R3D_OFFSET_MASK 0x03ffffffu

/* r3d_vtx_t lives in vid_ati_rage128.h (shared with the raster module). */

/* Decode one vertex. The fields VC_FORMAT selects appear in the fixed
   order of the FTLVERTEX table (SDK: Table F-45, pp. F-51-F-53 /
   PDF 341-343). The caller guarantees that stride dwords are present. */
static void
r3d_decode_vertex(uint32_t fmt, const uint32_t *d, r3d_vtx_t *v)
{
    uint32_t i = 0;

    memcpy(&v->x, &d[i++], 4);
    memcpy(&v->y, &d[i++], 4);
    memcpy(&v->z, &d[i++], 4);
    v->rhw     = 1.0f;
    v->diffuse = 0xffffffff;
    /* Defaults for fields the format leaves out. Specular is black and
       the fog factor, the top byte of the specular dword, is 255. Fog
       computes f * pixel + (1 - f) * fog color (SDK: Setting 3D Render
       States, p. 6-51 / PDF 163), so f = 1 leaves the pixel unfogged;
       Mesa r128 r128_tris.c r128RenderStart stores the GL fog factor in
       that byte as an unsigned byte. */
    v->spec = 0xff000000;
    v->s = v->t = v->s2 = v->t2 = 0.0f;
    if (fmt & RAGE128_VCF_RHW)
        memcpy(&v->rhw, &d[i++], 4);
    if (fmt & (RAGE128_VCF_DIFFUSE_BGR | RAGE128_VCF_DIFFUSE_A)) {
        /* Float diffuse: blue, green, red as three floats (DIFFUSE_BGR),
           then alpha as one float (DIFFUSE_A), each in [0, 1], in that
           order (SDK: Table F-45, p. F-52 / PDF 342). The Windows 98
           OpenGL driver uses this form in captured Quake III Arena
           traffic (format 0x087, 10 dwords per vertex). When only one of
           the two flags is set, the missing part defaults to white or
           opaque. */
        float    fb = 1.0f, fg = 1.0f, fr = 1.0f, fa = 1.0f;
        uint32_t cb, cg, cr, ca;

        if (fmt & RAGE128_VCF_DIFFUSE_BGR) {
            memcpy(&fb, &d[i++], 4);
            memcpy(&fg, &d[i++], 4);
            memcpy(&fr, &d[i++], 4);
        }
        if (fmt & RAGE128_VCF_DIFFUSE_A)
            memcpy(&fa, &d[i++], 4);
        /* The compares are written so that NaN clamps to 0 on every host:
           converting NaN to an unsigned integer is undefined in C, and
           arm64 and x86-64 give different results. */
        fb         = (fb > 0.0f) ? (fb > 1.0f ? 1.0f : fb) : 0.0f;
        fg         = (fg > 0.0f) ? (fg > 1.0f ? 1.0f : fg) : 0.0f;
        fr         = (fr > 0.0f) ? (fr > 1.0f ? 1.0f : fr) : 0.0f;
        fa         = (fa > 0.0f) ? (fa > 1.0f ? 1.0f : fa) : 0.0f;
        cb         = (uint32_t) (fb * 255.0f + 0.5f);
        cg         = (uint32_t) (fg * 255.0f + 0.5f);
        cr         = (uint32_t) (fr * 255.0f + 0.5f);
        ca         = (uint32_t) (fa * 255.0f + 0.5f);
        v->diffuse = (ca << 24) | (cr << 16) | (cg << 8) | cb;
    }
    if (fmt & RAGE128_VCF_DIFFUSE_ARGB)
        v->diffuse = d[i++];
    if (fmt & RAGE128_VCF_SPEC_BGR) {
        /* Float specular: blue, green, red (SDK: Table F-45, p. F-52 /
           PDF 342). The fog byte keeps its default. */
        float    sb, sg, sr;
        uint32_t cb, cg, cr;

        memcpy(&sb, &d[i++], 4);
        memcpy(&sg, &d[i++], 4);
        memcpy(&sr, &d[i++], 4);
        sb      = (sb > 0.0f) ? (sb > 1.0f ? 1.0f : sb) : 0.0f; /* NaN -> 0, as diffuse */
        sg      = (sg > 0.0f) ? (sg > 1.0f ? 1.0f : sg) : 0.0f;
        sr      = (sr > 0.0f) ? (sr > 1.0f ? 1.0f : sr) : 0.0f;
        cb      = (uint32_t) (sb * 255.0f + 0.5f);
        cg      = (uint32_t) (sg * 255.0f + 0.5f);
        cr      = (uint32_t) (sr * 255.0f + 0.5f);
        v->spec = (v->spec & 0xff000000u) | (cr << 16) | (cg << 8) | cb;
    }
    if (fmt & RAGE128_VCF_SPEC_F) {
        /* Fog factor as a float, 1.0 = unfogged, the same meaning as the
           packed fog byte divided by 255. */
        float ff;

        memcpy(&ff, &d[i++], 4);
        ff      = (ff > 0.0f) ? (ff > 1.0f ? 1.0f : ff) : 0.0f; /* NaN -> 0, as diffuse */
        v->spec = (v->spec & 0x00ffffffu)
            | ((uint32_t) (ff * 255.0f + 0.5f) << 24);
    }
    if (fmt & RAGE128_VCF_SPEC_FRGB)
        v->spec = d[i++];
    if (fmt & RAGE128_VCF_S_T) {
        memcpy(&v->s, &d[i++], 4);
        memcpy(&v->t, &d[i++], 4);
    }
    if (fmt & RAGE128_VCF_S2_T2) {
        memcpy(&v->s2, &d[i++], 4);
        memcpy(&v->t2, &d[i++], 4);
    }
    /* RHW2, the 1/w of the second texture coordinate set, is the last
       field (SDK: Table F-45, p. F-53 / PDF 343). In captured Quake III
       Arena traffic (format 0x387, 13 dwords per vertex) it equals the
       first rhw. Without it, the second set uses rhw. */
    if (fmt & RAGE128_VCF_RHW2)
        memcpy(&v->rhw2, &d[i++], 4);
    else
        v->rhw2 = v->rhw;
}

/* Mark the VRAM pages of [addr, addr + len) changed for the display.
   Runs on the raster workers and the CCE thread without a lock: every
   writer stores the same value, and the only read-modify-write from
   another thread (the decrement in svga_poll) can at worst lose one mark
   until the page is written again. */
static void
r3d_mark_dirty(rage128_t *dev, uint32_t addr, uint32_t len)
{
    svga_t *svga = &dev->svga;

    /* Step by length and mask each address, so a run that wraps past
       the end of VRAM marks the pages it really touches. */
    for (uint32_t o = 0; o < len; o += 0x1000u)
        svga->changedvram[((addr + o) & dev->vram_mask) >> 12] = svga->monitor->mon_changeframecount;
    svga->changedvram[((addr + len - 1u) & dev->vram_mask) >> 12] = svga->monitor->mon_changeframecount;
}

/* Mark the pixels [rx0, rx1] of color row py changed. drow is the row's
   linear base. A tiled row is not contiguous in memory, so for a tiled
   target the touched 1 KB tile columns of the whole tile row are marked
   instead, a contiguous range that covers the row. */
static void
r3d_mark_row_dirty(rage128_t *dev, uint32_t dst_offset, uint32_t drow,
                   uint32_t cpb, int c_tld, int py, int rx0, int rx1,
                   uint32_t bpp)
{
    if (c_tld) {
        uint32_t trb = dst_offset + r128_tile_row_start((uint32_t) py, cpb);
        uint32_t cl  = ((uint32_t) rx0 * bpp) >> 6;
        uint32_t ch  = ((uint32_t) rx1 * bpp) >> 6;

        r3d_mark_dirty(dev, trb + (cl << 10), (ch - cl + 1u) << 10);
    } else
        r3d_mark_dirty(dev, drow + (uint32_t) rx0 * bpp,
                       (uint32_t) (rx1 - rx0 + 1) * bpp);
}

/* The compare code shared by the Z test, the stencil test and the alpha
   test: 0 never, 1 <, 2 <=, 3 ==, 4 >=, 5 >, 6 !=, 7 always, with a the
   incoming value and b the stored or reference one (SDK: Table 6-23,
   p. 6-55 / PDF 167; SDK: Table 6-25, p. 6-56 / PDF 168; SDK: Table 6-20,
   p. 6-51 / PDF 163). */
static int
r3d_cmp(uint32_t fn, uint32_t a, uint32_t b)
{
    /* clang-format off */
    switch (fn & 7) {
        case 0: return 0;
        case 1: return a < b;
        case 2: return a <= b;
        case 3: return a == b;
        case 4: return a >= b;
        case 5: return a > b;
        case 6: return a != b;
        default: return 1;
    }
    /* clang-format on */
}

/* 16 bpp direct color to ARGB8888 for datatypes 3 (aRGB 1555), 4 (RGB
   565) and 15 (aRGB 4444) (RRG: DP_DATATYPE, p. 3-168 / PDF 186). Each
   channel is widened by a plain shift, low bits zero; the guides do not
   say how the chip widens a channel. The destination read, the texel
   fetch and the chroma-key conversion share these so they agree bit for
   bit. RGB 565 has no alpha: the callers OR in an opaque alpha byte. */
static inline uint32_t
r3d_argb1555_to_argb(uint32_t v)
{
    return ((v & 0x8000) ? 0xff000000 : 0)
        | ((v & 0x7c00) << 9) | ((v & 0x03e0) << 6) | ((v & 0x001f) << 3);
}

static inline uint32_t
r3d_rgb565_to_rgb(uint32_t v)
{
    return ((v & 0xf800) << 8) | ((v & 0x07e0) << 5) | ((v & 0x001f) << 3);
}

static inline uint32_t
r3d_argb4444_to_argb(uint32_t v)
{
    return ((v & 0xf000) << 16) | ((v & 0x0f00) << 12)
        | ((v & 0x00f0) << 8) | ((v & 0x000f) << 4);
}

/* Destination pixel to ARGB8888, by DP_DATATYPE destination type. p
   points at the resolved cell, in local VRAM or in a staging arena. The
   draw is rejected before this for any type not handled here
   (draw_ok). */
static uint32_t
r3d_dst_read(const uint8_t *p, uint32_t dt)
{
    switch (dt) {
        case 3: /* ARGB1555 */
            return r3d_argb1555_to_argb(*(const uint16_t *) p);
        case 4: /* RGB565 */
            return 0xff000000 | r3d_rgb565_to_rgb(*(const uint16_t *) p);
        case 15: /* ARGB4444 */
            return r3d_argb4444_to_argb(*(const uint16_t *) p);
        default: /* 6 = ARGB8888 */
            return *(const uint32_t *) p;
    }
}

/* Ordered-dither thresholds, the standard 4x4 Bayer matrix, used on
   16 bpp targets when TEX_CNTL_C DITHER_EN (bit 8) is set (CCE 3D
   supplement, TEX_CNTL_C). SCALE_3D_CNTL SCALE_DITHER (bit 1) picks
   error diffusion (0) or table lookup (1) (SDK: Setting 3D Render
   States, p. 6-53 / PDF 165). Error diffusion carries an error term
   along each scanline, which rows split across raster threads cannot
   share, so both settings use this table. DITHER_INIT, which in table
   mode turns dithering off during alpha blending, is folded into the
   draw's dither flag in r3d_draw_state_derive. The chip's table is not
   documented; a different matrix would move only the sub-LSB noise
   pattern, not the average color. */
static const uint8_t r3d_bayer4[4][4] = {
    { 0,  8,  2,  10 },
    { 12, 4,  14, 6  },
    { 3,  11, 1,  9  },
    { 15, 7,  13, 5  },
};

/* Add an 8->n-bit truncation dither offset (Bayer threshold scaled to the
   discarded-bit range) to one 8-bit channel, saturating. */
static inline uint32_t
r3d_dq(uint32_t v8, uint32_t add)
{
    v8 += add;
    return v8 > 255 ? 255 : v8;
}

/* Write ARGB8888 to a destination pixel. bay is the Bayer threshold
   0..15 for this pixel, or < 0 for no dithering; the threshold, scaled
   to the bits a channel loses, is added before the channel is cut to the
   target width. pmask is PLANE_3D_MASK_C, a bit-plane mask in
   framebuffer format (Mesa r128 r128_state.c r128UpdateMasks packs
   glColorMask per pixel depth, so all planes on is 0x0000ffff at
   16 bpp). It is applied to the packed pixel after dithering, never to
   the ARGB value. */
static void
r3d_dst_write(uint8_t *p, uint32_t dt, uint32_t argb, int bay, uint32_t pmask)
{
    uint32_t a = (argb >> 24) & 0xff;
    uint32_t r = (argb >> 16) & 0xff;
    uint32_t g = (argb >> 8) & 0xff;
    uint32_t b = argb & 0xff;
    uint32_t raw, m;

    switch (dt) {
        case 3: /* ARGB1555; the 1-bit alpha is the top bit, not dithered */
            if (bay >= 0) {
                r = r3d_dq(r, (uint32_t) bay >> 1);
                g = r3d_dq(g, (uint32_t) bay >> 1);
                b = r3d_dq(b, (uint32_t) bay >> 1);
            }
            raw = ((a << 8) & 0x8000)
                | ((r << 7) & 0x7c00) | ((g << 2) & 0x03e0) | (b >> 3);
            goto raw16;
        case 4: /* RGB565 */
            if (bay >= 0) {
                r = r3d_dq(r, (uint32_t) bay >> 1);
                g = r3d_dq(g, (uint32_t) bay >> 2);
                b = r3d_dq(b, (uint32_t) bay >> 1);
            }
            raw = ((r << 8) & 0xf800) | ((g << 3) & 0x07e0) | (b >> 3);
            goto raw16;
        case 15: /* ARGB4444 */
            if (bay >= 0) {
                a = r3d_dq(a, (uint32_t) bay);
                r = r3d_dq(r, (uint32_t) bay);
                g = r3d_dq(g, (uint32_t) bay);
                b = r3d_dq(b, (uint32_t) bay);
            }
            raw = ((a << 8) & 0xf000) | ((r << 4) & 0x0f00)
                | (g & 0x00f0) | (b >> 4);
raw16:
            m = pmask & 0xffff;
            if (m != 0xffff)
                raw = (raw & m) | (*(uint16_t *) p & ~m);
            *(uint16_t *) p = (uint16_t) raw;
            break;
        default: /* 6 = ARGB8888 -- no quantization, nothing to dither */
            if (pmask != 0xffffffffu)
                argb = (argb & pmask) | (*(uint32_t *) p & ~pmask);
            *(uint32_t *) p = argb;
            break;
    }
}

static int
r3d_dst_bpp(uint32_t dt)
{
    return (dt == 6) ? 4 : 2;
}

/* Texture address mode of PRIM_TEXTURE_CLAMP_MODE_S / _T: 0 wrap,
   1 mirror, 2 clamp, 3 border color (SDK: Table 6-6, p. 6-41 /
   PDF 153). Border returns -1 for a coordinate outside the map and the
   caller substitutes the border color. */
static int
r3d_tex_wrap(int c, int n, uint32_t mode)
{
    int m;

    switch (mode & 3) {
        case 0:
            return c & (n - 1);
        case 1:
            m = c & (2 * n - 1);
            return (m < n) ? m : (2 * n - 1 - m);
        case 2:
            return (c < 0) ? 0 : (c >= n) ? n - 1
                                          : c;
        default:
            return (c < 0 || c >= n) ? -1 : c;
    }
}

/* A saturated coordinate's mathematical successor still follows the
   address mode (SDK: Texture Mapping, p. 6-41 / PDF 153). A bounded
   representative preserves its low bits for wrap and mirror, and its
   upper-edge result for clamp and border without signed overflow. */
static int
r3d_tex_wrap_next(int c, int n, uint32_t mode)
{
    int next = c == INT_MAX ? ((mode & 2) ? n : 0) : c + 1;

    return r3d_tex_wrap(next, n, mode);
}

#ifdef REG_HARNESS
/* Register-harness access to the address-mode helpers, so a vector can
   check a neighbor coordinate even where its filter weight is 0. */
int
rage128_test_tex_wrap(int c, int n, uint32_t mode, int next)
{
    return next ? r3d_tex_wrap_next(c, n, mode) : r3d_tex_wrap(c, n, mode);
}
#endif

/* One stage's half of TEX_SIZE_PITCH_C, the low half for the primary
   stage and the high half for the secondary: TEX_PITCH [3:0], TEX_SIZE
   [7:4], TEX_HEIGHT [11:8] and TEX_MIN_SIZE [15:12], each a log2 (CCE 3D
   supplement, TEX_SIZE_PITCH_C). */
#define R3D_TSP_HALF(rs, st) ((st) ? ((rs)->t3d.tex_size_pitch >> 16) \
                                   : ((rs)->t3d.tex_size_pitch & 0xffff))
/* Offset slot of the largest level. TEX_0_OFFSET points at the smallest
   map (CCE 3D supplement, PRIM_TEX_0_OFFSET_C), so slot k holds the level
   of size 2^(TEX_MIN_SIZE + k) and the largest sits at TEX_SIZE -
   TEX_MIN_SIZE. The SDK's prose calls TEX_0_OFFSET the base texture
   (SDK: Texture Mapping, p. 6-39 / PDF 151); the two agree when the
   texture has one level. */
#define R3D_TOPSLOT(tsp) ((int) (((tsp) >> 4) & 0xf) - (int) (((tsp) >> 12) & 0xf))

/* Dimensions of the mip level in offset slot `slot`. The largest level
   sits at R3D_TOPSLOT(tsp) and each slot below halves both dimensions,
   down to 1. Each level's pitch is its own width: the documents give
   TEX_PITCH only for the largest map and say nothing about the pitch of
   the smaller ones, so this is modeled. */
static void
r3d_level_dims(uint32_t tsp, int slot, uint32_t *lw, uint32_t *lh)
{
    int      shift = R3D_TOPSLOT(tsp) - slot;
    uint32_t w     = 1u << (tsp & 0xf);
    uint32_t h     = 1u << ((tsp >> 8) & 0xf);

    if (shift < 0)
        shift = 0;
    if (shift > 31)
        shift = 31;
    w >>= shift;
    h >>= shift;
    *lw = w ? w : 1;
    *lh = h ? h : 1;
}

/* S3TC block type in bits [27:26] of the stage's texture control
   register (PRIM_TEX_CNTL_C or SEC_TEX_CNTL_C): 1 = DXT1, 2 = DXT2/3,
   3 = DXT4/5. Not documented: the CCE 3D supplement lists [31:26] as
   reserved. The Windows 98 Direct3D driver gives its DXTn textures
   datatype 0 and sets these bits (0x04000080, 0x08000080 and 0x0c000080
   for DXT1, DXT3 and DXT5 in captured traffic). It writes them only with
   datatype 0 and leaves old values there otherwise, so they are read
   only when the datatype is 0. */
#define R3D_S3TC_CLASS(cntl) ((((cntl) >> 16) & 0xf) ? 0u : (((cntl) >> 26) & 3u))

/* Byte size of one mip level: datatype 0 is S3TC blocks (8 bytes per
   4x4 block for DXT1, 16 for DXT2-5); the 8 bpp formats take 1 byte per
   texel; 32 bpp and RGB888 take 4 (RGB888 is 3 packed bytes, padded here
   to 4 so the size stays a power of two); the rest take 2. lw and lh are
   powers of two, so the result is a power of two, at least 8, and a
   staged level can be indexed with a wrap mask (see r3d_stage_slot). */
uint32_t
r3d_level_bytes(uint32_t dt, uint32_t s3tc, uint32_t lw, uint32_t lh)
{
    if (dt == 0)
        return ((lw + 3u) >> 2) * ((lh + 3u) >> 2) * ((s3tc >= 2u) ? 16u : 8u);
    if (dt == 1 || dt == 2 || dt == 7 || dt == 8 || dt == 9)
        return lw * lh; /* CI4 (one texel per byte), CI8, RGB332, Y8,
                           RGB8: 1 byte per texel */
    return lw * lh * ((dt == 6 || dt == 5 || dt == 14) ? 4u : 2u);
}

/* Row bytes of a level, for the formats the tile transform covers. It
   returns 0 for S3TC block rows and packed RGB888, whose tiled layout no
   document describes; those levels are sampled linearly, with a one-time
   log line at the slot resolve. */
static inline uint32_t
r3d_level_pitch_b(uint32_t dt, uint32_t lw)
{
    if (dt == 0 || dt == 5)
        return 0;
    if (dt == 1 || dt == 2 || dt == 7 || dt == 8 || dt == 9)
        return lw;
    return lw * ((dt == 6 || dt == 14) ? 4u : 2u);
}

/* Byte extent of one tiled mip level: whole tile rows of 16 lines, so a
   level shorter than 16 lines still takes a full tile row. lw and the
   texel sizes are powers of two, so the result is too. The caller has
   checked r128_tiled_ok. */
static inline uint32_t
r3d_level_bytes_tiled(uint32_t pitch_b, uint32_t lh)
{
    return pitch_b * (lh < 16u ? 16u : lh);
}

/* YCbCr to ARGB8888 for the YUV texture datatypes: the device's shared
   BT.601 matrix (r128_yuv_to_rgb) with the texel's alpha on top. */
static inline uint32_t
r3d_yuv_to_argb(uint32_t a, int y, int cb, int cr)
{
    return (a << 24) | r128_yuv_to_rgb(y, cb, cr);
}

/* Fetch one texel as ARGB8888 from integer coordinates, using level
   constants (base, width, which is also the pitch, and datatype) that
   the caller resolved once per level. texbase and mask pick the backing
   store: a level in VRAM passes (svga.vram, base, vram_mask); a staged
   level passes (its arena slice, 0, slice size - 1), so every address is
   wrapped inside the level's own slice and a bad offset cannot read past
   it. */
static inline uint32_t
r3d_texel(const uint8_t *texbase, uint32_t base, uint32_t mask, uint32_t lw, uint32_t tiled,
          uint32_t dt, uint32_t amask, uint32_t s3tc, const uint32_t *pal, int u, int v)
{
    uint32_t       off = (uint32_t) v * lw + u;
    const uint8_t *p;
    uint32_t       texel;

/* Byte offset of byte column xb, row v within the level: linear, or the
   64-byte by 16-line tile transform of r128_tile_off when the level's
   offset register selects a tile mode. The CCE 3D supplement names three
   tile modes (PRIM_TEX_0_OFFSET_C TEX_0_TILE) but not their layout; all
   three are modeled with that one geometry. `tiled` is cleared at the
   slot resolve for formats and pitches the transform does not cover, so
   S3TC and RGB888 never take the tiled arm. */
#define R3D_TOFF(xb, pb) (tiled ? r128_tile_off((xb), (uint32_t) v, (pb)) \
                                : (uint32_t) v * (pb) + (xb))

    switch (dt) {
        case 0:
            { /* S3TC: all five DXTn formats use datatype 0, with the
                 block type in s3tc (see R3D_S3TC_CLASS). Types 2 and
                 3 have 16-byte blocks: an alpha block (explicit 4-bit
                 for DXT2/3, interpolated for DXT4/5), then a color
                 block that always uses four colors. DXT2 and DXT4
                 differ from DXT3 and DXT5 only in premultiplied
                 alpha, which changes nothing in the decode. Type 0
                 is the datatype's documented meaning, 2 bpp VQ ("not
                 supported in the initial part", CCE 3D supplement,
                 PRIM_TEX_CNTL_C); no Rage 128 driver seen here uses
                 it and the codebook format is unknown, so it is
                 decoded as DXT1, with a one-time log line. */
                if (s3tc == 0u) {
                    static int seen_vq = 0;
                    if (!seen_vq) {
                        seen_vq = 1;
                        rage128_log("[r128 TEXFMT] dt=0 class=0 (real VQ) -- no codebook wiring on r128, decoding as DXT1\n");
                    }
                }
                uint32_t bpitch = (lw + 3u) >> 2;
                uint32_t boff   = ((uint32_t) (v >> 2) * bpitch + (uint32_t) (u >> 2))
                    * ((s3tc >= 2u) ? 16u : 8u);
                uint32_t coff   = boff + ((s3tc >= 2u) ? 8u : 0u);
                uint32_t texidx = ((uint32_t) (v & 3) << 2) | (uint32_t) (u & 3);
                uint32_t a      = 0xffu;
                uint16_t c0, c1;
                uint32_t bits, sel, r0, g0, b0, r1, g1, b1;

                if (s3tc == 2u) { /* DXT2/3: 16 nibbles, texel order, low first */
                    uint32_t an = texbase[(base + boff + (texidx >> 1)) & mask];
                    a           = (texidx & 1u) ? (an >> 4) : (an & 0xfu);
                    a *= 0x11u;
                } else if (s3tc == 3u) { /* DXT4/5: a0,a1 + 16 3-bit selectors */
                    uint32_t a0 = texbase[(base + boff) & mask];
                    uint32_t a1 = texbase[(base + boff + 1) & mask];
                    uint32_t bp = texidx * 3u; /* bit position in 48-bit field */
                    uint32_t aw = *(const uint32_t *) &texbase[(base + boff + 2 + (bp >> 3)) & mask];
                    uint32_t ac = (aw >> (bp & 7u)) & 7u;

                    if (ac == 0)
                        a = a0;
                    else if (ac == 1)
                        a = a1;
                    else if (a0 > a1)
                        a = ((8u - ac) * a0 + (ac - 1u) * a1) / 7u;
                    else if (ac == 6)
                        a = 0;
                    else if (ac == 7)
                        a = 255;
                    else
                        a = ((6u - ac) * a0 + (ac - 1u) * a1) / 5u;
                }

                a |= amask >> 24;
                c0   = *(const uint16_t *) &texbase[(base + coff) & mask];
                c1   = *(const uint16_t *) &texbase[(base + coff + 2) & mask];
                bits = *(const uint32_t *) &texbase[(base + coff + 4) & mask];
                sel  = (bits >> (texidx * 2u)) & 3;
                r0   = ((c0 >> 11) & 0x1f) * 255 / 31;
                g0   = ((c0 >> 5) & 0x3f) * 255 / 63;
                b0   = (c0 & 0x1f) * 255 / 31;
                r1   = ((c1 >> 11) & 0x1f) * 255 / 31;
                g1   = ((c1 >> 5) & 0x3f) * 255 / 63;
                b1   = (c1 & 0x1f) * 255 / 31;
                switch (sel) {
                    case 0:
                        return (a << 24) | (r0 << 16) | (g0 << 8) | b0;
                    case 1:
                        return (a << 24) | (r1 << 16) | (g1 << 8) | b1;
                    case 2:
                        if (c0 > c1 || s3tc >= 2u)
                            return (a << 24) | (((2 * r0 + r1) / 3) << 16)
                                | (((2 * g0 + g1) / 3) << 8) | ((2 * b0 + b1) / 3);
                        return (a << 24) | (((r0 + r1) >> 1) << 16)
                            | (((g0 + g1) >> 1) << 8) | ((b0 + b1) >> 1);
                    default:
                        if (c0 > c1 || s3tc >= 2u)
                            return (a << 24) | (((r0 + 2 * r1) / 3) << 16)
                                | (((g0 + 2 * g1) / 3) << 8) | ((b0 + 2 * b1) / 3);
                        return amask; /* DXT1 3-color mode: black, with selected alpha */
                }
            }
        case 1: /* CI4, "4 bpp pseudocolor": modeled as one texel per
                   byte with the index in the low nibble (the high nibble
                   is ignored), looking up entries 0..15 of the texture
                   palette. The Rage 128 documents do not describe the
                   packing. PRIMARY_PALETTE_OFF [23:20], which selects one
                   of 16 palettes for this datatype (CCE 3D supplement,
                   PRIM_TEX_CNTL_C), is ignored: no driver seen here sets
                   it. */
            p = &texbase[(base + R3D_TOFF((uint32_t) u, lw)) & mask];
            return pal[*p & 0xf];
        case 2: /* CI8, "8 bpp pseudocolor" (RRG: DP_DATATYPE, p. 3-168 /
                   PDF 186): an 8-bit index into the 256-entry texture
                   palette loaded through TEX_PALETTE_INDEX / DATA. The
                   entries are stored as opaque ARGB8888. */
            p = &texbase[(base + R3D_TOFF((uint32_t) u, lw)) & mask];
            return pal[*p];
        case 3: /* ARGB1555 */
            p = &texbase[(base + R3D_TOFF((uint32_t) u * 2u, lw * 2u)) & mask];
            return r3d_argb1555_to_argb(*(const uint16_t *) p) | amask;
        case 4: /* RGB565 */
            p = &texbase[(base + R3D_TOFF((uint32_t) u * 2u, lw * 2u)) & mask];
            return 0xff000000 | r3d_rgb565_to_rgb(*(const uint16_t *) p);
        case 15: /* ARGB4444 */
            p = &texbase[(base + R3D_TOFF((uint32_t) u * 2u, lw * 2u)) & mask];
            return r3d_argb4444_to_argb(*(const uint16_t *) p) | amask;
        case 6: /* ARGB8888 */
            p = &texbase[(base + R3D_TOFF((uint32_t) u * 4u, lw * 4u)) & mask];
            return *(const uint32_t *) p | amask;
        case 5: /* "24 bpp RGB" (SDK: Table 6-3, p. 6-39 / PDF 151): 3
                   packed bytes per texel, blue first, the byte order of a
                   24 bpp frame buffer (SDK: Table 2-8, p. 2-14 / PDF 30).
                   The CCE 3D supplement lists 5 as
                   reserved for the primary stage and as 24 bpp RGB for
                   the secondary; both stages decode it. */
            p = &texbase[(base + off * 3) & mask];
            return 0xff000000 | ((uint32_t) p[2] << 16) | ((uint32_t) p[1] << 8) | p[0];
        case 7: /* RGB332: each channel widened by a plain shift, like the
                   16 bpp decodes. */
            p     = &texbase[(base + R3D_TOFF((uint32_t) u, lw)) & mask];
            texel = *p;
            return 0xff000000 | ((texel & 0xe0) << 16) | ((texel & 0x1c) << 11) | ((texel & 0x03) << 6);
        case 8: /* Y8 gray scale: the intensity in R, G and B, opaque. */
            p     = &texbase[(base + R3D_TOFF((uint32_t) u, lw)) & mask];
            texel = *p;
            return 0xff000000 | (texel << 16) | (texel << 8) | texel;
        case 9: /* RGB8 gray scale: the intensity in all four channels,
                   alpha included, as the RRG describes it ("duplicated
                   for all 4 channels", RRG: DP_DATATYPE, p. 3-168 /
                   PDF 186). The CCE 3D supplement's SECONDARY_DATATYPE
                   says three channels; the model follows the RRG. */
            p     = &texbase[(base + R3D_TOFF((uint32_t) u, lw)) & mask];
            texel = *p;
            return (texel << 24) | (texel << 16) | (texel << 8) | texel | amask;
        case 11: /* YUV 422 packed (VYUY): bytes Y0 U Y1 V, two texels */
        case 12:
            { /* YUV 422 packed (YVYU): bytes U Y0 V Y1. The guides
                 give the names without a byte diagram; these are
                 the byte orders the overlay decode in
                 vid_ati_rage128_ov0.c uses for its VYUY422 and
                 YVYU422 sources. */
                /* A texel pair is 4 aligned bytes and a tile column is 64
                   bytes wide, so a pair never spans two tile columns and one
                   transformed base address covers it. */
                uint32_t poff = (base + R3D_TOFF((uint32_t) (u & ~1) * 2u, lw * 2u)) & mask;
                int      yb, cb, cr;

                if (dt == 11) {
                    yb = texbase[(poff + ((uint32_t) (u & 1) * 2u)) & mask];
                    cb = texbase[(poff + 1u) & mask];
                    cr = texbase[(poff + 3u) & mask];
                } else {
                    yb = texbase[(poff + 1u + ((uint32_t) (u & 1) * 2u)) & mask];
                    cb = texbase[poff];
                    cr = texbase[(poff + 2u) & mask];
                }
                return r3d_yuv_to_argb(0xffu, yb, cb, cr);
            }
        case 14: /* aYUV 444 (8:8:8:8): modeled as A [31:24], Y [23:16],
                    U [15:8], V [7:0], by analogy with aRGB 8888; the
                    guides do not give the lanes. */
            p     = &texbase[(base + R3D_TOFF((uint32_t) u * 4u, lw * 4u)) & mask];
            texel = *(const uint32_t *) p;
            return r3d_yuv_to_argb(texel >> 24, (int) ((texel >> 16) & 0xff),
                                   (int) ((texel >> 8) & 0xff), (int) (texel & 0xff))
                | amask;
        default:
            {
                /* No decoder for this datatype: the texel is opaque white and
                   the draw still goes ahead. Each such datatype is logged
                   once so the white texture names its format. The only
                   documented datatype left is 10, "16 bpp a:pseudocolor
                   (8:8)" in the CCE 3D
                   supplement; the documents do not say how its two bytes
                   are used and no driver seen here uses it. */
                static uint16_t seen_dt = 0;
                if (!(seen_dt & (uint16_t) (1u << (dt & 0xf)))) {
                    seen_dt |= (uint16_t) (1u << (dt & 0xf));
                    rage128_log("[r128 TEXFMT] unhandled texture datatype dt=%u -> opaque white\n", dt);
                }
                return 0xffffffff;
            }
    }
#undef R3D_TOFF
}

/* Raw palette index of a CI4 or CI8 texel, for the chroma-key compare.
   The key is modeled as an index, not a palette color: in captured
   Windows 98 Direct3D traffic the driver keys CI8 textures with key 0
   and mask 0xffffffff, and index 0 holds a different color in each
   texture's palette. Same addressing as r3d_texel. */
static inline uint32_t
r3d_texel_ci_raw(const uint8_t *texbase, uint32_t base, uint32_t mask, uint32_t lw,
                 uint32_t tiled, uint32_t dt, int u, int v)
{
    uint32_t off = tiled ? r128_tile_off((uint32_t) u, (uint32_t) v, lw)
                         : (uint32_t) v * lw + (uint32_t) u;
    uint32_t idx = texbase[(base + off) & mask];

    return dt == 1 ? (idx & 0xfu) : idx;
}

/* The chroma-key color and mask registers hold raw texel values: in
   captured Windows 2000 Direct3D traffic the driver keys RGB565 textures
   with 0x07ff (565 cyan) and mask 0xffffffff. The compare sees the texel
   after conversion to ARGB8888, so the key and the mask go through the
   same bit mapping as the texel; equality is kept exactly. Datatype 6
   needs no change. CI4 and CI8 pass through, because their compare uses
   the raw index (r3d_texel_ci_raw). S3TC also passes through unchanged:
   how the chip keys a compressed texel is not documented. */
static uint32_t
r3d_ck_to_argb(uint32_t dt, uint32_t v)
{
    switch (dt) {
        case 3: /* ARGB1555 */
            return r3d_argb1555_to_argb(v);
        case 4: /* RGB565: no opaque alpha added. The key has no alpha
                   bits, so the converted mask must not cover any. */
            return r3d_rgb565_to_rgb(v);
        case 15: /* ARGB4444 */
            return r3d_argb4444_to_argb(v);
        case 5: /* RGB888 */
            return 0xff000000 | (v & 0x00ffffff);
        case 7: /* RGB332 */
            return 0xff000000 | ((v & 0xe0) << 16) | ((v & 0x1c) << 11) | ((v & 0x03) << 6);
        case 8: /* Y8 */
            return 0xff000000 | ((v & 0xff) * 0x00010101u);
        case 9: /* RGB8 */
            return (v & 0xff) * 0x01010101u;
        case 11: /* YUV texels compare after conversion to RGB. The matrix
                    clamps, so two YUV keys can map to the same RGB
                    value. The key holds one texel: Y, U, (unused), V
                    from the low byte up for the 422 types (the second Y
                    of the pair is ignored), and the texel's own lanes
                    for 444. */
        case 12:
            return r3d_yuv_to_argb(0xffu, (int) (v & 0xff), (int) ((v >> 8) & 0xff),
                                   (int) ((v >> 24) & 0xff));
        case 14:
            return r3d_yuv_to_argb(v >> 24, (int) ((v >> 16) & 0xff),
                                   (int) ((v >> 8) & 0xff), (int) (v & 0xff));
        default:
            return v;
    }
}

/* Round a texel-space coordinate to a 1/4096-texel grid before it is
   floored. Text and interface quads often put an exact texel edge on a
   pixel center; the float barycentric weights can land one ulp either
   side of it, and floorf() would then pick different texels from pixel
   to pixel. The chip's coordinate precision is not documented. */
static inline float
r3d_texcoord_fx(float f)
{
    return nearbyintf(f * 4096.0f) * (1.0f / 4096.0f);
}

/* Float to int conversions for texture coordinates, filter weights and
   mip levels. A plain C cast has no defined result for NaN, an infinity
   or a value outside the destination range, and real compilers differ:
   arm64 saturates, x86-64 returns 0x80000000 or the low half of a 64-bit
   convert. Every lane (this interpreter, both JITs, the GPU kernel) has
   to agree bit for bit, so the result is spelled out here and the other
   lanes reproduce it: truncate toward zero, saturate to the range, NaN
   gives 0. That matches the arm64 conversion instructions. What the
   chip does with such a coordinate is not documented; this rule is
   modeled. */
static inline int32_t
r3d_f2i(float f)
{
    if (f != f)
        return 0;
    if (f >= 2147483648.0f)
        return INT32_MAX;
    if (f < -2147483648.0f)
        return INT32_MIN;
    return (int32_t) f;
}

static inline uint32_t
r3d_f2u(float f)
{
    if (!(f > 0.0f))
        return 0; /* also NaN */
    if (f >= 4294967296.0f)
        return UINT32_MAX;
    return (uint32_t) f;
}

/* Fill a stage's per-triangle sampler descriptor. r3d_stage_desc_t is
   declared in vid_ati_rage128.h because JIT blocks with inline texture
   code read its slot cache directly by offset. */
static void
r3d_stage_desc_init(const rage128_raster_state_t *rs, int st, r3d_stage_desc_t *d)
{
    /* The decoded fields come from the per-draw state; only the palette
       pointer (into this copy of rs) and the slot cache are per
       triangle. */
    const r3d_stage_hdr_t *h = &rs->d.sh[st];

    d->tsp        = h->tsp;
    d->clamp_s    = h->clamp_s;
    d->clamp_t    = h->clamp_t;
    d->dt         = h->dt;
    d->s3tc       = h->s3tc;
    d->amask      = h->aone ? 0xff000000u : 0u;
    d->border     = h->border;
    d->minb       = h->minb;
    d->mag        = h->mag;
    d->mipdis     = h->mipdis;
    d->top        = h->top;
    d->pal        = rs->t3d.tex_palette; /* CI4 / CI8 palette */
    d->slot_valid = 0;
    d->slot_tiled = 0;
}

/* Resolve one mip slot's level constants into the descriptor (backing
   store, base, wrap mask, dimensions) on first use; the result is cached
   for the rest of the triangle. */
static const struct r3d_slot_desc_t *
r3d_stage_slot(rage128_t *dev, const rage128_raster_state_t *rs, int st,
               r3d_stage_desc_t *d, int cslot)
{
    struct r3d_slot_desc_t *sl = &d->slot[cslot];

    if (!(d->slot_valid & (1u << cslot))) {
        uint32_t so  = st ? rs->sec_stage_off[cslot] : rs->prim_stage_off[cslot];
        uint32_t raw = st ? rs->t3d.sec_tex_offset[cslot]
                          : rs->t3d.prim_tex_offset[cslot];

        d->slot_valid |= (uint16_t) (1u << cslot);
        r3d_level_dims(d->tsp, cslot, &sl->lw, &sl->lh); /* pitch == lw */
        /* Offset bits [31:30] (TEX_0_TILE) select a tile mode, and all
           three modes are given the r128_tile_off geometry. Formats with
           no documented tiled layout (S3TC, packed RGB888: pitch_b is 0
           below) and pitches the transform does not handle are sampled
           linearly, with a one-time log line. A level the transform does
           cover is normally staged detiled, so its arena copy is a
           linear image sampled like any other staged level; slot_tiled
           marks only a level that could not be staged and is sampled
           from VRAM through the transform. The flag is kept in this
           per-stage bitmask rather than in the slot struct because the
           JIT bakes the struct's size in as the slot[] stride. */
        d->slot_tiled &= (uint16_t) ~(1u << cslot);
        if (raw >> 30) {
            uint32_t pb = r3d_level_pitch_b(d->dt, sl->lw);

            if (!r128_tiled_ok(1u, pb)) {
                static int seen_tile_lin = 0;
                if (!seen_tile_lin) {
                    seen_tile_lin = 1;
                    rage128_log("[r128 3D] tiled texture level dt=%u pitch_b=%u -- transform uncited/undefined here, sampling linear\n",
                                d->dt, pb);
                }
            } else if (so == R128_TEX_STAGE_NONE)
                d->slot_tiled |= (uint16_t) (1u << cslot);
        }
        if (so != R128_TEX_STAGE_NONE) {
            /* Staged level (in AGP memory, or tiled and copied detiled):
               the arena slice is the level's linear image, indexed from 0
               and masked to its own size. The slice size is a power of
               two (see r3d_level_bytes), so slice - 1 keeps every
               address inside it and a bad offset cannot read past it. */
            uint32_t slice = r3d_level_bytes(d->dt, d->s3tc, sl->lw, sl->lh);

            sl->texbase = dev->tex_stage.arena + so;
            sl->base    = 0;
            sl->mask    = slice ? (slice - 1u) : 0u;
        } else {
            sl->texbase = dev->svga.vram;
            sl->base    = raw & R3D_OFFSET_MASK;
            sl->mask    = dev->vram_mask;
        }
    }
    return sl;
}

/* Blend two ARGB8888 colors by w in [0, 256], two 8-bit lanes per
   32-bit multiply: each lane gets (x * (256 - w) + y * w + 128) >> 8.
   A lane's sum is at most 256 * 255 + 128, below 2^16, so it never
   carries into the lane above. */
static inline uint32_t
r3d_lerp_packed(uint32_t x, uint32_t y, uint32_t w)
{
    uint32_t iw = 256u - w;
    uint32_t rb = ((x & 0x00ff00ffu) * iw + (y & 0x00ff00ffu) * w + 0x00800080u) >> 8;
    uint32_t ag = (((x >> 8) & 0x00ff00ffu) * iw + ((y >> 8) & 0x00ff00ffu) * w + 0x00800080u) >> 8;

    return ((ag & 0x00ff00ffu) << 8) | (rb & 0x00ff00ffu);
}

/* Sample one mip level of stage `st` at normalized (s, t) with nearest
   or bilinear filtering; returns ARGB8888. If nearest is not NULL it
   receives the nearest texel, which the chroma key compares before
   filtering. The level constants come from the per-triangle
   descriptor. */
static uint32_t
r3d_tex_level(rage128_t *dev, const rage128_raster_state_t *rs, int st,
              r3d_stage_desc_t *d, int slot, float s, float t,
              int linear, uint32_t *nearest)
{
    int                           cslot  = slot < 0 ? 0 : (slot > 10 ? 10 : slot);
    uint32_t                      border = d->border;
    int                           u, v;
    uint32_t                      near;
    const struct r3d_slot_desc_t *sl      = r3d_stage_slot(dev, rs, st, d, cslot);
    const uint8_t                *texbase = sl->texbase;
    uint32_t                      basesel = sl->base;
    uint32_t                      masksel = sl->mask;
    uint32_t                      tilesel = (d->slot_tiled >> cslot) & 1u;
    uint32_t                      lw = sl->lw, lh = sl->lh;

    /* The nearest texel is needed only as the result of a nearest fetch
       or for the chroma-key compare (nearest != NULL). A bilinear fetch
       with no chroma key skips it, which saves a fifth texel read per
       pixel and changes no output. */
    if (!linear || nearest) {
        u    = r3d_tex_wrap(r3d_f2i(floorf(r3d_texcoord_fx(s * (float) lw))), lw, d->clamp_s);
        v    = r3d_tex_wrap(r3d_f2i(floorf(r3d_texcoord_fx(t * (float) lh))), lh, d->clamp_t);
        near = (u < 0 || v < 0) ? border : r3d_texel(texbase, basesel, masksel, lw, tilesel, d->dt, d->amask, d->s3tc, d->pal, u, v);
        if (nearest)
            /* CI formats key on the raw palette index, not the color. */
            *nearest = ((d->dt == 1 || d->dt == 2) && u >= 0 && v >= 0)
                ? r3d_texel_ci_raw(texbase, basesel, masksel, lw, tilesel, d->dt, u, v)
                : near;
        if (!linear)
            return near;
    }

    /* Bilinear over 2x2 texels with 8-bit integer weights. The
       fractions start on the 1/4096 coordinate grid; cutting them to
       1/256 changes a channel by at most 1 LSB against a float blend.
       The chip's weight precision is not documented. */
    {
        float    fu = r3d_texcoord_fx(s * (float) lw) - 0.5f;
        float    fv = r3d_texcoord_fx(t * (float) lh) - 0.5f;
        int      u0 = r3d_f2i(floorf(fu));
        int      v0 = r3d_f2i(floorf(fv));
        uint32_t wu = r3d_f2u((fu - (float) u0) * 256.0f + 0.5f);
        uint32_t wv = r3d_f2u((fv - (float) v0) * 256.0f + 0.5f);
        uint32_t c[4];
        int      uu0 = r3d_tex_wrap(u0, lw, d->clamp_s), uu1 = r3d_tex_wrap_next(u0, lw, d->clamp_s);
        int      vv0 = r3d_tex_wrap(v0, lh, d->clamp_t), vv1 = r3d_tex_wrap_next(v0, lh, d->clamp_t);

        c[0] = (uu0 < 0 || vv0 < 0) ? border : r3d_texel(texbase, basesel, masksel, lw, tilesel, d->dt, d->amask, d->s3tc, d->pal, uu0, vv0);
        c[1] = (uu1 < 0 || vv0 < 0) ? border : r3d_texel(texbase, basesel, masksel, lw, tilesel, d->dt, d->amask, d->s3tc, d->pal, uu1, vv0);
        c[2] = (uu0 < 0 || vv1 < 0) ? border : r3d_texel(texbase, basesel, masksel, lw, tilesel, d->dt, d->amask, d->s3tc, d->pal, uu0, vv1);
        c[3] = (uu1 < 0 || vv1 < 0) ? border : r3d_texel(texbase, basesel, masksel, lw, tilesel, d->dt, d->amask, d->s3tc, d->pal, uu1, vv1);
        return r3d_lerp_packed(r3d_lerp_packed(c[0], c[1], wu),
                               r3d_lerp_packed(c[2], c[3], wu), wv);
    }
}

/* Per-channel lerp of two ARGB8888 colors by f in [0,1]. */
static uint32_t
r3d_lerp_argb(uint32_t x, uint32_t y, float f)
{
    return r3d_lerp_packed(x, y, r3d_f2u(f * 256.0f + 0.5f));
}

/* Sample texture stage `st`; returns the filtered ARGB8888 color and,
   in *nearest, the nearest texel (the chroma key compares texels before
   filtering). The filter codes are PRIM_MIN_BLEND_FCN (SDK: Table 6-4,
   p. 6-40 / PDF 152): 0 nearest in the largest map, 1 bilinear in the
   largest map, 2 nearest in the nearest map, 3 bilinear in the nearest
   map, 4 "1x1 filtering", 5 trilinear. mag is the stage's decoded
   nearest-or-bilinear flag (see r3d_stage_hdr_init). lod is the
   log2 minification relative to the largest map (> 0 minifies), with
   LOD_BIAS already applied. When has_lod is 0, no per-pixel derivatives
   exist for this stage and the largest map is sampled with the stage's
   filter. */
static uint32_t
r3d_tex_sample(rage128_t *dev, const rage128_raster_state_t *rs, int st,
               r3d_stage_desc_t *d, float s, float t, float lod,
               int has_lod, uint32_t *nearest)
{
    uint32_t minb   = d->minb;
    uint32_t mag    = d->mag;
    int      mipdis = d->mipdis;
    int      top    = d->top;

    /* No per-pixel LOD, mip-mapping disabled, or a filter that does not
       use mip maps (0 or 1): sample the largest map. Minify or magnify
       does not depend on mip-mapping: the sign of lod picks the MIN
       filter's texel filter (odd codes are bilinear) when minifying and
       the MAG filter's when magnifying. Without a per-pixel LOD the
       fetch is bilinear unless both filters are nearest. */
    if (!has_lod || mipdis || minb < 2) {
        int linear = has_lod ? (lod > 0.0f ? (int) (minb & 1) : (mag == 1))
                             : !(minb == 0 && mag == 0);
        return r3d_tex_level(dev, rs, st, d, top, s, t, linear, nearest);
    }

    /* Magnifying (lod <= 0): the MAG filter picks the texel filter. */
    if (lod <= 0.0f)
        return r3d_tex_level(dev, rs, st, d, top, s, t, mag == 1, nearest);

    /* Minifying with mip maps. 2 takes the nearest texel in the nearest
       map, 3 a bilinear fetch in the nearest map, 4 the nearest texel in
       each of the two nearest maps blended by the LOD fraction (the
       supplement's SEC_MIN_BLEND_FCN calls it "1x1 Filtering when mip
       mapping"), and 5 bilinear in both maps, blended. Both drivers
       read the codes this way: Mesa r128 r128_tex.c r128SetTexFilter maps
       GL_LINEAR_MIPMAP_NEAREST to 3 (R128_MIN_BLEND_MIPLINEAR) and
       GL_NEAREST_MIPMAP_LINEAR to 4, and the Windows 98 Direct3D driver
       writes the Direct3D filter code minus 1 into [3:1] (RE:
       ati3draa.dll @b00d8132). */
    {
        float lvl          = lod > (float) top ? (float) top : lod;
        int   texel_linear = (minb == 3 || minb == 5);
        int   mip_linear   = (minb == 4 || minb == 5);

        if (mip_linear) {
            int      l0    = r3d_f2i(floorf(lvl));
            float    f     = lvl - (float) l0;
            int      slotA = top - l0;
            int      slotB = top - (l0 + 1);
            uint32_t ca, cb;

            if (slotB < 0)
                slotB = 0;
            ca = r3d_tex_level(dev, rs, st, d, slotA, s, t, texel_linear, nearest);
            cb = r3d_tex_level(dev, rs, st, d, slotB, s, t, texel_linear, NULL);
            return r3d_lerp_argb(ca, cb, f);
        }
        return r3d_tex_level(dev, rs, st, d, top - r3d_f2i(lvl + 0.5f), s, t,
                             texel_linear, nearest);
    }
}

/* Fast log2f for the per-pixel LOD: the float's exponent plus a cubic
   least-squares fit of log2(m) for the mantissa m in [1, 2); the error
   is at most 1.4e-3 of a mip level. Finite positive input only (the
   caller checks rho2 > 0). */
static inline float
r3d_log2f_fast(float x)
{
    union {
        float    f;
        uint32_t u;
    } v     = { x };
    float e = (float) (int) ((v.u >> 23) & 0xff) - 127.0f;
    float m;

    v.u = (v.u & 0x007fffffu) | 0x3f800000u; /* mantissa -> [1,2) */
    m   = v.f;
    return e + (-2.133847707f + m * (3.010783972f + m * (-1.029521946f + m * 0.153918478f)));
}

/* Alpha-blend factor for an ALPHA_BLND_SRC or ALPHA_BLND_DST code, as
   four floats 0..1 in R, G, B, A order (SDK: Table 6-17, p. 6-49 /
   PDF 161). Every factor has four components: the color codes carry the
   matching alpha (2 is (Rs, Gs, Bs, As), 8 is (Rd, Gd, Bd, Ad)), and
   SRCALPHASAT is (f, f, f, 1) with f = min(As, 1 - Ad). sc and dc are
   the source and destination colors with alpha in [3]. */
static void
r3d_blend_factor(uint32_t code, const float sc[4], const float dc[4], float out[4])
{
    float f;

    /* clang-format off */
    switch (code & 0xf) {
        case 0x0: out[0] = out[1] = out[2] = out[3] = 0.0f; return;
        case 0x1: out[0] = out[1] = out[2] = out[3] = 1.0f; return;
        case 0x2: out[0] = sc[0]; out[1] = sc[1]; out[2] = sc[2]; out[3] = sc[3]; return;
        case 0x3: out[0] = 1.0f - sc[0]; out[1] = 1.0f - sc[1]; out[2] = 1.0f - sc[2]; out[3] = 1.0f - sc[3]; return;
        case 0x4: out[0] = out[1] = out[2] = out[3] = sc[3]; return;
        case 0x5: out[0] = out[1] = out[2] = out[3] = 1.0f - sc[3]; return;
        case 0x6: out[0] = out[1] = out[2] = out[3] = dc[3]; return;
        case 0x7: out[0] = out[1] = out[2] = out[3] = 1.0f - dc[3]; return;
        case 0x8: out[0] = dc[0]; out[1] = dc[1]; out[2] = dc[2]; out[3] = dc[3]; return;
        case 0x9: out[0] = 1.0f - dc[0]; out[1] = 1.0f - dc[1]; out[2] = 1.0f - dc[2]; out[3] = 1.0f - dc[3]; return;
            /* clang-format on */
        case 0xa: /* SRCALPHASAT, listed for both source and destination */
            f      = sc[3] < 1.0f - dc[3] ? sc[3] : 1.0f - dc[3];
            out[0] = out[1] = out[2] = f;
            out[3]                   = 1.0f;
            return;
        default: /* 11, 12: done by the caller; 13-15 reserved, as ONE */
            out[0] = out[1] = out[2] = out[3] = 1.0f;
            return;
    }
}

/* Convert one float window coordinate to fixed point on the subpixel
   grid, in subpixel units. The vertex FPU truncates or rounds to nearest
   by PM4_VC_FPU_SETUP FPU_ROUND_EN (bit 15) (CCE 3D supplement,
   PM4_VC_FPU_SETUP); Mesa r128, xf86-video-r128 and the Windows 98
   Direct3D driver all select truncate. lim is the format's range in
   subpixel units: 2^14 for S.12.2 and 2^15 for S.11.4, the two formats
   SUB_PIX_AMNT selects (RRG: WINDOW_XY_OFFSET, p. 3-249 / PDF 267).
   NaN and out-of-range values are clamped: drivers clip before they
   submit, so this only guards against a malformed draw and does not
   model the chip. */
static int32_t
r3d_snap(float v, float subf, int rnd, int32_t lim)
{
    float s = v * subf;

    if (!(s >= (float) -lim))
        return -lim; /* also catches NaN */
    if (s >= (float) lim)
        return lim - 1;
    return rnd ? (int32_t) lrintf(s) : (int32_t) floorf(s);
}

/* Apply one stage's texture combine to the running pixel color (CCE 3D
   supplement, PRIM_TEXTURE_COMBINE_CNTL_C and SEC_TEX_COMBINE_CNTL_C;
   SDK: Texture Mapping, pp. 6-41-6-45 / PDF 153-157). The color
   function COMB_FCN [3:0] combines two operands: the color factor Ct,
   chosen by COLOR_FACTOR [7:4] and normally the texel, and the input
   factor Cf, chosen by INPUT_FACTOR [13:10]. Cf is normally the
   iterated vertex color on the first stage and the first stage's result
   ("previous color") on the second; Mesa r128 r128_texstate.c
   r128UpdateTextureEnv programs exactly that (INPUT_INTERP on unit 0,
   INPUT_PREVIOUS on unit 1). The stages are chained only through that
   operand; nothing multiplies the two stage results together
   afterwards. The alpha function COMB_FCN_ALPHA [17:14] works the same
   way with ALPHA_FACTOR [21:18] and INPUT_FACTOR_ALPHA [27:25].

   col[] is the running color: the iterated color when the first stage
   runs, the first stage's result when the second runs. prev[] is col[]
   on entry, int_color[] the iterated color from before the first stage
   (so the second stage can still select it), and cc[] is
   CONSTANT_COLOR_C as floats.

   Bit 8 is a fifth function bit on the Pro (R128_COMB_FCN_MSB in the
   xf86-video-r128 register header; the supplement lists [9:8] as
   reserved).
   Mesa r128 sets it with function 4 for GL_BLEND on the Pro
   (COLOR_COMB_BLEND_COLOR in r128_texstate.c). The other codes follow
   the SDK tables, the xf86-video-r128 header, or the Windows 98
   Direct3D driver's tables that map each Direct3D stage operation to a
   function code (RE: ati3draa.dll @b00ce170 builds them; the bit-8
   forms are used only on a Pro, RE: ati3draa.dll @b00ea199). Each case
   names its source.

   The texel arrives as floats. The texture-lighting pass passes the
   chain output in as its texel, with the texel as Ct and the iterated
   color as Cf (see r3d_draw_state_derive), so it runs through this same
   code. */
static void
r3d_tex_combine(const r3d_comb_desc_t *cd, float col[4], float tr, float tg,
                float tb, float ta, const float int_color[4], const float cc[4],
                int first)
{
    uint32_t comb  = cd->comb;
    uint32_t fmsb  = cd->fmsb;
    uint32_t cfac  = cd->cfac;
    uint32_t ifac  = cd->ifac;
    uint32_t comba = cd->comba;
    uint32_t afac  = cd->afac;
    uint32_t ifaca = cd->ifaca;
    float    prev[4];
    float    fc[3];  /* COLOR_FACTOR operand (Ct) */
    float    ci[3];  /* INPUT_FACTOR operand (Cf) */
    float    fa, ia; /* ALPHA_FACTOR / INPUT_FACTOR_ALPHA operands */
    int      i;

    prev[0] = col[0];
    prev[1] = col[1];
    prev[2] = col[2];
    prev[3] = col[3];

    /* COLOR_FACTOR, the Ct operand. The SDK documents 4 texture color,
       5 its inverse, 6 texture alpha and 7 its inverse (SDK: Table 6-8,
       p. 6-43 / PDF 155). 0, 1 and 8 come only from the xf86-video-r128
       header (R128_COLOR_FACTOR_CONST_COLOR,
       R128_COLOR_FACTOR_NCONST_COLOR, R128_COLOR_FACTOR_PREV_COLOR).
       Mesa r128 uses 4, 5, and 0 for GL_BLEND. Undefined codes are
       treated as 4. */
    /* clang-format off */
    switch (cfac) {
        case 0: fc[0] = cc[0];        fc[1] = cc[1];        fc[2] = cc[2];        break; /* constant color */
        case 1: fc[0] = 1.0f - cc[0]; fc[1] = 1.0f - cc[1]; fc[2] = 1.0f - cc[2]; break; /* 1 - constant color */
        case 5: fc[0] = 1.0f - tr;    fc[1] = 1.0f - tg;    fc[2] = 1.0f - tb;    break; /* 1 - texture color */
        case 6: fc[0] = fc[1] = fc[2] = ta;        break;                                /* texture alpha */
        case 7: fc[0] = fc[1] = fc[2] = 1.0f - ta; break;                                /* 1 - texture alpha */
        case 8: fc[0] = prev[0]; fc[1] = prev[1]; fc[2] = prev[2]; break;                /* previous color */
        default: fc[0] = tr; fc[1] = tg; fc[2] = tb; break;                              /* 4: texture color */
    }
    /* clang-format on */
    /* INPUT_FACTOR, the Cf operand: 2 constant color, 3 constant alpha,
       4 interpolated color, 5 interpolated alpha (SDK: Table 6-9,
       p. 6-43 / PDF 155), and on the second stage 8 previous color and
       9 previous alpha (SDK: Table 6-13, p. 6-45 / PDF 157). Mesa r128
       uses 4 on the first stage and 8 on the second. Undefined codes are
       treated as 4. */
    /* clang-format off */
    switch (ifac) {
        case 2: ci[0] = cc[0]; ci[1] = cc[1]; ci[2] = cc[2]; break;                      /* constant color */
        case 3: ci[0] = ci[1] = ci[2] = cc[3]; break;                                    /* constant alpha */
        case 5: ci[0] = ci[1] = ci[2] = int_color[3]; break;                             /* interpolated alpha */
        case 8: ci[0] = prev[0]; ci[1] = prev[1]; ci[2] = prev[2]; break;                /* previous color */
        case 9: ci[0] = ci[1] = ci[2] = prev[3]; break;                                  /* previous alpha */
        default: ci[0] = int_color[0]; ci[1] = int_color[1]; ci[2] = int_color[2]; break;/* 4: interpolated color */
    }
    /* clang-format on */

    /* The color function, PRIMARY_COMB_FCN (SDK: Table 6-7, pp. 6-42-6-43
       / PDF 154-155). Disable outputs the texture color itself, copy
       outputs the COLOR_FACTOR operand Ct and copy input outputs the
       INPUT_FACTOR operand Cf, so disable and copy agree only while
       COLOR_FACTOR selects the texture color. Mesa r128 r128_texstate.c
       uses its own names in its comments, where Ct is the texel and Cf
       the incoming fragment color: it builds COLOR_COMB_DISABLE from
       R128_COMB_DIS and R128_COLOR_FACTOR_TEX to output the texel, and
       uses COLOR_COMB_COPY_INPUT to output the incoming color. */
    switch (comb) {
        case 0: /* Disable: C = the texture color. With bit 8:
                   C = clamp(Ct - Cf), the form the Windows 98 Direct3D
                   driver uses for the Direct3D SUBTRACT operation
                   (Arg1 - Arg2) on a Pro (RE: ati3draa.dll @b00ea199). */
            if (fmsb) {
                for (i = 0; i < 3; i++) {
                    col[i] = fc[i] - ci[i];
                    if (col[i] < 0.0f)
                        col[i] = 0.0f;
                }
                break;
            }
            col[0] = tr;
            col[1] = tg;
            col[2] = tb;
            break;
        case 1: /* Copy: C = Ct */
            col[0] = fc[0];
            col[1] = fc[1];
            col[2] = fc[2];
            break;
        case 2: /* Copy input: C = Cf */
            col[0] = ci[0];
            col[1] = ci[1];
            col[2] = ci[2];
            break;
        case 4: /* Modulate * 2: C = clamp(2 * Cf * Ct). With bit 8 it
                   blends by the texture color: C = Cf * (1 - Ctex) +
                   Ct * Ctex, with Ctex the texture color. Mesa r128's
                   GL_BLEND (COLOR_COMB_BLEND_COLOR) selects the
                   constant color as Ct, which gives its comment's
                   "C = Cf(1-Ct)+CcCt" (there Ct is the texture and Cc
                   the constant). Codes 0, 5 and 6 have their own bit-8
                   forms; every other code ignores the bit. */
            if (fmsb) {
                col[0] = ci[0] * (1.0f - tr) + fc[0] * tr;
                col[1] = ci[1] * (1.0f - tg) + fc[1] * tg;
                col[2] = ci[2] * (1.0f - tb) + fc[2] * tb;
            } else {
                for (i = 0; i < 3; i++) {
                    col[i] = ci[i] * fc[i] * 2.0f;
                    if (col[i] > 1.0f)
                        col[i] = 1.0f;
                }
            }
            break;
        case 5: /* Modulate * 4: C = clamp(4 * Cf * Ct); the Windows 98
                   Direct3D driver maps the MODULATE4X operation here.
                   With bit 8: C = clamp(Ct + Cf * (1 - Ctex)). The
                   driver uses that form for ADDSMOOTH (Arg1 + Arg2 *
                   (1 - Arg1), with Ct the texture color) and for the
                   operation (1 - Arg1) * Arg2 + Arg1.A (with Ct
                   switched to the texture alpha, RE: ati3draa.dll
                   @b00ea1c8); both reduce to this one equation. */
            if (fmsb) {
                float t[3] = { tr, tg, tb };

                for (i = 0; i < 3; i++) {
                    col[i] = fc[i] + ci[i] * (1.0f - t[i]);
                    if (col[i] > 1.0f)
                        col[i] = 1.0f;
                }
            } else {
                for (i = 0; i < 3; i++) {
                    col[i] = ci[i] * fc[i] * 4.0f;
                    if (col[i] > 1.0f)
                        col[i] = 1.0f;
                }
            }
            break;
        case 6: /* Add: C = clamp(Cf + Ct). With bit 8: C = clamp(Ct +
                   Cf * Ctex), the form the Windows 98 Direct3D driver
                   uses for the operation Arg1 * Arg2 + Arg1.A (with Ct
                   switched to the texture alpha, the same
                   RE: ati3draa.dll @b00ea1c8 path). */
            if (fmsb) {
                float t[3] = { tr, tg, tb };

                for (i = 0; i < 3; i++) {
                    col[i] = fc[i] + ci[i] * t[i];
                    if (col[i] > 1.0f)
                        col[i] = 1.0f;
                }
            } else {
                for (i = 0; i < 3; i++) {
                    col[i] = ci[i] + fc[i];
                    if (col[i] > 1.0f)
                        col[i] = 1.0f;
                }
            }
            break;
        case 7: /* Add signed: C = Cf + Ct - 0.5, the SDK's "- 128" on a
                   0..1 scale, clamped to [0, 1]; the SDK does not say
                   whether or how the result is clamped. */
            for (i = 0; i < 3; i++) {
                col[i] = ci[i] + fc[i] - 0.5f;
                col[i] = col[i] < 0.0f ? 0.0f : (col[i] > 1.0f ? 1.0f : col[i]);
            }
            break;
        case 8: /* Blend vertex: Ct * Ai + Cf * (1 - Ai), with Ai the
                   interpolated alpha. The Windows 98 Direct3D driver
                   maps BLENDDIFFUSEALPHA (blend by the vertex diffuse
                   alpha) here on either stage, so the weight is
                   int_color's alpha, not the stage input's. */
            {
                float f = int_color[3];

                for (i = 0; i < 3; i++)
                    col[i] = ci[i] * (1.0f - f) + fc[i] * f;
                break;
            }
        case 9: /* Blend texture: C = Cf * (1 - At) + Ct * At, with At
                   this stage's texel alpha (the SDK writes "primary
                   texel alpha"). Mesa r128 uses it for GL_DECAL
                   (COLOR_COMB_BLEND_TEX). */
            for (i = 0; i < 3; i++)
                col[i] = ci[i] * (1.0f - ta) + fc[i] * ta;
            break;
        case 10: /* Blend constant: the weight is CONSTANT_ALPHA. The
                    Windows 98 Direct3D driver maps BLENDFACTORALPHA
                    here. */
            for (i = 0; i < 3; i++)
                col[i] = ci[i] * (1.0f - cc[3]) + fc[i] * cc[3];
            break;
        case 11: /* Blend premultiply: C = clamp(Ct + Cf * (1 - At)), the
                    SDK's equation. The Windows 98 Direct3D driver maps
                    two operations with that equation here,
                    BLENDTEXTUREALPHAPM (Arg1 + Arg2 * (1 - At)) and
                    (1 - Arg1.A) * Arg2 + Arg1. */
            for (i = 0; i < 3; i++) {
                col[i] = fc[i] + ci[i] * (1.0f - ta);
                if (col[i] > 1.0f)
                    col[i] = 1.0f;
            }
            break;
        case 12: /* Blend previous: the weight is the alpha of col[] on
                    entry. The SDK's text for this code repeats Blend
                    texture's ("primary texel alpha"); the model follows
                    the name and the Windows 98 Direct3D driver, which
                    maps BLENDCURRENTALPHA (blend by the running alpha)
                    here. */
            for (i = 0; i < 3; i++)
                col[i] = ci[i] * (1.0f - prev[3]) + fc[i] * prev[3];
            break;
        case 13: /* Blend premultiply inverse: C = clamp(Ct + Cf * At),
                    the SDK's equation. The Windows 98 Direct3D driver
                    maps Arg1 + Arg1.A * Arg2 here. */
            for (i = 0; i < 3; i++) {
                col[i] = fc[i] + ci[i] * ta;
                if (col[i] > 1.0f)
                    col[i] = 1.0f;
            }
            break;
        case 14: /* Add signed * 2: C = clamp(2 * (Cf + Ct - 0.5)). The
                    Windows 98 Direct3D driver maps ADDSIGNED2X here. */
            for (i = 0; i < 3; i++) {
                col[i] = (ci[i] + fc[i] - 0.5f) * 2.0f;
                col[i] = col[i] < 0.0f ? 0.0f : (col[i] > 1.0f ? 1.0f : col[i]);
            }
            break;
        case 15: /* Blend constant color: C = Ct * CONSTANT_COLOR + Cf *
                    (1 - CONSTANT_COLOR), per channel, the SDK's
                    equation. No driver seen here uses it. */
            for (i = 0; i < 3; i++)
                col[i] = ci[i] * (1.0f - cc[i]) + fc[i] * cc[i];
            break;
        default: /* not reached: all 16 codes have a case */
            /* fall through */
        case 3: /* Modulate: C = Cf * Ct */
            col[0] = ci[0] * fc[0];
            col[1] = ci[1] * fc[1];
            col[2] = ci[2] * fc[2];
            break;
    }

    /* The alpha operands. ALPHA_FACTOR Af: 6 texture alpha, 7 its
       inverse (SDK: Table 6-11, p. 6-44 / PDF 156); other codes are
       treated as 6. INPUT_FACTOR_ALPHA Ai: 1 constant alpha, 2
       interpolated alpha (SDK: Table 6-12, p. 6-45 / PDF 157), and on the
       second stage 4 previous alpha (SDK: Table 6-14, p. 6-45 / PDF 157);
       4 and every other code take the alpha of col[] on entry. */
    fa = (afac == 7) ? (1.0f - ta) : ta;
    /* clang-format off */
    switch (ifaca) {
        case 1:  ia = cc[3];        break; /* constant alpha */
        case 2:  ia = int_color[3]; break; /* interpolated alpha */
        default: ia = prev[3];      break; /* 4: previous alpha */
    }
    /* clang-format on */
    /* The alpha function, COMB_FCN_ALPHA: the color codes 0-7 and 14
       (SDK: Table 6-10, p. 6-44 / PDF 156); 8-13 and 15 are reserved.
       Copy outputs Af. Disable is documented for the first stage only,
       where it outputs the texture alpha. On the second stage it is
       modeled as leaving the incoming alpha unchanged. Both Direct3D
       drivers program it that way: for a stage whose alpha operation
       is Direct3D's disable (alpha unchanged) they write code 0 on the
       second stage, but on the first stage, where code 0 would put the
       texel alpha in place of the vertex alpha, they write copy input
       instead (RE: ati3draa.dll @b00e99a0; RE: ati2dvaa.dll @0002f592).
       Mesa r128 r128_texstate.c r128UpdateTextureEnv reads it as the
       texel alpha on unit 1 too, for GL_REPLACE with an RGBA texture. A
       program that adds an environment map on the second stage and
       leaves that stage's alpha operation at disable depends on the
       pass-through: with an environment map whose texels carry alpha 0,
       the texel reading lets an alpha test discard the whole draw. On
       the texture-lighting pass, whose texel is the incoming color, both
       readings give the same alpha. */
    switch (comba) {
        case 0: /* Disable: A = the texture alpha on the first stage,
                   the incoming alpha after it */
            col[3] = first ? ta : prev[3];
            break;
        case 1: /* Copy: A = Af */
            col[3] = fa;
            break;
        case 2: /* Copy input: A = Ai */
            col[3] = ia;
            break;
        case 4: /* Modulate * 2: A = clamp(2 * Ai * Af) */
            col[3] = ia * fa * 2.0f;
            if (col[3] > 1.0f)
                col[3] = 1.0f;
            break;
        case 6: /* Add: A = clamp(Ai + Af) */
            col[3] = ia + fa;
            if (col[3] > 1.0f)
                col[3] = 1.0f;
            break;
        case 7: /* Add signed: A = Ai + Af - 0.5, clamped to [0, 1]; the
                   SDK gives the bias but not the clamp, as for color. */
            col[3] = ia + fa - 0.5f;
            col[3] = col[3] < 0.0f ? 0.0f : (col[3] > 1.0f ? 1.0f : col[3]);
            break;
        case 5: /* Modulate * 4: A = clamp(4 * Ai * Af); the Windows 98
                    Direct3D driver maps the alpha MODULATE4X here. */
            col[3] = ia * fa * 4.0f;
            if (col[3] > 1.0f)
                col[3] = 1.0f;
            break;
        case 14: /* Add signed * 2: A = clamp(2 * (Ai + Af - 0.5)); the
                    driver maps the alpha ADDSIGNED2X here. */
            col[3] = (ia + fa - 0.5f) * 2.0f;
            col[3] = col[3] < 0.0f ? 0.0f : (col[3] > 1.0f ? 1.0f : col[3]);
            break;
        default: /* reserved codes are treated as modulate */
        case 3:  /* Modulate: A = Ai * Af */
            col[3] = ia * fa;
            break;
    }
}

/* The per-pixel texture stages: sampling, chroma keys, the two combine
   stages and texture lighting. It is a separate function so that JIT
   spans which do not carry their own texture code call this same code,
   and so produce the same pixels as the interpreter. col carries the
   iterated vertex color in and the combined color out; the return is 0
   when a chroma key discards the pixel. The interpreter loop and the
   compiled spans of both JIT backends call it per pixel through the host
   C calling convention (on ARM64: tc in x0, the weights in s0-s2, col in
   x1; on x86-64 the System V or Windows convention, as the x86-64
   emitter sets it up). */
int
rage128_texstage_run(void *tcv, float w0, float w1, float w2, float *col)
{
    r3d_texctx_t                 *tc  = (r3d_texctx_t *) tcv;
    rage128_t                    *dev = tc->dev;
    const rage128_raster_state_t *rs  = tc->rs;
    const rage128_draw_state_t   *ds  = &rs->d;
    float                         int_color[4]; /* iterated color, the combine's interpolated operand */
    float                         ir  = 1.0f;
    float                         rhw = 0.0f;

    int_color[0] = col[0];
    int_color[1] = col[1];
    int_color[2] = col[2];
    int_color[3] = col[3];

    if (ds->do_persp) {
        rhw = w0 * tc->arhw + w1 * tc->brhw + w2 * tc->crhw;
        if (rhw != 0.0f)
            ir = 1.0f / rhw;
    }
    if (ds->tex_en) {
        float    sp  = w0 * tc->sta + w1 * tc->stb + w2 * tc->stc;
        float    tp  = w0 * tc->tta + w1 * tc->ttb + w2 * tc->ttc;
        float    s   = sp * ir;
        float    t   = tp * ir;
        float    lod = 0.0f;
        uint32_t tx, tnear;

        /* Per-pixel LOD: log2 of the larger of the texel-space
           derivative lengths along x and along y, taken through the
           perspective divide. The documents do not give the chip's LOD
           formula; this is the usual isotropic measure. It is computed
           only when the sampler uses it (need_lod): for mip-map
           selection, or when the MIN and MAG filters differ and the
           sign of the LOD picks between them. */
        if (ds->need_lod) {
            float dsx, dsy, dtx, dty, ax2, ay2, rho2;

            if (ds->do_persp) {
                /* rhw is this same dot, computed once above. */
                float wp  = rhw;
                float iw2 = (wp != 0.0f) ? 1.0f / (wp * wp) : 0.0f;

                dsx = (tc->dSdx * wp - sp * tc->dWdx) * iw2;
                dsy = (tc->dSdy * wp - sp * tc->dWdy) * iw2;
                dtx = (tc->dTdx * wp - tp * tc->dWdx) * iw2;
                dty = (tc->dTdy * wp - tp * tc->dWdy) * iw2;
            } else {
                dsx = tc->dSdx;
                dsy = tc->dSdy;
                dtx = tc->dTdx;
                dty = tc->dTdy;
            }
            dsx *= ds->texw0;
            dsy *= ds->texw0;
            dtx *= ds->texh0;
            dty *= ds->texh0;
            ax2  = dsx * dsx + dtx * dtx;
            ay2  = dsy * dsy + dty * dty;
            rho2 = ax2 > ay2 ? ax2 : ay2;
            lod  = rho2 > 0.0f ? 0.5f * r3d_log2f_fast(rho2) + ds->lod_bias
                               : -1000.0f;
        }

        tx = r3d_tex_sample(dev, rs, 0, &tc->sd0, s, t, lod, ds->need_lod,
                            ds->need_ck ? &tnear : NULL);

        /* Texture chroma key, MISC_3D_STATE_CNTL_REG CLR_CMP_FCN_3D
           [31:30] (CCE 3D supplement, MISC_3D_STATE_CNTL_REG), modeled
           as discarding the pixel when the compare is true: 2 discards
           when the texel differs from CLR_CMP_CLR_3D (0x1a24) and 3
           when it matches, both under CLR_CMP_MSK_3D (0x1a28), using
           the nearest texel before filtering. A zero mask disables the
           compare. The documents do not say what a zero mask does, but
           the Windows 98 Direct3D driver runs code 3 with mask 0 and
           key 0 on ordinary textured draws, and read literally that
           would match, and discard, every texel. */
        if (ds->ck3d_on) {
            int eq = (tnear & ds->ck3d_msk) == (ds->ck3d_clr & ds->ck3d_msk);

            if (ds->ckfn == 3 ? eq : !eq)
                return 0;
        }
        /* The second chroma key: TEX_CNTL_C TEX_CHROMA_KEY_EN (bit 12)
           with TEXTURE_CLR_CMP_CLR_C and TEXTURE_CLR_CMP_MSK_C (CCE 3D
           supplement). It has no function field, so it is modeled as
           reject on a masked match, with a zero mask disabling it as for
           the first key. */
        if (ds->ckc_on) {
            if ((tnear & ds->ckc_msk) == (ds->ckc_clr & ds->ckc_msk))
                return 0;
        }
        r3d_tex_combine(&ds->comb[0], col, ((tx >> 16) & 0xff) / 255.0f,
                        ((tx >> 8) & 0xff) / 255.0f, (tx & 0xff) / 255.0f,
                        (tx >> 24) / 255.0f, int_color, ds->cc, 1);
    }
    if (ds->sec_en) {
        /* sel is SEC_TEX_CNTL_C SEC_SRC_SEL_ST (bit 0): the secondary
           stage samples the first (0) or second (1) coordinate set
           (SDK: Texture Mapping, p. 6-47 / PDF 159). The stage has its
           own perspective enable and W (sec_persp_diff and sel_w in
           r3d_draw_state_derive). When both match the primary stage's,
           its rhw and ir are the very same values and are reused; the
           compiled spans emit no second divide in that case either.
           Otherwise the stage forms its own W dot product and
           reciprocal, 1 with perspective off, as the primary does. */
        int      sel    = ds->sec_sel;
        int      persp2 = ds->do_persp ^ ds->sec_persp_diff;
        int      own    = ds->sel_w || ds->sec_persp_diff;
        float    rhw2 = rhw;
        float    ir2  = ir;
        float    sp2  = sel ? (w0 * tc->s2a + w1 * tc->s2b + w2 * tc->s2c)
                            : (w0 * tc->sta + w1 * tc->stb + w2 * tc->stc);
        float    tp2  = sel ? (w0 * tc->t2a + w1 * tc->t2b + w2 * tc->t2c)
                            : (w0 * tc->tta + w1 * tc->ttb + w2 * tc->ttc);
        float    s, t;
        float    lod2 = 0.0f;
        uint32_t tx;

        if (own) {
            rhw2 = 0.0f;
            ir2  = 1.0f;
            if (persp2) {
                rhw2 = w0 * tc->a2rhw + w1 * tc->b2rhw + w2 * tc->c2rhw;
                if (rhw2 != 0.0f)
                    ir2 = 1.0f / rhw2;
            }
        }
        s = sp2 * ir2;
        t = tp2 * ir2;

        /* Per-pixel LOD for the secondary stage: the same measure as
           the primary stage, over the gradients of the coordinate set
           sel picks and of the stage's W, scaled to the secondary
           texture's size. The chroma keys compare only the primary
           texel, so no nearest texel is fetched here (NULL). */
        if (ds->need_lod2) {
            float gsx = sel ? tc->dS2dx : tc->dSdx, gsy = sel ? tc->dS2dy : tc->dSdy;
            float gtx = sel ? tc->dT2dx : tc->dTdx, gty = sel ? tc->dT2dy : tc->dTdy;
            float dsx, dsy, dtx, dty, ax2, ay2, rho2;

            if (persp2) {
                float wp  = rhw2;
                float iw2 = (wp != 0.0f) ? 1.0f / (wp * wp) : 0.0f;
                float gwx = ds->sel_w ? tc->dW2dx : tc->dWdx;
                float gwy = ds->sel_w ? tc->dW2dy : tc->dWdy;

                dsx = (gsx * wp - sp2 * gwx) * iw2;
                dsy = (gsy * wp - sp2 * gwy) * iw2;
                dtx = (gtx * wp - tp2 * gwx) * iw2;
                dty = (gty * wp - tp2 * gwy) * iw2;
            } else {
                dsx = gsx;
                dsy = gsy;
                dtx = gtx;
                dty = gty;
            }
            dsx *= ds->texw1;
            dsy *= ds->texw1;
            dtx *= ds->texh1;
            dty *= ds->texh1;
            ax2  = dsx * dsx + dtx * dtx;
            ay2  = dsy * dsy + dty * dty;
            rho2 = ax2 > ay2 ? ax2 : ay2;
            lod2 = rho2 > 0.0f ? 0.5f * r3d_log2f_fast(rho2) + ds->lod_bias
                               : -1000.0f;
        }
        tx = r3d_tex_sample(dev, rs, 1, &tc->sd1, s, t, lod2, ds->need_lod2,
                            NULL);
        r3d_tex_combine(&ds->comb[1], col, ((tx >> 16) & 0xff) / 255.0f,
                        ((tx >> 8) & 0xff) / 255.0f, (tx & 0xff) / 255.0f,
                        (tx >> 24) / 255.0f, int_color, ds->cc, 0);
    }
    /* Texture lighting: "the first argument is implicitly the output of
       the texture combine units, and the second is the interpolated
       color" (SDK: Texture Mapping, p. 6-42 / PDF 154). So the chain
       output goes in as this pass's texel and the iterated color as its
       input. Code 9 therefore blends by the chain output's alpha; SDK
       Table 6-15 writes "primary texel alpha" for it, and no driver seen
       here uses the code. */
    if (ds->light_on)
        r3d_tex_combine(&ds->lcomb, col, col[0], col[1], col[2], col[3],
                        int_color, ds->cc, 0);
    return 1;
}

/* Run one owned row through a compiled span, handling a tiled color
   and/or Z surface without any change to the emitted code.

   A tiled row is contiguous only inside one 64-byte tile column; the next
   column starts 1024 bytes on. So the row is walked in pieces that stay
   inside one column of both surfaces, and each piece gets a rebased row
   address, base' = tiled_addr(first byte) - first_x * bpp, which makes
   the block's own base + x * bpp land on the tiled byte for every x in
   the piece. The block sees only a different x range and base.

   The split gives exactly the output of an unsplit walk: the edge seeds
   are 64-bit integers, exact under multiplication, and the depth seed is
   advanced by the same repeated add the block does per pixel, never by a
   multiply. Returns the row's written column range, packed as the block
   packs it. Exported because the JIT verify mode drives blocks through
   this same walk.

   The walked range is the triangle's (scissored) bounding box, so most
   pieces of a row hold no covered pixel. When the span is the GPU lane's
   capture, the row's covered columns [cl, ch] are known up front from the
   same exact integer edge clip the capture applies per piece, and the
   walk uses them: a row with none returns at once; a piece wholly left
   of cl still advances the depth seed (the covered pieces' seeds depend
   on those adds) but is not called, since its capture would reject it
   with no effect; and the walk stops after the piece holding ch, since no
   later piece is called and nothing reads the seed past it. Every piece
   that is called receives the same seed from the same adds in the same
   order, so the captured spans are unchanged. The JIT blocks report the
   range they wrote (after the depth and alpha tests), not coverage, so
   for them the bounds stay the whole row. */
uint64_t
rage128_3d_jit_row(r128_jit_tri_t *jt, r128_jit_span_fn jfn, int64_t e0, int64_t e1,
                   int64_t e2, double zline, uint32_t drow, uint32_t zrow, int32_t py,
                   int c_tld, int z_tld, uint32_t bpp, uint32_t zbpp)
{
    int32_t xs  = jt->x0;
    int32_t xe  = jt->x1;
    int32_t rx0 = -1, rx1 = -1;
    int32_t cl = xs, ch = xe; /* covered columns: whole row unless known */
    double  zc = zline;

    if (!c_tld && !z_tld)
        return jfn(jt, e0, e1, e2, zline, drow, zrow, py);

    if (jfn == rage128_gpu_capture_span
        && !rage128_gpu_capture_row_cover(jt, e0, e1, e2, py, &cl, &ch))
        return ((uint64_t) (uint32_t) rx1 << 32) | (uint32_t) rx0;

    for (int32_t xa = xs; xa <= xe;) {
        int32_t  xb = xe;
        int64_t  k  = (int64_t) (xa - xs);
        uint32_t cb = drow;
        uint32_t zb = zrow;
        uint64_t r;
        int32_t  jr0;

        /* last column of this piece: the nearest tile-column edge of
           either tiled surface (bpp is 2 or 4, both divide 64) */
        if (c_tld) {
            int32_t nb = xa + (int32_t) ((64u - (((uint32_t) xa * bpp) & 63u)) / bpp);

            if (nb - 1 < xb)
                xb = nb - 1;
            cb = drow + r128_tile_x((uint32_t) xa * bpp) - (uint32_t) xa * bpp;
        }
        if (z_tld) {
            int32_t nb = xa + (int32_t) ((64u - (((uint32_t) xa * zbpp) & 63u)) / zbpp);

            if (nb - 1 < xb)
                xb = nb - 1;
            zb = zrow + r128_tile_x((uint32_t) xa * zbpp) - (uint32_t) xa * zbpp;
        }

        if (xb >= cl) {
            jt->x0 = xa;
            jt->x1 = xb;
            r      = jfn(jt, e0 + k * jt->e0dxi, e1 + k * jt->e1dxi, e2 + k * jt->e2dxi,
                         zc, cb, zb, py);
            jr0    = (int32_t) (uint32_t) r;
            if (jr0 >= 0) {
                if (rx0 < 0)
                    rx0 = jr0;
                rx1 = (int32_t) (uint32_t) (r >> 32);
            }
        }
        /* nothing past the piece holding the last covered column is
           called, so its seed is never read */
        if (xb >= ch)
            break;
        /* advance the depth DDA exactly as the block did, pixel by pixel
           (a multiply here could differ in the last bit), but only when
           another piece follows: nothing reads the seed past the last
           piece, and most rows are a single piece */
        if (xb < xe)
            for (int32_t i = xa; i <= xb; i++)
                zc += jt->dZdx;
        xa = xb + 1;
    }
    jt->x0 = xs;
    jt->x1 = xe;
    return ((uint64_t) (uint32_t) rx1 << 32) | (uint32_t) rx0;
}

/* Line walk. The setup engine draws lines with the datapath's
   Bresenham walk: the guide says DST_Y_MAJOR and DST_LAST_PEL are
   "written during setup engine operations" (RRG: DP_CNTL, p. 3-165 /
   PDF 183), BRES_SIGN is set during them (RRG: DP_CNTL, p. 3-166 /
   PDF 184), and DST_BRES_ERR, DST_BRES_INC and DST_BRES_DEC serve "line
   and Trapezoid leading edge" (RRG: DST_BRES_ERR, p. 3-142 / PDF 160).
   The walk takes one pixel per step along the major axis between the
   snapped endpoints. The minor pixel is the one whose interval holds
   the ideal minor coordinate at the major pixel's center, and an exact
   tie takes the smaller coordinate. The tie side follows the 2D line
   walker (rage128_2d_bres_line), which reads the garbled BRES_SIGN text
   as stepping the minor axis on a zero error term only for X-major
   lines with DST_Y_DIR 0 and Y-major lines with DST_X_DIR 0; that is an
   inference, not the guide's words. It makes a line and its reverse
   cover the same pixels. |dy| == |dx| counts as X-major (DST_Y_MAJOR 0).
   Attributes interpolate along the major axis between the first and
   last walked pixel centers. How the chip seeds the interpolants below
   a pixel, and which side a 45-degree tie takes, are not documented. */
typedef struct r3d_line_t {
    int     ymajor; /* one pixel per row (else per column)           */
    int32_t sub;
    int64_t ma, na;         /* snapped start, major/minor subunits           */
    int64_t d_maj, d_min;   /* end - start; d_maj made > 0                  */
    int32_t m0, mstep;      /* first major pixel and a->b direction          */
    int64_t len;            /* |last - first| major pixels (weight span)     */
    int32_t p_lo, p_hi;     /* drawn major pixel range, last-pixel rule applied */
    int     x0, y0, x1, y1; /* pixel bbox of the walk                     */
} r3d_line_t;

static inline int64_t
r3d_floor_div(int64_t n, int64_t d) /* d > 0 */
{
    int64_t q = n / d;

    return (n % d < 0) ? q - 1 : q;
}

/* Minor pixel of major pixel p: ceil(ideal / sub) - 1 in exact integer
   arithmetic (== floor for a non-tie, the smaller pixel on a tie). */
static inline int32_t
r3d_line_minor(const r3d_line_t *ln, int32_t p)
{
    int64_t pc  = (int64_t) p * ln->sub + (ln->sub >> 1);
    int64_t num = ln->na * ln->d_maj + (pc - ln->ma) * ln->d_min - 1;

    return (int32_t) r3d_floor_div(num, (int64_t) ln->sub * ln->d_maj);
}

/* Returns 0 when the walk draws nothing (a one-pixel line whose last
   pixel is left out). */
static int
r3d_line_setup(const rage128_raster_state_t *rs, const r3d_vtx_t *a,
               const r3d_vtx_t *b, r3d_line_t *ln)
{
    const rage128_draw_state_t *ds  = &rs->d;
    int32_t                     axi = r3d_snap(a->x, ds->subf, ds->rnd, ds->slim) + ds->woxi;
    int32_t                     ayi = r3d_snap(a->y, ds->subf, ds->rnd, ds->slim) + ds->woyi;
    int32_t                     bxi = r3d_snap(b->x, ds->subf, ds->rnd, ds->slim) + ds->woxi;
    int32_t                     byi = r3d_snap(b->y, ds->subf, ds->rnd, ds->slim) + ds->woyi;
    int64_t                     dx = (int64_t) bxi - axi, dy = (int64_t) byi - ayi;
    int64_t                     adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int32_t                     m0, m1, n_lo, n_hi;

    ln->ymajor = ady > adx;
    ln->sub    = ds->sub;
    if (ln->ymajor) {
        ln->ma    = ayi;
        ln->na    = axi;
        ln->d_maj = dy;
        ln->d_min = dx;
    } else {
        ln->ma    = axi;
        ln->na    = ayi;
        ln->d_maj = dx;
        ln->d_min = dy;
    }
    m0 = (int32_t) r3d_floor_div(ln->ma, ln->sub);
    m1 = (int32_t) r3d_floor_div(ln->ma + ln->d_maj, ln->sub);
    if (ln->d_maj < 0) {
        ln->d_maj = -ln->d_maj;
        ln->d_min = -ln->d_min;
    } else if (ln->d_maj == 0)
        ln->d_maj = 1; /* d_min is 0 too: a point, the divide is inert */
    ln->m0    = m0;
    ln->mstep = m1 >= m0 ? 1 : -1;
    ln->len   = m1 >= m0 ? (int64_t) m1 - m0 : (int64_t) m0 - m1;
    if (rs->t3d.setup_cntl & (1 << 18)) { /* SU_POLY_LINE: not the last line */
        if (m1 == m0)
            return 0;
        m1 -= ln->mstep;
    }
    ln->p_lo = m0 < m1 ? m0 : m1;
    ln->p_hi = m0 < m1 ? m1 : m0;
    n_lo     = r3d_line_minor(ln, ln->p_lo);
    n_hi     = r3d_line_minor(ln, ln->p_hi);
    if (n_lo > n_hi) {
        int32_t t = n_lo;
        n_lo      = n_hi;
        n_hi      = t;
    }
    if (ln->ymajor) {
        ln->x0 = n_lo;
        ln->x1 = n_hi;
        ln->y0 = ln->p_lo;
        ln->y1 = ln->p_hi;
    } else {
        ln->x0 = ln->p_lo;
        ln->x1 = ln->p_hi;
        ln->y0 = n_lo;
        ln->y1 = n_hi;
    }
    return 1;
}

/* Cylindrical wrap of one texture coordinate over a triangle, enabled by
   PRIM_TEX_WRAP_S / _T (SDK: Texture Mapping, p. 6-41 / PDF 153). The
   SDK gives no rule; the model uses Direct3D's definition of texture
   wrapping: interpolate along the shorter way round, with 0.0 and 1.0
   the same place. The vertex whose two edges both span more than half
   the texture is the one across the seam. If it is the low end it moves
   up one period, otherwise the other two do; moving always upward keeps
   a tiled address in range. Any other pattern, including an
   inconsistent one, is left alone. The test order and direction follow
   the Windows 98 Direct3D driver's own software version of the wrap
   (RE: ati3draa.dll @b00f14f3); what the chip does with an inconsistent
   triangle is not documented. u are the raw coordinates, p the size of
   one period on each vertex's stored interpolant. */
static void
r3d_cyl_wrap(float u0, float u1, float u2, float p0, float p1, float p2,
             float *o0, float *o1, float *o2)
{
    float e20 = fabsf(u2 - u0), e12 = fabsf(u1 - u2), e01 = fabsf(u0 - u1);

    if (e20 > 0.5f && e12 > 0.5f) {
        if (u2 < u1)
            *o2 += p2;
        else {
            *o0 += p0;
            *o1 += p1;
        }
    } else if (e12 > 0.5f && e01 > 0.5f) {
        if (u1 < u0)
            *o1 += p1;
        else {
            *o0 += p0;
            *o2 += p2;
        }
    } else if (e01 > 0.5f && e20 > 0.5f) {
        if (u0 < u2)
            *o0 += p0;
        else {
            *o1 += p1;
            *o2 += p2;
        }
    }
}

/* Rasterize one triangle under the captured CCE 3D state. Vertex x and
   y are snapped to the subpixel grid, S.12.2 or S.11.4 as SETUP_CNTL
   SUB_PIX_AMNT (bit 19) selects (CCE 3D supplement, SETUP_CNTL; RRG:
   WINDOW_XY_OFFSET, p. 3-249 / PDF 267). The edge functions run in
   exact integer subpixel units at pixel centers with a top-left fill
   rule, so two triangles that share an edge paint each pixel on it
   exactly once; both windings are drawn. Colors interpolate linearly in
   screen space and texture coordinates with perspective correction
   (see r3d_draw_state_derive for premult and do_persp). The fill rule
   and the pixel-center sampling are modeled; the guides do not state
   them.

   ln != NULL rasterizes a line instead. The walk supplies each row's
   run of pixels and stand-in barycentric values (e1 counts major pixels
   from the first walked pixel, e0 = len - e1, e2 = 0, so w1 runs 0..1
   along the major axis and vertex c never contributes), and the
   per-pixel pipeline, the compiled span and the GPU capture then run as
   for a triangle.

   r3d_setup does the per-primitive work (per-draw values, snap, area,
   bounding box, edge seeds, interpolant constants, the JIT triangle
   record) into an r3d_setup_t; r3d_raster runs the row and pixel loops
   from it. */
typedef struct r3d_setup_t {
    /* per-draw values */
    uint32_t dst_dt;
    int      bpp;
    uint32_t wmask;
    int      aux_on;
    uint32_t zfn;
    int      zbpp;
    uint32_t zmax;
    int      zshift;
    int      sten_on;
    uint32_t sfn, sfail_op, zpass_op, zfail_op, sref, svmask, swmask;
    int      sshift;
    int      z_en, tex_en, sec_en, alpha_en;
    int      c_tld, z_tld;
    uint32_t cpb, zpb;
    uint8_t *zptr;
    uint32_t z_base, z_lim;
    uint8_t *cptr;
    uint32_t c_base, c_lim;
    int      dither;
    /* per-primitive */
    int64_t area2i;
    int     x0, y0, x1, y1;
    int64_t e0dxi, e0dyi, e1dxi, e1dyi, e2dxi, e2dyi, e0ri, e1ri, e2ri;
    int     e0b, e1b, e2b; /* 1 where the fill rule biased that edge */
    float   invs;
    double  zr_d, dZdx, dZdy;
    float   vca[4], vcb[4], vcc[4];
    float   spa[3], spb[3], spc[3];
    float   fga, fgb, fgc;
    float   fogr, fogg, fogb;
    int     jneg, jverify;
    int32_t lcur, lrem, lstep;
} r3d_setup_t;

/* Returns 0 when there is nothing to draw (rejected datatype, zero
   snapped area, or an empty scissor intersection). tctx and jt are the
   pixel loop's own locals, filled here. Their addresses are passed to
   the texture stage and the JIT calls, so they are kept out of st, which
   lets the compiler hold st's fields in registers. */
static int
r3d_setup(rage128_t *dev, const rage128_raster_state_t *rs,
          const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c,
          r128_jit_span_fn jfn, const r3d_line_t *ln, r3d_setup_t *st,
          r3d_texctx_t *tctx, r128_jit_tri_t *jt)
{
    /* The per-draw register decode is in rs->d (explained in
       r3d_draw_state_derive); the most used fields are copied to
       locals. */
    const rage128_draw_state_t *ds     = &rs->d;
    uint32_t                    zrowpx = ds->zrowpx;
    int                         tex_en = ds->tex_en;
    int                         sec_en = ds->sec_en;
    /* Row bytes of the color and Z surfaces. For a tiled surface the
       interpreter applies the tile transform per pixel, and the JIT and
       GPU lanes take each row through the tile-column walk of
       rage128_3d_jit_row. */
    uint32_t cpb = rs->dst_pitch * (uint32_t) ds->bpp;
    uint32_t zpb = zrowpx * (uint32_t) ds->zbpp;
    /* Where Z is read and written, chosen once per draw. A staged Z
       buffer (rs->z_staged, Z in AGP memory) is read and written in
       z_stage.arena at zaddr - z_base, never in svga.vram, where the
       vram_mask wrap would land an AGP address on local VRAM. Otherwise
       zptr stays NULL and the svga.vram path runs. z_base is z_offset,
       so the AGP bit (25) cancels out of the index. z_lim bounds it: a
       pixel outside the staged range is skipped, so a malformed draw
       cannot run off the arena. */
    uint8_t *zptr   = rs->z_staged ? dev->z_stage.arena : NULL;
    uint32_t z_base = rs->t3d.z_offset;
    uint32_t z_lim  = rs->z_staged ? dev->z_stage.len : 0;
    /* The same choice for the color target: a staged (AGP) target is
       read and written in c_stage.arena at daddr - dst_offset; a local
       one in svga.vram (cptr NULL). */
    uint8_t *cptr   = rs->c_staged ? dev->c_stage.arena : NULL;
    uint32_t c_base = rs->dst_offset;
    uint32_t c_lim  = rs->c_staged ? dev->c_stage.len : 0;
    int      sub    = ds->sub;
    float    subf   = ds->subf;
    int      rnd    = ds->rnd;
    int32_t  slim   = ds->slim;
    int32_t  axi, ayi, bxi, byi, cxi, cyi;
    int64_t  area2i;
    float    ax, ay, bx, by, cx, cy;
    float    area2, inv, invs;
    int      x0, y0, x1, y1, sx0, sy0, sx1, sy1;

    if (!ds->draw_ok) {
        /* Unhandled destination datatype: reject the draw. */
        return 0;
    }

    if (ln) {
        /* Stand-in barycentric frame (see above); the walk's bounding
           box takes the place of the triangle's. The snapped coordinates
           are not used: every edge quantity below comes from the walk. */
        area2i = ln->len > 0 ? ln->len : 1;
        axi = ayi = bxi = byi = cxi = cyi = 0;
        ax = ay = bx = by = cx = cy = 0.0f;
        x0                          = ln->x0;
        y0                          = ln->y0;
        x1                          = ln->x1;
        y1                          = ln->y1;
    } else {
        axi = r3d_snap(a->x, subf, rnd, slim) + ds->woxi;
        ayi = r3d_snap(a->y, subf, rnd, slim) + ds->woyi;
        bxi = r3d_snap(b->x, subf, rnd, slim) + ds->woxi;
        byi = r3d_snap(b->y, subf, rnd, slim) + ds->woyi;
        cxi = r3d_snap(c->x, subf, rnd, slim) + ds->woxi;
        cyi = r3d_snap(c->y, subf, rnd, slim) + ds->woyi;

        /* Twice the signed area, exact, in subpixel units squared, with the
           sign convention of the edge functions below (edge(a, b) evaluated
           at c, the negative of the usual orient2d). A triangle that snaps
           to zero area covers no pixel and is dropped. The float copies
           (snapped coordinates, area in pixels squared) feed the interpolant
           gradients; invs scales the integer edge values to weights. */
        area2i = (int64_t) (cxi - axi) * (byi - ayi)
            - (int64_t) (cyi - ayi) * (bxi - axi);
        if (area2i == 0)
            return 0;
        ax = (float) axi / subf;
        ay = (float) ayi / subf;
        bx = (float) bxi / subf;
        by = (float) byi / subf;
        cx = (float) cxi / subf;
        cy = (float) cyi / subf;
        x0 = (int) floorf(ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx));
        y0 = (int) floorf(ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy));
        x1 = (int) ceilf(ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx));
        y1 = (int) ceilf(ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy));
    }
    area2 = (float) area2i / (subf * subf);
    inv   = 1.0f / area2;
    invs  = 1.0f / (float) area2i;

    /* Clip the bounding box to the scissor (inclusive bounds). */
    sx0 = ds->sx0;
    sy0 = ds->sy0;
    sx1 = ds->sx1;
    sy1 = ds->sy1;
    if (x0 < sx0)
        x0 = sx0;
    if (y0 < sy0)
        y0 = sy0;
    if (x1 > sx1)
        x1 = sx1;
    if (y1 > sy1)
        y1 = sy1;
    if (x0 > x1 || y0 > y1)
        return 0;

    /* Per-triangle setup, kept out of the pixel loop: in serial mode the
       rasterizer runs on the emulation thread, so per-pixel cost is
       emulation speed. */
    {
        float e0dx, e0dy, e1dx, e1dy, e2dx, e2dy; /* edge gradients, pixel units, */
                                                  /* for the interpolant constants */
        /* Integer edge functions in subpixel units squared, evaluated at
           pixel centers (px * sub + sub / 2) and stepped by whole pixels
           (sub units). They are exact, with no drift, and e == 0 marks a
           pixel center that lies on an edge, for the fill rule. */
        int64_t px0s = (int64_t) x0 * sub + (sub >> 1);
        int64_t py0s = (int64_t) y0 * sub + (sub >> 1);
        int64_t e0dxi, e0dyi, e1dxi, e1dyi, e2dxi, e2dyi, e0ri, e1ri, e2ri;

        if (ln) {
            /* e1 counts major pixels from the first walked pixel in the
               a -> b direction. The float copies keep the triangle's
               pixels-squared scale, so inv * e_dx equals e_dxi / area2i as
               it does for a triangle. */
            e1dxi = ln->ymajor ? 0 : ln->mstep;
            e1dyi = ln->ymajor ? ln->mstep : 0;
            e0dxi = -e1dxi;
            e0dyi = -e1dyi;
            e2dxi = 0;
            e2dyi = 0;
            e1ri  = ((int64_t) (ln->ymajor ? y0 : x0) - ln->m0) * ln->mstep;
            e0ri  = area2i - e1ri;
            e2ri  = 0;
            e0dx  = (float) e0dxi / (subf * subf);
            e0dy  = (float) e0dyi / (subf * subf);
            e1dx  = (float) e1dxi / (subf * subf);
            e1dy  = (float) e1dyi / (subf * subf);
            e2dx  = 0.0f;
            e2dy  = 0.0f;
        } else {
            e0dx  = cy - by;
            e0dy  = bx - cx; /* edge(b,c) */
            e1dx  = ay - cy;
            e1dy  = cx - ax; /* edge(c,a) */
            e2dx  = by - ay;
            e2dy  = ax - bx; /* edge(a,b) */
            e0dxi = (int64_t) sub * (cyi - byi);
            e0dyi = (int64_t) sub * (bxi - cxi);
            e1dxi = (int64_t) sub * (ayi - cyi);
            e1dyi = (int64_t) sub * (cxi - axi);
            e2dxi = (int64_t) sub * (byi - ayi);
            e2dyi = (int64_t) sub * (axi - bxi);
            e0ri  = (px0s - bxi) * (cyi - byi) - (py0s - byi) * (cxi - bxi);
            e1ri  = (px0s - cxi) * (ayi - cyi) - (py0s - cyi) * (axi - cxi);
            e2ri  = (px0s - axi) * (byi - ayi) - (py0s - ayi) * (bxi - axi);
        }
        /* Seed the z DDA from the exact integer row seeds, before the
           fill-rule bias below changes them (dZdx and dZdy follow after the
           texture gradients). */
        double invd = 1.0 / (double) area2i;
        double azd = (double) a->z, bzd = (double) b->z, czd = (double) c->z;
        double zr_d = ((double) e0ri * azd + (double) e1ri * bzd + (double) e2ri * czd) * invd;
        /* Top-left fill rule: a pixel whose center lies on an edge (e == 0)
           belongs to the triangle only if that is a top or left edge, so two
           triangles that share an edge never both paint it. An edge is
           top-left when (dy > 0) || (dy == 0 && dx < 0), mirrored for the
           other winding. Adding -1 (+1 for the other winding) to each other
           edge's row seed keeps the pixel test a plain sign check. A line's
           coverage is the walk itself, so it gets no rule and no bias.
           The bias decides coverage only. The barycentric weights that
           interpolate color, specular, fog and texture coordinates take the
           unbiased edge values, so they still sum to one and a constant
           attribute is not scaled down; on a one-pixel triangle the bias is
           a large share of the area and would visibly darken it. Each lane
           subtracts the recorded bias again before it converts an edge
           value to a weight. */
#define R3D_TL(dx, dy) (area2i > 0 ? ((dy) > 0 || ((dy) == 0 && (dx) < 0)) \
                                   : ((dy) < 0 || ((dy) == 0 && (dx) > 0)))
        int e0b = 0, e1b = 0, e2b = 0;

        if (!ln) {
            e0b = !R3D_TL(cxi - bxi, cyi - byi);
            e1b = !R3D_TL(axi - cxi, ayi - cyi);
            e2b = !R3D_TL(bxi - axi, byi - ayi);
            if (e0b)
                e0ri += (area2i > 0) ? -1 : 1;
            if (e1b)
                e1ri += (area2i > 0) ? -1 : 1;
            if (e2b)
                e2ri += (area2i > 0) ? -1 : 1;
        }
#undef R3D_TL
        float vca[4], vcb[4], vcc[4]; /* vertex colors, 0..1 */
        float spa[3], spb[3], spc[3]; /* vertex speculars */
        float fga, fgb, fgc;          /* vertex fog factors, 1 = unfogged */
        float fogr = ds->fogr, fogg = ds->fogg, fogb = ds->fogb;
        /* The texture-stage context (tctx, filled below) holds the
           per-triangle interpolants and sampler descriptors that
           rage128_texstage_run uses per pixel, called from here or from a
           compiled span through jt->texctx. */
        int premult = ds->premult;
        int dither  = ds->dither;

        {
            const r3d_vtx_t *vv[3]   = { a, b, c };
            float           *cols[3] = { vca, vcb, vcc };
            float           *sps[3]  = { spa, spb, spc };
            float           *fgs[3]  = { &fga, &fgb, &fgc };

            for (int i = 0; i < 3; i++) {
                uint32_t d = vv[i]->diffuse;
                uint32_t s = vv[i]->spec;

                cols[i][0] = ((d >> 16) & 0xff) / 255.0f;
                cols[i][1] = ((d >> 8) & 0xff) / 255.0f;
                cols[i][2] = (d & 0xff) / 255.0f;
                cols[i][3] = (d >> 24) / 255.0f;
                sps[i][0]  = ((s >> 16) & 0xff) / 255.0f;
                sps[i][1]  = ((s >> 8) & 0xff) / 255.0f;
                sps[i][2]  = (s & 0xff) / 255.0f;
                *fgs[i]    = (s >> 24) / 255.0f;
            }
            if (ds->solid_on) {
                /* Solid shading: CONSTANT_COLOR_C at all three vertices, so
                   the loop interpolates that constant. */
                memcpy(vca, ds->cc, sizeof(vca));
                memcpy(vcb, ds->cc, sizeof(vcb));
                memcpy(vcc, ds->cc, sizeof(vcc));
            } else if (ds->flat_on) {
                /* Flat shading: copy the chosen vertex's color to all three
                   so the loop interpolates a constant. */
                float fl[4];

                memcpy(fl, cols[ds->flat_src], sizeof(fl));
                memcpy(vca, fl, sizeof(fl));
                memcpy(vcb, fl, sizeof(fl));
                memcpy(vcc, fl, sizeof(fl));
            }
        }
        tctx->dev = dev;
        tctx->rs  = rs;
        tctx->sta = premult ? a->s * a->rhw : a->s;
        tctx->stb = premult ? b->s * b->rhw : b->s;
        tctx->stc = premult ? c->s * c->rhw : c->s;
        tctx->tta = premult ? a->t * a->rhw : a->t;
        tctx->ttb = premult ? b->t * b->rhw : b->t;
        tctx->ttc = premult ? c->t * c->rhw : c->t;
        /* The secondary stage's W: rhw2 under SEC_SRC_SEL_W, else the
           vertex rhw (see sel_w in r3d_draw_state_derive). Under
           premult the second coordinate set is multiplied by it, as
           the first set is by rhw. */
        tctx->a2rhw = ds->sel_w ? a->rhw2 : a->rhw;
        tctx->b2rhw = ds->sel_w ? b->rhw2 : b->rhw;
        tctx->c2rhw = ds->sel_w ? c->rhw2 : c->rhw;
        tctx->s2a = premult ? a->s2 * tctx->a2rhw : a->s2;
        tctx->s2b = premult ? b->s2 * tctx->b2rhw : b->s2;
        tctx->s2c = premult ? c->s2 * tctx->c2rhw : c->s2;
        tctx->t2a = premult ? a->t2 * tctx->a2rhw : a->t2;
        tctx->t2b = premult ? b->t2 * tctx->b2rhw : b->t2;
        tctx->t2c = premult ? c->t2 * tctx->c2rhw : c->t2;
        if (premult && (ds->wrap_s0 | ds->wrap_t0 | ds->wrap_s1 | ds->wrap_t1)) {
            /* Cylindrical wrap works on the raw coordinate, the stored value
               when SETUP_CNTL TEXTURE_ST_FORMAT is 0 and the setup engine
               multiplies by w; one period is +1.0 raw, which is +rhw on the
               s/w interpolant. With TEXTURE_ST_FORMAT set (coordinates
               already multiplied by w) "texture wrapping is not available"
               (CCE 3D supplement, SETUP_CNTL), so the wrap enables are left
               unapplied and the stored coordinates interpolate as they are. */
            if (ds->wrap_s0)
                r3d_cyl_wrap(a->s, b->s, c->s, a->rhw, b->rhw, c->rhw,
                             &tctx->sta, &tctx->stb, &tctx->stc);
            if (ds->wrap_t0)
                r3d_cyl_wrap(a->t, b->t, c->t, a->rhw, b->rhw, c->rhw,
                             &tctx->tta, &tctx->ttb, &tctx->ttc);
            if (ds->wrap_s1)
                r3d_cyl_wrap(a->s2, b->s2, c->s2, tctx->a2rhw, tctx->b2rhw, tctx->c2rhw,
                             &tctx->s2a, &tctx->s2b, &tctx->s2c);
            if (ds->wrap_t1)
                r3d_cyl_wrap(a->t2, b->t2, c->t2, tctx->a2rhw, tctx->b2rhw, tctx->c2rhw,
                             &tctx->t2a, &tctx->t2b, &tctx->t2c);
        }
        tctx->arhw = a->rhw;
        tctx->brhw = b->rhw;
        tctx->crhw = c->rhw;

        /* Screen-space gradients for the primary stage's per-pixel LOD. S
           and T are the s * rhw and t * rhw interpolants and W is rhw, so
           per pixel ds/dx = (dSdx * W - S * dWdx) / W^2 gives the derivative
           through the perspective divide. Each barycentric weight's gradient
           is its edge gradient times inv. */
        tctx->dSdx = inv * (tctx->sta * e0dx + tctx->stb * e1dx + tctx->stc * e2dx);
        tctx->dSdy = inv * (tctx->sta * e0dy + tctx->stb * e1dy + tctx->stc * e2dy);
        tctx->dTdx = inv * (tctx->tta * e0dx + tctx->ttb * e1dx + tctx->ttc * e2dx);
        tctx->dTdy = inv * (tctx->tta * e0dy + tctx->ttb * e1dy + tctx->ttc * e2dy);
        tctx->dWdx = inv * (a->rhw * e0dx + b->rhw * e1dx + c->rhw * e2dx);
        tctx->dWdy = inv * (a->rhw * e0dy + b->rhw * e1dy + c->rhw * e2dy);
        /* Gradients of the secondary stage's own W, for its LOD when it
           divides by rhw2; with the primary W selected the stage reads
           dWdx and dWdy instead, and these stay zero. */
        tctx->dW2dx = 0.0f;
        tctx->dW2dy = 0.0f;
        if (ds->sel_w) {
            tctx->dW2dx = inv * (tctx->a2rhw * e0dx + tctx->b2rhw * e1dx + tctx->c2rhw * e2dx);
            tctx->dW2dy = inv * (tctx->a2rhw * e0dy + tctx->b2rhw * e1dy + tctx->c2rhw * e2dy);
        }
        /* Depth runs on its own double-precision DDA, not on the float
           barycentric weights. Window z is linear in screen space, so a
           plane DDA gives it exactly up to rounding, and stepping it in
           double keeps the drift below 0.001 LSB of a 24-bit Z. dZdx and
           dZdy come from the exact integer edge gradients (the subpixel
           scale cancels against invd); the row seed zr_d was taken above
           from the integer row seeds before the fill-rule bias. The chip's
           depth precision is not documented. */
        double dZdx = ((double) e0dxi * azd + (double) e1dxi * bzd + (double) e2dxi * czd) * invd;
        double dZdy = ((double) e0dyi * azd + (double) e1dyi * bzd + (double) e2dyi * czd) * invd;
        /* Gradients of the second coordinate set, for the secondary stage's
           LOD. They are computed only when that stage samples the second set
           (sec_sel); otherwise it uses the first set's gradients above. */
        tctx->dS2dx = 0.0f;
        tctx->dS2dy = 0.0f;
        tctx->dT2dx = 0.0f;
        tctx->dT2dy = 0.0f;

        if (ds->need_lod2 && ds->sec_sel) {
            tctx->dS2dx = inv * (tctx->s2a * e0dx + tctx->s2b * e1dx + tctx->s2c * e2dx);
            tctx->dS2dy = inv * (tctx->s2a * e0dy + tctx->s2b * e1dy + tctx->s2c * e2dy);
            tctx->dT2dx = inv * (tctx->t2a * e0dx + tctx->t2b * e1dx + tctx->t2c * e2dx);
            tctx->dT2dy = inv * (tctx->t2a * e0dy + tctx->t2b * e1dy + tctx->t2c * e2dy);
        }
        /* Per-triangle sampler descriptors: the stage headers decoded per
           draw, plus a mip-slot cache filled on first use. */
        if (tex_en)
            r3d_stage_desc_init(rs, 0, &tctx->sd0);
        if (sec_en)
            r3d_stage_desc_init(rs, 1, &tctx->sd1);

        /* A JIT block with inline texture code and the GPU span capture both
           read the slot cache directly, without the fill-on-first-use call,
           so every slot they can reach (0..top) is resolved here first; the
           values are the same either way. JIT blocks that call
           rage128_texstage_run resolve slots inside it. The capture is named
           separately because rage128_jit_tex_inline is false on a host with
           no JIT backend; without this, its triangle records would carry an
           unresolved slot cache and every texel read would be garbage. */
        if (jfn && (tex_en || sec_en)
            && (jfn == rage128_gpu_capture_span || rage128_jit_tex_inline(ds))) {
            if (tex_en)
                for (int sl = 0; sl <= ds->sh[0].top; sl++)
                    r3d_stage_slot(dev, rs, 0, &tctx->sd0, sl);
            if (sec_en)
                for (int sl = 0; sl <= ds->sh[1].top; sl++)
                    r3d_stage_slot(dev, rs, 1, &tctx->sd1, sl);
        }

        /* Span JIT: a block compiled for this draw state runs whole owned
           rows in place of r3d_raster's pixel loop. In verify mode both run
           and are compared, and the interpreter's output is kept. The
           triangle record carries only per-triangle values; everything else
           is compiled into the block. The record is normalized for winding
           so the block always keeps a pixel when e >= 0. That is exact for
           the 64-bit edges, and (float)(-e) * (-invs) rounds the same as
           (float)e * invs, so the barycentric weights match the
           interpreter's bit for bit. */
        int jneg    = (area2i < 0);
        int jverify = 0;

        if (jfn) {
            memset(jt, 0, sizeof(*jt));
            jt->vram      = dev->svga.vram;
            jt->vram_mask = dev->vram_mask;
            jt->zptr      = zptr;
            jt->z_base    = z_base;
            jt->z_lim     = z_lim;
            jt->cptr      = cptr;
            jt->c_base    = c_base;
            jt->c_lim     = c_lim;
            jt->x0        = x0;
            jt->x1        = x1;
            jt->e0dxi     = jneg ? -e0dxi : e0dxi;
            jt->e1dxi     = jneg ? -e1dxi : e1dxi;
            jt->e2dxi     = jneg ? -e2dxi : e2dxi;
            jt->invs      = jneg ? -invs : invs;
            jt->e0b       = e0b;
            jt->e1b       = e1b;
            jt->e2b       = e2b;
            jt->dZdx      = dZdx;
            memcpy(jt->vca, vca, sizeof(jt->vca));
            memcpy(jt->vcb, vcb, sizeof(jt->vcb));
            memcpy(jt->vcc, vcc, sizeof(jt->vcc));
            jt->texctx    = tctx;
            jt->fog[0]    = fga;
            jt->fog[1]    = fgb;
            jt->fog[2]    = fgc;
            jt->fog_table = rs->t3d.fog_table; /* a pointer, so the table's contents
                                                 stay out of the block key; read only
                                                 when fog_table_en */
            memcpy(jt->spa, spa, sizeof(spa)); /* [3] stays 0 (memset above) */
            memcpy(jt->spb, spb, sizeof(spb));
            memcpy(jt->spc, spc, sizeof(spc));
            /* Verify mode compares writes made at once. The GPU capture
               defers its writes into segment records, so it would always
               mis-compare; the GPU lane has its own check (R128_GPU=verify).
               Only real JIT blocks are verified here. */
            jverify = jfn != rage128_gpu_capture_span
                && jfn != rage128_gpu_count_span
                && rage128_jit_verify_mode(dev);
        }

        /* The rows of an X-major line come from a cursor over the major
           pixels, taken in order of increasing row (the minor coordinate
           changes monotonically with the major one). A Y-major row is a
           single pixel, computed directly. */
        int32_t lcur = 0, lrem = 0, lstep = 1;

        if (ln && !ln->ymajor) {
            lrem  = ln->p_hi - ln->p_lo + 1;
            lstep = ln->d_min >= 0 ? 1 : -1;
            lcur  = lstep > 0 ? ln->p_lo : ln->p_hi;
        }

        /* Pass the loop everything else it reads. */
        st->dst_dt   = ds->dst_dt;
        st->bpp      = ds->bpp;
        st->wmask    = ds->wmask;
        st->aux_on   = ds->aux_on;
        st->zfn      = ds->zfn;
        st->zbpp     = ds->zbpp;
        st->zmax     = ds->zmax;
        st->zshift   = ds->zshift;
        st->sten_on  = ds->sten_on;
        st->sfn      = ds->sfn;
        st->sfail_op = ds->sfail_op;
        st->zpass_op = ds->zpass_op;
        st->zfail_op = ds->zfail_op;
        st->sref     = ds->sref;
        st->svmask   = ds->svmask;
        st->swmask   = ds->swmask;
        st->sshift   = ds->sshift;
        st->z_en     = ds->z_en;
        st->tex_en   = tex_en;
        st->sec_en   = sec_en;
        st->alpha_en = ds->alpha_en;
        st->c_tld    = ds->c_tiled;
        st->z_tld    = ds->z_tiled;
        st->cpb      = cpb;
        st->zpb      = zpb;
        st->zptr     = zptr;
        st->z_base   = z_base;
        st->z_lim    = z_lim;
        st->cptr     = cptr;
        st->c_base   = c_base;
        st->c_lim    = c_lim;
        st->dither   = dither;
        st->area2i   = area2i;
        st->x0       = x0;
        st->y0       = y0;
        st->x1       = x1;
        st->y1       = y1;
        st->e0dxi    = e0dxi;
        st->e0dyi    = e0dyi;
        st->e1dxi    = e1dxi;
        st->e1dyi    = e1dyi;
        st->e2dxi    = e2dxi;
        st->e2dyi    = e2dyi;
        st->e0ri     = e0ri;
        st->e1ri     = e1ri;
        st->e2ri     = e2ri;
        st->e0b      = e0b;
        st->e1b      = e1b;
        st->e2b      = e2b;
        st->invs     = invs;
        st->zr_d     = zr_d;
        st->dZdx     = dZdx;
        st->dZdy     = dZdy;
        memcpy(st->vca, vca, sizeof(vca));
        memcpy(st->vcb, vcb, sizeof(vcb));
        memcpy(st->vcc, vcc, sizeof(vcc));
        memcpy(st->spa, spa, sizeof(spa));
        memcpy(st->spb, spb, sizeof(spb));
        memcpy(st->spc, spc, sizeof(spc));
        st->fga     = fga;
        st->fgb     = fgb;
        st->fgc     = fgc;
        st->fogr    = fogr;
        st->fogg    = fogg;
        st->fogb    = fogb;
        st->jneg    = jneg;
        st->jverify = jverify;
        st->lcur    = lcur;
        st->lrem    = lrem;
        st->lstep   = lstep;
    }
    return 1;
}

static void
r3d_raster(rage128_t *dev, const rage128_raster_state_t *rs, int thr_id, int thr_mask,
           const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c,
           r128_jit_span_fn jfn, const r3d_line_t *ln)
{
    const rage128_draw_state_t *ds = &rs->d;
    r3d_setup_t                 st;
    r3d_texctx_t                tctx;
    r128_jit_tri_t              jt;

    if (!r3d_setup(dev, rs, a, b, c, jfn, ln, &st, &tctx, &jt))
        return;
    {
        /* the loop's locals, copied from the setup */
        uint32_t dst_dt   = st.dst_dt;
        int      bpp      = st.bpp;
        uint32_t wmask    = st.wmask;
        int      aux_on   = st.aux_on;
        uint32_t zfn      = st.zfn;
        int      zbpp     = st.zbpp;
        uint32_t zmax     = st.zmax;
        int      zshift   = st.zshift;
        int      sten_on  = st.sten_on;
        uint32_t sfn      = st.sfn;
        uint32_t sfail_op = st.sfail_op;
        uint32_t zpass_op = st.zpass_op;
        uint32_t zfail_op = st.zfail_op;
        uint32_t sref     = st.sref;
        uint32_t svmask   = st.svmask;
        uint32_t swmask   = st.swmask;
        int      sshift   = st.sshift;
        int      z_en     = st.z_en;
        int      tex_en   = st.tex_en;
        int      sec_en   = st.sec_en;
        int      alpha_en = st.alpha_en;
        int      c_tld    = st.c_tld;
        int      z_tld    = st.z_tld;
        uint32_t cpb      = st.cpb;
        uint32_t zpb      = st.zpb;
        uint8_t *zptr     = st.zptr;
        uint32_t z_base   = st.z_base;
        uint32_t z_lim    = st.z_lim;
        uint8_t *cptr     = st.cptr;
        uint32_t c_base   = st.c_base;
        uint32_t c_lim    = st.c_lim;
        int      dither   = st.dither;
        int64_t  area2i   = st.area2i;
        int      x0 = st.x0, y0 = st.y0, x1 = st.x1, y1 = st.y1;
        int64_t  e0dxi = st.e0dxi, e0dyi = st.e0dyi;
        int64_t  e1dxi = st.e1dxi, e1dyi = st.e1dyi;
        int64_t  e2dxi = st.e2dxi, e2dyi = st.e2dyi;
        int64_t  e0ri = st.e0ri, e1ri = st.e1ri, e2ri = st.e2ri;
        /* The fill-rule bias that r3d_setup added to each edge seed: the
           weights below take it back out (see the fill-rule note there). */
        int64_t      e0bias = st.e0b ? ((area2i > 0) ? -1 : 1) : 0;
        int64_t      e1bias = st.e1b ? ((area2i > 0) ? -1 : 1) : 0;
        int64_t      e2bias = st.e2b ? ((area2i > 0) ? -1 : 1) : 0;
        float        invs   = st.invs;
        double       zr_d = st.zr_d, dZdx = st.dZdx, dZdy = st.dZdy;
        const float *vca = st.vca, *vcb = st.vcb, *vcc = st.vcc;
        const float *spa = st.spa, *spb = st.spb, *spc = st.spc;
        float        fga = st.fga, fgb = st.fgb, fgc = st.fgc;
        float        fogr = st.fogr, fogg = st.fogg, fogb = st.fogb;
        int          jneg    = st.jneg;
        int          jverify = st.jverify;
        uint64_t     js_rows = 0, js_px = 0, js_empty = 0;
        int32_t      lcur = st.lcur, lrem = st.lrem, lstep = st.lstep;

        for (int py = y0; py <= y1; py++, e0ri += e0dyi, e1ri += e1dyi, e2ri += e2dyi, zr_d += dZdy) {
            /* Row ownership: a raster worker draws only the rows it owns.
               The edge and Z row values step in the loop increment either
               way, so the owned rows come out exactly as in a serial walk.
               thr_mask 0 (serial) owns every row. */
            if (!r128_row_owned(py, thr_id, thr_mask))
                continue;
            int64_t e0 = e0ri, e1 = e1ri, e2 = e2ri;
            double  zline = zr_d;
            int     rxa = x0, rxb = x1; /* this row's column walk */

            if (ln) {
                if (ln->ymajor) {
                    if (py < ln->p_lo || py > ln->p_hi)
                        continue;
                    rxa = rxb = r3d_line_minor(ln, py);
                } else {
                    while (lrem > 0 && r3d_line_minor(ln, lcur) < py) {
                        lcur += lstep;
                        lrem--;
                    }
                    if (lrem == 0 || r3d_line_minor(ln, lcur) != py)
                        continue;
                    rxa = rxb = lcur;
                    lcur += lstep;
                    lrem--;
                    while (lrem > 0 && r3d_line_minor(ln, lcur) == py) {
                        if (lcur < rxa)
                            rxa = lcur;
                        else
                            rxb = lcur;
                        lcur += lstep;
                        lrem--;
                    }
                }
                if (rxa < x0)
                    rxa = x0;
                if (rxb > x1)
                    rxb = x1;
                if (rxa > rxb)
                    continue; /* scissored away */
                /* Seed the run's first column: exact for the integer edges,
                   and the same double depth seed every lane starts from. */
                {
                    int64_t k = rxa - x0;

                    e0 += k * e0dxi;
                    e1 += k * e1dxi;
                    e2 += k * e2dxi;
                    zline = zr_d + (double) k * dZdx;
                }
                jt.x0 = rxa;
                jt.x1 = rxb;
            }
            /* Row base: the linear row address, or the y part of the tile
               transform (the x part is added per pixel by r128_tile_x). */
            uint32_t drow = rs->dst_offset + (c_tld ? r128_tile_y((uint32_t) py, cpb) : (uint32_t) py * cpb);
            uint32_t zrow = rs->t3d.z_offset + (z_tld ? r128_tile_y((uint32_t) py, zpb) : (uint32_t) py * zpb);
            int      rx0 = -1, rx1 = -1;
            int      jrow_ver = 0;

            if (jfn) {
                int64_t je0 = jneg ? -e0 : e0;
                int64_t je1 = jneg ? -e1 : e1;
                int64_t je2 = jneg ? -e2 : e2;

                if (!jverify) {
                    /* Compiled span replaces the pixel loop for this row. */
                    uint64_t jr   = rage128_3d_jit_row(&jt, jfn, je0, je1, je2, zline,
                                                       drow, zrow, py, c_tld, z_tld,
                                                       bpp, (uint32_t) ds->zbpp);
                    int32_t  jrx0 = (int32_t) (uint32_t) jr;
                    int32_t  jrx1 = (int32_t) (uint32_t) (jr >> 32);

                    js_rows++;
                    if (jrx0 >= 0) {
                        js_px += (uint32_t) (jrx1 - jrx0 + 1);
                        if (!cptr)
                            r3d_mark_row_dirty(dev, rs->dst_offset, drow, cpb,
                                               c_tld, py, jrx0, jrx1, bpp);
                    } else
                        js_empty++;
                    continue;
                }
                /* Verify mode: the JIT runs first (its writes are recorded
                   and undone), then the interpreter loop below, and
                   rage128_jit_verify_post compares the two. Tiled rows go
                   through the same split walk and a save and compare that use
                   the tile transform, so they are checked like linear ones. */
                jrow_ver = rage128_jit_verify_pre(dev, &jt, ds, jfn, je0, je1, je2,
                                                  zline, drow, zrow, py,
                                                  c_tld, z_tld);
            }

            for (int px = rxa; px <= rxb; px++, e0 += e0dxi, e1 += e1dxi, e2 += e2dxi, zline += dZdx) {
                float    w0, w1, w2;
                float    col[4]; /* r,g,b,a 0..1 */
                uint32_t zaddr = 0, zi = 0, zbuf;
                uint8_t *zcell = NULL; /* resolved Z cell (staged arena or VRAM) */
                uint8_t *dcell;        /* resolved color cell (same scheme)      */
                uint32_t daddr, dst = 0, out;

                if (area2i > 0) {
                    if (e0 < 0 || e1 < 0 || e2 < 0)
                        continue;
                } else {
                    if (e0 > 0 || e1 > 0 || e2 > 0)
                        continue;
                }
                /* Auxiliary scissors (RRG: AUX_SC_CNTL, p. 3-156 / PDF 174).
                   The bounding-box clip in r3d_setup covers only the main
                   scissor. Under the DRI, linux r128 DRM r128_state.c
                   r128_emit_clip_rects puts the window's visible clip
                   rectangles in the three auxiliary scissors, so they are
                   tested per pixel. */
                if (aux_on && !rage128_aux_sc_pass(rs->aux_sc_cntl, rs->aux_sc_rect, px, py))
                    continue;
                /* Polygon stipple (brush type 9, the 32x32 mono pattern): a
                   clear pattern bit drops the pixel before Z and stencil.
                   The pattern is aligned to the destination: row y & 31,
                   with bit 31 the leftmost pixel of the 32-pixel tile. Mesa
                   r128 r128_state.c r128DDPolygonStipple packs its rows that
                   way. */
                if (ds->stip_en
                    && !((rs->stipple[py & 31] >> (31 - (px & 31))) & 1))
                    continue;
                w0 = (float) (e0 - e0bias) * invs;
                w1 = (float) (e1 - e1bias) * invs;
                w2 = (float) (e2 - e2bias) * invs;

                /* Depth comes from the double DDA (zline), not the float
                   weights; see the dZdx / dZdy note in r3d_setup. zc is
                   that value clamped to [0, 1]. */
                double zc = zline;
                if (!(zc > 0.0))
                    zc = 0.0; /* written this way to catch NaN too,
                                 which must not reach the fog-table
                                 index or the z quantize: converting
                                 NaN to an integer is undefined */
                if (zc > 1.0)
                    zc = 1.0;

                /* Z and stencil tests. Stencil lives in the Z cell, and "if
                   the STENCIL_EN bit is set, Z reads will be done regardless
                   of the setting of the Z_EN bit" (CCE 3D supplement,
                   TEX_CNTL_C), so the cell is read in either case. With Z
                   testing off, a passing stencil test takes STEN_ZPASS_OP,
                   which the supplement applies when the Z test passes or Z
                   testing is off. Without stencil a failed Z test discards
                   at once. With it, a failed test does not discard yet: the
                   stencil update for a failure comes after the alpha test,
                   so the results wait in zres and sres. */
                int      zres = 1, sres = 1;
                uint32_t sbuf = 0;

                if (z_en || sten_on) {
                    /* Quantize in double: z * zmax + 0.5 is exact for zmax up
                       to 0xffffffff, so z == 1.0 maps to exactly zmax. In
                       float, 16777215.5 (the 24-bit case) is not representable
                       and rounds up to zmax + 1, which would put far pixels one
                       step past a buffer cleared to the far value, so they
                       would fail a less-or-equal test. The clamp guards
                       against z > 1. */
                    double zq = zc * (double) zmax + 0.5;

                    if (zq > (double) zmax)
                        zq = (double) zmax;
                    zi    = (uint32_t) zq;
                    zaddr = zrow + (z_tld ? r128_tile_x((uint32_t) px * zbpp) : (uint32_t) px * zbpp);
                    /* Resolve the Z cell once. Staged Z is read in the arena
                       at zaddr - z_base, and an index outside the staged range
                       is skipped, not wrapped, so a malformed draw cannot run
                       off the arena. Local Z uses svga.vram & vram_mask. The
                       test and the write below both use zcell, so they always
                       agree on the cell. */
                    if (zptr) {
                        uint32_t zoff = zaddr - z_base;

                        if (zoff + (uint32_t) zbpp > z_lim)
                            continue; /* out of staged region: drop the pixel */
                        zcell = zptr + zoff;
                    } else
                        zcell = &dev->svga.vram[zaddr & dev->vram_mask];
                    if (zbpp == 2)
                        zbuf = *(uint16_t *) zcell & zmax;
                    else
                        zbuf = (*(uint32_t *) zcell >> zshift) & zmax;
                    if (z_en)
                        zres = r3d_cmp(zfn, zi, zbuf);
                    if (sten_on) {
                        sbuf = (*(uint32_t *) zcell >> sshift) & 0xff;
                        sres = r3d_cmp(sfn, sref & svmask, sbuf & svmask);
                    } else if (!zres)
                        continue;
                }

                /* Shade: Gouraud (flat shading was folded into the vertex
                   colors). */
                col[0] = w0 * vca[0] + w1 * vcb[0] + w2 * vcc[0];
                col[1] = w0 * vca[1] + w1 * vcb[1] + w2 * vcc[1];
                col[2] = w0 * vca[2] + w1 * vcb[2] + w2 * vcc[2];
                col[3] = w0 * vca[3] + w1 * vcb[3] + w2 * vcc[3];

                /* Texture stages, in rage128_texstage_run (shared with the
                   compiled spans): perspective-correct coordinates through
                   the interpolated rhw, and the second stage chained on the
                   first one's result. A return of 0 is a chroma-key
                   discard. */
                if (tex_en || sec_en) {
                    if (!rage128_texstage_run(&tctx, w0, w1, w2, col))
                        continue;
                }

                /* Specular add, TEX_CNTL_C SPECULAR_LIGHT_EN (bit 11): the
                   iterated specular color is added after the texture
                   lighting (CCE 3D supplement, TEX_CNTL_C). */
                if (ds->spec_en) {
                    col[0] += w0 * spa[0] + w1 * spb[0] + w2 * spc[0];
                    col[1] += w0 * spa[1] + w1 * spb[1] + w2 * spc[1];
                    col[2] += w0 * spa[2] + w1 * spb[2] + w2 * spc[2];
                    if (col[0] > 1.0f)
                        col[0] = 1.0f;
                    if (col[1] > 1.0f)
                        col[1] = 1.0f;
                    if (col[2] > 1.0f)
                        col[2] = 1.0f;
                }

                /* Fog: C = f * C + (1 - f) * fog color, on RGB only (SDK:
                   Setting 3D Render States, p. 6-51 / PDF 163). It is applied
                   after the specular add, the OpenGL order; the documents do
                   not give the chip's order. MISC_3D_STATE_CNTL_REG
                   FOG_TABLE_EN (bit 14) picks the factor: 0 is vertex fog,
                   the interpolated top byte of the specular dword (SDK:
                   Table F-45, p. F-52 / PDF 342), and 1 is the fog table,
                   indexed by the pixel's interpolated z. */
                if (ds->fog_en) {
                    float f;

                    if (ds->fog_table_en) {
                        /* Modeled: entry i holds the factor at depth i / 255,
                           and adjacent entries are blended by the depth bits
                           below the index, so a fog ramp that spans only a
                           few entries comes out smooth rather than in steps.
                           The SDK says only that the interpolated z indexes
                           a 256-entry table. The blend is modeled because
                           the Windows 98 Direct3D driver bakes the
                           application's fog curve into the table, and with
                           a short fog range it packs the whole ramp into
                           about nine entries (seen in the emulator with
                           Thief 2), which a plain lookup draws as nine flat
                           bands. */
                        /* The index and fraction come from one exact
                           conversion: zc * 255 scaled by 2^32 and truncated
                           gives the entry in the high word and a 32-bit
                           fraction in the low word, which every lane (the
                           JITs, the GPU path) reproduces exactly; the blend
                           itself is float math. */
                        const uint8_t *ft = rs->t3d.fog_table;
                        uint64_t       q  = (uint64_t) (zc * 255.0 * 4294967296.0);
                        uint32_t       i  = (uint32_t) (q >> 32);
                        uint32_t       i1 = i < 255 ? i + 1 : i;
                        float          t  = (float) (uint32_t) q * (1.0f / 4294967296.0f);
                        float          fa = (float) ft[i];
                        float          fd = (float) ((int) ft[i1] - (int) ft[i]);

                        f = (fa + fd * t) / 255.0f;
                    } else
                        f = w0 * fga + w1 * fgb + w2 * fgc;

                    if (f < 0.0f)
                        f = 0.0f;
                    if (f > 1.0f)
                        f = 1.0f;
                    col[0] = col[0] * f + fogr * (1.0f - f);
                    col[1] = col[1] * f + fogg * (1.0f - f);
                    col[2] = col[2] * f + fogb * (1.0f - f);
                }

                /* Alpha test: MISC_3D_STATE_CNTL_REG ALPHA_TEST_OP [26:24]
                   against REF_ALPHA [7:0] (SDK: Setting 3D Render States,
                   p. 6-50 / PDF 162). */
                if (ds->atest_en
                    && !r3d_cmp(ds->atest_fn, (uint32_t) (col[3] * 255.0f + 0.5f), ds->atest_ref))
                    continue;

                /* Stencil update and the deferred discard. The operation is
                   chosen by the test results (STEN_SFAIL_OP, STEN_ZPASS_OP,
                   STEN_ZFAIL_OP) and the stencil byte is rewritten under
                   STEN_WRITE_MSK; the Z write keeps it. Codes 0-5 are the
                   SDK's (SDK: Table 6-26, p. 6-57 / PDF 169), with increment
                   and decrement clamped at 0 and 255 as the supplement says.
                   Codes 6 and 7, wrapping increment and decrement, are marked
                   as guesses in Mesa r128's register header
                   (R128_STENCIL_S_FAIL_INC_WRAP and its neighbors) and no
                   driver seen here uses them. A pixel that failed a test
                   ends here, before the color and Z writes. */
                if (sten_on) {
                    uint32_t sop  = !sres ? sfail_op : (zres ? zpass_op : zfail_op);
                    uint32_t snew = sbuf;

                    /* clang-format off */
                    switch (sop) {
                        case 0:                                        break; /* KEEP     */
                        case 1: snew = 0;                              break; /* ZERO     */
                        case 2: snew = sref;                           break; /* REPLACE  */
                        case 3: snew = sbuf == 0xff ? 0xff : sbuf + 1; break; /* INC sat  */
                        case 4: snew = sbuf == 0 ? 0 : sbuf - 1;       break; /* DEC sat  */
                        case 5: snew = ~sbuf & 0xff;                   break; /* INVERT   */
                        case 6: snew = (sbuf + 1) & 0xff;              break; /* INC wrap */
                        default: snew = (sbuf - 1) & 0xff;             break; /* DEC wrap */
                    }
                    /* clang-format on */
                    snew = (sbuf & ~swmask) | (snew & swmask);
                    if (snew != sbuf) {
                        uint32_t *zp = (uint32_t *) zcell;

                        *zp = (*zp & ~(0xffu << sshift)) | (snew << sshift);
                    }
                    if (!sres || !zres)
                        continue;
                }

                daddr = drow + (c_tld ? r128_tile_x((uint32_t) px * bpp) : (uint32_t) px * bpp);
                /* Resolve the color cell once so the blend read and the
                   final write use the same cell. An index outside the staged
                   range is skipped, not wrapped, so a malformed draw cannot
                   run off the arena. */
                if (cptr) {
                    uint32_t coff = daddr - c_base;

                    if (coff + (uint32_t) bpp > c_lim)
                        continue; /* out of staged region: drop the pixel */
                    dcell = cptr + coff;
                } else
                    dcell = &dev->svga.vram[daddr & dev->vram_mask];
                if (alpha_en)
                    dst = r3d_dst_read(dcell, dst_dt);

                /* Alpha blend: ALPHA_BLND_SRC [19:16] and ALPHA_BLND_DST
                   [23:20] pick the factors, ALPHA_COMB_FCN [13:12] combines
                   them (CCE 3D supplement, MISC_3D_STATE_CNTL_REG). */
                if (alpha_en) {
                    float    dc[4], fs[4], fd[4];
                    uint32_t bsrc = ds->bsrc;
                    uint32_t bdst = ds->bdst;
                    uint32_t bfcn = ds->bfcn;

                    dc[0] = ((dst >> 16) & 0xff) / 255.0f;
                    dc[1] = ((dst >> 8) & 0xff) / 255.0f;
                    dc[2] = (dst & 0xff) / 255.0f;
                    dc[3] = (dst >> 24) / 255.0f;
                    if (bsrc == 0xb || bsrc == 0xc) { /* BOTHSRCALPHA, BOTHINVSRCALPHA */
                        float sa = (bsrc == 0xb) ? col[3] : 1.0f - col[3];

                        fs[0] = fs[1] = fs[2] = fs[3] = sa;
                        fd[0] = fd[1] = fd[2] = fd[3] = 1.0f - sa;
                    } else {
                        r3d_blend_factor(bsrc, col, dc, fs);
                        r3d_blend_factor(bdst, col, dc, fd);
                    }
                    /* ALPHA_COMB_FCN: 0 add and clamp, 1 add without clamp,
                       2 source minus destination with clamp, 3 the same
                       without clamp (RRG: MISC_3D_STATE_CNTL_REG, p. 3-257 /
                       PDF 275). Mesa r128 r128_state.c r128UpdateAlphaMode
                       uses 2 for GL_FUNC_SUBTRACT. "No clamp" is modeled as an
                       8-bit result that wraps; the guide does not say what
                       happens on overflow. All four channels are blended, the
                       alpha with its own factor component, since every factor
                       in the table has four components. */
                    for (int c = 0; c < 4; c++) {
                        float v = (bfcn & 2) ? col[c] * fs[c] - dc[c] * fd[c]
                                             : col[c] * fs[c] + dc[c] * fd[c];

                        if (bfcn & 1)
                            v = (float) (lrintf(v * 255.0f) & 0xff) / 255.0f;
                        else
                            v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                        col[c] = v;
                    }
                }

                out = ((uint32_t) (col[3] * 255.0f + 0.5f) << 24)
                    | ((uint32_t) (col[0] * 255.0f + 0.5f) << 16)
                    | ((uint32_t) (col[1] * 255.0f + 0.5f) << 8)
                    | (uint32_t) (col[2] * 255.0f + 0.5f);
                r3d_dst_write(dcell, dst_dt, out,
                              dither ? r3d_bayer4[py & 3][px & 3] : -1, wmask);
                if (rx0 < 0)
                    rx0 = px;
                rx1 = px;

                if (z_en && ds->z_wr) {
                    /* Write through zcell, the same cell the test read (staged
                       arena or local VRAM). zcell is always set here: z_en
                       also opened the block that resolved it, and a pixel
                       outside the staged range was skipped before this. */
                    if (zbpp == 2)
                        *(uint16_t *) zcell = (uint16_t) zi;
                    else {
                        uint32_t *zp = (uint32_t *) zcell;

                        *zp = (*zp & ~(zmax << zshift)) | (zi << zshift);
                    }
                }
            }
            if (jrow_ver)
                rage128_jit_verify_post(dev, &jt, ds, py, rx0, rx1);
            if (rx0 >= 0 && !cptr) /* a staged (AGP) target touches no VRAM page */
                r3d_mark_row_dirty(dev, rs->dst_offset, drow, cpb, c_tld, py,
                                   rx0, rx1, bpp);
        }
        /* Thread 0 always reports, so a triangle that gives thread 0 no rows
           is still counted; small triangles often do, and leaving them out
           would push the rows-per-triangle statistic up. */
        if (jfn && !jverify && jfn != rage128_gpu_count_span
            && (js_rows || thr_id == 0))
            rage128_jit_stat_rows(dev, thr_id == 0, js_rows, js_px, js_empty);
    }
}

void
rage128_3d_tri(rage128_t *dev, const rage128_raster_state_t *rs, int thr_id, int thr_mask,
               const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c,
               r128_jit_span_fn jfn)
{
    r3d_raster(dev, rs, thr_id, thr_mask, a, b, c, jfn, NULL);
}

/* Rasterize one line segment: the walk of r3d_line_setup through the
   shared per-pixel pipeline. Vertex c is passed as b, so a flat-shading
   choice of the last vertex (index 2) still lands on b; its weight is
   zero anyway. SETUP_CNTL SU_POLY_LINE (bit 18) marks a line that is not
   the last of a polyline (CCE 3D supplement, SETUP_CNTL), and the setup
   engine writes the matching DP_CNTL POLY_LINE bit (RRG: DP_CNTL,
   p. 3-166 / PDF 184). The model leaves out the last pixel of such a
   line so the shared vertex is drawn once, by the next segment; the
   guide does not spell this out. A segment whose endpoints are equal
   draws nothing; what the chip draws for a zero-length line is not
   documented. */
void
rage128_3d_line(rage128_t *dev, const rage128_raster_state_t *rs, int thr_id, int thr_mask,
                const r3d_vtx_t *a, const r3d_vtx_t *b, r128_jit_span_fn jfn)
{
    r3d_line_t ln;

    if (a->x == b->x && a->y == b->y)
        return;
    if (!r3d_line_setup(rs, a, b, &ln))
        return;
    r3d_raster(dev, rs, thr_id, thr_mask, a, b, b, jfn, &ln);
}

/* Rasterize one point as a one-pixel square made of two triangles. The
   attributes are copied to all four corners, so every gradient is zero.
   Mesa r128 also draws its points and lines as triangles (the note above
   r128RasterPrimitive in r128_tris.c). The chip's point footprint and
   size are not documented. */
void
r3d_point_tris(const r3d_vtx_t *p, r3d_vtx_t v[6])
{
    r3d_vtx_t p0 = *p, p1 = *p, p2 = *p, p3 = *p;

    p0.x -= 0.5f;
    p0.y -= 0.5f;
    p1.x += 0.5f;
    p1.y -= 0.5f;
    p2.x += 0.5f;
    p2.y += 0.5f;
    p3.x -= 0.5f;
    p3.y += 0.5f;
    v[0] = p0;
    v[1] = p1;
    v[2] = p2;
    v[3] = p0;
    v[4] = p2;
    v[5] = p3;
}

void
rage128_3d_point(rage128_t *dev, const rage128_raster_state_t *rs, int thr_id, int thr_mask,
                 const r3d_vtx_t *v, r128_jit_span_fn jfn)
{
    r3d_vtx_t q[6];

    r3d_point_tris(v, q);
    rage128_3d_tri(dev, rs, thr_id, thr_mask, &q[0], &q[1], &q[2], jfn);
    rage128_3d_tri(dev, rs, thr_id, thr_mask, &q[3], &q[4], &q[5], jfn);
}

/* Vertex size in dwords for a VC_FORMAT word: x, y and z are always
   present, and each flag adds its field (SDK: Table F-43, p. F-50 /
   PDF 340). The sizes agree with the format-to-stride table of the
   Windows 98 OpenGL driver, which lists all four specular combinations
   for each base format. */
static uint32_t
rage128_3d_vertex_dwords(uint32_t fmt)
{
    uint32_t n = 3;

    if (fmt & RAGE128_VCF_RHW)
        n += 1;
    if (fmt & RAGE128_VCF_DIFFUSE_BGR)
        n += 3; /* blue, green, red floats */
    if (fmt & RAGE128_VCF_DIFFUSE_A)
        n += 1; /* alpha float */
    if (fmt & RAGE128_VCF_DIFFUSE_ARGB)
        n += 1;
    if (fmt & RAGE128_VCF_SPEC_BGR)
        n += 3; /* blue, green, red floats (driver table: 0x016 = 0x006 + 3) */
    if (fmt & RAGE128_VCF_SPEC_F)
        n += 1; /* fog float, own dword (driver table: 0x026 = 0x006 + 1) */
    if (fmt & RAGE128_VCF_SPEC_FRGB)
        n += 1; /* packed specular and fog dword, as Mesa r128 sends it */
    if (fmt & RAGE128_VCF_S_T)
        n += 2;
    if (fmt & RAGE128_VCF_S2_T2)
        n += 2;
    if (fmt & RAGE128_VCF_RHW2)
        n += 1; /* 1/w of the second coordinate set */
    return n;
}

/* Fetch one vertex (stride dwords) from the vertex buffer at base +
   idx * stride dwords, through the GART, and decode it by VC_FORMAT.
   Returns 0 if any read fails, and the caller then abandons the draw.
   The widest format in the OpenGL driver's stride table is 17 dwords
   (0x3b7: xyz, rhw, float diffuse, float specular, fog, two coordinate
   sets, rhw2); the scratch buffer holds 20. */
static int
r3d_fetch_vertex(rage128_t *dev, uint32_t base, uint32_t stride,
                 uint32_t idx, uint32_t fmt, r3d_vtx_t *out)
{
    uint32_t vdw[20];
    uint32_t j;

    if (stride == 0 || stride > 20)
        return 0;
    for (j = 0; j < stride; j++) {
        if (!rage128_pm4_bus_read(dev, base + (idx * stride + j) * 4, &vdw[j]))
            return 0;
    }
    r3d_decode_vertex(fmt, vdw, out);
    return 1;
}

/* Start a new draw generation in the indexed-walk vertex cache. When the
   32-bit generation counter wraps, every entry is cleared, since an old
   tag could equal a reused value. */
static void
r3d_vtx_cache_begin(rage128_t *dev)
{
    struct rage128_vtx_cache *vc = &dev->vtx_cache;

    if (++vc->cur == 0) {
        if (vc->cap)
            memset(vc->gen, 0, vc->cap * sizeof(*vc->gen));
        vc->cur = 1;
    }
}

/* r3d_fetch_vertex for the indexed walk: repeat references to a source
   index are served from the per-draw cache, so each distinct vertex pays
   for the per-dword GART reads and the VC_FORMAT decode once per draw.
   base, stride and fmt are fixed within a draw, so the index alone is
   the key. A failed fetch is not cached, so a failure abandons the draw
   exactly as without the cache; if memory runs out the fetch goes
   uncached. Submit thread only. */
static int
r3d_fetch_vertex_cached(rage128_t *dev, uint32_t base, uint32_t stride,
                        uint32_t idx, uint32_t fmt, r3d_vtx_t *out)
{
    struct rage128_vtx_cache *vc = &dev->vtx_cache;

    if (idx >= vc->cap) { /* indices are 16-bit, so cap stops at 65536 */
        uint32_t   ncap = vc->cap ? vc->cap : 1024;
        r3d_vtx_t *nv;
        uint32_t  *ng;

        while (ncap <= idx)
            ncap <<= 1;
        nv = (r3d_vtx_t *) realloc(vc->v, ncap * sizeof(*nv));
        if (nv)
            vc->v = nv;
        ng = (uint32_t *) realloc(vc->gen, ncap * sizeof(*ng));
        if (ng)
            vc->gen = ng;
        if (!nv || !ng)
            return r3d_fetch_vertex(dev, base, stride, idx, fmt, out);
        memset(vc->gen + vc->cap, 0, (ncap - vc->cap) * sizeof(*ng));
        vc->cap = ncap;
    }
    if (vc->gen[idx] == vc->cur) {
        *out = vc->v[idx];
        return 1;
    }
    if (!r3d_fetch_vertex(dev, base, stride, idx, fmt, out))
        return 0;
    vc->v[idx]   = *out;
    vc->gen[idx] = vc->cur;
    return 1;
}

/* Make room in the texture staging arena for `total` more bytes, for the
   draw about to be staged. The arena grows only at a batch boundary: if
   the bytes do not fit, the pending batch is flushed first, and only
   then is the arena reallocated. (In parallel mode the flush joins the
   workers and sets used back to zero; serial CPU mode already reset it in
   rage128_raster_state_capture; the GPU lane allocates upward across
   draws, so a wrap retires its queued segments here before the offsets
   are reused.) So a realloc never moves the arena while a worker or a
   segment holds a pointer into it, and the reset never overwrites a
   level of the current draw, because this runs before any of them is
   staged. Submit thread only. */
static void
r3d_stage_reserve(rage128_t *dev, uint32_t total)
{
    struct rage128_tex_stage *ts = &dev->tex_stage;

    if (ts->used + total <= ts->cap)
        return;

    if (ts->used)
        ts->n_rec_wrap++;      /* forced by a full arena: count it as a wrap */
    rage128_raster_flush(dev); /* parallel: render and recycle; serial: no-op */

    /* GPU lane: the flush above reclaims only an idle arena. When the
       arena really wraps, the queued segments must retire before the
       offsets are reused; this is the one wait the lane pays per pass
       through the arena. */
    if (dev->gpu && ts->used && ts->used + total > ts->cap) {
        rage128_gpu_stage_quiesce(dev, 0);
        ts->used      = 0;
        ts->ent_count = 0;
        ts->gen++;
        r128_texcache_lock(ts);
        ts->tgen++; /* the arena's bytes are gone, and the cached entries with them */
        ts->src_lo = 0;
        ts->src_hi = 0;
        r128_texcache_unlock(ts);
    }

    if (ts->used + total > ts->cap) {
        uint32_t ncap = ts->cap ? ts->cap : (1u << 20);
        uint8_t *na;
        int      gr;

        /* An arena shared with the GPU backend must stay importable and
           no segment may hold it across the move, so the GPU side does
           the regrow (drain, import again, rebind). */
        gr = rage128_gpu_stage_grow(dev, ts->used + total);
        if (gr >= 0) {
            if (gr > 0)
                ts->n_grow++;
            return; /* grown, or out of memory: r3d_stage_level then finds no room */
        }

        while (ncap < ts->used + total)
            ncap <<= 1;
        na = (uint8_t *) realloc(ts->arena, ncap);
        if (!na)
            return; /* out of memory: r3d_stage_level finds no room */
        ts->arena = na;
        ts->cap   = ncap;
        ts->n_grow++;
    }
}

/* Stage the mip level at card address vm, len bytes, and return its byte
   offset in the arena, or R128_TEX_STAGE_NONE when there is no room (see
   r3d_stage_textures for what the caller then does). A failed read still
   stages, with the bus master-abort value. r3d_stage_reserve has already
   made room, so this never grows the arena. Submit thread only. */
static uint32_t
r3d_stage_level(rage128_t *dev, uint32_t key, uint32_t vm, uint32_t len,
                uint32_t tbpp, uint32_t tiled_pb, int cacheable)
{
    struct rage128_tex_stage *ts = &dev->tex_stage;
    uint32_t                  off;
    uint32_t                  tg;

    /* Take tgen before any bytes are copied. A CPU store that
       invalidates the cache during the copy bumps tgen past this value,
       so the new entry is invalid from the start and can never serve
       the image from before the store. */
    r128_texcache_lock(ts);
    tg = ts->tgen;
    r128_texcache_unlock(ts);

    /* key is the raw offset register value. Its tile-mode bits [31:30]
       tell a detiled staging from a linear one at the same address, so
       the two never share an entry; tiled_pb completes the key, because
       two shapes of the same byte length detile to different layouts.
       Per-draw entries are checked against gen and last one draw; cached
       entries are checked against tgen and last until a store changes
       their source bytes. */
    for (uint32_t i = 0; i < ts->ent_count; i++)
        if (ts->ent[i].gen == (ts->ent[i].pd ? ts->gen : tg)
            && ts->ent[i].vm_base == key
            && ts->ent[i].len == len && ts->ent[i].tbpp == tbpp
            && ts->ent[i].tiled_pb == tiled_pb) {
            if (!ts->ent[i].pd)
                ts->n_hit++;
            return ts->ent[i].arena_off;
        }

    /* Place levels on 16-byte boundaries: the GPU kernel reads the arena
       as 16-bit and 32-bit word arrays and needs aligned level bases
       (the CPU sampler is byte-addressed and does not care). */
    off = (ts->used + 15u) & ~15u;
    if (!ts->arena || off + len > ts->cap || off < ts->used)
        return R128_TEX_STAGE_NONE;

    if (tiled_pb) {
        /* Tiled level: gather its detiled linear image, so every lane
           samples it with its linear path. The local and AGP parts are
           resolved run by run; a failed AGP part gets the master-abort
           value, as in the linear path below. */
        if (!r128_card_copy_tiled(dev, vm, tiled_pb, 0, ts->arena + off, len, 0))
            memset(ts->arena + off, 0xff, len);
    } else if (!rage128_pm4_bus_read_block(dev, vm, ts->arena + off, len))
        /* A failed AGP read stages the PCI master-abort value (all
           ones) rather than reading local memory: a failed bus
           transaction does not turn into a frame buffer access. */
        memset(ts->arena + off, 0xff, len);
    ts->used = off + len;
    if (ts->used > ts->peak_used)
        ts->peak_used = ts->used;

    if (ts->ent_count < 256) {
        ts->ent[ts->ent_count].vm_base   = key;
        ts->ent[ts->ent_count].len       = len;
        ts->ent[ts->ent_count].tbpp      = tbpp;
        ts->ent[ts->ent_count].tiled_pb  = tiled_pb;
        ts->ent[ts->ent_count].arena_off = off;
        ts->ent[ts->ent_count].pd        = !cacheable;
        ts->ent[ts->ent_count].gen       = cacheable ? tg : ts->gen;
        ts->ent_count++;
        if (cacheable) {
            /* Widen the source range that CPU stores are checked
               against. A tiled gather reads whole tile rows, more than
               the linear image's size. Skipped when an invalidation
               landed during the copy (the entry is already invalid):
               widening the range for it would only delay reclaiming the
               idle arena. */
            uint32_t slen = tiled_pb
                ? r3d_level_bytes_tiled(tiled_pb, len / tiled_pb)
                : len;

            r128_texcache_lock(ts);
            if (ts->tgen == tg) {
                if (ts->src_hi == ts->src_lo) {
                    ts->src_lo = vm;
                    ts->src_hi = vm + slen;
                } else {
                    if (vm < ts->src_lo)
                        ts->src_lo = vm;
                    if (vm + slen > ts->src_hi)
                        ts->src_hi = vm + slen;
                }
            }
            r128_texcache_unlock(ts);
        }
    }
    return off;
}

/* Byte length of one mip level as it sits in card memory: the linear
   size, or whole tile rows when the level's offset register selects a
   tile mode the transform covers. r3d_stage_slot applies the same rule,
   so the staged copy and the fetch always agree on the layout. */
static uint32_t
r3d_tex_level_len(uint32_t cntl, uint32_t raw_off, uint32_t lw, uint32_t lh)
{
    uint32_t dt = (cntl >> 16) & 0xf;

    if (raw_off >> 30) {
        uint32_t pb = r3d_level_pitch_b(dt, lw);

        if (r128_tiled_ok(1u, pb))
            return r3d_level_bytes_tiled(pb, lh);
    }
    return r3d_level_bytes(dt, R3D_S3TC_CLASS(cntl), lw, lh);
}

/* One stage's values for the staging passes: returns whether the stage
   is enabled, and gives its TEX_SIZE_PITCH_C half, its top slot clamped
   to 0..10, and its texture control register. */
static int
r3d_stage_slots(const rage128_raster_state_t *rs, int st, uint32_t *tsp,
                int *top, uint32_t *cntl)
{
    uint32_t tc = rs->t3d.tex_cntl;

    if (!(st ? (tc & R3D_TC_SEC_EN) : (tc & R3D_TC_TEX_EN)))
        return 0;
    *tsp = R3D_TSP_HALF(rs, st);
    *top = R3D_TOPSLOT(*tsp);
    if (*top < 0)
        *top = 0;
    if (*top > 10)
        *top = 10;
    *cntl = st ? rs->t3d.sec_tex_cntl : rs->t3d.prim_tex_cntl;
    return 1;
}

/* One mip level's staging plan. r3d_stage_level_plan returns whether the
   level is staged at all. A tiled level the transform covers is always
   staged, detiled, so every lane samples a linear image and the JIT and
   GPU lanes can keep the draw. A level in AGP memory is staged because
   the rasterizer cannot read the bus. Any other level is sampled in
   place in VRAM. */
typedef struct r3d_stage_lvl_t {
    uint32_t raw, vm, pb, llh, llen;
    int      tcov, is_agp;
} r3d_stage_lvl_t;

static int
r3d_stage_level_plan(const rage128_raster_state_t *rs, int st, uint32_t tsp,
                     uint32_t cntl, int s, r3d_stage_lvl_t *l)
{
    uint32_t llw;

    l->raw = st ? rs->t3d.sec_tex_offset[s] : rs->t3d.prim_tex_offset[s];
    l->vm  = l->raw & R3D_OFFSET_MASK;
    r3d_level_dims(tsp, s, &llw, &l->llh);
    l->pb     = r3d_level_pitch_b((cntl >> 16) & 0xf, llw);
    l->tcov   = (l->raw >> 30) != 0 && r128_tiled_ok(1u, l->pb);
    l->llen   = r3d_tex_level_len(cntl, l->raw, llw, l->llh);
    l->is_agp = r128_card_is_agp(l->vm)
        || (l->llen && r128_card_is_agp(l->vm + l->llen - 1u));
    return l->tcov || l->is_agp;
}

/* Stage the mip levels of the enabled stages that r3d_stage_level_plan
   selects (in AGP memory, or tiled), recording each slot's arena offset
   in rs; R128_TEX_STAGE_NONE means the level is read from VRAM. The
   draw's worst-case need is reserved in one call, so the arena grows
   only between draws. A card address is in AGP memory when bit 25 is
   set: the lower 32 MB of the 3D engine's address space is the frame
   buffer and the upper 32 MB is AGP memory (CCE 3D supplement,
   Z_OFFSET_C), and the drivers seen here set bit 25 on AGP textures.
   Submit thread only. */
static void
r3d_stage_textures(rage128_t *dev, rage128_raster_state_t *rs)
{
    uint32_t        total = 0;
    r3d_stage_lvl_t l;

    for (int st = 0; st < 2; st++) {
        uint32_t *stage = st ? rs->sec_stage_off : rs->prim_stage_off;
        uint32_t  tsp, cntl;
        int       top;

        for (int s = 0; s < 11; s++)
            stage[s] = R128_TEX_STAGE_NONE;
        if (!r3d_stage_slots(rs, st, &tsp, &top, &cntl))
            continue;
        for (int s = 0; s <= top; s++)
            if (r3d_stage_level_plan(rs, st, tsp, cntl, s, &l))
                total += (l.llen + 15u) & ~15u; /* r3d_stage_level aligns to 16 */
    }

    if (!total)
        return;

    r3d_stage_reserve(dev, total);

    for (int st = 0; st < 2; st++) {
        uint32_t *stage = st ? rs->sec_stage_off : rs->prim_stage_off;
        uint32_t  tsp, cntl, tbpp;
        int       top;

        if (!r3d_stage_slots(rs, st, &tsp, &top, &cntl))
            continue;
        tbpp = (((cntl >> 16) & 0xf) == 6) ? 4u : 2u;
        for (int s = 0; s <= top; s++) {
            if (!r3d_stage_level_plan(rs, st, tsp, cntl, s, &l))
                continue;
            /* A detiled staging holds the level's linear image. Only
               levels in local memory may stay cached across draws:
               every write to local card memory goes through a device
               path that can invalidate the cache, and writes to AGP
               memory do not. */
            stage[s] = r3d_stage_level(dev, l.raw, l.vm,
                                       l.tcov ? l.pb * l.llh : l.llen, tbpp,
                                       l.tcov ? l.pb : 0u,
                                       !l.is_agp && dev->gpu != NULL);
            if (stage[s] == R128_TEX_STAGE_NONE && l.is_agp)
                rs->stage_dead = 1; /* no room: an AGP level must never be
                                       read from local VRAM, so the draw is
                                       dropped (a local tiled level that
                                       fails to stage is sampled through
                                       the transform instead) */
        }
    }
}

/* Write a staged Z or color buffer (in AGP memory) back to guest memory;
   thin wrappers over the span-stage code. Submit thread only, and only
   at the two points where the arenas are known to be idle: the flush
   barrier (all workers joined) and the serial per-draw recycle in
   rage128_raster_state_capture. Calling one twice is harmless. */
void
rage128_z_stage_writeback(rage128_t *dev)
{
    r128_span_stage_writeback(dev, &dev->z_stage);
}

void
rage128_c_stage_writeback(rage128_t *dev)
{
    r128_span_stage_writeback(dev, &dev->c_stage);
}

/* Byte extent of a read-modify-write surface (Z or color), bounded by
   the scissor. It must cover every address a worker can compute,
   base + py * rowpx * bpp + px * bpp with px and py up to the inclusive
   bottom-right scissor: (sy1 * rowpx + sx1 + 1) cells, rounded up to a
   dword. Neither surface has a height or size register, so the scissor
   is the only bound a draw gives. The extent is also clamped to the
   bytes left in the 32 MB half (local memory or AGP) the base lies in
   (CCE 3D supplement, Z_OFFSET_C); AGP_APER_OFFSET reads 0x2000000,
   the start of the AGP half (RRG: AGP_APER_OFFSET, p. 3-187 / PDF 205).
   The clamp must stop at that boundary, not at the 64 MB top:
   with a wide-open scissor (8191, 8191) a local surface's extent would
   otherwise cross into the AGP half, the surface would be staged as
   AGP, and the write-back of its tail would land on the guest's ring
   buffer and indirect buffers. */
static uint32_t
r3d_stage_extent(const rage128_raster_state_t *rs,
                 uint32_t vm, uint32_t rowpx, uint32_t bpp, int tiled)
{
    /* signed bounds; a negative one draws nothing, and row 0 covers it */
    int      sx1s = rage128_sx14(rs->sc_bottom_right);
    int      sy1s = rage128_sx14(rs->sc_bottom_right >> 16);
    uint32_t sx1  = sx1s < 0 ? 0 : (uint32_t) sx1s;
    uint32_t sy1  = sy1s < 0 ? 0 : (uint32_t) sy1s;
    /* Tiled rows are spread across whole tile rows, so the extent is
       every tile row through sy1, which covers every transformed
       address. */
    uint64_t extent = tiled
        ? r128_tile_rows_bytes64(sy1, (uint64_t) rowpx * bpp)
        : ((uint64_t) sy1 * rowpx + sx1 + 1u) * bpp;
    uint64_t room   = R128_CARD_AGP_HALF - (vm & (R128_CARD_AGP_HALF - 1u));

    extent = (extent + 3u) & ~3ull;
    if (extent > room)
        extent = room;
    return (uint32_t) extent;
}

/* Per-draw plan for one read-modify-write surface (Z or color): where it
   is, how it is addressed, and whether it must be staged. It has no side
   effects, because the batch guard below needs the same answer before
   any of this draw's staging runs. For these surfaces, being in AGP
   memory (bit 25) is the only reason to stage: the raster workers cannot
   reach the bus, and svga.vram[addr & vram_mask] would wrap an AGP
   address onto local VRAM. A failed stage drops the draw (stage_dead),
   because a failed AGP transaction must never turn into a frame buffer
   access.

   Tiling alone does not stage a surface. A tiled surface in local
   memory is addressed in place through the tile transform: by the
   interpreter per access, and by the JIT and GPU lanes walking each row
   in tile-column pieces. A linear copy would be a second live copy of
   VRAM that every other engine would have to keep coherent, which costs
   far more than the addressing. A tiled surface in AGP memory is still
   staged, and its copy is made detiled. (Texture levels follow a
   different rule; see r3d_stage_level_plan.) */
typedef struct r3d_surf_plan_t {
    uint32_t vm;     /* surface base in card space                       */
    uint32_t rowb;   /* row bytes: the transform's pitch, tiled only     */
    uint32_t extent; /* bytes this draw's scissor reaches (r3d_stage_extent) */
    int      tiled;
    int      agp;
    int      stage; /* = agp: only AGP-resident surfaces are staged     */
} r3d_surf_plan_t;

/* Z cell size and Z row pitch in pixels, decoded for the staging plan
   and again for the draw state (derived after the plan). Z_PIX_WIDTH
   picks 16, 24 or 32 bits (CCE 3D supplement, Z_STEN_CNTL_C); the 24-bit
   form also uses a 32-bit cell. Z_PITCH is the pitch in units of 8
   pixels (CCE 3D supplement, Z_PITCH_C). Bit 0 of it is ignored, which
   makes Z rows a multiple of 16 pixels. That is not documented: the
   Windows 98 Direct3D driver (version 4.12) writes 81 for a Z surface it
   allocates, clears and everywhere else treats as 640 pixels wide, and
   honoring bit 0 would walk 648-pixel rows off the end of it. */
static inline uint32_t
r3d_z_bpp(const rage128_raster_state_t *rs)
{
    return ((rs->t3d.z_sten_cntl >> 1) & 3) ? 4u : 2u;
}

static inline uint32_t
r3d_z_rowpx(const rage128_raster_state_t *rs)
{
    return (rs->t3d.z_pitch & 0xfffe) * 8u;
}

static void
r3d_plan_zbuffer(const rage128_raster_state_t *rs, r3d_surf_plan_t *p)
{
    uint32_t tc    = rs->t3d.tex_cntl;
    uint32_t zbpp  = r3d_z_bpp(rs);
    uint32_t rowpx = r3d_z_rowpx(rs);

    memset(p, 0, sizeof(*p));
    /* Stencil lives in the Z cell, so STENCIL_EN needs the surface even
       with Z off. With both off the surface is not touched and the plan
       stays empty. */
    if (!(tc & (R3D_TC_Z_EN | R3D_TC_STEN_EN)))
        return;
    p->vm     = rs->t3d.z_offset & R3D_OFFSET_MASK;
    p->rowb   = rowpx * zbpp;
    p->tiled  = r128_tiled_ok((rs->t3d.z_pitch >> 16) & 1u, p->rowb);
    p->extent = r3d_stage_extent(rs, p->vm, rowpx, zbpp, p->tiled);
    /* The extent is clamped to the base's half, so both ends always
       agree; testing the last byte too only guards against that clamp
       breaking. */
    p->agp = r128_card_is_agp(p->vm)
        || r128_card_is_agp(p->vm + p->extent - 1u);
    p->stage = p->agp;
}

/* The same plan for the color target. Without it, a dst_offset in AGP
   memory (bit 25) would wrap through vram_mask onto local VRAM and draw
   over the frame buffer. */
static void
r3d_plan_cbuffer(const rage128_raster_state_t *rs, r3d_surf_plan_t *p)
{
    memset(p, 0, sizeof(*p));
    p->vm     = rs->dst_offset;
    p->rowb   = rs->dst_pitch * rs->dst_bpp;
    p->tiled  = r128_tiled_ok(rs->dst_tiled, p->rowb);
    p->extent = r3d_stage_extent(rs, p->vm, rs->dst_pitch, rs->dst_bpp,
                                 p->tiled);
    p->agp    = r128_card_is_agp(p->vm)
        || r128_card_is_agp(p->vm + p->extent - 1u);
    p->stage = p->agp;
}

/* Does this plan need a copy that the pending batch would make wrong?
   An active stage of a different surface (or of the same surface in the
   other addressing mode) still backs draws already submitted; a new copy
   would read guest memory that pending unstaged draws have not written
   yet. Either way the batch must be rendered first. */
static int
r3d_stage_restage(const struct rage128_span_stage *st,
                  const r3d_surf_plan_t *p, int pending)
{
    if (!p->stage)
        return 0;
    if (st->active)
        return st->vm != p->vm || (st->tiled != 0) != (p->tiled != 0);
    return pending;
}

/* One staged surface of each kind per batch: copied in once, written
   back once at the flush barrier, and reused in between without reading
   guest memory again (a new read would overwrite what the batch has
   already drawn). The flush that ends the old batch must come before
   this draw stages its textures, because it recycles the texture arena
   and would otherwise discard the levels this draw had just staged.
   Submit thread only. */
static void
r3d_stage_guard(rage128_t *dev, const r3d_surf_plan_t *zp,
                const r3d_surf_plan_t *cp)
{
    int pending = atomic_load(&dev->cce_batch_pending);

    if (r3d_stage_restage(&dev->z_stage, zp, pending)
        || r3d_stage_restage(&dev->c_stage, cp, pending))
        rage128_raster_flush(dev);
}

static int
r3d_stage_surface(rage128_t *dev, rage128_raster_state_t *rs,
                  struct rage128_span_stage *st, const r3d_surf_plan_t *p,
                  int *detiled)
{
    int ok;

    *detiled = 0;
    if (!p->stage)
        return 0;
    /* A 3D read-modify-write range never crosses the 32 MB local/AGP
       boundary, because r3d_stage_extent stops at the base's half. A
       plan that crosses means that clamp broke, and the write-back of
       the tail would land on the guest's ring and indirect buffers at
       AGP offset 0, so the stage is refused. (2D windows may still be
       split at the boundary per address: their size comes from the
       drawing's geometry, not from the scissor.) */
    if (r128_card_is_agp(p->vm) != r128_card_is_agp(p->vm + p->extent - 1u)) {
        pclog("[r128 3d] staged span crosses the local/AGP boundary:"
              " vm=%08x extent=%x -- refused\n",
              p->vm, p->extent);
        rs->stage_dead = 1;
        return 0;
    }
    ok = p->tiled
        ? r128_span_stage_acquire_tiled(dev, st, p->vm, p->rowb, p->extent)
        : r128_span_stage_acquire(dev, st, p->vm, p->extent);
    if (!ok) {
        if (p->agp)
            rs->stage_dead = 1; /* bus read failed or no memory: never use local VRAM */
        return 0;
    }
    *detiled = p->tiled;
    return 1;
}

/* Decode one stage's sampler constants once per draw; the per-triangle
   descriptor copies them and adds the slot cache. */
static void
r3d_stage_hdr_init(const rage128_raster_state_t *rs, int st, r3d_stage_hdr_t *h)
{
    uint32_t cntl = st ? rs->t3d.sec_tex_cntl : rs->t3d.prim_tex_cntl;
    uint32_t mag  = (cntl >> 4) & 7;

    h->tsp     = R3D_TSP_HALF(rs, st);
    h->clamp_s = (cntl >> 8) & 3;
    h->clamp_t = (cntl >> 11) & 3;
    h->dt      = (cntl >> 16) & 0xf;
    h->s3tc    = R3D_S3TC_CLASS(cntl);
    /* TEX_MAP_AEN clear assumes texture alpha is 0xff for every
       alpha-bearing datatype (Registers for CCE 3D Packets,
       SCALE_3D_CNTL). Formats without alpha already decode opaque;
       the mask applies to fetched texels before filtering, leaving
       the separately supplied border color unchanged. The Windows 98
       Direct3D driver sets the bit for every draw and clears it only
       when each bound texture's DirectDraw pixel format lacks an alpha
       channel, at any bit depth, so its 16 bpp ARGB1555 and ARGB4444
       textures keep their texel alpha (RE: ati3draa.dll @b00d2dd0 for
       the per-draw write; RE: ati3draa.dll @b00fb890 for the
       per-texture alpha flag it tests). Mesa r128 sets it always
       (R128_TEX_MAP_ALPHA_IN_TEXTURE in r128DDInitState) and keeps
       texel alpha out of the combine for GL_RGB textures. */
    h->aone = (h->dt == 0 || h->dt == 3 || h->dt == 6 || h->dt == 9
               || h->dt == 14 || h->dt == 15)
        && !(rs->t3d.scale_3d_cntl & (1u << 30));
    h->border = st ? rs->t3d.sec_tex_border_color
                   : rs->t3d.prim_tex_border_color;
    /* YUV datatypes store the border as AYUV (SDK: Texture Mapping,
       p. 6-41 / PDF 153). Decode it once into the sampler's ARGB8888
       color space, preserving alpha and using the modeled byte order
       of datatype 14 so border and in-range texels agree. */
    if (h->dt == 11 || h->dt == 12 || h->dt == 14)
        h->border = r3d_yuv_to_argb(h->border >> 24,
                                    (int) ((h->border >> 16) & 0xff),
                                    (int) ((h->border >> 8) & 0xff),
                                    (int) (h->border & 0xff));
    h->minb = (cntl >> 1) & 7;
    /* PRIM_MAG_BLEND_FCN [6:4] takes the same codes 0-5 as the MIN
       filter, but magnification always uses the largest map, so only
       bit 0 matters: 0, 2 and 4 nearest, 1, 3 and 5 bilinear (SDK: Table
       6-5, p. 6-40 / PDF 152). SEC_MAG_BLEND_FCN has a different code
       book: 0 nearest; 1, 2 and 3 bilinear (Registers for CCE 3D
       Packets, SEC_TEX_CNTL_C). Include secondary code 2 alongside the
       odd codes so every sampler receives a nearest-or-bilinear flag. */
    h->mag    = (mag & 1) || (st && mag == 2);
    h->mipdis = (cntl >> 7) & 1;
    h->top    = R3D_TOPSLOT(h->tsp);
    if (h->top < 0)
        h->top = 0;
    if (h->top > 10)
        h->top = 10;
}

/* A stage uses a per-pixel LOD only when it is mip-filtered (mip-mapping
   on and a MIN filter of 2 or more) or when the MIN and MAG texel
   filters differ (the sign of the LOD picks between them even on the
   largest map). Otherwise the derivatives and the log2 are skipped and
   has_lod = 0 takes the same largest-map path. In captured traffic,
   3DMark2000's fill-rate test sends every draw with mip-mapping off and
   matching bilinear MIN and MAG filters. */
static int
r3d_stage_need_lod(const r3d_stage_hdr_t *h)
{
    return (!h->mipdis && h->minb >= 2) || ((h->minb & 1) != (h->mag == 1));
}

/* Decode one stage's combine register (PRIM_TEXTURE_COMBINE_CNTL_C or
   SEC_TEX_COMBINE_CNTL_C): COMB_FCN [3:0], COLOR_FACTOR [7:4], the Pro's
   fifth function bit [8] (R128_COMB_FCN_MSB), INPUT_FACTOR [13:10],
   COMB_FCN_ALPHA [17:14], ALPHA_FACTOR [21:18] and INPUT_FACTOR_ALPHA
   [27:25]. */
static void
r3d_comb_crack(uint32_t reg, r3d_comb_desc_t *c)
{
    c->comb  = reg & 0xf;
    c->fmsb  = (reg >> 8) & 1;
    c->cfac  = (reg >> 4) & 0xf;
    c->ifac  = (reg >> 10) & 0xf;
    c->comba = (reg >> 14) & 0xf;
    c->afac  = (reg >> 18) & 0xf;
    c->ifaca = (reg >> 25) & 0x7;
}

/* Decode every register field the rasterizer uses into rs->d, once per
   draw. It must stay a pure function of the captured registers (no dev
   state, no pointers, padding zeroed by memset) so that the copy and
   memcmp in rb_intern_state, which merge equal states, stay valid. */
static void
r3d_draw_state_derive(rage128_raster_state_t *rs)
{
    rage128_draw_state_t *ds     = &rs->d;
    uint32_t              tc     = rs->t3d.tex_cntl;
    uint32_t              misc   = rs->t3d.misc_3d_state_cntl;
    uint32_t              zwidth = (rs->t3d.z_sten_cntl >> 1) & 3;
    uint32_t              colmode, fpucol;

    memset(ds, 0, sizeof(*ds));

    ds->dst_dt  = rs->dp_datatype & 0xf;
    ds->bpp     = r3d_dst_bpp(ds->dst_dt);
    ds->draw_ok = ds->dst_dt == 3 || ds->dst_dt == 4
        || ds->dst_dt == 6 || ds->dst_dt == 15;
    /* 3D pixel writes are masked by PLANE_3D_MASK_C, a bit-plane mask in
       framebuffer format (Mesa r128 r128_state.c r128UpdateMasks packs
       glColorMask per pixel depth, so all planes on is 0x0000ffff at
       16 bpp). It is applied to the packed pixel in r3d_dst_write, never
       to the ARGB value. Whether DP_WRITE_MASK also masks 3D writes is
       not documented (Mesa r128 and the DRM keep it all ones); the model
       uses PLANE_3D_MASK_C only. */
    ds->wmask = rs->t3d.plane_3d_mask;
    /* TEX_CNTL_C DITHER_EN (bit 8); dithering matters only on the 16 bpp
       targets, which lose bits. Under table dither (SCALE_3D_CNTL
       SCALE_DITHER, bit 1, set), DITHER_INIT (bit 3) turns dithering off
       for alpha-blended pixels (SDK: Setting 3D Render States, p. 6-53 /
       PDF 165; CCE 3D supplement, SCALE_3D_CNTL: "in two-dimensional
       dither, setting this bit indicates that dither should be disabled
       during alpha blending operations"). Blending is a per-draw enable
       (TEX_CNTL_C ALPHA_EN, bit 9), so the draw's dither flag goes off
       as a whole. Mesa r128 r128_state.c r128DDInitState selects table
       dither with this bit set, so every blended Mesa draw at 16 bpp
       takes this path; the Windows drivers leave the bit clear. */
    ds->dither = ((tc >> 8) & 1) && ds->dst_dt != 6
        && !((rs->t3d.scale_3d_cntl & 0x0a) == 0x0a && (tc & R3D_TC_ALPHA_EN));
    /* DP_BRUSH_DATATYPE 9, the 32x32 mono pattern "for OPEN GL support"
       (RRG: DP_DATATYPE, p. 3-169 / PDF 187), is the polygon stipple:
       Mesa r128 r128_state.c r128DDEnable selects it for
       GL_POLYGON_STIPPLE triangles, and linux r128 DRM r128_state.c
       r128_cce_dispatch_stipple loads the pattern into BRUSH_DATA0
       through BRUSH_DATA31. All raster paths test the captured pattern
       at destination coordinates before depth and stencil. */
    ds->stip_en = ((rs->dp_datatype >> 8) & 0xf) == 9;
    ds->aux_on  = (rs->aux_sc_cntl & RAGE128_AUX_SC_ENB_MASK) != 0;
    /* The auxiliary rectangles go into the JIT block key decoded. Only
       enabled ones are copied, so old coordinates in a disabled one
       cannot make two equal states look different. */
    if (ds->aux_on) {
        for (int i = 0; i < 3; i++) {
            if (!(rs->aux_sc_cntl & (1u << (i * 2))))
                continue;
            ds->aux_cntl |= ((rs->aux_sc_cntl >> (i * 2)) & 3u) << (i * 2);
            ds->aux_x0[i] = rage128_sx14(rs->aux_sc_rect[i][0]);
            ds->aux_x1[i] = rage128_sx14(rs->aux_sc_rect[i][1]);
            ds->aux_y0[i] = rage128_sx14(rs->aux_sc_rect[i][2]);
            ds->aux_y1[i] = rage128_sx14(rs->aux_sc_rect[i][3]);
        }
    }
    /* The scissor bounds are signed 14-bit fields (RRG: SC_LEFT, p. 3-155
       / PDF 173; RRG: SC_TOP_LEFT, p. 3-161 / PDF 179). A negative right
       or bottom clips everything. A negative left or top clips nothing
       on that side; the lower bound stops at 0 because the guide does not
       say what happens to a pixel at a negative coordinate (modeled: none
       is drawn), and every lane addresses from x, y >= 0. */
    ds->sx0 = rage128_sx14(rs->sc_top_left);
    ds->sy0 = rage128_sx14(rs->sc_top_left >> 16);
    ds->sx1 = rage128_sx14(rs->sc_bottom_right);
    ds->sy1 = rage128_sx14(rs->sc_bottom_right >> 16);
    if (ds->sx0 < 0)
        ds->sx0 = 0;
    if (ds->sy0 < 0)
        ds->sy0 = 0;

    ds->sub  = (rs->t3d.setup_cntl & (1 << 19)) ? 16 : 4;
    ds->subf = (float) ds->sub;
    ds->rnd  = (rs->t3d.fpu_setup >> 15) & 1; /* FPU_ROUND_EN */
    ds->slim = (ds->sub == 16) ? (1 << 15) : (1 << 14);
    /* WINDOW_XY_OFFSET: y in [15:0] and x in [31:16], signed, in the
       subpixel format SUB_PIX_AMNT selects (RRG: WINDOW_XY_OFFSET,
       p. 3-249 / PDF 267). It is added in subpixel units after the
       vertex snap. */
    if (rs->t3d.window_xy_offset) {
        ds->woyi = (int16_t) (rs->t3d.window_xy_offset & 0xffff);
        ds->woxi = (int16_t) (rs->t3d.window_xy_offset >> 16);
    }

    ds->z_en = (tc & R3D_TC_Z_EN) != 0;
    ds->z_wr = (tc & R3D_TC_Z_WR) != 0;
    ds->zfn  = (rs->t3d.z_sten_cntl >> 4) & 7;
    ds->zbpp = r3d_z_bpp(rs);
    /* Where the depth bits sit in the Z cell. 16-bit Z is depth [15:0].
       24-bit Z is depth [23:0] with stencil in [31:24], as the SDK
       describes the stencil buffer (SDK: Setting 3D Render States,
       p. 6-56 / PDF 168) and as Mesa r128 r128_state.c r128DDClearDepth
       clears it. 32-bit Z is modeled as 24 bits of depth in [31:8] with
       stencil in [7:0]: in captured Windows 98 Direct3D traffic the
       driver clears a 32-bit Z buffer to 0xffffff00, not 0xffffffff.
       zmax is the depth range (24 bits for both wide forms) and zshift
       its place in the cell, so the compare skips the stencil byte. */
    ds->zmax   = zwidth == 0 ? 0xffff : 0xffffff;
    ds->zshift = zwidth == 2 ? 8 : 0;
    ds->zrowpx = r3d_z_rowpx(rs);
    /* Z_PITCH_C Z_TILE (bit 16; CCE 3D supplement, Z_PITCH_C; Mesa r128
       r128DDInitState sets it) and the destination tile bit select tiled
       addressing of the Z and color surfaces. A surface staged detiled
       is a linear image in its arena, so the flag is dropped and every
       lane uses linear row addresses; only an unstaged tiled surface
       goes through the transform (per pixel in the interpreter, by the
       tile-column walk in the JIT and GPU lanes). The staging decision
       was made before this decode. */
    ds->z_tiled = !rs->z_detiled
        && r128_tiled_ok((rs->t3d.z_pitch >> 16) & 1u,
                         ds->zrowpx * (uint32_t) ds->zbpp);
    ds->c_tiled = !rs->c_detiled
        && r128_tiled_ok(rs->dst_tiled,
                         rs->dst_pitch * (uint32_t) ds->bpp);
    /* Stencil is the byte the depth compare leaves out ([31:24] with
       24-bit Z, [7:0] with 32-bit Z). There is no stencil with 16-bit Z;
       Mesa r128 r128_screen.c r128CreateBuffer also uses software
       stencil unless the depth buffer is 24-bit. STENCIL_TEST [14:12],
       STEN_SFAIL_OP [18:16], STEN_ZPASS_OP [22:20] and STEN_ZFAIL_OP
       [26:24] (CCE 3D supplement, Z_STEN_CNTL_C); the reference and the
       two masks are in STEN_REF_MSK_C. */
    ds->sten_on  = (tc & R3D_TC_STEN_EN) && zwidth != 0;
    ds->sfn      = (rs->t3d.z_sten_cntl >> 12) & 7;
    ds->sfail_op = (rs->t3d.z_sten_cntl >> 16) & 7;
    ds->zpass_op = (rs->t3d.z_sten_cntl >> 20) & 7;
    ds->zfail_op = (rs->t3d.z_sten_cntl >> 24) & 7;
    ds->sref     = rs->t3d.sten_ref_mask & 0xff;
    ds->svmask   = (rs->t3d.sten_ref_mask >> 16) & 0xff;
    ds->swmask   = rs->t3d.sten_ref_mask >> 24;
    ds->sshift   = ds->zshift ? 0 : 24;

    /* Flat shading. Two stages can flatten the color, one after the
       other. First the vertex controller's FPU: PM4_VC_FPU_SETUP
       PM4_COLOR_FCN [6:5] (0 solid, 1 flat, 2 and 3 Gouraud), with
       FLAT_SHADE_VERTEX [14] picking the first vertex (Direct3D) or the
       third (OpenGL) (SDK: Setting 3D Render States, pp. 6-52-6-53 /
       PDF 164-165). This is the only shading control Mesa r128
       r128DDShadeModel changes. Then the setup engine: SETUP_CNTL
       COLOR_FCN [5:3], where 1-3 use the color of vertex 1-3 (CCE 3D
       supplement, SETUP_CNTL), the control the Windows 98 Direct3D
       driver uses. The FPU stage acts first, so its choice wins when
       both flatten. COLOR_FCN 0, "solid shade using SOLID_COLOR", is
       treated as no flattening: in captured traffic the Windows 98
       OpenGL driver leaves SETUP_CNTL at 0x00080200 (COLOR_FCN 0) for
       every draw and sets its shading through PM4_COLOR_FCN alone.
       PM4_COLOR_FCN 0 is solid shading: "the primitive to be colored in
       the solid color set through the CONSTANT_COLOR_C register" (SDK:
       Setting 3D Render States, p. 6-53 / PDF 165), so every vertex
       takes that register's four components (cc below), alpha with the
       color; the setup engine's choice cannot change it. */
    colmode = (rs->t3d.setup_cntl >> 3) & 7;
    fpucol  = (rs->t3d.fpu_setup >> RAGE128_FPU_COLOR_SHIFT)
        & RAGE128_FPU_COLOR_MASK;
    ds->solid_on = fpucol == 0;
    ds->flat_on  = !ds->solid_on && (fpucol == 1 || (colmode >= 1 && colmode <= 3));
    if (fpucol == 1)
        ds->flat_src = (rs->t3d.fpu_setup & RAGE128_FPU_FLAT_VERTEX_OGL) ? 2 : 0;
    else
        ds->flat_src = (colmode == 2) ? 1 : (colmode == 3) ? 2
                                                           : 0;

    ds->tex_en = (tc & R3D_TC_TEX_EN) != 0;
    ds->sec_en = (tc & R3D_TC_SEC_EN) != 0;
    /* Two separate texture controls:
         premult  = SETUP_CNTL TEXTURE_ST_FORMAT (bit 9) clear. 0 means
                    "multiply S*W, T*W prior to interpolation": the stored
                    coordinates are raw and are multiplied by rhw to form
                    the s/w interpolant. 1 means they are "already
                    pre-multiplied by W" (CCE 3D supplement, SETUP_CNTL).
         do_persp = the divide by the interpolated rhw, turned off by
                    PRIM_TEX_CNTL_C PRIM_TEX_PERSPECTIVE_DIS (bit 14) (CCE
                    3D supplement, PRIM_TEX_CNTL_C); it changes nothing
                    when rhw is 1. The two must not be confused: in
                    captured Quake III Arena traffic the OpenGL driver
                    sends perspective triangles with bit 9 set, bit 14
                    clear and rhw not 1, which need the divide.
         sec_persp_diff = the secondary stage's own enable of that divide,
                    SEC_TEX_CNTL_C SEC_TEX_PERSPECTIVE_DIS (bit 14,
                    "applies only to texture 2"), differs from the
                    primary's; the stage divides when
                    do_persp ^ sec_persp_diff.
         sel_w    = SEC_TEX_CNTL_C SEC_SRC_SEL_W (bit 15): "0 = use
                    primary W for secondary texture parameters, 1 = use
                    secondary W" (CCE 3D supplement, SEC_TEX_CNTL_C; SDK:
                    Texture Mapping, p. 6-47 / PDF 159), the secondary W
                    being the vertex's rhw2 (SDK: Table F-45, p. F-53 /
                    PDF 343). The secondary stage divides by that W and,
                    under premult, its second coordinate set is multiplied
                    by it. Both are zero when the stage is off, so equal
                    states stay equal. Mesa r128 and the Windows drivers
                    seen here give both stages the same perspective
                    setting and leave the W select clear. */
    ds->premult  = !(rs->t3d.setup_cntl & (1 << 9));
    ds->do_persp = !(rs->t3d.prim_tex_cntl & (1 << 14));
    ds->sec_persp_diff = ds->sec_en
        && ((rs->t3d.sec_tex_cntl >> 14) & 1) != ((rs->t3d.prim_tex_cntl >> 14) & 1);
    ds->sel_w = ds->sec_en && ((rs->t3d.sec_tex_cntl >> 15) & 1);
    /* TEX_CNTL_C LOD_BIAS [31:24] is "an 8 bit signed fraction that
       affects the log of the stride": 0 switches maps when the stride
       passes 1.0, -128 at 0.5, and 127 just before 2.0, for both stages
       (CCE 3D supplement, TEX_CNTL_C). So the model subtracts b / 128
       from the LOD, a range of one level either way. The drivers seen
       here all bias toward the finer map: Mesa r128 r128_tex.c r128TexEnv
       writes 0x3f for a GL bias of 0, and the Windows 98 Direct3D driver
       writes 0x0f normally and 0x7f for nearest-map filtering (RE:
       ati3draa.dll @b00d8148). Whether the bias combines with the model's
       LOD measure the way it does with the chip's is not known. */
    ds->lod_bias = -(float) (int8_t) ((tc >> 24) & 0xff) / 128.0f;
    /* Texture lighting combines the output of the texture stages with
       the iterated vertex color and alpha (SDK: Texture Mapping,
       p. 6-42 / PDF 154; SDK: Table 6-15, p. 6-46 / PDF 158; SDK: Table
       6-16, p. 6-47 / PDF 159). It runs as a third pass of
       r3d_tex_combine with the stage output as its texel: the function
       is TEX_LIGHT_FN [17:14], the color factor the texel (4), the input
       the interpolated color (4), the alpha function ALPHA_LIGHT_FN
       [20:18], the alpha factor the texel alpha (6) and the alpha input
       the interpolated alpha (2). Bit 6, reserved in the supplement, is
       taken as the bit-8 extension of the stage combines; the Windows 98
       Direct3D driver uses it for a SUBTRACT in this pass on a Pro. The
       function codes are the SDK's; the xf86-video-r128 header numbers
       them differently (R128_LIGHT_MODULATE is 2, R128_LIGHT_ADD 3, and
       so on; only 0, disable, agrees). The supplement says the functions
       are ignored when
       TEX_EN is 0; the model runs the pass only when a texture stage is
       enabled. */
    ds->lcomb.comb  = (tc >> 14) & 0xf;
    ds->lcomb.fmsb  = (tc >> 6) & 1;
    ds->lcomb.cfac  = 4;
    ds->lcomb.ifac  = 4;
    ds->lcomb.comba = (tc >> 18) & 0x7;
    ds->lcomb.afac  = 6;
    ds->lcomb.ifaca = 2;
    ds->light_on    = (ds->tex_en || ds->sec_en)
        && (ds->lcomb.comb || ds->lcomb.fmsb || ds->lcomb.comba);
    if (!ds->light_on)
        memset(&ds->lcomb, 0, sizeof(ds->lcomb)); /* keeps equal states equal */
    ds->sec_sel = rs->t3d.sec_tex_cntl & 1;       /* SEC_SRC_SEL_ST */
    /* Cylindrical wrap enables per coordinate set: PRIM_TEX_WRAP_S and
       SEC_TEX_WRAP_S (bit 10), PRIM_TEX_WRAP_T and SEC_TEX_WRAP_T (bit 13)
       (CCE 3D supplement, PRIM_TEX_CNTL_C and SEC_TEX_CNTL_C). The primary
       stage always samples the first set; the secondary samples the
       second set only when SEC_SRC_SEL_ST is set, and otherwise its
       enables are ORed into the first set's. What the chip does when the
       two stages disagree about one set is not documented. */
    {
        uint32_t pc = rs->t3d.prim_tex_cntl;
        uint32_t sc = ds->sec_en ? rs->t3d.sec_tex_cntl : 0;
        uint32_t c1 = ds->sec_sel ? pc : (pc | sc);
        uint32_t c2 = ds->sec_sel ? sc : 0;

        ds->wrap_s0 = (c1 >> 10) & 1;
        ds->wrap_t0 = (c1 >> 13) & 1;
        ds->wrap_s1 = (c2 >> 10) & 1;
        ds->wrap_t1 = (c2 >> 13) & 1;
    }

    r3d_stage_hdr_init(rs, 0, &ds->sh[0]);
    r3d_stage_hdr_init(rs, 1, &ds->sh[1]);
    ds->texw0     = (float) (1u << (ds->sh[0].tsp & 0xf));
    ds->texh0     = (float) (1u << ((ds->sh[0].tsp >> 8) & 0xf));
    ds->texw1     = (float) (1u << (ds->sh[1].tsp & 0xf));
    ds->texh1     = (float) (1u << ((ds->sh[1].tsp >> 8) & 0xf));
    ds->need_lod  = r3d_stage_need_lod(&ds->sh[0]);
    ds->need_lod2 = (tc & R3D_TC_SEC_EN) && r3d_stage_need_lod(&ds->sh[1]);

    r3d_comb_crack(rs->t3d.prim_tex_combine_cntl, &ds->comb[0]);
    r3d_comb_crack(rs->t3d.sec_tex_combine_cntl, &ds->comb[1]);

    /* The chroma keys compare the primary stage's nearest texel before
       filtering; when both are off, r3d_tex_level need not fetch it.
       There are two separate keys. The first is MISC_3D_STATE_CNTL_REG
       CLR_CMP_FCN_3D with the key at 0x1a24 (CLR_CMP_CLR_3D) and the
       mask at 0x1a28 (CLR_CMP_MSK_3D), which the Windows 98 Direct3D
       driver uses. The second is TEX_CNTL_C TEX_CHROMA_KEY_EN (bit 12)
       with TEXTURE_CLR_CMP_CLR_C at 0x1ca4 and TEXTURE_CLR_CMP_MSK_C at
       0x1ca8 (CCE 3D supplement). A
       zero mask disables either key (see rage128_texstage_run). The keys
       hold raw texel bits, so key and mask are converted here into the
       ARGB8888 form the compare sees (r3d_ck_to_argb). CLR_CMP_FCN_3D 1,
       "True", is modeled as a compare that always matches, so every
       texel is discarded; it is done as code 3 with key 0 and mask 0 so
       the same compare code handles it. No driver seen here uses it. */
    ds->ckfn    = (misc >> 30) & 3;
    ds->ck3d_on = (ds->ckfn == 1 && ds->tex_en)
        || (ds->ckfn >= 2 && rs->t3d.clr_cmp_msk_3d != 0);
    ds->ckc_on  = ((tc >> 12) & 1) && rs->t3d.tex_clr_cmp_msk != 0;
    ds->need_ck = ds->ck3d_on || ds->ckc_on;
    if (ds->tex_en) {
        if (ds->ckfn == 1) {
            ds->ckfn     = 3;
            ds->ck3d_clr = 0;
            ds->ck3d_msk = 0;
        } else {
            ds->ck3d_clr = r3d_ck_to_argb(ds->sh[0].dt, rs->t3d.clr_cmp_clr_3d);
            ds->ck3d_msk = r3d_ck_to_argb(ds->sh[0].dt, rs->t3d.clr_cmp_msk_3d);
        }
        ds->ckc_clr = r3d_ck_to_argb(ds->sh[0].dt, rs->t3d.tex_clr_cmp_clr);
        ds->ckc_msk = r3d_ck_to_argb(ds->sh[0].dt, rs->t3d.tex_clr_cmp_msk);
    }

    /* CONSTANT_COLOR_C (0x1d34) as floats for the combine's constant
       operands: blue [7:0], green [15:8], red [23:16], alpha [31:24]
       (RRG: CONSTANT_COLOR_C, p. 3-260 / PDF 278). */
    ds->cc[0] = ((rs->t3d.constant_color >> 16) & 0xff) / 255.0f;
    ds->cc[1] = ((rs->t3d.constant_color >> 8) & 0xff) / 255.0f;
    ds->cc[2] = (rs->t3d.constant_color & 0xff) / 255.0f;
    ds->cc[3] = (rs->t3d.constant_color >> 24) / 255.0f;

    ds->spec_en      = (tc & R3D_TC_SPEC_EN) != 0;
    ds->fog_en       = (tc & R3D_TC_FOG_EN) != 0;
    ds->fog_table_en = (misc & (1u << 14)) != 0;
    if (ds->fog_en) {
        /* FOG_COLOR_C: blue [7:0], green [15:8], red [23:16] (CCE 3D
           supplement, FOG_COLOR_C). */
        ds->fogr = ((rs->t3d.fog_color >> 16) & 0xff) / 255.0f;
        ds->fogg = ((rs->t3d.fog_color >> 8) & 0xff) / 255.0f;
        ds->fogb = (rs->t3d.fog_color & 0xff) / 255.0f;
    }

    ds->atest_en  = (tc & R3D_TC_ATEST_EN) != 0;
    ds->atest_fn  = (misc >> 24) & 7;
    ds->atest_ref = misc & 0xff;
    ds->alpha_en  = (tc & R3D_TC_ALPHA_EN) != 0;
    ds->bsrc      = (misc >> 16) & 0xf;
    ds->bdst      = (misc >> 20) & 0xf;
    ds->bfcn      = (misc >> 12) & 3;
}

/* Capture the engine state a draw is submitted under, for the parallel
   rasterizer. Besides the register snapshot it stages what must be
   staged, decodes the draw state, and works out the card byte ranges
   each enabled texture stage can sample, so the scheduler can flush
   before a draw that reads pixels an earlier draw in the same batch
   wrote (render to texture). The ranges use the same addressing as
   r3d_texel. */
void
rage128_raster_state_capture(rage128_t *dev, rage128_raster_state_t *rs)
{
    uint32_t        tc;
    r3d_surf_plan_t zp, cp;

    /* Serial mode never flushes, so the staging arena is recycled per
       draw here (a serial draw is finished before the next capture).
       Parallel mode recycles it in rage128_raster_flush instead. */
    if (rage128_raster_nthreads(dev) <= 1) {
        /* GPU lane: queued segments may still sample staged texels, so
           the arena keeps allocating upward across draws instead of
           resetting, and the wait happens when the arena wraps
           (r3d_stage_reserve) or at an idle barrier, never per draw.
           The gen bump still ends every per-draw entry: no per-draw
           staged copy is reused by a later draw. */
        if (!dev->gpu) {
            /* A serial CPU draw is finished by the time it returns. */
            if (dev->tex_stage.used)
                dev->tex_stage.n_rec_draw++;
            dev->tex_stage.used      = 0;
            dev->tex_stage.ent_count = 0;
            r128_texcache_lock(&dev->tex_stage);
            dev->tex_stage.src_lo = 0;
            dev->tex_stage.src_hi = 0;
            r128_texcache_unlock(&dev->tex_stage);
        } else {
            /* Keep the valid cached entries; drop the previous draw's
               per-draw entries and any entry a tgen bump invalidated. */
            struct rage128_tex_stage *ts = &dev->tex_stage;
            uint32_t                  n  = 0;
            uint32_t                  tg;

            r128_texcache_lock(ts);
            tg = ts->tgen;
            r128_texcache_unlock(ts);
            for (uint32_t i = 0; i < ts->ent_count; i++)
                if (!ts->ent[i].pd && ts->ent[i].gen == tg)
                    ts->ent[n++] = ts->ent[i];
            ts->ent_count = n;
        }
        dev->tex_stage.gen++;
        /* The serial flush does nothing, so the previous serial draw's
           staged Z and color (already complete) are written back here,
           before this draw reads guest memory again; otherwise the new
           read would overwrite what the previous draw produced. The last
           serial draw is written back at the next barrier's
           rage128_raster_flush (a present, a 2D operation or a drain). */
        rage128_z_stage_writeback(dev);
        rage128_c_stage_writeback(dev);
    }

    rs->t3d        = dev->t3d;
    rs->dst_offset = dev->dst_offset;
    rs->dst_pitch  = dev->dst_pitch;
    /* One destination tile flag: DST_TILE of the packed DST_PITCH_OFFSET
       (bit 31) and of DST_PITCH (bit 16) both land in dst_pitch_reg bit
       16 (RRG: DST_PITCH_OFFSET, p. 3-137 / PDF 155; RRG: DST_PITCH,
       p. 3-137 / PDF 155). The DRM's tiled depth descriptor arrives that
       way, through DST_PITCH_OFFSET_C (linux r128 DRM r128_cce.c
       r128_do_init_cce sets R128_DST_TILE in depth_pitch_offset_c). */
    rs->dst_tiled       = (dev->dst_pitch_reg >> 16) & 1u;
    rs->dp_datatype     = dev->dp_datatype;
    rs->sc_top_left     = dev->sc_top_left;
    rs->sc_bottom_right = dev->sc_bottom_right;
    rs->aux_sc_cntl     = dev->aux_sc_cntl;
    memcpy(rs->aux_sc_rect, dev->aux_sc_rect, sizeof(rs->aux_sc_rect));
    rs->dst_bpp = (uint32_t) r3d_dst_bpp(rs->dp_datatype & 0xf);
    /* Polygon stipple: brush type 9 (the 32x32 mono pattern) captures
       all of BRUSH_DATA0 through BRUSH_DATA31. Any other brush leaves
       the rows zero, so old brush bytes cannot make two equal states
       look different to rb_intern_state. */
    if (((rs->dp_datatype >> 8) & 0xf) == 9)
        memcpy(rs->stipple, dev->brush_data, sizeof(rs->stipple));
    else
        memset(rs->stipple, 0, sizeof(rs->stipple));

    rs->stage_dead = 0;
    r3d_plan_zbuffer(rs, &zp);
    r3d_plan_cbuffer(rs, &cp);
    if (dev->gpu) {
        /* This draw renders into these ranges, so a cached staged level
           copied from them would silently go stale (the render-to-texture
           flush brings VRAM up to date, not the copy). */
        if (zp.extent)
            r128_texcache_dirty(dev, zp.vm, zp.extent);
        if (cp.extent)
            r128_texcache_dirty(dev, cp.vm, cp.extent);
    }
    r3d_stage_guard(dev, &zp, &cp); /* end the batch the old stages belong to */
    r3d_stage_textures(dev, rs);    /* mip levels in AGP memory, and tiled ones */

    rs->z_staged = r3d_stage_surface(dev, rs, &dev->z_stage, &zp,
                                     &rs->z_detiled);
    rs->c_staged = r3d_stage_surface(dev, rs, &dev->c_stage, &cp,
                                     &rs->c_detiled);

    r3d_draw_state_derive(rs); /* per-draw register decode                */

    /* The bytes each enabled stage can sample, for the render-to-texture
       checks of the accelerated lanes. A mip-filtered stage can reach
       every slot from top down to 0 (r3d_tex_sample), and the levels need
       not be next to each other. tex_lo / tex_hi is one interval over all
       of them, which may cover gaps between levels and so cost a needless
       flush; tex_rng holds the levels merged into separate pieces. A
       range that misses any level would miss a dependency and let the
       read race the write. This uses the decoded state, so it runs after
       r3d_draw_state_derive. */
    tc = rs->t3d.tex_cntl;
    for (int st = 0; st < 2; st++) {
        int en = st ? (tc & R3D_TC_SEC_EN) : (tc & R3D_TC_TEX_EN);

        /* Copy input on both the color and the alpha function passes the
           input through, so the stage's texel reaches no pixel and its
           bytes are not a dependency. The Windows 98 Direct3D driver draws
           untextured primitives this way, with the stage left at offset 0,
           which is often under the render target. A chroma key still
           discards on the texel, so with one the range is kept. */
        if (en && rs->d.comb[st].comb == 2 && rs->d.comb[st].comba == 2
            && !rs->d.ck3d_on && !rs->d.ckc_on)
            en = 0;

        rs->tex_lo[st] = rs->tex_hi[st] = 0;
        rs->tex_nrng[st]                = 0;
        memset(rs->tex_rng_lo[st], 0, sizeof(rs->tex_rng_lo[st]));
        memset(rs->tex_rng_hi[st], 0, sizeof(rs->tex_rng_hi[st]));
        if (en) {
            const r3d_stage_hdr_t *h    = &rs->d.sh[st];
            uint32_t               tsp  = R3D_TSP_HALF(rs, st);
            uint32_t               cntl = st ? rs->t3d.sec_tex_cntl
                                             : rs->t3d.prim_tex_cntl;
            int                    top  = h->top;
            int                    low;
            uint32_t               lo = 0xffffffffu, hi = 0;
            uint32_t               plo[11], phi[11];
            int                    np = 0;

            if (top < 0)
                top = 0;
            if (top > 10)
                top = 10;
            low = ((st ? rs->d.need_lod2 : rs->d.need_lod) && !h->mipdis
                   && h->minb >= 2)
                ? 0
                : top;
            for (int s = low; s <= top; s++) {
                uint32_t raw = st ? rs->t3d.sec_tex_offset[s]
                                  : rs->t3d.prim_tex_offset[s];
                uint32_t lw, lh, base, end;

                r3d_level_dims(tsp, s, &lw, &lh);
                /* Only an unstaged tiled level that the transform covers
                   keeps the draw off the JIT and GPU lanes (a level staged
                   detiled is linear on every lane, and uncovered formats
                   are sampled linearly everywhere). The range still
                   covers the whole tile rows the level occupies in card
                   memory. */
                if ((raw >> 30)
                    && r128_tiled_ok(1u, r3d_level_pitch_b((cntl >> 16) & 0xf, lw))
                    && (st ? rs->sec_stage_off[s]
                           : rs->prim_stage_off[s])
                        == R128_TEX_STAGE_NONE)
                    rs->d.tex_tiled = 1;
                base = raw & R3D_OFFSET_MASK;
                end  = base + r3d_tex_level_len(cntl, raw, lw, lh);
                if (end < base)
                    end = 0xffffffffu; /* wrapped: claim up to the top of the space */
                if (base < lo)
                    lo = base;
                if (end > hi)
                    hi = end;
                /* insertion-sort the level by base; merged below */
                {
                    int i = np++;

                    while (i > 0 && plo[i - 1] > base) {
                        plo[i] = plo[i - 1];
                        phi[i] = phi[i - 1];
                        i--;
                    }
                    plo[i] = base;
                    phi[i] = end;
                }
            }
            if (hi > lo) {
                uint32_t n = 0;

                rs->tex_lo[st] = lo;
                rs->tex_hi[st] = hi;
                /* Merge touching or overlapping levels into separate
                   pieces (a chain with gaps between levels keeps one
                   piece per level). */
                for (int i = 0; i < np; i++) {
                    if (phi[i] <= plo[i])
                        continue;
                    if (n && plo[i] <= rs->tex_rng_hi[st][n - 1]) {
                        if (phi[i] > rs->tex_rng_hi[st][n - 1])
                            rs->tex_rng_hi[st][n - 1] = phi[i];
                        continue;
                    }
                    rs->tex_rng_lo[st][n] = plo[i];
                    rs->tex_rng_hi[st][n] = phi[i];
                    n++;
                }
                rs->tex_nrng[st] = n;
            }
        }
    }

    /* The four-wide vector (SoA) JIT loop fetches all four lanes' texels
       before it stores the group, so in a draw whose texture overlaps the
       rows it writes, a lane can read a cell that an earlier lane of the
       same group is about to write. The interpreter and the scalar block
       store pixel n before they fetch pixel n + 1. Such draws are sent to
       the scalar block through the block key. The rows checked are the
       scissor's, widened to whole 16-row tile bands so a tiled surface's
       bytes stay covered. Staged levels sample an arena copy but are
       still counted; a needless switch to the scalar block costs only the
       vector speedup. */
    rs->d.soa_selftex = 0;
    {
        uint64_t r0  = (uint64_t) (rs->d.sy0 < 0 ? 0 : rs->d.sy0) & ~15ull;
        uint64_t r1  = (((uint64_t) (rs->d.sy1 < 0 ? 0 : rs->d.sy1)) | 15ull) + 1ull;
        uint64_t cs  = (uint64_t) rs->dst_pitch * (uint32_t) rs->dst_bpp;
        uint64_t zs  = (uint64_t) rs->d.zrowpx * (uint32_t) rs->d.zbpp;
        uint64_t clo = rs->dst_offset + r0 * cs;
        uint64_t chi = rs->dst_offset + r1 * cs;
        uint64_t zlo = rs->t3d.z_offset + r0 * zs;
        uint64_t zhi = rs->t3d.z_offset + r1 * zs;

        for (int st = 0; st < 2; st++) {
            if (rs->tex_hi[st] <= rs->tex_lo[st])
                continue;
            if ((cs && clo < 0x100000000ull
                 && r3d_tex_rng_hit(rs, st, (uint32_t) clo,
                                    chi > 0xffffffffull ? 0xffffffffu : (uint32_t) chi))
                || ((rs->d.z_en || rs->d.sten_on) && zs && zlo < 0x100000000ull
                    && r3d_tex_rng_hit(rs, st, (uint32_t) zlo,
                                       zhi > 0xffffffffull ? 0xffffffffu : (uint32_t) zhi)))
                rs->d.soa_selftex = 1;
        }
    }
}

/* Face culling by PM4_VC_FPU_SETUP, for every vertex walk (CCE 3D
   supplement, PM4_VC_FPU_SETUP; SDK: Setting 3D Render States, p. 6-54
   / PDF 166). Bit 18 set turns culling off. The supplement lists bit 18
   as reserved, so that meaning is modeled: in captured traffic the
   Windows 98 Direct3D driver writes 0x000403d8 (back faces set to cull,
   bit 18 set) whatever cull mode the program asks for, culls on the CPU
   itself and needs both windings drawn, while the OpenGL driver and
   Mesa r128 set culling per pass with bit 18 clear and depend on it.
   FRONT_DIR is meant in OpenGL's y-up sense, and Mesa r128
   r128CalcViewport flips y (MAT_SY), so in the y-down window
   coordinates here a front face under FRONT_DIR 1 (counterclockwise)
   has negative area. The face function is the two-bit field of the face
   the triangle shows: 0 cull, 1 draw as points, 2 draw as lines, 3
   draw solid (SDK: Table 6-22, p. 6-54 / PDF 166); with bit 18 set
   every triangle is drawn solid. */
static uint32_t
r3d_vc_face_fn(uint32_t fs, const r3d_vtx_t *a, const r3d_vtx_t *b,
               const r3d_vtx_t *c)
{
    float area2 = (b->x - a->x) * (c->y - a->y)
        - (c->x - a->x) * (b->y - a->y);

    if (fs & (1u << 18))
        return RAGE128_FPU_FACE_SOLID;
    int front = (fs & RAGE128_FPU_FRONT_DIR_CCW) ? (area2 < 0.0f)
                                                 : (area2 > 0.0f);
    return (fs >> (front ? RAGE128_FPU_FRONTFACE_SHIFT
                         : RAGE128_FPU_BACKFACE_SHIFT))
        & RAGE128_FPU_FACE_MODE_MASK;
}

/* Draw one walked triangle under its face function: as points, one at
   each vertex; as lines, its three edges in vertex order; solid, as a
   triangle; or not at all. The points and lines are the same primitives
   the point and line walks submit, so each lane draws them as it draws
   those. How the chip places a triangle's points and lines is not
   documented; a one-pixel point at each vertex and a line along each
   edge are modeled. */
static void
r3d_vc_face_draw(rage128_t *dev, const rage128_raster_state_t *rs, uint32_t fs,
                 const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c)
{
    switch (r3d_vc_face_fn(fs, a, b, c)) {
        case RAGE128_FPU_FACE_POINTS:
            rage128_raster_submit_point(dev, rs, a);
            rage128_raster_submit_point(dev, rs, b);
            rage128_raster_submit_point(dev, rs, c);
            break;
        case RAGE128_FPU_FACE_LINES:
            rage128_raster_submit_line(dev, rs, a, b);
            rage128_raster_submit_line(dev, rs, b, c);
            rage128_raster_submit_line(dev, rs, c, a);
            break;
        case RAGE128_FPU_FACE_SOLID:
            rage128_raster_submit_tri(dev, rs, a, b, c);
            break;
        default:
            break;
    }
}

/* Log each undecoded primitive type once, like the texture-format log,
   so a dropped draw names its type. */
static void
r3d_prim_log_once(uint32_t prim)
{
    static uint16_t seen = 0;

    if (seen & (uint16_t) (1u << (prim & 0xf)))
        return;
    seen |= (uint16_t) (1u << (prim & 0xf));
    rage128_log("[r128 PRIM] unhandled prim type %u -- draw dropped\n", prim);
}

/* The PM4_VC_DEBUG_CONFIG debug bits: PM4_VC_DONT_START keeps the vertex
   walker from starting and PM4_VC_NO_OUTPUT drops all of its output
   (RRG: PM4_VC_DEBUG_CONFIG, p. 3-219 / PDF 237). Either way the draw
   produces nothing, so both share one check. Packet parsing and ring
   progress are the caller's and carry on as normal, and a write to this
   register does not drain the executor (doing so slowed Windows 2000
   Direct3D fills measurably). */
static inline int
r3d_vc_inhibited(const rage128_t *dev)
{
    return (dev->pm4_vc_debug_config
            & (RAGE128_VC_DEBUG_DONT_START | RAGE128_VC_DEBUG_NO_OUTPUT))
        != 0;
}

/* Assemble and rasterize one draw by VC_PRIM_TYPE (SDK: Table F-44,
   p. F-51 / PDF 341), the same way for every walk: the list and indexed
   walks of 3D_RNDR_GEN_INDX_PRIM (0x23) fetch vertices from the buffer
   at base, and the ring walk of 3D_RNDR_GEN_PRIM (0x25) decodes them in
   place from vd. In the indexed walk (PRIM_WALK 1), draw position p uses
   the source vertex index in pl[4 + p / 2], 16-bit indices with the
   first in the low half (SDK: Table F-52, p. F-57 / PDF 347); in the
   list walk the source index is p itself, counting from base. The
   triangle orders are the SDK's (SDK: F.25, pp. F-54-F-56 /
   PDF 344-346). */
static void
r3d_buffer_draw(rage128_t *dev, const uint32_t *pl, uint32_t count,
                uint32_t base, uint32_t fmt, uint32_t stride,
                uint32_t prim, uint32_t num, int indexed,
                const uint32_t *vd)
{
    r3d_vtx_t va, vb, vc;
    uint32_t  i;
    uint32_t  fs = dev->t3d.fpu_setup; /* latched at submit, like rs */
    /* Snapshot the engine state this draw was submitted under, so the
       rasterizer, which may be a worker thread, draws with it whatever
       state changes follow later in the batch. */
    rage128_raster_state_t        rs_store;
    const rage128_raster_state_t *rs = &rs_store;

    rage128_raster_state_capture(dev, &rs_store);

    /* Only the indexed walk revisits source indices; the list walk reads
       each vertex once, so a cache would only cost time there. */
    if (indexed)
        r3d_vtx_cache_begin(dev);

        /* Vertex p: inline vertices (vd, the ring payload) are decoded in
           place and cannot fail; buffer vertices are read over the bus from
           base, and a failed read, or an index past the end of the packet,
           abandons the draw. */
#define SRC(p) (indexed                                                                 \
                    ? ((4u + (p) / 2u < (count))                                        \
                           ? ((pl[4u + (p) / 2u] >> (((p) & 1u) ? 16u : 0u)) & 0xffffu) \
                           : 0xffffffffu)                                               \
                    : (p))
#define GET(dst, p)                                                                    \
    do {                                                                               \
        if (vd)                                                                        \
            r3d_decode_vertex(fmt, &vd[(p) * stride], &(dst));                         \
        else {                                                                         \
            uint32_t _s = SRC(p);                                                      \
            if (_s == 0xffffffffu                                                      \
                || !(indexed                                                           \
                         ? r3d_fetch_vertex_cached(dev, base, stride, _s, fmt, &(dst)) \
                         : r3d_fetch_vertex(dev, base, stride, _s, fmt, &(dst))))      \
                return;                                                                \
        }                                                                              \
    } while (0)

    switch (prim) {
        case 4: /* independent triangles */
            for (i = 0; i + 2 < num; i += 3) {
                GET(va, i);
                GET(vb, i + 1);
                GET(vc, i + 2);
                r3d_vc_face_draw(dev, rs, fs, &va, &vb, &vc);
            }
            break;

        case 5: /* triangle fan */
            if (num < 3)
                break;
            GET(va, 0);
            GET(vb, 1);
            for (i = 2; i < num; i++) {
                GET(vc, i);
                r3d_vc_face_draw(dev, rs, fs, &va, &vb, &vc);
                vb = vc;
            }
            break;

        case 6: /* triangle strip; every second triangle has its first two
                   vertices swapped so all keep the same winding */
            if (num < 3)
                break;
            GET(va, 0);
            GET(vb, 1);
            for (i = 2; i < num; i++) {
                GET(vc, i);
                if (i & 1)
                    r3d_vc_face_draw(dev, rs, fs, &vb, &va, &vc);
                else
                    r3d_vc_face_draw(dev, rs, fs, &va, &vb, &vc);
                va = vb;
                vb = vc;
            }
            break;

        case 2: /* independent lines: vertex pairs */
            for (i = 0; i + 1 < num; i += 2) {
                GET(va, i);
                GET(vb, i + 1);
                rage128_raster_submit_line(dev, rs, &va, &vb);
            }
            break;

        case 3: /* polyline: connected segments */
            if (num < 2)
                break;
            GET(va, 0);
            for (i = 1; i < num; i++) {
                GET(vb, i);
                rage128_raster_submit_line(dev, rs, &va, &vb);
                va = vb;
            }
            break;

        case 1: /* points: one vertex each */
            for (i = 0; i < num; i++) {
                GET(va, i);
                rage128_raster_submit_point(dev, rs, &va);
            }
            break;

        default: /* 0 draws nothing; 7, "type-2 triangles", is not decoded
                    (no driver seen here sends it); 8-15 are reserved */
            r3d_prim_log_once(prim);
            break;
    }

#undef GET
#undef SRC
}

/* 3D_RNDR_GEN_INDX_PRIM (0x23): the payload is the vertex buffer address
   (PM4_VC_VLOFF), the number of vertices in the buffer (PM4_VC_VSIZE),
   VC_FORMAT, VC_CNTL and then, for the indexed walk, the 16-bit indices
   in pairs (SDK: Table F-52, p. F-57 / PDF 347). The vertices are in a
   buffer reached through the GART. In captured Quake III Arena traffic
   the OpenGL driver sends every draw this way: indexed walk, independent
   triangles, format 0x087. */
static void
rage128_3d_draw_packet(rage128_t *dev, const uint32_t *pl, uint32_t count)
{
    uint32_t addr;
    uint32_t fmt;
    uint32_t cntl;
    uint32_t prim;
    uint32_t walk;
    uint32_t num;
    uint32_t stride;

    dev->vc_bundle.pending = 0; /* a new draw abandons an unfinished walk */
    if (count < 4 || r3d_vc_inhibited(dev))
        return;
    /* pl[0] is taken as a card address (bit 25 marks the AGP half) and
       resolved to the bus address the vertex walker reads, the same way
       the ring base is. */
    addr   = rage128_pm4_vm_addr(dev, pl[0]);
    fmt    = pl[2];
    cntl   = pl[3];
    prim   = RAGE128_VC_PRIM_TYPE(cntl);
    walk   = RAGE128_VC_PRIM_WALK(cntl);
    num    = RAGE128_VC_NUM(cntl);
    stride = rage128_3d_vertex_dwords(fmt);

    if (stride == 0)
        return; /* no usable vertex size: do not walk */

    switch (walk) {
        case RAGE128_VC_WALK_LIST:
            /* num vertices of stride dwords, sequential from addr. */
            r3d_buffer_draw(dev, pl, count, addr, fmt, stride, prim, num, 0,
                            NULL);
            break;

        case RAGE128_VC_WALK_IND:
            /* The index count comes from VC_CNTL NUM_VERTEX, not from the
               packet length, which rounds an odd count up to a whole dword
               (the high half of the last dword "may be filled with 0",
               SDK: Table F-52, p. F-57 / PDF 347). The indices are pairs
               at pl[4] on, low half first. A NUM_VERTEX larger than this
               packet carries (the COUNT field limits a packet to about
               16380 dwords, while NUM_VERTEX reaches 65535) is a walk
               that continues in NEXT_VERTEX_BUNDLE packets, so it is held
               until they arrive. */
            if (4 + (num + 1) / 2 > count) {
                struct rage128_vc_bundle *b = &dev->vc_bundle;

                b->base   = addr;
                b->fmt    = fmt;
                b->stride = stride;
                b->prim   = prim;
                b->num    = num;
                b->have   = count - 4;
                memcpy(&b->idx[4], &pl[4], b->have * sizeof(uint32_t));
                b->pending = 1;
                break;
            }
            r3d_buffer_draw(dev, pl, count, addr, fmt, stride, prim, num, 1,
                            NULL);
            break;

        default:
            break;
    }
}

/* 3D_RNDR_GEN_PRIM (0x25), the Windows 98 Direct3D driver's draw packet:
   VC_FORMAT, VC_CNTL, then the vertices themselves, num times stride
   dwords (SDK: Table F-42, p. F-49 / PDF 339). PRIM_WALK is 3, the ring
   buffer method: the vertices travel in the command stream, with no
   buffer and no indices. VC_FORMAT and VC_CNTL mean the same as in
   0x23. */
static void
rage128_3d_draw_inline(rage128_t *dev, const uint32_t *pl, uint32_t count)
{
    uint32_t fmt;
    uint32_t cntl;
    uint32_t prim;
    uint32_t walk;
    uint32_t num;
    uint32_t stride;

    dev->vc_bundle.pending = 0; /* a new draw abandons an unfinished walk */
    if (count < 2 || r3d_vc_inhibited(dev))
        return;
    fmt    = pl[0];
    cntl   = pl[1];
    prim   = RAGE128_VC_PRIM_TYPE(cntl);
    walk   = RAGE128_VC_PRIM_WALK(cntl);
    num    = RAGE128_VC_NUM(cntl);
    stride = rage128_3d_vertex_dwords(fmt);

    if (stride == 0 || walk != RAGE128_VC_WALK_RING
        || (uint64_t) num * stride != count - 2)
        return; /* not the packet shape seen in traffic: do not guess a walk */

    /* Primitive assembly from the inline vertices at pl[2]. */
    r3d_buffer_draw(dev, pl, count, 0, fmt, stride, prim, num, 0, &pl[2]);
}

/* NEXT_VERTEX_BUNDLE (0x2e): the payload is index pairs only, a
   continuation of the previous 3D_RNDR_GEN_INDX_PRIM whose primitives
   "will be rendered in the same manner" (SDK: Table F-54, p. F-59 /
   PDF 349). The pairs are appended to the indexed walk that packet left
   waiting, whole dwords at a time; the draw runs once NUM_VERTEX
   indices have arrived, with the first packet's vertex buffer, format
   and VC_CNTL, so a strip or fan continues across the packet boundary.
   With no walk waiting the packet is modeled as doing nothing; the SDK
   does not cover that case. */
static void
rage128_3d_next_bundle(rage128_t *dev, const uint32_t *pl, uint32_t count)
{
    struct rage128_vc_bundle *b = &dev->vc_bundle;
    uint32_t                  need;

    if (!b->pending)
        return;
    need = (b->num + 1) / 2 - b->have;
    if (count < need) {
        memcpy(&b->idx[4 + b->have], pl, count * sizeof(uint32_t));
        b->have += count;
        return;
    }
    memcpy(&b->idx[4 + b->have], pl, need * sizeof(uint32_t));
    b->have += need;
    b->pending = 0;
    if (r3d_vc_inhibited(dev))
        return;
    r3d_buffer_draw(dev, b->idx, 4 + b->have, b->base, b->fmt, b->stride,
                    b->prim, b->num, 1, NULL);
}

int
rage128_3d_packet3(rage128_t *dev, uint32_t hdr, const uint32_t *pl, uint32_t count)
{
    uint32_t op = RAGE128_PM4_T3_OPCODE(hdr);

    switch (op) {
        case RAGE128_PM4_OP_3D_RNDR_GEN_INDX_PRIM:
            rage128_3d_draw_packet(dev, pl, count);
            return 1;

        case RAGE128_PM4_OP_PURGE:
            /* "Purge the pixel cache" (SDK: Type-0 CCE Packet, p. F-11 /
               PDF 301). The model has no pixel cache, so the packet is
               accepted and skipped; the Windows 98 drivers send it. */
            return 1;

        case RAGE128_PM4_OP_3D_RNDR_GEN_PRIM:
            rage128_3d_draw_inline(dev, pl, count);
            return 1;

        case RAGE128_PM4_OP_LOAD_PALETTE:
            /* Palette for a following 2D scaling operation: dword 0 is
               SCALE_DATATYPE (1 for 16 entries, 2 for 256), then one
               entry per dword with blue in [7:0], green [15:8], red
               [23:16], and the entry count is 16 or 256 by that field,
               not by the packet length (SDK: Table F-39, p. F-46 /
               PDF 336). A 16-entry load stores entries 0 to 15 and
               leaves the rest of the palette as it is; a packet shorter
               than its count stores the entries it carries. The guide
               defines codes 1 and 2 only; any other code is loaded as
               the 256-entry layout (modeled). The Rage Fury MAXX's
               Windows 9x display driver
               sends 24-bit 0x00RRGGBB entries with each channel lowered
               by 4, 2 and 4, so that the engine's truncating pack to the
               destination format lands on the driver's (v * 31) >> 8
               values (RE: ati2drau.drv @0004:5373). The palette is kept
               apart from the texture palette (TEX_PALETTE_INDEX /
               DATA). */
            if (count >= 2) {
                uint32_t n   = count - 1;
                uint32_t max = (pl[0] == 1) ? 16 : 256;

                if (n > max)
                    n = max;
#ifdef ENABLE_RAGE128_LOG
                uint32_t wide = 0;
#endif

                for (uint32_t k = 0; k < n; k++) {
                    dev->scl_palette[k] = pl[k + 1];
#ifdef ENABLE_RAGE128_LOG
                    wide |= pl[k + 1] >> 16;
#endif
                }
#ifdef ENABLE_RAGE128_LOG
                /* The ati2draa.drv display driver's 16 bpp path packs
                   its entries to RGB565 before sending them, unlike
                   ati2drau.drv's 24-bit entries. With no captured output
                   to check that form against, a payload that looks like
                   it is logged once and stored unchanged. */
                if (!wide && n > 16) {
                    static int announced = 0;

                    if (!announced) {
                        announced = 1;
                        rage128_log("[r128 3d] LOAD_PALETTE: every entry fits 16 bits -- "
                                    "dst-packed payload, conversion unverified\n");
                    }
                }
#endif
            }
            return 1;
        case RAGE128_PM4_OP_NEXT_VERTEX_BUNDLE:
            rage128_3d_next_bundle(dev, pl, count);
            return 1;

        case RAGE128_PM4_OP_3D_SAVE_CONTEXT:
        case RAGE128_PM4_OP_3D_PLAY_CONTEXT:
            /* Only the opcodes are public (R128_CCE_PACKET3_3D_SAVE_CONTEXT
               and R128_CCE_PACKET3_3D_PLAY_CONTEXT in the xf86-video-r128
               header); the SDK does not list them and the payload format
               is unknown, so they are skipped. */
            return 1;

        default:
            return 0; /* not a 3D opcode: the 2D dispatcher handles it */
    }
}
