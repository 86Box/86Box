/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- GPU backend, records shared by the host and the
 *          kernels.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
/* rage128 GPU raster backend -- records shared by the host and the
   kernels.

   The shading kernel (vid_ati_rage128_gpu_seg.comp) and the z-ladder
   pre-pass (vid_ati_rage128_gpu_lad.comp) include this file. It holds
   the batch slot constants and the two std430 records the host fills
   for each segment: seg_span_t, one per captured span, in binding 4,
   and seg_tri_t, one per triangle, in binding 5.
   vid_ati_rage128_gpu.c declares the same two records as C structs and
   checks their sizes and some field offsets with _Static_assert. A
   field added, removed or moved here must change there too, in the
   same position.

   Relevant literature:

   [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
       RRG-G04500-C Rev 1.01, January 2000. Cited below as
       "RRG: <register>, p. <printed page> / PDF <viewer page>".

   [2] ATI Technologies, "RAGE 128 Software Development Guide",
       SDK-G04000 Rev 0.01, August 1999. Cited as "SDK: ...". */

/* Batch slots. The shading workgroup is 256 threads, one pixel per
   thread (local_size_x is GPU_BATCH_SLOTS * GPU_SLOT_W). The threads
   are cut into GPU_BATCH_SLOTS groups of GPU_SLOT_W lanes, and a batch
   is one row of GPU_BATCH_SLOTS slot words, one word per group. A span
   of n pixels takes n / GPU_SLOT_W slots rounded up, so a batch holds
   at most GPU_BATCH_SLOTS spans. To change the slot count, change
   GPU_SLOT_W, not GPU_BATCH_SLOTS: raising GPU_BATCH_SLOTS alone widens
   the workgroup past 256 threads, and a 512-thread workgroup runs
   slower because fewer workgroups stay resident on the GPU at once. A
   narrower slot keeps the workgroup size and leaves fewer lanes idle on
   short spans.

   A slot word is the span index << 8 | the pixel offset the group
   starts at, or 0xffffffff for an idle slot. The host caps a span at
   256 pixels, so the offset fits the low byte. Bit 0 of a batch's first
   slot word marks a batch that opens a new level, where the kernel
   must place a barrier first. The offset is a multiple of GPU_SLOT_W,
   which leaves bit 0 free for that flag, and GPU_SLOT_MASK removes it.
   So GPU_SLOT_W must be a power of two and at least 2, with
   GPU_SLOT_SHIFT its log2. vid_ati_rage128_gpu.c defines the same
   values; if the two sides differ, spans are silently dropped or
   overlap. */
#define GPU_SLOT_W      16
#define GPU_SLOT_SHIFT  4
#define GPU_BATCH_SLOTS (256 / GPU_SLOT_W)
#define GPU_SLOT_MASK   (0xff & ~(GPU_SLOT_W - 1))

/* One captured span: pixels x0..x1 of row py, from triangle tri. e0,
   e1 and e2 are the three edge-function values at x0, and zline is the
   depth at x0 as the bit pattern of a double. The host stores them as
   int64_t and uint64_t; here each is a uvec2, low word first.
   pad1 is not padding: the host sets it to 1 when all three edge values
   stay within signed 32 bits across the whole span, and the shading
   kernel then steps them as 32-bit integers instead of 64-bit ones.
   drow and zrow are the byte addresses of the color row and the depth
   row. With bit 31 set, the row is in the staging buffers for an AGP
   render target (bindings 9 to 12) and the low 31 bits are an offset
   into them rather than into VRAM. px_base is where the span's words
   start in the z-ladder buffer (binding 6): one word per pixel, or two
   for a table-fog draw. */
struct seg_span_t {
    uvec2 e0, e1, e2;
    uvec2 zline;
    int   x0, x1, py, tri;
    uint     drow, zrow;
    uint     px_base, pad1;
};

/* One triangle. Most fields are copies of the interpreter's triangle
   record (r128_jit_tri_t), draw state (rage128_draw_state_t) and
   texture context (r3d_texctx_t) under the same names;
   capture_tri_record in vid_ati_rage128_gpu.c fills the record.
   e0dxi, e1dxi and e2dxi are the per-pixel x steps of the span's edge
   values, and dZdx is the per-pixel depth step as the bits of a double,
   both stored as in seg_span_t.

   st_cfg holds 8 words per texture stage: 0 texel datatype, plus bit 4
   when a fetched texel of an alpha-bearing datatype reads as alpha
   0xff, which the host sets while the texture-alpha enable,
   SCALE_3D_CNTL bit 30, is clear (the interpreter's amask); 1 S3TC
   class; 2 clamp_s; 3 clamp_t; 4 border color; 5 minification filter;
   6 magnification filter; 7 mip-map disable.
   comb_cfg holds 8 words per stage, the fields of that stage's texture
   combine register: 0 comb, 1 fmsb, 2 cfac, 3 ifac, 4 comba, 5 afac,
   6 ifaca, 7 spare. Stage 0's spare word carries the texture-lighting
   setting, 0 when lighting is off: bits [3:0] TEX_LIGHT_FN (SDK:
   Texture Mapping, p. 6-46 / PDF 158, Table 6-15), bit 4 the value of
   TEX_CNTL_C bit 6, which the interpreter treats like a stage
   combine's fmsb bit, and bits [7:5] ALPHA_LIGHT_FN (SDK: Texture
   Mapping, p. 6-47 / PDF 159, Table 6-16). */
struct seg_tri_t {
    uvec2 e0dxi, e1dxi, e2dxi;
    uvec2 dZdx;
    float invs, lod_bias;
    float    texw0, texh0, texw1, texh1;
    uint     zfn, z_wr;
    uint     atest_en, atest_fn, atest_ref;
    uint     bsrc, bdst, bfcn;
    uint     aux_cntl;
    int      aux_x0[3];
    int      aux_x1[3];
    int      aux_y0[3];
    int      aux_y1[3];
    float    cc[4];
    float    vca[4], vcb[4], vcc[4];
    float    sta, stb, stc, tta, ttb, ttc;
    float    s2a, s2b, s2c, t2a, t2b, t2c;
    float    arhw, brhw, crhw;
    float    dSdx, dSdy, dTdx, dTdy, dWdx, dWdy;
    float    dS2dx, dS2dy, dT2dx, dT2dy;
    /* The secondary stage's own W: a2rhw, b2rhw, c2rhw per vertex and
       its screen gradients dW2dx, dW2dy. persp2 is that stage's
       perspective enable (SEC_TEX_CNTL_C SEC_TEX_PERSPECTIVE_DIS
       clear) and sel_w says the W is the vertex rhw2 (SEC_SRC_SEL_W);
       when the stage's enable equals persp and sel_w is clear it
       divides by the primary rhw, as the interpreter does. */
    float    a2rhw, b2rhw, c2rhw, dW2dx, dW2dy;
    uint     persp2, sel_w, stip_en;
    uint     sec_sel, need_lod2;
    int      top0, top1;
    uint     st_cfg[16];
    uint     comb_cfg[16];
    /* One entry per mip level, levels 0 to top0 (top1 for slot1), at
       most 11: {lw, lh, base, mask}. With bit 31 of base set, the level
       is in the texture staging buffer (binding 7) and the low bits are
       an offset into it rather than into VRAM. */
    uint     slot0[44]; /* 11 x {lw, lh, base, mask} */
    uint     slot1[44];
    /* Destination surface. dst_dt is the destination datatype the
       kernel packs to: 3 ARGB1555, 4 RGB565, 15 ARGB4444 or 6 ARGB8888
       (RRG: DP_DATATYPE, p. 3-168 / PDF 186). wmask is PLANE_3D_MASK_C
       (RRG: PLANE_3D_MASK_C, p. 3-260 / PDF 278), applied to the packed
       pixel after dithering, as the interpreter applies it. dither turns
       on the 4x4 ordered dither; the host sets it only for the 16-bit
       formats. Bit 0 of persp is clear when PRIM_TEX_CNTL_C sets the
       perspective disable bit (xf86-video-r128 macro
       R128_TEX_PERSPECTIVE_DISABLE):
       texture coordinates are then interpolated without the divide by
       rhw, and the LOD uses the screen-space gradients as they are.
       Bit 1 skips the primary sampler, chroma keys, and combine in a
       secondary-only draw, leaving the iterated color as stage 1's
       input while bit 0 still controls the shared reciprocal. */
    uint dst_dt, wmask, dither, persp;
    /* Depth cell. zbpp 2 is a 16-bit depth cell. zbpp 4 is a 4-byte cell
       holding 24-bit depth and a stencil byte, so a depth write reads
       the cell and keeps the stencil byte. zshift is 0 when depth is the
       low 24 bits (24-bit Z) and 8 when it is the high 24 bits (32-bit
       Z), as the interpreter models the two formats. There is no zmax
       field: the kernels derive it from zbpp (0xffff or 0xffffff), and
       the host refuses a draw whose depth range does not match zbpp, so
       the two cannot disagree. */
    uint zbpp, zshift;
    /* Stencil, only with zbpp 4. The stencil byte is the one the depth
       compare masks off: bits [31:24] when zshift is 0, bits [7:0] when
       it is 8. sten_ctl: bit 0 enable, [6:4] compare function, [10:8]
       op on stencil fail, [14:12] op on depth pass, [18:16] op on depth
       fail. sten_rm: [7:0] reference, [15:8] compare mask, [23:16]
       write mask. Both are repacked from Z_STEN_CNTL_C and
       STEN_REF_MASK_C (xf86-video-r128 macros R128_Z_STEN_CNTL_C,
       R128_STEN_REF_MASK_C) with the field codes unchanged. */
    uint sten_ctl, sten_rm;
    /* Specular add, enabled by the TEX_CNTL_C bit xf86-video-r128 names
       R128_SPEC_LIGHT_ENABLE. spa, spb and spc are the specular RGB at
       the three vertices; the kernel interpolates them and adds the
       result to the combined color, clamping each channel at 1.0.
       Alpha is unchanged. */
    float spa[3];
    float spb[3];
    float spc[3];
    uint  spec_en;
    /* Fog, applied after the specular add: C = f * C + (1 - f) * fog
       color, on RGB only. fog_en is the TEX_CNTL_C bit xf86-video-r128
       names R128_FOG_ENABLE; fogr, fogg and fogb are the fog color.
       With ftab_en clear, f is interpolated from the per-vertex fog
       factors fga, fgb and fgc. ftab_en is set when fog_en is on and
       MISC_3D_STATE_CNTL_REG.FOG_TABLE_EN, bit 14, selects the fog
       table (RRG: MISC_3D_STATE_CNTL_REG, p. 3-257 / PDF 275); f then
       comes from fog_table. The z-ladder pre-pass splits the pixel's depth,
       clamped to 0..1 and times 255, into a table index, stored in bits
       [31:24] of the pixel's ladder word, and a 32-bit fraction, stored
       in the word after it. The kernel interpolates between the table
       entries at that index and the next one by the fraction. Table fog
       therefore needs a depth cell: the host refuses a table-fog draw
       with neither depth nor stencil enabled, and the interpreter draws
       it. */
    float fga, fgb, fgc;
    float fogr, fogg, fogb;
    uint  fog_en, ftab_en;
    /* Stage 0 chroma key. The kernel takes the nearest texel before
       filtering, masks it and compares it with a masked key; a rejected
       fragment is dropped before the combine. There are two independent
       paths. The first is set by CLR_CMP_FCN_3D (RRG:
       MISC_3D_STATE_CNTL_REG, p. 3-260 / PDF 278), with key ck3d_clr
       and mask ck3d_msk. The second is enabled by the TEX_CNTL_C bit
       xf86-video-r128 names R128_TEX_CHROMA_KEY_ENABLE, with key ckc_clr
       and mask ckc_msk from R128_TEXTURE_CLR_CMP_CLR_C and
       R128_TEXTURE_CLR_CMP_MSK_C, and rejects a texel that matches. The
       host converts each key and mask to the ARGB layout of the
       converted texel (r3d_ck_to_argb) before storing it.
       ck_ctl: bit 0 first path on, bit 1 second path on, bit 2 the first
       path rejects a match (CLR_CMP_FCN_3D = 3); with bit 2 clear it
       rejects a mismatch (code 2). Code 1, "True" in the RRG, is modeled
       as rejecting every texel: it arrives as code 3 with key and mask
       0, which every texel matches.
       ck_ctl 0 means no keying. */
    uint  ck3d_clr, ck3d_msk, ckc_clr, ckc_msk;
    /* pal_base is the word index of this draw's palette in the palette
       buffer (binding 8), which holds 256 ARGB words per copy. A draw
       with a CI4 or CI8 texture stage gets a copy of the palette taken
       when the draw is captured; later draws reuse that copy while the
       palette is unchanged. 0 for a draw with no CI stage. */
    uint  ck_ctl, pal_base;
    /* The fog table, copied when the draw is captured: 256 one-byte
       entries, four per word, entry 0 in the low byte of word 0. Read
       only when ftab_en is set. */
    uint  fog_table[64];
    /* The modeled pattern is captured per triangle so a segment can mix
       patterns. Row y & 31 selects a word; bit 31 is the tile's left edge. */
    uint  stipple[32];
};
